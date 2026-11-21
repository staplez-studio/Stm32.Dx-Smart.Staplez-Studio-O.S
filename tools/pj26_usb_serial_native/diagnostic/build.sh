#!/bin/sh
set -e
CC=${CC:-clang}; LD=${LD:-ld.lld}; OBJCOPY=${OBJCOPY:-llvm-objcopy}
F="--target=thumbv7m-none-eabi -mcpu=cortex-m3 -mthumb -Os -ffreestanding -fno-builtin -fno-stack-protector -nostdlib -Wall -Wno-gnu-designator"
for f in start diag; do $CC $F -c $f.c -o $f.o; done
$LD -T link.ld -o pj26_diag.elf start.o diag.o
$OBJCOPY -O binary pj26_diag.elf pj26_diag.bin
$OBJCOPY -O ihex pj26_diag.elf pj26_diag.hex
mkdir -p prebuilt; cp pj26_diag.bin pj26_diag.hex prebuilt/; ls -l prebuilt/pj26_diag.bin
