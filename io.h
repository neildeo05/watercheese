#ifndef IO_H_
#define IO_H_
#include <stdint.h>
#include <stdatomic.h>
#define WC_IO_WAKE 42
#define WC_IO_MAX_EVENTS 8
#define WC_NAME_MAX 256
enum mmio_type {
    WC_UNDEF,
    WC_GICD,
    WC_GICR,
    WC_UART,
    WC_RTC,
    WC_VIRTIO,
    WC_NUM_DEVICES
};

// need to add status for unsupported size and to cross region
enum mmio_status {
    WC_STATUS_SUCCESS,
    WC_STATUS_UNMAPPED,
    WC_STATUS_UNSUPPORTED_SIZE,
    WC_STATUS_MISALIGNED,
    WC_STATUS_DEVICE_ERROR

};

struct wc_vcpu_mmio_exit {
    uint64_t syndrome;
    uint64_t fault_gpa;
    uint64_t guest_pc;
    uint64_t write_value;
    uint32_t vcpu_id;
    uint8_t access_size;
    uint8_t reg_idx;
    uint8_t direction;
};

struct wc_mmio_access {
    uint64_t gpa;
    uint64_t offset;
    uint64_t write_value;
    uint32_t vcpu_id;
    uint8_t size;
    uint8_t direction;
};

struct wc_mmio_result {
    enum mmio_status status;
    uint64_t read_value;
};


struct wc_mmio_device_intf {
    enum mmio_status (*read) (
        void* device,
        const struct wc_mmio_access* access,
        uint64_t* value
    );

    enum mmio_status (*write) (
        void* device,
        const struct wc_mmio_access* access
    );


};

struct wc_mmio_region {
    const char* name;
    uint64_t base;
    uint64_t size;

    void* device;
    const struct wc_mmio_device_intf* intf;
};


/// For I/O loop and backend devices
struct wc_io_loop {
    int kq; // kqueue descriptor
    atomic_bool stopping_flag; // tells main loop when to exit
};
struct wc_char_backend_device {
    int fd; // master FD for the PTY
    int sfd; // sfd for the pty
    char slave_name[WC_NAME_MAX];
    struct wc_io_loop* io_loop; // global io loop
    struct wc_uart* uart; // uart frontend
};

int InitIO();
void DestroyIO();
int RunIOLoop();
int WakeIOLoop();
int StopIOLoop();
struct wc_mmio_region* get_mmio_region(uintptr_t base);


struct wc_mmio_result handle_mmio(struct wc_vcpu_mmio_exit* exit);



#endif