// Watercheese Virtual Machine Monitor
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include "wcutil.h"
#include <sys/mman.h>
#include "watercheese_types.h"
#include "machine.h"
#include "io.h"
#include "vcpu.h"
#include <pthread.h>

#include <Hypervisor/Hypervisor.h>
#define NVCPUS 1
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

struct wc_machine mach = {
    .state=INITIALIZED,
    .ncpus = NVCPUS,
    .ready_count=0,
    .creation_failed=0
};

int InitMemory() {
    long page_size = sysconf(_SC_PAGESIZE);
    int err = posix_memalign(&MainMemory, page_size, RAM_SIZE);
    if (err != 0 || MainMemory == NULL) {
        return -ENOMEM;
    }
    memset(MainMemory, 0, RAM_SIZE);
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
    return 0;
}

int InitCPUs(struct wc_machine* mach) {
    for(int i = 0; i < mach->ncpus; i++) {
        mach->vcpus[i] = (struct wc_vcpu_ctx) {
            .vcpu_id=i,
            .mpidr=i,
            .created=false,
            .state=WC_VCPU_UNDEFINED,
            .is_boot=(i==0),
            .machine=mach
        };
        if(pthread_mutex_init(&mach->vcpus[i].waiting_lock, NULL) != 0) return errno;
        if(pthread_mutex_init(&mach->vcpus[i].lifecycle_lock, NULL) != 0) return errno;
        if(pthread_cond_init(&mach->vcpus[i].waiting_cv, NULL) != 0) return errno;

    }
    return 0;

}
int RequestMachineStop(int vmec) {
    enum wc_machine_state expected_state = RUNNING;
    if(!atomic_compare_exchange_strong(&mach.state, &expected_state, STOPPING)) return 0; // someone else is stopping la machina

    mach.vmec = vmec;
    StopIOLoop(); // notify the main thread that the I/O loop should stop
    pthread_mutex_lock(&mach.ready_lock);
    pthread_cond_broadcast(&mach.ready_cv);
    pthread_mutex_unlock(&mach.ready_lock);


    // Wake all vCPUs currently waiting, we wake them (they will destroy themselves)
    for(int i = 0; i < mach.ncpus; i++) {
        if(!mach.vcpus[i].is_boot) {
            pthread_mutex_lock(&mach.vcpus[i].waiting_lock);
            pthread_cond_broadcast(&mach.vcpus[i].waiting_cv);
            pthread_mutex_unlock(&mach.vcpus[i].waiting_lock);
        }
    }

    // For all vCPUs that are currently running
    for(int i = 0; i < mach.ncpus; i++) {
        // The lifecyle lock essentially 
        pthread_mutex_lock(&mach.vcpus[i].lifecycle_lock);
        if(mach.vcpus[i].created) {
            if(hv_vcpus_exit(&mach.vcpus[i].vcpu, 1) != HV_SUCCESS) {
                mach.vmec = EXIT_FAILURE;
            }
        }
        pthread_mutex_unlock(&mach.vcpus[i].lifecycle_lock);
    }
    return 1;
}


int main(void)
{
    // Contains exit reasons, I am assuming similar to the VMCS exit reasons
    hv_vm_create(NULL);
    if (InitMemory()) abort();
    if (InitGIC()) abort(); // TODO: implement this
    if (InitIO()) abort();
    mach.vmec = -1;
    mach.ncpus = 1;
    mach.state = RUNNING;
    pthread_mutex_init(&mach.ready_lock, NULL);
    pthread_cond_init(&mach.ready_cv, NULL);
    if (InitCPUs(&mach)) abort();
    
    // Create a vCPU associated with the current host thread
    // if(pthread_create(&vcpu_thread, NULL, vcpu_worker, &vcpu0) != 0) {
    //     fprintf(stderr, "pthread create failed");
    //     hv_vm_destroy();
    //     free(MainMemory);
    //     return EXIT_FAILURE;
    // }
    for(int i = 0; i < mach.ncpus; i++) {
        if(pthread_create(&mach.vcpus[i].host_tid, NULL, vcpu_worker, &mach.vcpus[i]) != 0) {
            fprintf(stderr, "pthread create failed");
            pthread_mutex_lock(&mach.ready_lock);
            mach.creation_failed = 1;
            pthread_cond_broadcast(&mach.ready_cv);
            pthread_mutex_unlock(&mach.ready_lock);
            RequestMachineStop(EXIT_FAILURE);
        }
    }
    RunIOLoop();
    // TODO: don't naively assume that every pthread was created, don't want to join one that doesn't exist cause that would block
    for(int i = 0; i < mach.ncpus; ++i) {
        pthread_join(mach.vcpus[i].host_tid, NULL);
    }
    mach.state = STOPPED;
    // pthread_join(vcpu_thread, NULL);
    // vmec = vcpu0.vmec;
    DestroyIO();
    DestroyGIC();
    hv_vm_destroy();
    free(MainMemory);
    pthread_mutex_destroy(&mach.ready_lock);
    pthread_cond_destroy(&mach.ready_cv);
    for(int i = 0; i < mach.ncpus; i++) {
        pthread_mutex_destroy(&mach.vcpus[i].lifecycle_lock);
        pthread_mutex_destroy(&mach.vcpus[i].waiting_lock);
        pthread_cond_destroy(&mach.vcpus[i].waiting_cv);
    }
    return mach.vmec;

}
