# Notes and change log (historical)

Carried over from the original README. Some entries describe the early firmware (for example the bit-banged I2C driver); the code has since moved to hardware I2C1 on PB8/PB9. Where this file and `README.md` or `docs/WIRING.md` disagree, those two are current.

## Revision notes (early firmware review pass)

Fixes made in this revision. All were checked with a strict host-GCC syntax pass and, for the I2C driver,
a host simulation against a simulated slave. **None of it has been run on real hardware yet.**

| Area | Problem | Fix |
|------|---------|-----|
| RTC / ISR | `EXTI15_10_IRQHandler` called `DS3231_GetTime()` (bit-banged I2C) from interrupt context while the main loop could be mid-transfer to the OLED. This could corrupt both, and it blocked IR edge capture, which shares that vector. | ISR now only sets `rtc_sqw_pending`; `Staplez_Process()` does the read. |
| I2C timing | `i2c_delay()` was two NOPs, so SCL ran at roughly 1 MHz (-O0) to several MHz (optimised) against a 400 kHz limit, and changed with the optimisation level. | Delay is timed with the DWT cycle counter. About 250-280 kHz at 72 MHz, tunable via `I2C_BITBANG_HZ`. Repeated-START now lets SDA settle before SCL rises. |
| USART2 | `BRR` was hard-coded for a 72 MHz clock, but USART2 is on APB1 (36 MHz), so it ran at **57,600 baud** instead of 115,200. | `BRR` is computed from the real APB1 clock and `Init.BaudRate`. If your ESP module was working at 57,600, set it to 115200 (`AT+UART_DEF=115200,8,1,0,0`) or change `huart2.Init.BaudRate`. |
| DFPlayer alarm | In MP3 mode the "play track" frame (~10 ms, bit-banged) was resent on every main-loop pass, and dismissing the alarm never stopped the MP3. | Sent once per ring; `DFPlayer_Pause()` on dismiss. |
| Fan PWM | "100%" output was 959/960 counts (small low glitch every period). | Integer scaling; 100% now holds the pin high. |
| Misc | `sysmem.c` relied on a transitive `<stddef.h>`; one always-true unsigned comparison in `sound_app.c`. | Explicit include; comparison removed. |

Notes: the files in `Drivers/` are a small hand-written HAL, not ST's STM32Cube HAL. Regenerating the project from
`SSOS.ioc` in CubeMX would replace them with the real HAL and overwrite the `MX_*` init code. The `.ioc` also
lists STM32F103**CB** while the linker script and project target the 64 KB **C8**.

### DX-SMART PJ26 board notes
- Programming: SWD header JP3 = 3.3V, GND, PA13 (SWDIO), PA14 (SWCLK). USB-C goes to a CH340X (PA9 TX / PA10 RX), not to the MCU's USB pins.
- PA9/PA10 are shared with the CH340X, so avoid wiring other devices to them (the DFPlayer bit-bang TX on PA9 also reaches the CH340X RX, which is harmless).
- The CH340X DTR# pin is wired to BOOT0. If the board only runs after you open a serial terminal, that is the cause.
- The 2x6 LoRa socket uses PA0-PA7 and PC15; PA2/PA3 (Wi-Fi UART) are on it, so do not fit a LoRa module while a Wi-Fi module is attached.
- The 3.3 V regulator is rated 300 mA; power the ESP module separately.

### Sound / DSP notes (ADAU1401/1701 + DFPlayer)
- Every DSP parameter address lives in `Core/Inc/adau_params.h`. The current values are **placeholders**: replace them with the addresses from your SigmaStudio export, or the writes will land on unrelated cells. Parameter RAM is word-addressed, so cells are normally 1 apart, not 4.
- Volume, bass/mid/treble and the 5 EQ bands are applied from `Sound_Apply_Hardware()`. It runs once ~2.5 s after power-up (the DFPlayer and the ADAU's EEPROM self-boot need that time), and again whenever a slider changes. DSP writes are skipped unless the chip answers on I2C.
- Slider centres: bass/mid/treble 7 = 0 dB; 5-band EQ 11 = 0 dB (1.5 dB per step, -16.5 to +15 dB).
- The DFPlayer drops commands that arrive too close together; `DFPLAYER_MIN_GAP_MS` (50 ms) enforces a gap.
- Sound screen BACK returns to the MP3 player when opened from Entertainment, otherwise to the icon menu (decided in `set_system_state()`).

### Clock: calendar, time zone and daylight saving (works with no internet)
- The DS3231 always holds **standard time**; the firmware adds daylight saving when it reads the clock. It needs no internet, survives power loss, and the chip never has to change at a transition.
- **Set Time > zone** opens the Time Zone screen: UTC offset (15-minute steps) and DST rule (off, US/Canada, Europe/UK, Australia, New Zealand). Saved in EEPROM at 0x0180 (the whole EEPROM layout is in `eeprom_map.h`). Default: UTC-5, US/Canada rule.
  The offset matters for network time and for the Europe/Australia/NZ rules; changing only the offset does not move the clock.
- **PLAY on the main clock opens the calendar** (LEFT/RIGHT month, UP/DOWN year, PLAY = back to today, GEAR = exit). A "DST" tag appears on the main clock while daylight time is in effect.
- Setting the time by hand: enter what the clock should show; the firmware stores the matching standard time. In the repeated hour at the end of DST the first (daylight) pass is assumed.
- Without a DS3231 the software clock keeps no DST transitions while running.

### Wi-Fi (ESP8266 / ESP32 running Espressif AT firmware)
- Wiring: module TX -> PA3, module RX -> PA2, 3.3 V and a common ground, EN/CH_PD high. Power the module separately (it can draw ~300 mA peaks). AT baud must be 115200.
- Start-up: the module is checked ~2.5 s after power-up (it needs 1-3 s to start), then the saved network is joined automatically (`WIFI_AUTOCONNECT` in `wifi_config.h`).
- Screens: SCAN (real results only, strongest first), SYNC (network time -> clock), DISC (disconnect and forget the saved network), BACK.
- Password editor: UP/DOWN change the character (a-z, 0-9, A-Z, symbols, space), OK moves to the next slot, `*` is backspace, up to 63 characters. After the last character OK moves to CONNECT. A wrong password keeps you on the editor.
- The saved network (name and password, plain text) is stored in the DS3231 board's EEPROM at 0x0100 (see `eeprom_map.h`).
- Weather: Weather screen fetches the weather over plain HTTP from wttr.in (`wifi_config.h`: location, units, refresh time). PLAY refreshes, GEAR exits. Nothing is shown until it has really been fetched.
- UART receive is interrupt-driven with a 512-byte ring (`usart2_rx_dropped` counts losses).

### Automatic online jobs (time, date and weather)
- Whenever the module is online the clock sets **time and date** from the network (`WIFI_AUTO_TIME_SYNC`): right after it connects, then every `WIFI_TIME_SYNC_HOURS` (12). A failed sync retries after 10 minutes. The weather is fetched right after the first sync and then every `WEATHER_REFRESH_MIN` (30; a failure waits 15 minutes).
- Until a time zone has been saved once (Set Time > zone > SAVE) the automatic sync waits and the Wi-Fi screen says SET TIME ZONE FIRST, because the default zone is only a guess. Set `WIFI_AUTOSYNC_REQUIRES_ZONE` to 0 to skip that. The SYNC button always works.
- These jobs only run on the main clock or the Wi-Fi screen, never while an alarm is ringing. Each one blocks the clock display for a second or two (up to ~12 s if the network is down), and shows a short SYNCING TIME / UPDATING message.
- EEPROM layout is in `eeprom_map.h`; the build fails if two records overlap.

### Remote control cheat sheet (new screens)
The remote's UP / DOWN / LEFT / RIGHT / PLAY (OK) / GEAR (back) control every screen. PREV / VOL- act as LEFT and NEXT / VOL+ as RIGHT. FUNC and MODE always open the app menu. Number buttons 1-9 are global shortcuts (1 Wi-Fi, 2 Set Time, 3 Alarm, 4 Fan, 5 Sound, 6 IR Remote, 7 Entertainment, 8 Weather) - except in the Wi-Fi password editor, where they type digits.

| Screen | Keys |
|--------|------|
| Main clock | PLAY = calendar. DOWN/UP selects the gear, then PLAY = app menu. FUNC/MODE = app menu |
| Calendar | LEFT/RIGHT month, UP/DOWN year, PLAY = back to today, GEAR = back to the clock |
| Set Time | LEFT/RIGHT move across the fields and the buttons (set, zone, cancel); UP/DOWN change a value; PLAY on "zone" opens the Time Zone screen |
| Time Zone | UP/DOWN change the offset (15 min) or the DST rule; LEFT/RIGHT move between OFFSET, DST RULE, SAVE, BACK; PLAY on SAVE saves; GEAR = back |
| Wi-Fi list | LEFT/RIGHT move across SCAN, SYNC, DISC, BACK (and the list); PLAY runs the selected button or opens the selected network; GEAR = BACK |
| Wi-Fi password | UP/DOWN change the character; number buttons 1-9 type digits; PLAY = next character, then CONNECT; GEAR = backspace; LEFT/RIGHT move the cursor |
| Weather | PLAY = update now, GEAR = leave |

### Main clock icon
Offline (or with a reading older than 3 hours): the sun by day, the moon by night. Online with a fresh reading: the real weather icon (cloud, rain, snow, storm, fog) with the temperature beside it; clear skies keep the sun / moon.
