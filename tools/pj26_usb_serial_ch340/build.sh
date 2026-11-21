#!/bin/sh
# Builds pj26_usb_serial.elf/.bin/.hex with clang + lld (or set CC/LD). Needs: clang, ld.lld, llvm-objcopy.
set -e
CC=${CC:-clang}; LD=${LD:-ld.lld}; OBJCOPY=${OBJCOPY:-llvm-objcopy}
F="--target=thumbv7m-none-eabi -mcpu=cortex-m3 -mthumb -Os -ffreestanding -fno-builtin -fno-stack-protector -nostdlib -Wall"
$CC $F -c start.c -o start.o
$CC $F -c bridge.c -o bridge.o
$LD -T link.ld -o pj26_usb_serial.elf start.o bridge.o
$OBJCOPY -O binary pj26_usb_serial.elf pj26_usb_serial.bin
$OBJCOPY -O ihex pj26_usb_serial.elf pj26_usb_serial.hex
ls -l pj26_usb_serial.bin
