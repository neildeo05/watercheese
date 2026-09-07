#ifndef WC_MACHINE_H_
#define WC_MACHINE_H_
#include "vcpu.h"
#define WC_MAX_VCPUS 16

enum wc_machine_state {
    INITIALIZED,
    RUNNING,
    STOPPING,
    STOPPED,
};
struct wc_machine {
    // whether the VM should continue existing
    atomic_int state; // prevent two threads from imposing a transition at the same time
    // status of the VM
    int vmec;
    // number of CPUs
    int ncpus;

    // all the cpus that belong to this machine
    struct wc_vcpu_ctx vcpus[WC_MAX_VCPUS];

    // HVF says that once one vCPU runs hvf_cpu_run, the topology is considered final
    pthread_mutex_t ready_lock;
    pthread_cond_t ready_cv;
    int ready_count;
    int creation_failed;
};

int RequestMachineStop(int vmec);

#endif