#!/bin/sh
set -e
CC=${CC:-clang}; LD=${LD:-ld.lld}; OBJCOPY=${OBJCOPY:-llvm-objcopy}
F="--target=thumbv7m-none-eabi -mcpu=cortex-m3 -mthumb -Os -ffreestanding -fno-builtin -fno-stack-protector -nostdlib -Wall -Wno-gnu-designator"
for f in start main usb_proto oled; do $CC $F -c $f.c -o $f.o; done
$LD -T link.ld -o pj26_usb_native.elf start.o main.o usb_proto.o oled.o
$OBJCOPY -O binary pj26_usb_native.elf pj26_usb_native.bin
$OBJCOPY -O ihex pj26_usb_native.elf pj26_usb_native.hex
mkdir -p prebuilt; cp pj26_usb_native.bin pj26_usb_native.hex prebuilt/; ls -l prebuilt/pj26_usb_native.bin
