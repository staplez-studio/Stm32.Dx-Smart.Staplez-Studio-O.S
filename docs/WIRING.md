# Wiring — Staplez Studio OS on the DX-SMART PJ26 (STM32F103C8T6)
Checked against the `PJ26.pdf` schematic in the project (it is titled **REV 1.0, dated 2026-03-16**; you described the board as v1.2 — if your v1.2 differs, re-check the rows marked *board*). Not hardware-tested.

## What the schematic confirms
- LED on **PB11**, user button SW2 on **PB12** (to GND), reset button SW1, 20-pin headers P2/P3, 2x6 header JP4 (PA0..PA7, 3V3, GND), SWD pads (PA13/PA14), RT9193-3.3 LDO, CH340X on the USB-C.
- PB8/PB9 are brought out and are the remap pins of hardware **I2C1** — exactly how `i2c.c` uses them (`AFIO I2C1 remap`).
- PA2/PA3 = USART2 TX/RX, PA9/PA10 = USART1 TX/RX (CH340 side), PA8 = TIM1_CH1 (fan PWM), PA12 = IR (also USB D+ pin, unused here).
- Pin voltage tolerance (datasheet): **PA0–PA7, PB0, PB1, PB5 are NOT 5 V tolerant**. PB6, PB8–PB15, PA8–PA15 are. Never put 5 V on PA1/PA2/PA3/PB5.

## Pin table
| Function | STM32 pin | Connects to | Notes |
|---|---|---|---|
| Programming | PA13 SWDIO, PA14 SWCLK, RESET, 3V3, GND | ST-Link | |
| Status LED | PB11 | on board | Active high |
| USB serial | PA9 TX, PA10 RX | CH340 on board | |
| RFID reader (RDM6300, 125 kHz) | PB11 (USART3 RX, 9600 8N1) | RDM6300 TXD | VCC to 5 V, GND common. PB11 is also the blue LED: it blinks once at boot, then the pin becomes the reader input (the LED cannot be used afterwards). Read-only: the RDM6300 cannot write tags |
| I2C bus (hardware I2C1, remapped) | PB8 SCL, PB9 SDA | SH1106 OLED (0x3C), DS3231 (0x68), AT24C32 (0x57 — what the firmware uses; DS3231 boards normally strap it that way), ADAU1401 (0x34, later) | One pair of 4.7 k pull-ups to 3V3 for the whole bus (many DS3231 modules already have them: don't add more than ~2 sets) |
| IR receiver | PA12 | IR module OUT | 3V3 + GND |
| Fan PWM | PA8 | Fan driver (MOSFET gate, series 100 Ω, 100 k pull-down) | Fan on its own supply, flyback diode, common GND |
| Passive buzzer | PB5 | via NPN/MOSFET | PB5 is not 5 V tolerant — keep its load 3.3 V-referenced |
| Active buzzer | PB6 | via NPN/MOSFET | |
| ESP32-CAM link | PA2 (STM TX) → cam **U0R**, PA3 (STM RX) ← cam **U0T** | 115200 8N1 | Crossed. Share GND. See below |
| DFPlayer commands | PA9 (STM TX) → DFPlayer RX | ~1 kΩ in series | PA9 only goes to the CH340's RX input, so sharing is OK |
| DFPlayer replies | PA1 ← DFPlayer TX | direct | **Required** for the MP3 count. 9600 baud. DFPlayer TX is 3.3 V |
| User button SW2 | PB12 | on board | Firmware now sets pull-up so a press reads low (unused by menus yet) |
| Level meter L | PA4 (ADC1 ch4) | audio via 10 µF → node with 10 k to 3V3 + 10 k to GND → 1 k → PA4 | Real measurement for Sound > STEREO LEVEL. Signal must stay under 3.3 Vpp (divide line level down). PA4 is not 5 V tolerant |
| Level meter R | PA5 (ADC1 ch5) | same network for the right channel | |
| Free on JP4 | PA0, PA6, PA7, PC15 | | |

JP4 is the handy 3.3 V/GND + PA0–PA7 header: PA1, PA2, PA3 are all on it.

## ESP32-CAM as the Wi-Fi source (its main job)
1. Flash `tools/esp32cam_bridge/esp32cam_wifi_sd_bridge.ino` (Arduino IDE, board "AI Thinker ESP32-CAM"). Programming needs a USB-serial adapter on the cam's U0R/U0T/GND with **GPIO0 tied to GND**, then remove that link and press the cam's reset.
2. The sketch answers the same AT commands the firmware already uses for Wi-Fi (join, scan, IP, SNTP time, TCP/HTTP for wttr.in weather) so no other module is needed. Plain Espressif "AT firmware" for ESP32 is a different route and is not required.
3. Wiring: cam **5 V → PJ26 5V pin**, cam **GND → GND**, cam U0T → **PA3**, cam U0R ← **PA2**. Power the cam from the 5 V pin (it has its own regulator); the PJ26's 3.3 V LDO is too small to feed an ESP32 Wi-Fi radio plus the rest. Add 220–470 µF across the cam's 5 V/GND.
4. The cam prints boot text at power-up; the firmware ignores it and re-syncs.
5. The SD card (cam's slot) is optional: the Hard Drive app lists names, and the MP3 player (source `CAM SD>ADAU`) plays from it.

### Cam MP3 audio -> ADAU1401 (I2S, UNTESTED)
The ADAU1401/1701 has no sample-rate converter on its serial input, so the **ADAU is the I2S clock master** and the cam is the slave:

| From | To |
|---|---|
| ADAU serial OUTPUT BCLK | cam GPIO13 **and** ADAU INPUT_BCLK |
| ADAU serial OUTPUT LRCLK | cam GPIO16 **and** ADAU INPUT_LRCLK |
| cam GPIO12 (data out) | ADAU INPUT_SDATA0 |
| GND | common |

- GPIO12 is a boot strapping pin: nothing may pull it HIGH at power-up (the ADAU input is high-Z, so a direct connection is fine).
- Arduino IDE: PSRAM **Disabled** (GPIO16 is the PSRAM pin), core 3.x, library `arduino-libhelix`.
- SigmaStudio: serial output port = master, serial input = I2S 24-bit, 48 kHz (MCLK 12.288 MHz). MP3s are resampled to 48 kHz on the cam.
- Optional input selector: set `ADAU_P_INPUT_SEL` in `adau_params.h` to the Mux address from your SigmaStudio export (0xFFFF = unused).
- DFPlayer source: its analog DAC outputs go to the ADAU analog inputs, the other route.
- The MP3 player switches source with key 8; a missing ADAU clock is shown as an error state.
6. Logic levels are 3.3 V on both sides — no level shifter needed.

## Power
- 5 V from USB-C/ST-Link powers DFPlayer, fan driver and the cam's 5 V pin; 3V3 (LDO) only for the MCU, OLED, DS3231, IR. Watch the USB supply: cam (≈300 mA peaks) + DFPlayer + fan.
- All grounds common.

## Bring-up order
OLED/clock → IR → buzzers/fan → DFPlayer (check PA1 on the MP3 page) → ESP32-CAM Wi-Fi/weather/time → Hard Drive app → ADAU1401.
