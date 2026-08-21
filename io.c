#include "io.h"
#include "uart.h"
#include <unistd.h>
#include <termios.h>
#include "watercheese_types.h"
#include <sys/event.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <util.h>




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
    .read=&uart_read,
    .write=&uart_write
};


static struct wc_uart uart = {
    .dll=0, .dlm=0, .fcr=0, .ier=0, .lcr=0, .mcr=0, .scr=0, .tx_cnt=0, .tx_head=0, .tx_tail=0
}; // static uart

static struct wc_io_loop loop;
static struct wc_char_backend_device backend_dev;

static void ServiceTX(struct wc_char_backend_device* backend_dev) {
    struct wc_uart* uart = backend_dev->uart;
    pthread_mutex_lock(&uart->uart_lock);
    while(uart->tx_cnt > 0) {
        uint8_t head_byte = uart->tx_fifo[uart->tx_head % WC_UART_FIFO_MAX_SIZE];
        ssize_t ret = write(backend_dev->fd, &head_byte, 1);
        if(ret < 0) {
            if (errno == EWOULDBLOCK) {
                struct kevent ev;
                EV_SET(&ev, backend_dev->fd, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, NULL);
                if(kevent(loop.kq, &ev, 1, NULL, 0, NULL) == -1) {
                    perror("kevent register");
                    pthread_mutex_unlock(&uart->uart_lock);
                    return;
                }
                pthread_mutex_unlock(&uart->uart_lock);
                return;
            }
            if(errno == EINTR) {
                continue;
            }
            else {
                perror("backend write");
                pthread_mutex_unlock(&uart->uart_lock);
                return;
            }
        }
        else if (ret == 0) {
            fprintf(stderr, "Backend write not working...\n");
            pthread_mutex_unlock(&uart->uart_lock);
            return;
        }
        else {
            uart->tx_head += ret;
            uart->tx_cnt -= ret;
        }
    }
    struct kevent ev;
    EV_SET(&ev, backend_dev->fd, EVFILT_WRITE, EV_DISABLE, 0, 0, NULL);
    kevent(loop.kq, &ev, 1, NULL, 0, NULL); // ENOENT is fine for us

    pthread_mutex_unlock(&uart->uart_lock);

}

int InitIOLoop() {
    int kq;
    if ((kq = kqueue()) == -1) {
        perror("kqueue");
        return -1;
    }
    struct kevent ev;
    // register a user event called wc_io_wake, after it has been serviced, clear its state (re-use the event)
    EV_SET(&ev, WC_IO_WAKE, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, NULL);
    if(kevent(kq, &ev, 1, NULL, 0, NULL) == -1) {
        perror("kevent");
        close(kq);
        return -1;
    }

    loop.kq = kq;
    atomic_init(&loop.stopping_flag, false);
    return 0;
}

int RunIOLoop() {
    // Get's populated when we switch back to the I/O thread if there are new events
    struct kevent events[WC_IO_MAX_EVENTS];
    while(!atomic_load(&loop.stopping_flag)) {
        // wait indefinitely until the registered event becomes ready
        int nev;
        if((nev = kevent(loop.kq, NULL, 0, events, WC_IO_MAX_EVENTS, NULL)) == -1) {
            if(errno == EINTR) continue;
            perror("kevent");
            return -1;
        }

        // StopIOLoop got called
        if(atomic_load(&loop.stopping_flag)) break;

        for(int i = 0; i < nev; i++) {
            struct kevent* ev = &events[i];
            if(ev->flags & EV_ERROR) {
                return -1;
            }
            // WC_IO_WAKE is a user event that gets triggered when we want to service the backend
            else if(ev->filter == EVFILT_USER && ev->ident == WC_IO_WAKE) {
                ServiceTX(&backend_dev);
            }
            // Idk if this is necessary, only happens if the backend device wouldblock on a write
            else if(ev->filter == EVFILT_WRITE && ev->ident == (uintptr_t) backend_dev.fd) {
                ServiceTX(&backend_dev);
            }
        }
    }
    return 0;
}


int WakeIOLoop() {
    struct kevent ev;
    // The already registered WC_IO_WAKE event should be triggered
    EV_SET(&ev, WC_IO_WAKE, EVFILT_USER, 0, NOTE_TRIGGER, 0, NULL);
    int ret = kevent(loop.kq, &ev, 1, NULL, 0, NULL);
    if(ret == -1) return -1;
    return 0;
}
int StopIOLoop() {
    atomic_store(&loop.stopping_flag, 1);
    return WakeIOLoop();
}

int InitCharBackend() {
    // openpty(&master_fd, &slave)
    if(openpty(&backend_dev.fd, &backend_dev.sfd, backend_dev.slave_name, NULL, NULL) == -1) {
        perror("openpty");
        return -1;
    }

    struct termios t;
    if(tcgetattr(backend_dev.sfd, &t) == -1) {
        perror("tcgetattr"); close(backend_dev.sfd); close(backend_dev.fd);
        return -1;
    }

    cfmakeraw(&t);
    if(tcsetattr(backend_dev.sfd, TCSANOW, &t) == -1) {
        perror("tcsetattr"); close(backend_dev.sfd); close(backend_dev.fd);
        return -1;
    }
    fprintf(stderr, "Guest UART console: %s\n", backend_dev.slave_name);
    // make the host filedescriptor non blocking so our threads don't sleep for ts
    int flags = fcntl(backend_dev.fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl getfl"); close(backend_dev.sfd); close(backend_dev.fd);
        return -1;
    }
    if(fcntl(backend_dev.fd, F_SETFL, flags|O_NONBLOCK) == -1) {
        perror("fcntl setfl"); close(backend_dev.sfd); close(backend_dev.fd);
        return -1;
    }
    backend_dev.io_loop=&loop;
    return 0;

}



int InitIO() {
    int ret = InitIOLoop();
    if(ret < 0) return ret;
    ret = InitCharBackend();
    if(ret < 0) return ret;
    while (getchar() != '\n');

    registry[WC_UART].name = "UART";
    registry[WC_UART].base = UART_BASE;
    registry[WC_UART].size = UART_SIZE;
    if(pthread_mutex_init(&uart.uart_lock, NULL) != 0) {
        perror("pthread mutex init");
        return -1;
    }
    uart.backend_dev = &backend_dev;
    backend_dev.uart = &uart;
    registry[WC_UART].device = &uart; // point to uart state table
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
void DestroyIO() {
    pthread_mutex_destroy(&uart.uart_lock);
    close(backend_dev.sfd); close(backend_dev.fd);
    close(loop.kq);

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
    uint64_t out = -1;
    if(mx->direction) {
        return (struct wc_mmio_result) {.status=mmio_region->intf->write(mmio_region->device, &access), .read_value=-1};
    }
    else {
        return (struct wc_mmio_result) {.status=mmio_region->intf->read(mmio_region->device, &access, &out), .read_value=out};
    }


}