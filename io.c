#include "io.h"
#include "watercheese_types.h"

enum mmio_type is_mmio_access(uintptr_t base) {
    // GICD
    if (base >= GICD_BASE && base < GICD_BASE + GICD_SIZE) return GICD;
    if (base >= GICR_BASE && base < GICR_BASE + GICR_SIZE) return GICR;
    if (base >= UART_BASE && base < UART_BASE + UART_SIZE) return PL011;
    if (base >= RTC_BASE && base < RTC_BASE + RTC_SIZE) return RTC;
    if (base >= VIRTIO_BASE && base < VIRTIO_BASE + VIRTIO_SIZE) return VIRTIO;
    return UNDEF;
}

int handle_mmio(struct mmio_excp_payload* p) {
    return -1;

}