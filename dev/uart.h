// NS16550A UART emulation
#ifndef UART_H_
#define UART_H_
#include <stdint.h>
#include "io.h"

/*
struct uart_regs {
    union {
        char rbr; // DLAB=0 read
        char thr; // DLAB=0 write
        uint8_t dll; // DLAB=1
    };
    
    union {
        uint8_t ier; // DLAB=0
        uint8_t dlm; // DLAB=1
    };
    
    union {
        uint8_t iir; // read
        uint8_t fcr; // write
    };

    uint8_t lcr;
    uint8_t mcr;
    uint8_t lsr;
    uint8_t msr;
    uint8_t scr;
};
*/
#define REG_SHIFT 2
#define UART_NUM_REGS 7
#define OFFSET_TO_REGISTER(offset) offset >> REG_SHIFT
#define LCR_DLAB (1 << 7)
#define LSR_OE (1 << 1)
#define LSR_DR (1 << 0)
#define LSR_THRE (1 << 5)
#define IER_DRIE (1 << 0)
#define IER_THREIE (1 << 1)

struct wc_uart {
    uint8_t ier; // interrupt enable
    uint8_t lcr; // line control
    uint8_t mcr; // modem control
    uint8_t scr; // scratch
    // with LCR.DLAB=1 -> divisor latch
    uint8_t dll; // low
    uint8_t dlm; // hi
    uint8_t fcr; // fifo configuration register
    int backend_fd; // backend file descriptor where the uart feeds to
};


// Register Layout
/*
| Offset |          GPA | DLAB=0    | DLAB=1    |
| -----: | -----------: | --------- | --------- |
| `0x00` | `0x09000000` | RBR / THR | DLL       |
| `0x04` | `0x09000004` | IER       | DLM       |
| `0x08` | `0x09000008` | IIR / FCR | IIR / FCR |
| `0x0C` | `0x0900000C` | LCR       | LCR       |
| `0x10` | `0x09000010` | MCR       | MCR       |
| `0x14` | `0x09000014` | LSR       | LSR       |
| `0x18` | `0x09000018` | MSR       | MSR       |
| `0x1C` | `0x0900001C` | SCR       | SCR       |
*/
enum mmio_status uart_write(struct wc_uart* device, struct wc_mmio_access* access);





#endif