OUTPUT_FORMAT("elf64-littleaarch64")
OUTPUT_ARCH(aarch64)
ENTRY(_start)

/*
 * These constants must match watercheese_types.h.
 *
 * The flat binary contains:
 *
 *   0x40200000–0x40203fff  payload/code
 *   0x40204000–0x40207fff  stack
 *
 * Total binary size: 0x8000 bytes.
 */
PAYLOAD_BASE = 0x40200000;
PAYLOAD_SIZE = 0x00004000;

STACK_BASE   = 0x40204000;
STACK_SIZE   = 0x00004000;

IMAGE_BASE   = PAYLOAD_BASE;
IMAGE_SIZE   = PAYLOAD_SIZE + STACK_SIZE;
IMAGE_END    = IMAGE_BASE + IMAGE_SIZE;

MEMORY
{
    PAYLOAD (rx) : ORIGIN = PAYLOAD_BASE, LENGTH = PAYLOAD_SIZE
    STACK   (rw) : ORIGIN = STACK_BASE,   LENGTH = STACK_SIZE
}

SECTIONS
{
    __image_start = IMAGE_BASE;
    __payload_start = PAYLOAD_BASE;

    /*
     * Entry code must be placed first because the VMM starts execution
     * at PAYLOAD_BASE.
     */
    .text.boot ALIGN(16) :
    {
        KEEP(*(.text.boot))
    } > PAYLOAD

    .text.handlers ALIGN(16) :
    {
        KEEP(*(.text.handlers))
    } > PAYLOAD

    .text ALIGN(16) :
    {
        *(.text)
        *(.text.*)
    } > PAYLOAD

    /*
     * VBAR_EL1 requires the vector table to be 2 KiB aligned.
     */
    .vectors ALIGN(0x800) :
    {
        __vectors_start = .;
        KEEP(*(.vectors))
        __vectors_end = .;
    } > PAYLOAD

    .rodata ALIGN(16) :
    {
        *(.rodata)
        *(.rodata.*)
    } > PAYLOAD

    .data ALIGN(16) :
    {
        *(.data)
        *(.data.*)
        *(.sdata)
        *(.sdata.*)
    } > PAYLOAD

    .bss ALIGN(16) (NOLOAD) :
    {
        __bss_start = .;

        *(.bss)
        *(.bss.*)
        *(.sbss)
        *(.sbss.*)
        *(COMMON)

        __bss_end = .;
    } > PAYLOAD

    __payload_content_end = .;

    /*
     * Reserve the last byte of the payload region as a real byte in the
     * flat binary. This ensures that objcopy emits the complete 0x4000-byte
     * payload region, including zero padding.
     */
    ASSERT(
        __payload_content_end <= PAYLOAD_BASE + PAYLOAD_SIZE - 1,
        "guest code, vectors, and data exceed PAYLOAD_SIZE"
    )

    .payload_end_marker PAYLOAD_BASE + PAYLOAD_SIZE - 1 :
    {
        BYTE(0)
    } > PAYLOAD

    __payload_end = PAYLOAD_BASE + PAYLOAD_SIZE;

    /*
     * Make the stack real bytes in the flat binary rather than an omitted
     * NOBITS allocation.
     */
    .stack STACK_BASE :
    {
        __stack_bottom = .;

        FILL(0)
        . += STACK_SIZE - 1;
        BYTE(0)

        /*
         * __stack_top is the first address above the stack allocation.
         * This is the initial SP_EL1 value.
         */
        __stack_top = .;
    } > STACK

    __image_end = .;

    ASSERT(
        (PAYLOAD_BASE & 0x3fff) == 0,
        "PAYLOAD_BASE must be 16 KiB aligned"
    )

    ASSERT(
        (PAYLOAD_SIZE & 0x3fff) == 0,
        "PAYLOAD_SIZE must be a multiple of 16 KiB"
    )

    ASSERT(
        (STACK_BASE & 0x3fff) == 0,
        "STACK_BASE must be 16 KiB aligned"
    )

    ASSERT(
        (STACK_SIZE & 0x3fff) == 0,
        "STACK_SIZE must be a multiple of 16 KiB"
    )

    ASSERT(
        PAYLOAD_BASE + PAYLOAD_SIZE == STACK_BASE,
        "payload and stack must be contiguous"
    )

    ASSERT(
        (__vectors_start & 0x7ff) == 0,
        "VBAR_EL1 vector table is not 2 KiB aligned"
    )

    ASSERT(
        (__vectors_end - __vectors_start) == 0x800,
        "AArch64 vector table must be exactly 0x800 bytes"
    )

    ASSERT(
        __stack_bottom == STACK_BASE,
        "stack begins at the wrong address"
    )

    ASSERT(
        __stack_top == STACK_BASE + STACK_SIZE,
        "stack has the wrong size"
    )

    ASSERT(
        __image_end == IMAGE_END,
        "guest image ends at the wrong address"
    )

    ASSERT(
        (__image_end - __image_start) == IMAGE_SIZE,
        "guest flat binary must be exactly 0x8000 bytes"
    )

    /DISCARD/ :
    {
        *(.comment)
        *(.note)
        *(.note.*)
        *(.eh_frame)
        *(.eh_frame.*)
        *(.gnu.attributes)
    }
}