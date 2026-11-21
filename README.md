<p align="center">
  <img src="https://github.com/staplez-studio/BluePill_Extra_Applications/blob/main/github-banner-1024x512.png?raw=true" alt="Staplez Studio Banner" width="85%" />
</p>
# Staplez Studio OS (SSOS)

Firmware for a pocket clock, weather station and media box that runs on a **DX-SMART PJ26 v1.2** board (STM32F103C8T6, "Blue Pill" class). It drives a 128x64 SH1106 OLED, is operated with an IR remote, and uses an ESP32-CAM as its Wi-Fi source (time, weather) and as an SD card player into an ADAU1401/1701 DSP.

> **Status: not everything has been tested on real hardware.** The logic is tested on a PC (259 host checks, plus a screen layout lint). Everything that touches real silicon, the ESP32-CAM sketch, the USB adapter firmware and the ADAU1401, is untested. See [Project status](#project-status).

## Contents
- [Features](#features)
- [Hardware](#hardware)
- [Pin map](#pin-map)
- [Getting started](#getting-started)
- [Using the device](#using-the-device)
- [ESP32-CAM](#esp32-cam)
- [Flashing the ESP32-CAM with a second PJ26](#flashing-the-esp32-cam-with-a-second-pj26)
- [Power](#power)
- [Repository layout](#repository-layout)
- [Tests](#tests)
- [Project status](#project-status)
- [Documentation index](#documentation-index)
- [Credits and licence](#credits-and-licence)

## Features
- **Clock** with DS3231 battery backup, calendar, time zone and daylight-saving rules that work with no internet.
- **Wi-Fi manager** that remembers up to 6 networks and rejoins the strongest known one automatically. Scan, add hidden networks, edit or forget a password, all on the 128x64 screen. Details in [docs/WIFI.md](docs/WIFI.md).
- **Weather and time over Wi-Fi** (NTP, and wttr.in over plain HTTP), refreshed automatically.
- **Alarm** with single tone or MP3 (DFPlayer).
- **Fan control**, real PWM on PA8.
- **Sound app**: volume, 3-tone and 5-band equalizer (ADAU1401/1701), and a real stereo level meter on the ADC (PA4/PA5).
- **MP3 player with a source select**: DFPlayer SD card, or the ESP32-CAM's SD card routed through I2S into the ADAU1401.
- **Hard Drive app**: browse the ESP32-CAM's SD card.
- **RFID read tool**: RDM6300 125 kHz reader, shows the tag ID and keeps a log of the last 3 tags. Read-only.
- **IR remote** is the only navigation input.

This is the real firmware source. Where hardware is absent, the screens show the real state (no module, no card, no clock), never invented data.

## Hardware
| Part | Notes |
|---|---|
| DX-SMART PJ26 v1.2 | STM32F103C8T6, USB-C to a CH340X on PA9/PA10, blue LED on PB11, button SW2 on PB12, RT9193-3.3 LDO (300 mA). Your schematic file is titled REV 1.0 (2026-03-16); recheck the board-specific rows if your v1.2 differs. |
| SH1106 1.3" OLED | I2C, 0x3C |
| DS3231 + AT24C32 module | I2C, 0x68 clock, EEPROM at 0x57 |
| IR receiver | PA12 |
| ESP32-CAM (AI-Thinker) | UART2, Wi-Fi source and SD card |
| DFPlayer Mini | MP3 from its own SD card |
| ADAU1401/1701 board | DSP, I2C control, I2S input from the cam |
| Fan driver, buzzers, RDM6300 | optional |

## Pin map
Full wiring, voltages and notes: **[docs/WIRING.md](docs/WIRING.md)**.

| Function | Pin |
|---|---|
| OLED, DS3231, EEPROM, ADAU control (hardware I2C1, remapped) | PB8 SCL, PB9 SDA |
| IR receiver | PA12 |
| Fan PWM (TIM1_CH1) | PA8 |
| ESP32-CAM UART2 | PA2 TX (to cam U0R), PA3 RX (from cam U0T), 115200 8N1 |
| DFPlayer commands / replies | PA9 TX / PA1 RX |
| Stereo level meter (ADC) | PA4 left, PA5 right |
| Buzzers | PB5 (passive), PB6 (active) |
| RFID reader (RDM6300 TX) | PB11 (USART3 RX, 9600). Shared with the blue LED: it blinks at boot, then becomes the reader input |
| Status LED / button | PB11 / PB12 |
| ST-Link (SWD) | PA13 SWDIO, PA14 SWCLK |

PA0-PA7 and PB0/PB1/PB5 are **not 5 V tolerant**.

## Getting started
1. **Import** the folder into STM32CubeIDE (*File > Open Projects from File System*) and build. The ST HAL and CMSIS are bundled (a small hand-written HAL in `Drivers/`). Do not regenerate from `SSOS.ioc`: that would replace it and overwrite the init code. The linker script targets the 64 KB C8.
2. **Flash** over ST-Link (SWDIO PA13, SWCLK PA14, 3V3, GND on header JP3).
3. **Settings** worth checking first: `Core/Inc/wifi_config.h` (auto-connect, NTP server, weather location and units) and `Core/Inc/main.h` (`LED_ON_PB11`).
4. **Wire the ESP32-CAM** and flash its sketch ([below](#esp32-cam)).
5. **ADAU1401/1701:** export your SigmaStudio project and replace the placeholder addresses in `Core/Inc/adau_params.h`. Until you do, the DSP writes land on the wrong cells.

## Using the device
Everything is driven from the IR remote.

| Key | Action |
|---|---|
| LEFT / RIGHT (0x72C8 / 0x72D8) | Move across icons, fields, controls |
| UP / DOWN (0xF2F0 / 0xF2E0) | Change a value, scroll a list |
| PLAY (0x7158) | Open, confirm, save |
| GEAR (0x7198) | Back / jump to the BACK button |
| FUNC, MODE | Open the app menu |
| 1-9 | Shortcuts: 1 Wi-Fi, 2 Set Time, 3 Alarm, 4 Fan, 5 Sound, 6 IR, 7 Entertainment, 8 Weather |

The numbered keys type digits in the Wi-Fi password editor. Per-screen key tables are in [docs/CHANGELOG-and-notes.md](docs/CHANGELOG-and-notes.md#remote-control-cheat-sheet-new-screens).

**Apps:** main clock, Wi-Fi, Set Time, Time Zone, Alarm, Fan, Sound, IR Remote, Weather, Entertainment (Hard Drive, MP3 Player, Screen Calibrator, Read RFID).

**MP3 player:** the track and state show on screen; DOWN (key 8) switches the source between `DFPLAYER SD` and `CAM SD>ADAU`. The choice is saved in the EEPROM.

## ESP32-CAM
The cam does the Wi-Fi work and plays its SD card. The sketch is in [`tools/esp32cam_bridge/`](tools/esp32cam_bridge/). It answers the AT commands the firmware uses (scan, join, IP, SNTP time, TCP), plus the SD commands.

- **Arduino IDE:** board *AI Thinker ESP32-CAM*, Arduino-ESP32 core 3.x, PSRAM **Disabled**, library `arduino-libhelix`. Do not print debug text to `Serial`: it is the AT link.
- **Link:** cam U0T to PA3, cam U0R from PA2, common GND, cam powered from 5 V.
- **A new cam does not ship with this sketch.** It must be flashed once, and again whenever the sketch changes.
- **Audio to the ADAU1401:** the ADAU is the I2S clock master and the cam is the slave (cam GPIO13 BCLK in, GPIO16 LRCLK in, GPIO12 data out). The wiring and the SigmaStudio settings are in [docs/WIRING.md](docs/WIRING.md). GPIO12 must not be pulled high at boot.

## Flashing the ESP32-CAM with a second PJ26
A spare PJ26 can act as a USB-serial adapter. Two firmwares are provided:

| Folder | Pins used | How the PC connects | Notes |
|---|---|---|---|
| [`tools/pj26_usb_serial_native`](tools/pj26_usb_serial_native/) | PA2, PA3, PA4, PA5, PA11, PA12, PB0, PB1 (OLED), PB11 | Native USB on PA11/PA12 (needs a 1.5 kΩ pull-up from PA12 to 3V3) | Baud, DTR and RTS follow the PC; DTR drives cam GPIO0 (and PA5 can drive the reset pad). Preferred. |
| [`tools/pj26_usb_serial_ch340`](tools/pj26_usb_serial_ch340/) | PA2, PA3, PA9, PA10, PB11 | The board's own USB-C and CH340 | Fixed 115200 baud, boot mode entered by hand |

Build each with the `build.sh` in its `firmware/` folder (clang + ld.lld + llvm-objcopy), or flash the prebuilt `.bin` from `firmware/prebuilt/` at 0x08000000 over ST-Link.

Wiring (PA2 to cam U0R, PA3 to cam U0T, GND to GND, 5 V to 5 V), the upload sequence and the limits are in the `README` inside each folder's source and in [docs/WIRING.md](docs/WIRING.md). After flashing the cam, remove the adapter, remove the GPIO0-to-GND jumper, wire the cam to the main board, and press the cam's reset button once. Upload at 460800 baud or lower.

## Power
USB-C 5 V is the only supply. The 3.3 V LDO only feeds the MCU side (about 80 mA of its 300 mA). Rough load estimates (typical datasheet values, not measured): about 0.4 A typical and up to 1-1.3 A in the worst case, so use a **5 V 2 A** supply.

Split the 5 V rail in this order:
1. **ESP32-CAM:** Wi-Fi bursts of 300-400 mA cause brownouts. Give it its own feed with a 470-1000 µF capacitor at its pin and a common ground.
2. **Fan:** its PWM noise and inductive kick. Separate path, flyback diode.
3. **Audio chain (ADAU1401 and DFPlayer):** a quieter feed, an LC filter and a single ground point.

## Repository layout
```
Core/Src, Core/Inc    application and drivers (one .c/.h pair per app or module)
Core/Startup          startup assembly
Drivers/              bundled mini HAL and CMSIS
SSOS.ioc              pin reference (do not regenerate from it)
docs/                 WIRING.md, WIFI.md, SD_AND_DFPLAYER.md, change log
tools/esp32cam_bridge        ESP32-CAM sketch (Wi-Fi, SD, MP3 to I2S)
tools/pj26_usb_serial_*      USB-serial adapter firmware for a second PJ26
tests/host            PC tests: the real firmware sources built with gcc
```

## Tests
The tests compile the real firmware sources on a PC with simulated ESP, EEPROM and SD hardware.

```
cd tests/host
./hb.sh harness && ./harness      # 259 end-to-end checks
./hb.sh lint && ./lint 3          # screen layout lint: no text crossing a box or leaving the screen
```
They need gcc and Python 3. The USB adapter's protocol code has its own check: `cd tools/pj26_usb_serial_native && gcc -o usb_test usb_test.c usb_proto.c && ./usb_test` (19 checks).

These prove the logic. They do not replace testing on the board.

## Project status
| Area | Status |
|---|---|
| Clock, calendar, time zone, DST | Host tested |
| Wi-Fi store, scan, auto-join, UI | Host tested against a simulated ESP-AT module |
| Level meter | Host tested with injected samples; ADC wiring is yours |
| MP3 source select, SD browsing | STM32 side host tested against a simulated cam |
| RFID reader | Parser host tested with real frame formats; PB11 wiring untested |
| **ESP32-CAM sketch** | **Untested.** Needs Arduino-ESP32 3.x; the I2S slave clocking with the ADAU is the first thing to verify |
| **ADAU1401/1701** | Parameter addresses in `adau_params.h` are placeholders until you export from SigmaStudio |
| **USB adapter (native)** | Descriptor and request logic host tested; the USB register layer compiles but has not run on a board |
| Everything on real silicon | Not yet run |

PB11 is both the blue LED and the RFID input. The LED blinks once at boot and cannot be used afterwards. If you do not fit an RFID reader, leave the pin alone or move the reader to another pin.

## Documentation index
- [docs/WIRING.md](docs/WIRING.md): full pin table, cam wiring, I2S to the ADAU
- [docs/WIFI.md](docs/WIFI.md): how the Wi-Fi app and network memory work
- [docs/SD_AND_DFPLAYER.md](docs/SD_AND_DFPLAYER.md): Hard Drive app, DFPlayer list, cam protocol
- [docs/CHANGELOG-and-notes.md](docs/CHANGELOG-and-notes.md): historical notes, clock/DST design, key tables
- [tests/host/ht/tests/README.txt](tests/host/ht/tests/README.txt): what each test file covers

## Credits and licence
Copyright (c) 2026 **Staplez Studio**.

**Free for personal, non-commercial use. Commercial use needs a licence: contact Staplez Studio first.** Full terms in [LICENSE.md](LICENSE.md).

All original application code, screen layouts, weather and status icons, bitmaps and artwork in this repository are the work of Staplez Studio, except where listed below.

### Third-party credits
| Who | What |
|---|---|
| STMicroelectronics | `Drivers/STM32F1xx_HAL_Driver`, the CMSIS device headers (`stm32f103xb.h`, `stm32f1xx.h`, `system_stm32f1xx.h`) and `Core/Startup/startup_stm32f103c8tx.s`, from the STM32CubeF1 / CubeMX tooling. **Restore ST's original licence notices** (they are not in this tree). Some headers were trimmed. |
| Arm | `Drivers/CMSIS/Include/core_cm3.h` (CMSIS-Core). **Restore the original Apache-2.0 notice.** |
| RealNetworks (Helix), Phil Schatzmann (`arduino-libhelix`), Earle Philhower (ESP8266Audio lineage) | MP3 decoder library used by the ESP32-CAM sketch (`tools/esp32cam_bridge`). Not bundled; the decoder code is under the RealNetworks Public Source License. Read it before sharing a compiled cam binary. |
| Espressif | Arduino-ESP32 core and I2S driver used by the cam sketch; the AT command style follows ESP-AT; the DTR/RTS auto-reset convention in the USB adapter. |
| Igor Chubin (wttr.in) | Public weather service queried over HTTP (project licence: Apache-2.0). No attribution is required by its page; credited as a courtesy. |
| NTP Pool Project | `pool.ntp.org` time servers. For a commercial product use a vendor zone. |
| pid.codes | USB ID 1209:0001 in the USB adapter, their test pair (testing only). |
| USB-IF, Solomon Systech / Sino Wealth, DFRobot, Analog Devices, RDM6300/EM4100 makers | Specifications and datasheets followed by the USB adapter, the OLED driver, the DFPlayer, the ADAU1401/1701 and the RFID reader code (written from the documents, no code copied). |
| DX-SMART | The PJ26 development board. |
| LLVM Project | clang and lld, used to build the USB adapter firmware (not distributed). |

### To confirm
- `Core/Inc/font5x7.h`: a 5x7 ASCII font table supplied with the project. Staplez Studio to confirm its origin before publishing.
