#ifndef IO_H_
#define IO_H_
#include <stdint.h>

enum mmio_type {
    UNDEF,
    GICD,
    GICR,
    PL011,
    RTC,
    VIRTIO
};

struct mmio_excp_payload {
    uint64_t syndrome;
    uint64_t fault_gpa;
    uint64_t offset;
    uint8_t r_w;
    uint64_t access_size;
    uint64_t reg_idx;
    uint64_t write_val;
    uint64_t vcpu_id; // index into the VCPU table
};

struct io_device_intf;

struct io_device {
    char* name;
    const struct io_device_intf* intf;
};

enum mmio_type is_mmio_access(uintptr_t base);


int handle_mmio(struct mmio_excp_payload* p);



#endif