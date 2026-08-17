#ifndef VCPU_H_
#define VCPU_H_
#include <stdint.h>

struct wc_vcpu_ctx {
    uint32_t vcpu_id;
    int vmec;
};

void* vcpu_worker(void* ctx);






#endif