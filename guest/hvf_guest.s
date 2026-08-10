.section .text
.global _start

.equ UART_BASE, 0x09000000
.equ UART_THR,  0x00
.equ UART_LCR,  0x0c
.equ UART_IER,  0x04
.equ UART_FCR,  0x08

_start:
    /*
     * Optional boot notification to the VMM.
     */
    mov     x0, #0x120
    hvc     #0

    /*
     * x20 = UART base address.
     */
    ldr     x20, =UART_BASE

    /*
     * LCR = 0x03
     *
     * 8 data bits, no parity, one stop bit.
     * More importantly, DLAB = 0, so register 0 is THR.
     */
    mov     w0, #0x03
    str     w0, [x20, #UART_LCR]

    /*
     * Disable UART interrupts for this polling/simple TX test.
     */
    mov     w0, #0
    str     w0, [x20, #UART_IER]

    /*
     * FIFO enable. Your current model only stores this,
     * but exercising the register is useful.
     */
    mov     w0, #0x01
    str     w0, [x20, #UART_FCR]

    /*
     * Print the test string.
     */
    adr     x21, message

1:
    ldrb    w0, [x21], #1
    cbz     w0, 2f

    /*
     * Register 0 with DLAB=0 is THR.
     *
     * Use a 32-bit store because your MMIO model currently
     * expects 4-byte accesses. uart_write() then consumes
     * the low byte.
     */
    str     w0, [x20, #UART_THR]

    b       1b

2:
    /*
     * Your existing VMM shutdown ABI.
     */
    mov     x0, #0x122
    hvc     #0

3:
    wfe
    b       3b


.section .rodata
message:
    .asciz "Hello from WaterCheese UART!\n"