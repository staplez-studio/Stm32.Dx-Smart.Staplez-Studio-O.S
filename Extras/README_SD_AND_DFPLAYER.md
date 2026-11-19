# Hard Drive app + DFPlayer MP3 list — wiring and protocol (NOT hardware-tested)

## Hard Drive app (App menu, last icon)
Browses file NAMES on the ESP32-CAM's SD card over UART2 (PA2/PA3, shared with Wi-Fi AT).
Keys: 2/8 move, 6 or PLAY opens a folder / plays an MP3 (PLAY again stops), 4 = up one folder, GEAR = jump to BACK.
Needs `Extras/esp32cam_sd_bridge.ino` flashed on the cam (answers AT+SDINFO / AT+SDLS / AT+SDPLAY / AT+SDSTOP).
The MP3 decode -> I2S -> ADAU1401 part (`playMp3()`) is a stub you must fill in.
If your Wi-Fi is a separate ESP-01S, it cannot share PA2/PA3 with the cam without extra wiring.

## DFPlayer list
The DFPlayer cannot send file names, only the count of MP3s on ITS OWN card (command 0x48), so tracks are listed as numbers.
**Wire DFPlayer TX -> PA1** (1k series resistor is fine). Without it the screens say "NO ANSWER (PA1?)".
The ESP32-CAM cannot feed the DFPlayer; it only plays its own SD card.

## Still placeholders
ADAU1401 parameter addresses (`adau_params.h`) must come from a SigmaStudio export; VU meter page is animated filler;
PCM1808/PCM5102 unusable (F103 has no I2S). Host tests: 154 checks pass; layout lint: 0 problems. Nothing run on the real board.
