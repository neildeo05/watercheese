#include "vcpu.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include "machine.h"
#include "io.h"

#include <Hypervisor/Hypervisor.h>

static inline void vcpu_creation_failure(struct wc_vcpu_ctx* ctx) {
    pthread_mutex_lock(&ctx->machine->ready_lock);
    ctx->machine->creation_failed = 1;
    pthread_cond_broadcast(&ctx->machine->ready_cv);
    pthread_mutex_unlock(&ctx->machine->ready_lock);
    RequestMachineStop(EXIT_FAILURE);

}
// needs to be void* worker(void* p) for pthread_create to work
void* vcpu_worker(void* ctp) {
    struct wc_vcpu_ctx* ctx = ctp;
    ctx->state = WC_VCPU_UNDEFINED;
    ctx->created = false;

    hv_vcpu_t vcpu;
    hv_vcpu_exit_t *vcpu_exit;
    if (hv_vcpu_create(&vcpu, &vcpu_exit, NULL) != HV_SUCCESS) {
        fprintf(stderr, "Failed to create vCPU (id=%d)\n", ctx->vcpu_id);
        vcpu_creation_failure(ctx);
        goto exit;
    }
    ctx->vcpu = vcpu;
    ctx->exit = vcpu_exit;
    ctx->created = true;

    if(hv_vcpu_set_trap_debug_exceptions(vcpu, true) != HV_SUCCESS) {
        fprintf(stderr, "Failed to create vCPU (id=%d)\n", ctx->vcpu_id);
        vcpu_creation_failure(ctx);
        goto exit;

    }
    if(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_MPIDR_EL1, ctx->mpidr) != HV_SUCCESS) {
        fprintf(stderr, "Failed to create vCPU (id=%d)\n", ctx->vcpu_id);
        vcpu_creation_failure(ctx);
        goto exit;
    }
    if(ctx->is_boot) { 
        // Set the P-state of the BOOT processor to be:
        // 0x005 -> run at EL1
        // 0x3C0 -> all interrupts masked initially
        if(hv_vcpu_set_reg(vcpu, HV_REG_CPSR, 0x3c5) != HV_SUCCESS) {
            fprintf(stderr, "Failed to create vCPU (id=%d)\n", ctx->vcpu_id);
            vcpu_creation_failure(ctx);
            goto exit;
            
        }
        if(hv_vcpu_set_reg(vcpu, HV_REG_PC, PAYLOAD_BASE) != HV_SUCCESS) {
            fprintf(stderr, "Failed to create vCPU (id=%d)\n", ctx->vcpu_id);
            vcpu_creation_failure(ctx);
            goto exit;
        }

        if(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_SP_EL1, STACK_BASE + STACK_SIZE) != HV_SUCCESS) {
            fprintf(stderr, "Failed to create vCPU (id=%d)\n", ctx->vcpu_id);
            vcpu_creation_failure(ctx);
            goto exit;
        }

    }
    ctx->state = WC_VCPU_READY;
    if(!ctx->is_boot) {
        pthread_mutex_lock(&ctx->waiting_lock);
        ctx->state = WC_VCPU_WAITING;
        pthread_mutex_unlock(&ctx->waiting_lock); 
    }


    // NOTE: HVF requires the vCPU topology to be fixed before hv_vcpu_run gets called (makes sense i mean)
    // so here we have to wait until the machine is fully created, because we use pthread_create for each vCPU,
    // and we need to ensure that no worker gets to vcpu run
    bool creation_failed = false;
    pthread_mutex_lock(&ctx->machine->ready_lock);
    ctx->machine->ready_count++;
    pthread_cond_broadcast(&ctx->machine->ready_cv);
    if(ctx->is_boot) {
        while(ctx->machine->ready_count < ctx->machine->ncpus && !ctx->machine->creation_failed && ctx->machine->state != STOPPING) {
            pthread_cond_wait(&ctx->machine->ready_cv, &ctx->machine->ready_lock);
        }
        creation_failed = ctx->machine->creation_failed;
        pthread_mutex_unlock(&ctx->machine->ready_lock);
        if(creation_failed) {
            RequestMachineStop(EXIT_FAILURE);
            goto exit;
        }
        if(ctx->machine->state == STOPPING) {
            goto exit;
        }
        ctx->state = WC_VCPU_RUNNING;
    }
    else { // !is_boot
        // we have to make sure that all secondary vCPUs wait until PSCI awakes them
        pthread_mutex_unlock(&ctx->machine->ready_lock);
        pthread_mutex_lock(&ctx->waiting_lock);
        while(ctx->state == WC_VCPU_WAITING && ctx->machine->state != STOPPING) {
            pthread_cond_wait(&ctx->waiting_cv, &ctx->waiting_lock);
        }
        bool is_stopping = ctx->machine->state == STOPPING;
        bool is_waking = ctx->state == WC_VCPU_WAKING;
        pthread_mutex_unlock(&ctx->waiting_lock);
        if(is_stopping) {
            goto exit;
        }
        if(is_waking) {
            ctx->state = WC_VCPU_RUNNING;
        } else {
            RequestMachineStop(EXIT_FAILURE);
            goto exit;
        }
    }



    // Infinite loop for the VM's runtime
    for (;;) {
        if(ctx->machine->state == STOPPING) {
            goto exit; // every time we come back from a vmexit, we should check and see if there is a stoppping request.
        }
        /*
        From qemu mailing list:
        I addressed pselect() in my other reply.
        It isn't on the website but the hv_vcpu.h header says this about
        hv_vcpus_exit():
        * @discussion
        *             If a vcpu is not running, the next time hv_vcpu_run is
        called for the corresponding
        *             vcpu, it will return immediately without entering the guest.

        So at least as documented I think we are okay.
         */


        // This call blocks until the VM exits
        // I am assuming it traps into EL2, the hypervisor/lowvisor in EL2 world switches to the VM context on this thread
        hv_return_t ret = hv_vcpu_run(vcpu);
        if(ctx->machine->state == STOPPING) { // vCPU was kicked out, needs to exit now
            goto exit;
        }
        if (ret != HV_SUCCESS) {
            fprintf(stderr, "Failed to run vcpu (id=%d)\n", ctx->vcpu_id);
            RequestMachineStop(EXIT_FAILURE);
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
                        RequestMachineStop(EXIT_FAILURE);
                        goto exit;
                    } 
                    else if(x0 == 0x122) {
                        RequestMachineStop(EXIT_SUCCESS);
                        goto exit;
                    } 
                    RequestMachineStop(EXIT_FAILURE);
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
                    RequestMachineStop(EXIT_FAILURE);
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
                    RequestMachineStop(EXIT_FAILURE);
                    goto exit;
                }
                default: {
                    fprintf(stderr, "Unexpected VM exception: 0x%llx, EC 0x%x, GVA 0x%llx, GPA 0x%llx\n",
                        syndrome,
                        EC(syndrome),
                        vcpu_exit->exception.virtual_address,
                        vcpu_exit->exception.physical_address
                    );
                    RequestMachineStop(EXIT_FAILURE);
                    goto exit;
                }
            }
        } 
        else {
            fprintf(stderr, "Unexpected VM exit reason: %d\n", vcpu_exit->reason);
            RequestMachineStop(EXIT_FAILURE);
            goto exit;
        }
    }
exit:
    RequestMachineStop(EXIT_FAILURE);
    ctx->state = WC_VCPU_STOPPING;
    pthread_mutex_lock(&ctx->lifecycle_lock);
    if(ctx->created) {
        if(hv_vcpu_destroy(ctx->vcpu) != HV_SUCCESS) {
            fprintf(stderr, "Failed to destory vCPU %d\n", ctx->vcpu_id);
        }
        ctx->created = false;
    }
    pthread_mutex_unlock(&ctx->lifecycle_lock);
    ctx->state = WC_VCPU_STOPPED;
    return NULL;

}