#include "uart.h"
#include "io.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define LCR_DLAB (1 << 7)
#define FCR_EN (1 << 0)
#define FCR_CLEAR (1 << 2)
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
enum mmio_status uart_read(void* device, const struct wc_mmio_access* access, uint64_t* value) {
    struct wc_uart* udevice = device;
    uint8_t reg = OFFSET_TO_REGISTER(access->offset);
    uint8_t result = 0;

    pthread_mutex_lock(&udevice->uart_lock);

    switch (reg) {
        case 0: {
            if (udevice->lcr & LCR_DLAB) result = udevice->dll;
            else result = 0;
            break;
        }
        case 1: {
            if (udevice->lcr & LCR_DLAB) result = udevice->dlm;
            else result = udevice->ier;
            break;
        }
        case 2:
            result = 0x01;
            if (udevice->fcr & FCR_EN) result |= 0xC0;
            break;

        case 3:
            result = udevice->lcr;
            break;

        case 4:
            result = udevice->mcr;
            break;

        case 5:
            if (udevice->tx_cnt == 0) result |= (LSR_THRE | LSR_TEMT);
            break;

        case 6:
            result = 0;
            break;

        case 7:
            result = udevice->scr;
            break;

        default:
            pthread_mutex_unlock(&udevice->uart_lock);
            return WC_STATUS_DEVICE_ERROR;
    }

    pthread_mutex_unlock(&udevice->uart_lock);
    *value = result;

    return WC_STATUS_SUCCESS;
}

enum mmio_status uart_write(void* device, const struct wc_mmio_access* access) {
    struct wc_uart* udevice = (struct wc_uart*) device;
    uint8_t reg = OFFSET_TO_REGISTER(access->offset);
    // TODO: Note that since this is a character device, we only write one byte, but we accept one word, probably something should be fixed later
    uint8_t write_val = (uint8_t) access->write_value;
    uint8_t request_tx = 0;
    pthread_mutex_lock(&udevice->uart_lock);
    switch (reg) {
    case 0: {
        if((udevice->lcr & LCR_DLAB) != 0) {
            udevice->dll = write_val;
            break;
        } else {
            size_t cap = (udevice->fcr & FCR_EN) ? WC_UART_FIFO_MAX_SIZE : 1;
            if(udevice->tx_cnt >= cap) {
                udevice->tx_head = (udevice->tx_head + 1) % WC_UART_FIFO_MAX_SIZE;
                udevice->tx_cnt -= 1;
            }
            udevice->tx_fifo[udevice->tx_tail] = write_val;
            udevice->tx_tail = (udevice->tx_tail + 1) % WC_UART_FIFO_MAX_SIZE;
            udevice->tx_cnt++;

            request_tx = 1;
            break;
        }
    }
    case 1: {
        if((udevice->lcr & LCR_DLAB) != 0) {
            udevice->dlm = write_val;
        } else {
            udevice->ier = write_val;
        }
        break;
    }
    case 2: {
        if(write_val & FCR_CLEAR) {
            udevice->tx_head = 0;
            udevice->tx_tail = 0;
            udevice->tx_cnt = 0;
        }
        udevice->fcr = write_val & FCR_EN;
        break;
    }
    case 3: {
        udevice->lcr = write_val;
        break;
    }
    case 4: {
        udevice->mcr = write_val;
        break;
    }
    case 5: {
        pthread_mutex_unlock(&udevice->uart_lock);
        return WC_STATUS_DEVICE_ERROR;
    }
    case 6: {
        pthread_mutex_unlock(&udevice->uart_lock);
        return WC_STATUS_DEVICE_ERROR;
    }
    case 7: {
        udevice->scr = write_val;
        break;
    }
    default: {
        pthread_mutex_unlock(&udevice->uart_lock);
        return WC_STATUS_DEVICE_ERROR;
     }
    }
    pthread_mutex_unlock(&udevice->uart_lock);
    if(request_tx) {
        // TODO: request tx service from the character backend
        WakeIOLoop();
    }

    return WC_STATUS_SUCCESS;

}