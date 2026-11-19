# Wi-Fi app — how it works (DX-SMART PJ26, ESP32-CAM as the Wi-Fi source)

## Remembering networks
* Up to **6 networks** (name + password) are stored in the AT24C32 EEPROM (0x0200–0x04FF) and survive power-off.
* A network is saved **automatically after a successful connection**. A wrong password is never saved.
* When 6 are stored and a 7th connects, the **least recently used** one is replaced.
* Passwords are stored as plain text in the EEPROM.
* A network saved by the older firmware (single slot at 0x0100) is imported on first start.

## Automatic joining
* **At power-up:** the clock scans, then joins the **strongest saved network that is in range** (tries up to 3, falls back to the next one if the password is refused). Away from every saved network it simply stays offline ("NO KNOWN WIFI NEARBY") — no prompts.
* **Dropped link:** checked every 45 s; when lost it looks for a known network again (after 15 s, then 45 s, 2 min, 5 min, 10 min).
* **Hidden networks** (added with + ADD NETWORK) are tried by name after the visible ones.
* **DISCONNECT** keeps the clock offline until you choose a network again. A saved network whose password was refused is skipped until you fix its password.

## Screens (same IR keys as everywhere: 2/8 up/down, 4/6 left/right, OK = PLAY (#), GEAR (*) jumps to BACK)
* **WIFI list** (opens with a scan, like a phone): lock = secured, signal bars, check mark = saved, filled square = connected now.
  OK on a *saved* network joins it with no password; on a *new secured* network asks for the password; on an *open* network joins at once; on the network you are on opens its options.
  Buttons: **SCAN · SAVED · SYNC · BACK**.
* **SAVED WIFI**: your remembered networks (n/6), signal bars when in range, filled square on the connected one, **+ ADD NETWORK** at the end (type the name of a hidden network, then NEXT, then its password).
* **Options for one network**: CONNECT (or DISCONNECT if you are on it) · EDIT PASSWORD · FORGET (asks to press OK twice; forgetting the current network disconnects it).
* If a saved password is refused, the password editor opens pre-filled so you can fix it.

## ESP32-CAM
Flash `esp32cam_wifi_sd_bridge/esp32cam_wifi_sd_bridge.ino` (untested on hardware). It implements the AT commands the clock uses (scan, join, IP, SNTP time, TCP for weather).
