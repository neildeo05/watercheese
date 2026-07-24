// Built from https://gist.github.com/imbushuo/51b09e61ecd7b7ac063853ad65cedf34
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <sys/mman.h>

#include <Hypervisor/Hypervisor.h>

typedef uint64_t excp_syndrome_t;
#define EC(syndrome) ((syndrome >> 26) & 0x3f)
#define EC_HVC 0x16
#define EC_SMC 0x17
#define EC_BRK 0x3C
/* Memory Map
   - from 0x8000_0000 to 0x8000_0000 (16 MiB) is the main RAM
     - User Stack Pointer is at +0x4000
     - Kernel Stack Pointer is at +0x8000
 */
const uint64_t gp_MainMemory = 0x80000000;
const uint64_t gp_MainMemorySize = 0x1000000;

// Since post-reset the CPU starts in EL0, in order to start in EL1, we need to do a supervisor call
const char s_cArm64ResetVector[] = {
    0x01, 0x00, 0x00, 0xD4, // svc #0
    0x00, 0x00, 0x20, 0xD4, // brk #0
};

// Trampoline Code to jump to 0x8000_0000 which is the start of memory, but the reset vector executes first so we are in EL1 mode
const char s_cArm64ResetTramp[] = {
    0x00, 0x00, 0xB0, 0xD2, // mov x0, #0x80000000
    0x00, 0x00, 0x1F, 0xD6, // br  x0
    0x00, 0x00, 0x20, 0xD4, // brk #0
};

// ARM64 instructions to compute ((2 + 2) - 1) and make a hypervisor call with the result
const char s_ckVMCode[] = {
    0x40, 0x00, 0x80, 0xD2, // mov x0, #2
    0x00, 0x08, 0x00, 0x91, // add x0, x0, #2
    0x00, 0x04, 0x00, 0xD1, // sub x0, x0, #1
    0x03, 0x00, 0x00, 0xD4, // smc #0
    0x02, 0x00, 0x00, 0xD4, // hvc #0
    0x00, 0x00, 0x20, 0xD4, // brk #0
};

// Overview of this memory layout:
// The EL1 vector table is between [0xF0000000, 0xF0000800]; (reset trampoline is placed in every entry of the vector table)
// It contains 16 0x80 byte slots that have instructions that run given a specifc trap vector
// Reset vector code at 0xF0000800, PC starts there initially
// https://wiki.osdev.org/AArch64_Exceptions
const uint64_t gp_EL1VecTable = 0xF0000000;
const uint64_t gp_VecTableRegionSize = 0x10000;


void* ResetTrampoline = NULL;
void* MainMemory = NULL;

int InitMemory()
{
    // Populate the entire vector table with the reset trampoline
    posix_memalign(&ResetTrampoline, 0x10000, gp_VecTableRegionSize);
    if (ResetTrampoline == NULL) {
        return -ENOMEM;
    }
    memset(ResetTrampoline, 0, gp_VecTableRegionSize);
    for (uint64_t offset = 0; offset < 0x780; offset += 0x80) {
        memcpy((void*) ResetTrampoline + offset, s_cArm64ResetTramp, sizeof(s_cArm64ResetTramp));
    }
    // Place the reset vector at 0xF0000800
    memcpy((void*) ResetTrampoline + 0x800, s_cArm64ResetVector, sizeof(s_cArm64ResetVector));

    // hvm_vm_map(host_pointer, guest_intermediate_physical_address, size, permissions)
    // For the extended page tables, this maps guest physical [0xF0000000-0xF0010000] -> host physical ResetTrampoline
    hv_vm_map(ResetTrampoline, gp_EL1VecTable, gp_VecTableRegionSize, HV_MEMORY_READ | HV_MEMORY_EXEC);

    // Allocate main memory and map it guest physical [0x80000000, 0x8100_0000]
    posix_memalign(&MainMemory, 0x1000, gp_MainMemorySize);
    if (MainMemory == NULL) {
        return -ENOMEM;
    }
    memset(MainMemory, 0, gp_MainMemorySize);
    memcpy(MainMemory, s_ckVMCode, sizeof(s_ckVMCode));
    hv_vm_map(MainMemory, gp_MainMemory, gp_MainMemorySize, HV_MEMORY_READ | HV_MEMORY_WRITE | HV_MEMORY_EXEC);

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
    // Set the vector table pointer to point to the guest physical address trampoline pointer
    hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_VBAR_EL1, gp_EL1VecTable);

#if USE_EL0_TRAMPOILNE
    // Set the CPU's PC to execute from the trampoline
    hv_vcpu_set_reg(vcpu, HV_REG_PC, gp_EL1VecTable + 0x800);
#else
    hv_vcpu_set_reg(vcpu, HV_REG_CPSR, 0x3c4);
    hv_vcpu_set_reg(vcpu, HV_REG_PC, 0x80000000);
#endif

    // Set the stack pointers for the EL0 and EL1
    hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_SP_EL0, gp_MainMemory + 0x4000);
    hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_SP_EL1, gp_MainMemory + 0x8000);

    // Force BRK instructions to trap
    hv_vcpu_set_trap_debug_exceptions(vcpu, true);

    // Infinite loop for the VM's runtime
    for (;;) {
        // This call blocks until the VM exits
        // I am assuming it traps into EL2, the hypervisor/lowvisor in EL2 world switches to the VM context on this thread
        hv_vcpu_run(vcpu);
        
        // Decoede the exit reason
        if (vcpu_exit->reason == HV_EXIT_REASON_EXCEPTION) {
            // ESR_EL2 contains the syndrome for an exception to EL2
            excp_syndrome_t syndrome = vcpu_exit->exception.syndrome;
            printf("Excepction Code: %llx\n", EC(syndrome));
            switch(EC(syndrome)) {
                case EC_HVC: {
                    uint64_t x0;
                    hv_vcpu_get_reg(vcpu, HV_REG_X0, &x0);
                    printf("VM made an HVC call! x0 register holds 0x%llx\n", x0);
                    goto exit;
                }
                case EC_SMC: {
                    uint64_t pc;
                    hv_vcpu_get_reg(vcpu, HV_REG_PC, &pc);
                    pc += 4;
                    hv_vcpu_set_reg(vcpu, HV_REG_PC, pc);
                    break;
                }
                case EC_BRK: {
                    uint64_t x0;
                    hv_vcpu_get_reg(vcpu, HV_REG_X0, &x0);
                    printf("VM made an BRK call!\n");
                    printf("Reg dump:\n");
                    for (uint32_t reg = HV_REG_X0; reg < HV_REG_X5; reg++) {
                        uint64_t s;
                        hv_vcpu_get_reg(vcpu, reg, &s);
                        printf("X%d: 0x%llx\n", reg, s);
                    }
                    goto exit;
                }
                default: {
                    fprintf(stderr, "Unexpected VM exception: 0x%llx, EC 0x%llx, GVA 0x%llx, GPA 0x%llx\n",
                        syndrome,
                        EC(syndrome),
                        vcpu_exit->exception.virtual_address,
                        vcpu_exit->exception.physical_address
                    );
                    goto exit;
                }
            }
        } 
        else {
            fprintf(stderr, "Unexpected VM exit reason: %d\n", vcpu_exit->reason);
            goto exit;
        }
    }
exit:
    printf("Exitting...\n");
    hv_vcpu_destroy(vcpu);
    hv_vm_destroy();
    free(ResetTrampoline);
    free(MainMemory);

    return 0;
}
