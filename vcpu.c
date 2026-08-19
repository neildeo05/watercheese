#include "vcpu.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
// #include "wcutil.h"
#include <sys/mman.h>
#include "watercheese_types.h"
#include "io.h"

#include <Hypervisor/Hypervisor.h>

// needs to be void* worker(void* p) for pthread_create to work
void* vcpu_worker(void* ctp) {
    struct wc_vcpu_ctx* ctx = ctp;

    hv_vcpu_t vcpu;
    hv_vcpu_exit_t *vcpu_exit;
    ctx->vmec = EXIT_FAILURE;
    if (hv_vcpu_create(&vcpu, &vcpu_exit, NULL) != HV_SUCCESS) {
        fprintf(stderr, "Failed to create vCPU (id=%d)\n", ctx->vcpu_id);
        StopIOLoop();
        return NULL;
    }

    // Set the P-state of the processor to be:
    // 0x005 -> run at EL1
    // 0x3C0 -> all interrupts masked initially
    hv_vcpu_set_reg(vcpu, HV_REG_CPSR, 0x3c5);
    hv_vcpu_set_reg(vcpu, HV_REG_PC, PAYLOAD_BASE);

    hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_SP_EL1, STACK_BASE + STACK_SIZE);

    // Force BRK instructions to trap
    hv_vcpu_set_trap_debug_exceptions(vcpu, true);

    // Infinite loop for the VM's runtime
    for (;;) {
        // This call blocks until the VM exits
        // I am assuming it traps into EL2, the hypervisor/lowvisor in EL2 world switches to the VM context on this thread
        if (hv_vcpu_run(vcpu) != HV_SUCCESS) {
            fprintf(stderr, "Failed to run vcpu (id=%d)\n", ctx->vcpu_id);
            ctx->vmec = EXIT_FAILURE;
            goto exit;
        }
        
        // Decoede the exit reason
        if (vcpu_exit->reason == HV_EXIT_REASON_EXCEPTION) {
            // ESR_EL2 contains the syndrome for an exception to EL2
            excp_syndrome_t syndrome = vcpu_exit->exception.syndrome;
            switch(EC(syndrome)) {
                case EC_AA64_HVC: {
                    uint64_t x0;
                    hv_vcpu_get_reg(vcpu, HV_REG_X0, &x0);
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
                        ctx->vmec = EXIT_FAILURE;
                        goto exit;
                    } 
                    else if(x0 == 0x122) {
                        ctx->vmec = EXIT_SUCCESS;
                        goto exit;
                    } 
                    ctx->vmec = EXIT_FAILURE;
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
                    ctx->vmec = EXIT_FAILURE;
                    goto exit;
                }
                case EC_DATAABORT: {
                    // need to check that it is inside deferred RAM and DFSC is a translation fault
                    uint64_t pc;
                    hv_vcpu_get_reg(vcpu, HV_REG_PC, &pc);
                    uint64_t pa = vcpu_exit->exception.physical_address; // check faulting address
                    uint8_t isv = ISV(syndrome);
                    uint8_t s1ptw = S1PTW(syndrome);
                    uint8_t cm = CM(syndrome);
                    uint8_t dfsc = DFSC(syndrome);
                    if((isv && !s1ptw && !cm) && dfsc >= 0x4 && dfsc <= 0x7) {
                        // MMIO access
                        uint64_t access_size = 1 << SAS(syndrome);
                        uint64_t write_val = 0;
                        uint8_t is_write = WnR(syndrome);
                        uint64_t reg_idx = SRT(syndrome);
                        if (is_write == 1 && reg_idx != 31) hv_vcpu_get_reg(vcpu, reg_idx_to_hv(reg_idx), &write_val);
                        write_val &= (UINT64_MAX >> ((sizeof(uint64_t) - access_size) * 8));
                        // valid mmio access fault
                        struct wc_vcpu_mmio_exit mx = {
                            .syndrome=syndrome,
                            .fault_gpa=pa,
                            .guest_pc=pc,
                            .write_value=write_val,
                            .vcpu_id=ctx->vcpu_id,
                            .access_size=access_size,
                            .reg_idx=reg_idx,
                            .direction=is_write
                        };
                        struct wc_mmio_result res = handle_mmio(&mx);
                        if(res.status == WC_STATUS_SUCCESS) {
                            if(!is_write) {
                                uint64_t out = res.read_value;
                                if(reg_idx != 31) {
                                    uint64_t mask = UINT64_MAX >> ((8U - access_size) * 8U);
                                    out &= mask;
                                    hv_vcpu_set_reg(vcpu, reg_idx_to_hv(reg_idx), out);
                                }
                            }
                            pc += 4;
                            hv_vcpu_set_reg(vcpu, HV_REG_PC, pc);
                            break;
                        }
                    }
                    fprintf(stderr, "Unexpected VM exception: 0x%llx, EC 0x%x, GVA 0x%llx, GPA 0x%llx\n",
                        syndrome,
                        EC(syndrome),
                        vcpu_exit->exception.virtual_address,
                        vcpu_exit->exception.physical_address
                    );
                    ctx->vmec = EXIT_FAILURE;
                    goto exit;
                }
                default: {
                    fprintf(stderr, "Unexpected VM exception: 0x%llx, EC 0x%x, GVA 0x%llx, GPA 0x%llx\n",
                        syndrome,
                        EC(syndrome),
                        vcpu_exit->exception.virtual_address,
                        vcpu_exit->exception.physical_address
                    );
                    ctx->vmec = EXIT_FAILURE;
                    goto exit;
                }
            }
        } 
        else {
            fprintf(stderr, "Unexpected VM exit reason: %d\n", vcpu_exit->reason);
            ctx->vmec = EXIT_FAILURE;
            goto exit;
        }
    }
exit:
    if(ctx->vmec) {
        fprintf(stderr, "VM Failure! Exiting...");
    } else {
        fprintf(stdout, "Exiting..");
    }
    StopIOLoop();
    hv_vcpu_destroy(vcpu);
    return NULL;

}