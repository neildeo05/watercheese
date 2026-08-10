#include "uart.h"
#include "io.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define LCR_DLAB (1 << 7)
struct wc_uart* uart_init() {
    struct wc_uart* uart = malloc(sizeof(struct wc_uart));
    memset(uart, 0, sizeof(struct wc_uart));
    (uart)->backend_fd = STDOUT_FILENO;
    return uart;
}
enum mmio_status uart_write(struct wc_uart* device, struct wc_mmio_access* access) {
    uint8_t reg = OFFSET_TO_REGISTER(access->offset);
    // TODO: Note that since this is a character device, we only write one byte, but we accept one word, probably something should be fixed later
    uint8_t write_val = (uint8_t) access->write_value;

/*
      | Offset |          GPA | DLAB=0    | DLAB=1    |
      | -----: | -----------: | --------- | --------- |
    0  | `0x00` | `0x09000000` | RBR / THR | DLL       |
    1  | `0x04` | `0x09000004` | IER       | DLM       |
    2  | `0x08` | `0x09000008` | IIR / FCR | IIR / FCR |
    3  | `0x0C` | `0x0900000C` | LCR       | LCR       |
    4  | `0x10` | `0x09000010` | MCR       | MCR       |
    5  | `0x14` | `0x09000014` | LSR       | LSR       |
    6  | `0x18` | `0x09000018` | MSR       | MSR       |
    7  | `0x1C` | `0x0900001C` | SCR       | SCR       |
*/
   switch (reg) {
    case 0: {
        if((device->lcr & LCR_DLAB) != 0) {
            device->dll = write_val;
        } else {
            write(device->backend_fd, &write_val, 1);
        }
        return WC_STATUS_SUCCESS;
    }
    case 1: {
        if((device->lcr & LCR_DLAB) != 0) {
            device->dlm = write_val;
        } else {
            device->ier = write_val;
        }
        return WC_STATUS_SUCCESS;
    }
    case 2: {
        device->fcr = write_val;
        return WC_STATUS_SUCCESS;
    }
    case 3: {
        device->lcr = write_val;
        return WC_STATUS_SUCCESS;
    }
    case 4: {
        device->mcr = write_val;
        return WC_STATUS_SUCCESS;
    }
    case 5: {
        return WC_STATUS_DEVICE_ERROR;
    }
    case 6: {
        return WC_STATUS_DEVICE_ERROR;
    }
    case 7: {
        device->scr = write_val;
        return WC_STATUS_SUCCESS;
    }
    default: {
        return WC_STATUS_DEVICE_ERROR;
    }
   }
}