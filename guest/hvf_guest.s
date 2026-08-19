.section .text.boot, "ax"
.align 4
.global _start
.type _start, %function

/*
 * WaterCheese development HVC ABI.
 */
.equ HCALL_BOOT,      0x120
.equ HCALL_SHUTDOWN,  0x122
.equ HCALL_PANIC,     0x1ff

/*
 * NS16550A UART.
 *
 * Guest-visible configuration:
 *
 *   base         = 0x09000000
 *   reg-shift    = 2
 *   reg-io-width = 4
 *
 * Therefore logical UART registers are spaced four bytes apart
 * and are accessed with 32-bit MMIO operations.
 */
.equ UART_BASE,       0x09000000

.equ UART_THR,        0x00
.equ UART_IER,        0x04
.equ UART_FCR,        0x08
.equ UART_LCR,        0x0c
.equ UART_LSR,        0x14

/*
 * LSR bits.
 */
.equ LSR_THRE,        (1 << 5)
.equ LSR_TEMT,        (1 << 6)


_start:
    /*
     * Enter in the same development configuration used by
     * the previous guest:
     *
     *     EL1h
     *     asynchronous exceptions masked
     *     guest Stage-1 MMU off
     */
    msr     daifset, #0xf

    /*
     * Verify that WaterCheese entered the payload at EL1.
     *
     * CurrentEL encodes EL1 as 0b01 << 2 = 0x4.
     */
    mrs     x9, CurrentEL
    cmp     x9, #0x4
    b.ne    .Lwrong_el

    /*
     * Install the guest stack.
     *
     * __stack_top is supplied by your linker script and should
     * resolve to:
     *
     *     0x40208000
     */
    adrp    x9, __stack_top
    add     x9, x9, :lo12:__stack_top
    mov     sp, x9

    /*
     * Install the guest EL1 vector table.
     */
    adrp    x9, vectors
    add     x9, x9, :lo12:vectors
    msr     vbar_el1, x9
    isb

    /*
     * Tell WaterCheese that guest bootstrap completed.
     */
    mov     x0, #HCALL_BOOT
    adrp    x1, _start
    add     x1, x1, :lo12:_start
    hvc     #0


    /*
     * ---------------------------------------------------------
     * Stage 1.3 UART TX test
     * ---------------------------------------------------------
     */

    /*
     * x20 = UART_BASE = 0x09000000
     *
     * 0x0900 << 16 = 0x09000000.
     */
    movz    x20, #0x0900, lsl #16

    /*
     * LCR = 0x03
     *
     * 8 data bits
     * no parity
     * one stop bit
     * DLAB = 0
     *
     * DLAB must be clear so register 0 is THR rather than DLL.
     */
    mov     w0, #0x03
    str     w0, [x20, #UART_LCR]

    /*
     * Interrupts disabled for this polling TX test.
     */
    mov     w0, #0x00
    str     w0, [x20, #UART_IER]

    /*
     * Enable the UART FIFO.
     */
    mov     w0, #0x01
    str     w0, [x20, #UART_FCR]

    /*
     * x21 = address of the test message.
     */
    adrp    x21, message
    add     x21, x21, :lo12:message


.Lprint_loop:
    /*
     * Fetch next byte from the message.
     */
    ldrb    w0, [x21], #1

    /*
     * NUL terminator means the entire string has been submitted.
     */
    cbz     w0, .Lprint_done


.Lwait_thre:
    /*
     * Poll LSR using a 32-bit MMIO access.
     *
     * With the current WaterCheese model:
     *
     *     tx_cnt == 0:
     *         THRE = 1
     *         TEMT = 1
     *
     *     tx_cnt > 0:
     *         THRE = 0
     *         TEMT = 0
     *
     * Therefore waiting for THRE ensures the previous byte has
     * been removed from WaterCheese's TX FIFO before submitting
     * another byte.
     */
    ldr     w1, [x20, #UART_LSR]
    tst     w1, #LSR_THRE
    b.eq    .Lwait_thre

    /*
     * DLAB=0, so logical register 0 is THR.
     *
     * Use a 32-bit store because WaterCheese exposes this UART
     * with reg-io-width = 4. uart_write() consumes the low byte.
     */
    str     w0, [x20, #UART_THR]

    b       .Lprint_loop


.Lprint_done:

.Lwait_temt:
    /*
     * Wait for the final queued byte to leave WaterCheese's
     * emulated TX FIFO before asking the VMM to shut down.
     *
     * This is important with your current StopIOLoop():
     * shutdown can otherwise cause the main event loop to stop
     * before the final UART character is serviced.
     */
    ldr     w1, [x20, #UART_LSR]
    tst     w1, #LSR_TEMT
    b.eq    .Lwait_temt

    /*
     * Successful test.
     */
    mov     x0, #HCALL_SHUTDOWN
    mov     x1, #0
    hvc     #0

    /*
     * Shutdown should be terminal.
     */
    brk     #0xdead


.Lwrong_el:
    /*
     * EL1 entry contract failed.
     */
    brk     #0x00e1
    b       .Lhang


.Lhang:
    wfe
    b       .Lhang

.size _start, . - _start


/*
 * =============================================================
 * EL1 exception diagnostic handler
 * =============================================================
 */

.section .text.handlers, "ax"
.align 4
.type vector_fatal, %function

vector_fatal:
    /*
     * x16 contains the vector slot number installed by the
     * corresponding vector-table stub.
     *
     * Panic ABI:
     *
     *     x0 = HCALL_PANIC
     *     x1 = vector slot
     *     x2 = ESR_EL1
     *     x3 = ELR_EL1
     *     x4 = SPSR_EL1
     *     x5 = FAR_EL1
     *     x6 = interrupted x0
     *     x7 = interrupted x1
     */

    mov     x6, x0
    mov     x7, x1

    mov     x1, x16
    mrs     x2, esr_el1
    mrs     x3, elr_el1
    mrs     x4, spsr_el1
    mrs     x5, far_el1

    mov     x0, #HCALL_PANIC
    hvc     #0

1:
    wfe
    b       1b

.size vector_fatal, . - vector_fatal


/*
 * =============================================================
 * AArch64 EL1 exception vector table
 * =============================================================
 *
 * VBAR_EL1 requires 2 KiB alignment.
 *
 * There are 16 vector entries, each exactly 0x80 bytes:
 *
 *     16 * 0x80 = 0x800
 *
 * This satisfies your linker's assertion that .vectors is
 * exactly 0x800 bytes long.
 */

.section .vectors, "ax"
.balign 0x800
.global vectors
.type vectors, %object

vectors:

.macro VECTOR_SLOT slot
    mov     x16, #\slot
    b       vector_fatal

    /*
     * Pad this vector entry to exactly 0x80 bytes.
     */
    .balign 0x80
.endm


    /*
     * Current EL using SP_EL0.
     */
    VECTOR_SLOT 0      /* synchronous */
    VECTOR_SLOT 1      /* IRQ         */
    VECTOR_SLOT 2      /* FIQ         */
    VECTOR_SLOT 3      /* SError      */

    /*
     * Current EL using SP_ELx.
     *
     * WaterCheese currently enters EL1h, so exceptions from the
     * running guest itself would normally use this group.
     */
    VECTOR_SLOT 4      /* synchronous */
    VECTOR_SLOT 5      /* IRQ         */
    VECTOR_SLOT 6      /* FIQ         */
    VECTOR_SLOT 7      /* SError      */

    /*
     * Lower EL using AArch64.
     */
    VECTOR_SLOT 8      /* synchronous */
    VECTOR_SLOT 9      /* IRQ         */
    VECTOR_SLOT 10     /* FIQ         */
    VECTOR_SLOT 11     /* SError      */

    /*
     * Lower EL using AArch32.
     */
    VECTOR_SLOT 12     /* synchronous */
    VECTOR_SLOT 13     /* IRQ         */
    VECTOR_SLOT 14     /* FIQ         */
    VECTOR_SLOT 15     /* SError      */

.size vectors, . - vectors


/*
 * =============================================================
 * Read-only test data
 * =============================================================
 */

.section .rodata, "a"
.align 4

message:
    /*
     * CRLF is deliberate because the PTY slave is configured
     * raw, so the host tty layer will not translate '\n'.
     */
    .asciz "Hello from WaterCheese UART!\r\nPTY transmit path works.\r\n"