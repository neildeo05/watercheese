#!/bin/bash
clang --target=aarch64-none-elf \
    -c hvf_guest.S \
    -o hvf_guest.o

ld.lld \
    -T hvf_guest.ld \
    -o hvf_guest.elf \
    hvf_guest.o

llvm-objcopy \
    -O binary \
    hvf_guest.elf \
    hvf_guest.bin

# llvm-objdump \
#     -d \
#     --no-show-raw-insn \
#     hvf_guest.elf