# PJ26 USB-serial adapter using the on-board CH340

Copies bytes between the PJ26's CH340 (USART1, PA9/PA10) and USART2 (PA2 TX, PA3 RX) at a fixed **115200 baud**. No automatic reset: tie cam GPIO0 to GND and press the cam's reset button by hand. Not tested on hardware.

Wiring: PA2 to cam U0R, PA3 to cam U0T, GND, 5V. Build with `./build.sh`; flash `pj26_usb_serial.bin` at 0x08000000. Set the upload speed to 115200. Prefer `../pj26_usb_serial_native` if you can use PA11/PA12.
