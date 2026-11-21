#!/bin/bash
S=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$S/../.." && pwd)
rm -rf $S/hostsrc; mkdir -p $S/hostsrc; cp -r $R/Core $R/Drivers $S/hostsrc/
sed -i -E 's/__asm volatile \("[^"]*"[^;]*;/;/' $S/hostsrc/Drivers/CMSIS/Include/core_cm3.h
python3 $S/patch_cm3.py $S/hostsrc
cd $S/hostsrc
INC="-ICore/Inc -ICore/Src -IDrivers/CMSIS/Device/ST/STM32F1xx/Include -IDrivers/CMSIS/Include -IDrivers/STM32F1xx_HAL_Driver/Inc"
SRCS=$(ls Core/Src/*.c | grep -v -E "/main.c|syscalls|sysmem|system_stm32|stm32f1xx_it|wifi_manager.c|weather_app.c|hard_drive_app.c" | tr '\n' ' ')
HAL=$(ls Drivers/STM32F1xx_HAL_Driver/Src/*.c | grep -v uart | tr '\n' ' ')
WR=""; if [ "$1" = lint ]; then WR="-Wl,--wrap=sh1106_clear -Wl,--wrap=sh1106_drawString -Wl,--wrap=sh1106_drawChar -Wl,--wrap=sh1106_drawRect -Wl,--wrap=sh1106_fillRect -Wl,--wrap=draw_16x16_icon -Wl,--wrap=draw_16x32_icon -Wl,--wrap=draw_8x8_icon -Wl,--wrap=Weather_DrawIcon16 -Wl,--wrap=sh1106_update"; fi
gcc -w -g -DSTM32F103xB $INC -o $S/$1 $S/ht/tests/$1.c $SRCS $HAL $EXTRA -lm -Wl,--allow-multiple-definition $WR 2>&1 | grep -v "in function" | head -${LINES_MAX:-25}
