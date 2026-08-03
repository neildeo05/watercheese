// Built from https://gist.github.com/imbushuo/51b09e61ecd7b7ac063853ad65cedf34
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include "util.h"
#include <sys/mman.h>
#include "watercheese_types.h"
#include "io.h"

#include <Hypervisor/Hypervisor.h>

/* Memory Map
0x0 - 0x07ffffff: boot rom
0x08000000–0x0800ffff : GICv3 distribution
0x080a0000–0x08ffffff : GICv3 re-distribution
0x09000000–0x09000fff : UART
0x09010000–0x09010fff : RTC
0x0a000000–0x0a003fff : virtio-mmio (32, 0x200)
0x10000000–0x3fffffff : expansion space
0x40000000–RAM_END : guest RAM
 */


// https://wiki.osdev.org/AArch64_Exceptions

void* MainMemory = NULL;

int InitMemory()
{
    long page_size = sysconf(_SC_PAGESIZE);
    int err = posix_memalign(&MainMemory, page_size, RAM_SIZE);
    if (err != 0 || MainMemory == NULL) {
        return -ENOMEM;
    }

    memset(MainMemory, 0, RAM_SIZE);
#if WC_CUSTOM_PAYLOAD
    uintptr_t mm = (uintptr_t) MainMemory;
    uint8_t* code;
    char* payload_path = "guest/hvf_guest.bin";
    uint64_t codesz = read_payload(payload_path, &code);
    if(codesz != (PAYLOAD_SIZE + STACK_SIZE)) {
        fprintf(stderr, "Code size and linker size disagree\n");
        return -EBADF;
    }
    memcpy((((uint8_t*) MainMemory) + (PAYLOAD_BASE - RAM_BASE)), code, codesz);
    free(code);
    // we map all of main memory as RWX because so the guest can control the permissions themselves
    // if a guest application accesses a region that it doesn't have permissions to do the OS should fault
    hv_vm_map(MainMemory, RAM_BASE, RAM_SIZE, HV_MEMORY_READ | HV_MEMORY_WRITE | HV_MEMORY_EXEC);


#else
    hv_vm_map(MainMemory, RAM_BASE, RAM_SIZE, HV_MEMORY_READ | HV_MEMORY_WRITE | HV_MEMORY_EXEC);
#endif

    return 0;
}

int main(int argc, const char * argv[])
{
    hv_vcpu_t vcpu;
    // Contains exit reasons, I am assuming similar to the VMCS exit reasons
    hv_vcpu_exit_t *vcpu_exit;

    hv_vm_create(NULL);
    if (InitMemory()) abort();
    
    // Create a vCPU associated with the current host thread
    hv_vcpu_create(&vcpu, &vcpu_exit, NULL);

    // Set the P-state of the processor to be:
    // 0x005 -> run at EL1
    // 0x3C0 -> all interrupts masked initially
    hv_vcpu_set_reg(vcpu, HV_REG_CPSR, 0x3c5);
    hv_vcpu_set_reg(vcpu, HV_REG_PC, PAYLOAD_BASE);

    hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_SP_EL1, STACK_BASE + STACK_SIZE);

    // Force BRK instructions to trap
    hv_vcpu_set_trap_debug_exceptions(vcpu, true);

    int vmec = EXIT_FAILURE;
    // Infinite loop for the VM's runtime
    for (;;) {
        // This call blocks until the VM exits
        // I am assuming it traps into EL2, the hypervisor/lowvisor in EL2 world switches to the VM context on this thread
        hv_vcpu_run(vcpu);
        
        // Decoede the exit reason
        if (vcpu_exit->reason == HV_EXIT_REASON_EXCEPTION) {
            // ESR_EL2 contains the syndrome for an exception to EL2
            excp_syndrome_t syndrome = vcpu_exit->exception.syndrome;
            printf("Excepction Code: %x\n", EC(syndrome));
            switch(EC(syndrome)) {
                case EC_AA64_HVC: {
                    uint64_t x0;
                    hv_vcpu_get_reg(vcpu, HV_REG_X0, &x0);
                    // TODO: handle the x0 boot contract thing
                    if(x0 == 0x120) {
                        // boot report
                        break;
                    } 
                    else if (x0 == 0x121) {
                        uint64_t x1, x2, x3;
                        hv_vcpu_get_reg(vcpu, HV_REG_X1, &x1);
                        hv_vcpu_get_reg(vcpu, HV_REG_X2, &x2);
                        hv_vcpu_get_reg(vcpu, HV_REG_X3, &x3);
                        if(x1 == DEFERRED_BASE + 0x1238 && x2 == x3) {
                            break;
                        }
                        vmec = EXIT_FAILURE;
                        goto exit;
                    } 
                    else if(x0 == 0x122) {
                        vmec = EXIT_SUCCESS;
                        goto exit;
                    } 
                    vmec = EXIT_FAILURE;
                    goto exit;
                }
                case EC_AA64_SMC: {
                    uint64_t pc;
                    hv_vcpu_get_reg(vcpu, HV_REG_PC, &pc);
                    pc += 4;
                    hv_vcpu_set_reg(vcpu, HV_REG_PC, pc);
                    break;
                }
                case EC_AA64_BKPT: {
                    uint64_t x0;
                    hv_vcpu_get_reg(vcpu, HV_REG_X0, &x0);
                    printf("VM made an BRK call!\n");
                    printf("Reg dump:\n");
                    for (uint32_t reg = HV_REG_X0; reg < HV_REG_X5; reg++) {
                        uint64_t s;
                        hv_vcpu_get_reg(vcpu, reg, &s);
                        printf("X%d: 0x%llx\n", reg, s);
                    }
                    vmec = EXIT_FAILURE;
                    goto exit;
                }
                case EC_DATAABORT: {
                    // need to check that it is inside deferred RAM and DFSC is a translation fault
                    uint64_t pa = vcpu_exit->exception.physical_address; // check faulting address
                    struct mmio_excp_payload p;
                    uint8_t isv = ISV(syndrome);
                    uint8_t s1ptw = S1PTW(syndrome);
                    uint8_t cm = CM(syndrome);
                    uint8_t dfsc = DFSC(syndrome);
                    if((isv && !s1ptw && !cm) && dfsc >= 0x4 && dfsc <= 0x7) {
                        uint64_t access_size = 1 << SAS(syndrome);
                        // valid mmio access fault
                        enum mmio_type mt = is_mmio_access(pa);
                        switch(mt) {
                            case UNDEF:
                            case GICD:
                            case GICR:
                            case RTC:
                            case VIRTIO: break;
                            case PL011: {
                                uint64_t offset = pa - UART_BASE;
                                // ensure there is no access overflow and the access is addressed aligned
                                if(access_size > (UART_SIZE - offset) || pa % access_size != 0) break;
                                uint8_t is_write = WnR(syndrome);
                                uint64_t reg_idx = SRT(syndrome);
                                uint64_t write_val = 0;
                                // reg_idx = 31 means the sourze is a XZR (always zero)
                                if (is_write == 1 && reg_idx != 31) hv_vcpu_get_reg(vcpu, reg_idx, &write_val);
                                write_val &= (UINT64_MAX >> ((sizeof(uint64_t) - access_size) * 8));

                                struct mmio_excp_payload mp = {
                                    .syndrome=syndrome,
                                    .access_size=access_size,
                                    .fault_gpa=pa,
                                    .offset=offset,
                                    .r_w=is_write,
                                    .reg_idx=reg_idx,
                                    .write_val=write_val,
                                    .vcpu_id=0
                                };
                                handle_mmio(&mp);
                            }
                            default: break;
                        }
                    }
                    fprintf(stderr, "Unexpected VM exception: 0x%llx, EC 0x%x, GVA 0x%llx, GPA 0x%llx\n",
                        syndrome,
                        EC(syndrome),
                        vcpu_exit->exception.virtual_address,
                        vcpu_exit->exception.physical_address
                    );
                    vmec = EXIT_FAILURE;
                    goto exit;
                }
                default: {
                    fprintf(stderr, "Unexpected VM exception: 0x%llx, EC 0x%x, GVA 0x%llx, GPA 0x%llx\n",
                        syndrome,
                        EC(syndrome),
                        vcpu_exit->exception.virtual_address,
                        vcpu_exit->exception.physical_address
                    );
                    vmec = EXIT_FAILURE;
                    goto exit;
                }
            }
        } 
        else {
            fprintf(stderr, "Unexpected VM exit reason: %d\n", vcpu_exit->reason);
            vmec = EXIT_FAILURE;
            goto exit;
        }
    }
exit:
    if(vmec) {
        fprintf(stderr, "VM Failure! Exiting...");
    } else {
        fprintf(stdout, "Exiting..");
    }
    // unmap guest memory
    hv_vcpu_destroy(vcpu);
    hv_vm_destroy();
    free(MainMemory);

    return vmec;
}
