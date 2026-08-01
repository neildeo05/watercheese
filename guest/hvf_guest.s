.section .text.boot, "ax"
.align 4
.global _start
.type _start, %function

/* Development-only HVC ABI. */
.equ HCALL_BOOT,      0x120
.equ HCALL_REPORT,    0x121
.equ HCALL_SHUTDOWN,  0x122
.equ HCALL_PANIC,     0x1ff

/* Valid RAM in the machine map, deliberately left unmapped initially. */
.equ DEFERRED_TEST_GPA, 0x40401238

_start:
    /* Linux-compatible entry shape: EL1h, DAIF masked, MMU off. */
    msr     daifset, #0xf

    mrs     x9, CurrentEL
    cmp     x9, #0x4
    b.ne    .Lwrong_el

    adrp    x9, __stack_top
    add     x9, x9, :lo12:__stack_top
    mov     sp, x9

    adrp    x9, vectors
    add     x9, x9, :lo12:vectors
    msr     vbar_el1, x9
    isb

    /* Tell the VMM that bootstrap completed. */
    mov     x0, #HCALL_BOOT
    adrp    x1, _start
    add     x1, x1, :lo12:_start
    hvc     #0

    /* x9 = 0x40401238. The containing host-page-sized chunk is deferred. */
    movz    x9, #0x1238
    movk    x9, #0x4040, lsl #16

    /* First access must cause one recoverable Stage-2 translation fault. */
    movz    x10, #0x7788
    movk    x10, #0x5566, lsl #16
    movk    x10, #0x3344, lsl #32
    movk    x10, #0x1122, lsl #48
    str     x10, [x9]
    ldr     x11, [x9]
    cmp     x11, x10
    b.ne    .Lbad_readback

    /* A second access to the same mapped chunk must not fault again. */
    movz    x12, #0xcdef
    movk    x12, #0x89ab, lsl #16
    movk    x12, #0x4567, lsl #32
    movk    x12, #0x0123, lsl #48
    str     x12, [x9]
    ldr     x13, [x9]
    cmp     x13, x12
    b.ne    .Lbad_second_readback

    /* Successful report: address, final value, and expected value. */
    mov     x0, #HCALL_REPORT
    mov     x1, x9
    mov     x2, x13
    mov     x3, x12
    hvc     #0

    mov     x0, #HCALL_SHUTDOWN
    mov     x1, #0
    hvc     #0

    /* Shutdown is terminal. */
    brk     #0xdead

.Lwrong_el:
    brk     #0x00e1
    b       .Lhang

.Lbad_readback:
    mov     x0, #HCALL_PANIC
    mov     x1, #1
    mov     x2, x11
    mov     x3, x10
    hvc     #0
    b       .Lhang

.Lbad_second_readback:
    mov     x0, #HCALL_PANIC
    mov     x1, #2
    mov     x2, x13
    mov     x3, x12
    hvc     #0

.Lhang:
    wfe
    b       .Lhang

.size _start, . - _start


.section .text.handlers, "ax"
.align 4
.type vector_fatal, %function

vector_fatal:
    /*
     * x16 = vector slot number, set by the slot stub.
     * Report guest-local exception state to the VMM and stop.
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


.section .vectors, "ax"
.balign 0x800
.global vectors
.type vectors, %object
vectors:

.macro VECTOR_SLOT slot
    mov     x16, #\slot
    b       vector_fatal
    .balign 0x80
.endm

    VECTOR_SLOT 0
    VECTOR_SLOT 1
    VECTOR_SLOT 2
    VECTOR_SLOT 3
    VECTOR_SLOT 4
    VECTOR_SLOT 5
    VECTOR_SLOT 6
    VECTOR_SLOT 7
    VECTOR_SLOT 8
    VECTOR_SLOT 9
    VECTOR_SLOT 10
    VECTOR_SLOT 11
    VECTOR_SLOT 12
    VECTOR_SLOT 13
    VECTOR_SLOT 14
    VECTOR_SLOT 15

.size vectors, . - vectors
