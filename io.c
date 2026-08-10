#include "io.h"
#include "uart.h"
#include <unistd.h>
#include "watercheese_types.h"

static struct wc_mmio_region registry[WC_NUM_DEVICES];

// default results given errors
static const struct wc_mmio_result RESULT_ERROR_UNMAPPED = {
    .status=WC_STATUS_UNMAPPED,
    .read_value=-1
};
static const struct wc_mmio_result RESULT_ERROR_UNSUPPORTED_SIZE = {
    .status=WC_STATUS_UNSUPPORTED_SIZE,
    .read_value=-1
};
static const struct wc_mmio_result RESULT_ERROR_MISALIGNED = {
    .status=WC_STATUS_MISALIGNED,
    .read_value=-1
};
static const struct wc_mmio_result RESULT_ERROR_DEVICE = {
    .status=WC_STATUS_DEVICE_ERROR,
    .read_value=-1
};


// Device interfaces
static const struct wc_mmio_device_intf uart_dev_intf = {
    .write=&uart_write
};


int InitIO() {

    registry[WC_UART].name = "UART";
    registry[WC_UART].base = UART_BASE;
    registry[WC_UART].size = UART_SIZE;
    registry[WC_UART].device = uart_init(); // point to uart state table
    registry[WC_UART].intf = &uart_dev_intf; // point to uart callback table

    registry[WC_RTC].name = "RTC"; 
    registry[WC_RTC].base = RTC_BASE; 
    registry[WC_RTC].size = RTC_SIZE; 

    registry[WC_GICD].name = "GICD"; 
    registry[WC_GICD].base = GICD_BASE; 
    registry[WC_GICD].size = GICD_SIZE; 

    registry[WC_GICR].name = "GICR"; 
    registry[WC_GICR].base = GICR_BASE; 
    registry[WC_GICR].size = GICR_SIZE; 

    registry[WC_VIRTIO].name = "VIRTIO"; 
    registry[WC_VIRTIO].base = VIRTIO_BASE; 
    registry[WC_VIRTIO].size = VIRTIO_SIZE; 


    return 0;

}
struct wc_mmio_region* get_mmio_region(uintptr_t base) {
    // GICD
    if (base >= GICD_BASE && base < GICD_BASE + GICD_SIZE) return &registry[WC_GICD];
    if (base >= GICR_BASE && base < GICR_BASE + GICR_SIZE) return &registry[WC_GICR];
    if (base >= UART_BASE && base < UART_BASE + UART_SIZE) return &registry[WC_UART];
    if (base >= RTC_BASE && base < RTC_BASE + RTC_SIZE) return &registry[WC_RTC];
    if (base >= VIRTIO_BASE && base < VIRTIO_BASE + VIRTIO_SIZE) return &registry[WC_VIRTIO];
    return NULL;
}

struct wc_mmio_result handle_mmio(struct wc_vcpu_mmio_exit* mx) {
    struct wc_mmio_region* mmio_region = get_mmio_region(mx->fault_gpa);
    if(mmio_region == NULL) return RESULT_ERROR_UNMAPPED;
    uint64_t offset = mx->fault_gpa - mmio_region->base;
    // if the offset goes past the size or the access size would go past the region - offset
    if(offset >= mmio_region->size || mx->access_size > (mmio_region->size - offset)) return RESULT_ERROR_UNSUPPORTED_SIZE;
    if(mx->fault_gpa % mx->access_size != 0) return RESULT_ERROR_MISALIGNED;
    if(mmio_region->device == NULL || mmio_region->intf == NULL) return RESULT_ERROR_DEVICE;
    struct wc_mmio_access access = (struct wc_mmio_access) {.direction=mx->direction, .gpa=mx->fault_gpa, .offset=offset, .size=mx->access_size, .vcpu_id=mx->vcpu_id, .write_value=mx->write_value};
    uint64_t out;
    if(mx->direction) {
        return (struct wc_mmio_result) {.status=mmio_region->intf->write(mmio_region->device, &access), .read_value=-1};
    }
    else {
        return (struct wc_mmio_result) {.status=mmio_region->intf->read(mmio_region->device, &access, &out), .read_value=out};
    }


}