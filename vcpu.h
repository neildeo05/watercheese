#ifndef VCPU_H_
#define VCPU_H_
#include "watercheese_types.h"
#include <stdint.h>
#include <Hypervisor/Hypervisor.h>
#include <pthread.h>
#include <stdatomic.h>

#define WC_VCPU_KICK_MACHINE_STOP 1

enum wc_vcpu_state {
    WC_VCPU_UNDEFINED = 0,
    WC_VCPU_READY,
    WC_VCPU_RUNNING,
    WC_VCPU_WAITING,
    WC_VCPU_WAKING,
    WC_VCPU_STOPPING,
    WC_VCPU_STOPPED
};

struct wc_vcpu_ctx {
    bool is_boot; // desingates the boot CPU

    uint32_t vcpu_id;
    uint64_t mpidr;
    struct wc_machine* machine;
    pthread_t host_tid;
    hv_vcpu_t vcpu;
    hv_vcpu_exit_t* exit;
    atomic_bool created;
    atomic_int state;
    pthread_mutex_t waiting_lock;
    pthread_cond_t waiting_cv;

    pthread_mutex_t lifecycle_lock; // this ensures that we can't be deleted while in the middle of being created

};

void* vcpu_worker(void* ctx);






#endif