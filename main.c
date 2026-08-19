// Built from https://gist.github.com/imbushuo/51b09e61ecd7b7ac063853ad65cedf34
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
#include "io.h"
#include "vcpu.h"
#include <pthread.h>

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

int InitMemory() {
    long page_size = sysconf(_SC_PAGESIZE);
    int err = posix_memalign(&MainMemory, page_size, RAM_SIZE);
    if (err != 0 || MainMemory == NULL) {
        return -ENOMEM;
    }

    memset(MainMemory, 0, RAM_SIZE);
#if WC_CUSTOM_PAYLOAD
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

int main(void)
{
    int vmec = EXIT_FAILURE;
    pthread_t vcpu_thread;
    struct wc_vcpu_ctx vcpu0 = {
        .vcpu_id =0,
        .vmec = vmec
    };
    // Contains exit reasons, I am assuming similar to the VMCS exit reasons
    hv_vm_create(NULL);
    if (InitMemory()) abort();
    if (InitIO()) abort();
    
    // Create a vCPU associated with the current host thread
    if(pthread_create(&vcpu_thread, NULL, vcpu_worker, &vcpu0) != 0) {
        fprintf(stderr, "pthread create failed");
        hv_vm_destroy();
        free(MainMemory);
        return EXIT_FAILURE;
    }
    RunIOLoop();
    pthread_join(vcpu_thread, NULL);
    vmec = vcpu0.vmec;
    if(vmec == EXIT_SUCCESS) fprintf(stdout, "Exitting...\n");
    else fprintf(stderr, "VM Failure, exitting...\n");
    DestroyIO();
    free(MainMemory);
    hv_vm_destroy();
    return vmec;

}
