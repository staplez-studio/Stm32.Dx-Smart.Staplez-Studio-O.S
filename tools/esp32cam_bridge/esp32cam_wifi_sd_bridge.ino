// ESP32-CAM Wi-Fi + SD bridge for Staplez Studio OS (DX-SMART PJ26 v1.2).  UNTESTED on hardware.
// Talks ESP-AT-style commands on UART0 (GPIO1 TX / GPIO3 RX) at 115200 8N1 -> STM32 PA3 / PA2.
//   * Wi-Fi: scan / join / IP / SNTP time / TCP (weather over HTTP)   <- the cam's main job
//   * SD card browsing: AT+SDINFO, AT+SDLS
//   * MP3 playback from the cam's SD card to the ADAU1401 over I2S: AT+SDMP3, AT+SDPLAYN, AT+SDPLAY, AT+SDSTOP, AT+SDSTAT?
//
// REQUIREMENTS (Arduino IDE): board "AI Thinker ESP32-CAM", Arduino-ESP32 core 3.x (needs the new I2S driver),
//   PSRAM: Disabled (GPIO16 is used), library "arduino-libhelix" (pschatzmann) for MP3 decoding.
//   Programming: GPIO0 to GND while uploading, then remove it. Do NOT print debug text on Serial: it is the AT link.
//
// I2S TO THE ADAU1401/1701 - WHY THE CAM IS THE I2S *SLAVE*:
//   The ADAU1401/1701 has no sample rate converter on its serial input, so its input must be synchronous with its own
//   master clock. Therefore the ADAU is the clock master and the ESP32-CAM only follows:
//       ADAU OUTPUT_BCLK  -> cam GPIO13 (BCLK in)   and also -> ADAU INPUT_BCLK
//       ADAU OUTPUT_LRCLK -> cam GPIO16 (LRCLK in)  and also -> ADAU INPUT_LRCLK
//       cam GPIO12 (data out)  -> ADAU INPUT_SDATA0
//       GND common.   (GPIO12 must not be pulled HIGH at boot: leave it unconnected to any pull-up.)
//   In SigmaStudio: set the serial OUTPUT port to master, the serial INPUT port to I2S, 24 bit, and use "Input 0".
//   The DSP runs at ADAU_FS (48 kHz with a 12.288 MHz crystal); MP3s of other rates are resampled to it here.
#include <WiFi.h>
#include <SD_MMC.h>
#include <FS.h>
#include <time.h>
#include <vector>
#include <algorithm>
#include "driver/i2s_std.h"
#include "MP3DecoderHelix.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define ADAU_FS        48000          // sample rate of the DSP (MCLK / 256)
#define I2S_BCLK_PIN   GPIO_NUM_13    // input  (from the ADAU)
#define I2S_LRCLK_PIN  GPIO_NUM_16    // input  (from the ADAU)
#define I2S_DOUT_PIN   GPIO_NUM_12    // output (to the ADAU)

static bool echoOn = true;
static String line;
static WiFiClient tcp;
static bool tcpOpen = false;
static String ntpServer = "pool.ntp.org";
static bool ntpOn = false;

static void ok()  { Serial.print("\r\nOK\r\n"); }
static void err() { Serial.print("\r\nERROR\r\n"); }

// ---- quoted-argument parser: "..." with \\ \" \, escapes, as the STM32 sends them ----
static bool readQuoted(const String &s, int &i, String &out) {
  while (i < (int)s.length() && s[i] != '"') i++;
  if (i >= (int)s.length()) return false;
  i++; out = "";
  while (i < (int)s.length() && s[i] != '"') {
    if (s[i] == '\\' && i + 1 < (int)s.length()) i++;
    out += s[i++];
  }
  if (i >= (int)s.length()) return false;
  i++; return true;
}

static int ecnOf(wifi_auth_mode_t m) {
  switch (m) { case WIFI_AUTH_OPEN: return 0; case WIFI_AUTH_WEP: return 1; case WIFI_AUTH_WPA_PSK: return 2;
    case WIFI_AUTH_WPA2_PSK: return 3; case WIFI_AUTH_WPA_WPA2_PSK: return 4; default: return 5; }
}

static void cmdJoin(const String &a) {
  int i = 0; String ssid, pwd;
  if (!readQuoted(a, i, ssid)) { err(); return; }
  readQuoted(a, i, pwd);
  tcp.stop(); tcpOpen = false;
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false);
  WiFi.begin(ssid.c_str(), pwd.c_str());
  uint32_t t0 = millis(); bool linkShown = false;
  while (millis() - t0 < 15000) {
    wl_status_t st = WiFi.status();
    if (st == WL_CONNECTED) {
      Serial.print("WIFI CONNECTED\r\nWIFI GOT IP\r\n"); ok(); return;
    }
    if (st == WL_NO_SSID_AVAIL && millis() - t0 > 4000) { Serial.print("+CWJAP:3\r\n\r\nFAIL\r\n"); return; }
    if (st == WL_CONNECT_FAILED && millis() - t0 > 4000) { Serial.print("+CWJAP:2\r\n\r\nFAIL\r\n"); return; }
    delay(100); (void)linkShown;
  }
  WiFi.disconnect(false);
  Serial.print("+CWJAP:1\r\n\r\nFAIL\r\n");
}

static void cmdScan() {
  int n = WiFi.scanNetworks();
  for (int k = 0; k < n; k++) {
    Serial.printf("+CWLAP:(%d,\"%s\",%d,\"%s\",%d)\r\n", ecnOf(WiFi.encryptionType(k)), WiFi.SSID(k).c_str(),
                  WiFi.RSSI(k), WiFi.BSSIDstr(k).c_str(), WiFi.channel(k));
  }
  WiFi.scanDelete(); ok();
}

static void cmdIp() {
  IPAddress ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP() : IPAddress(0, 0, 0, 0);
  Serial.printf("+CIFSR:STAIP,\"%s\"\r\n+CIFSR:STAMAC,\"%s\"\r\n", ip.toString().c_str(), WiFi.macAddress().c_str());
  ok();
}

static void cmdSntpTime() {
  time_t now = time(nullptr); struct tm t; gmtime_r(&now, &t);
  char buf[40]; strftime(buf, sizeof buf, "%a %b %d %H:%M:%S %Y", &t);   // "Thu Nov 19 19:06:00 2026"
  Serial.printf("+CIPSNTPTIME:%s\r\n", buf); ok();
}

static void cmdTcpStart(const String &a) {
  int i = 0; String type, host;
  if (!readQuoted(a, i, type) || !readQuoted(a, i, host)) { err(); return; }
  int c = a.indexOf(',', i); uint16_t port = c >= 0 ? (uint16_t)a.substring(c + 1).toInt() : 80;
  if (WiFi.status() != WL_CONNECTED || type != "TCP") { err(); return; }
  tcp.stop(); tcpOpen = false;
  tcp.setTimeout(8);
  if (tcp.connect(host.c_str(), port, 8000)) { tcpOpen = true; Serial.print("CONNECT\r\n"); ok(); }
  else err();
}

static void cmdTcpSend(const String &a) {
  int n = a.toInt();
  if (!tcpOpen || n <= 0 || n > 2048) { err(); return; }
  Serial.print("\r\nOK\r\n> ");
  uint8_t buf[256]; int got = 0; uint32_t t0 = millis();
  while (got < n && millis() - t0 < 5000) {
    if (!Serial.available()) { delay(1); continue; }
    int chunk = min(n - got, (int)sizeof buf), r = Serial.readBytes(buf, min(chunk, Serial.available()));
    tcp.write(buf, r); got += r; t0 = millis();
  }
  Serial.printf("\r\nRecv %d bytes\r\n\r\nSEND OK\r\n", got);
}

static void pumpTcp() {                       // forward server data as "+IPD,<n>:<data>", then "CLOSED"
  if (!tcpOpen) return;
  int av = tcp.available();
  while (av > 0) {
    uint8_t buf[256]; int r = tcp.read(buf, min(av, (int)sizeof buf));
    if (r <= 0) break;
    Serial.printf("\r\n+IPD,%d:", r); Serial.write(buf, r);
    av = tcp.available();
  }
  if (!tcp.connected() && !tcp.available()) { tcp.stop(); tcpOpen = false; Serial.print("CLOSED\r\n"); }
}

// ================= MP3 player: SD card -> libhelix decoder -> resample -> I2S slave -> ADAU =================
static SemaphoreHandle_t sdMutex;                 // the SD card is used by the AT handlers and by the audio task
static std::vector<String> playlist;              // every .mp3 on the card, fixed order
static bool plBuilt = false;
static volatile int  reqIndex = -1;               // audio task: start this play-list entry
static volatile bool reqStop = false;
static volatile int  curIndex = -1;
static volatile uint8_t pState = 0;               // 0 stopped, 1 playing, 2 no I2S clock from the ADAU
static i2s_chan_handle_t i2sTx = nullptr;
static volatile bool noClock = false;

static bool isMp3(const String &n) { String l = n; l.toLowerCase(); return l.endsWith(".mp3") && !l.startsWith("."); }

static void scanDir(const String &dir, int depth) {
  File d = SD_MMC.open(dir);
  if (!d || !d.isDirectory()) return;
  for (File f = d.openNextFile(); f; f = d.openNextFile()) {
    String nm = f.name(); int sl = nm.lastIndexOf('/'); String base = sl >= 0 ? nm.substring(sl + 1) : nm;
    String full = (dir == "/" ? "" : dir) + "/" + base;
    if (f.isDirectory()) { if (depth < 4) scanDir(full, depth + 1); }
    else if (isMp3(base) && playlist.size() < 512) playlist.push_back(full);
  }
}
static void buildPlaylist() {                     // call with sdMutex held
  playlist.clear(); scanDir("/", 0);
  std::sort(playlist.begin(), playlist.end(), [](const String &a, const String &b) { return a < b; });
  plBuilt = true;
}
static String baseName(const String &p) { int s = p.lastIndexOf('/'); return s >= 0 ? p.substring(s + 1) : p; }

static bool i2sStart() {
  if (i2sTx) return true;
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_SLAVE);
  cc.dma_desc_num = 6; cc.dma_frame_num = 240;
  if (i2s_new_channel(&cc, &i2sTx, nullptr) != ESP_OK) { i2sTx = nullptr; return false; }
  i2s_std_config_t sc = {};
  sc.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(ADAU_FS);                   // ignored in slave mode
  sc.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  sc.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32;                 // 32 bit slots = 64 x fs BCLK, what the ADAU sends
  sc.gpio_cfg.mclk = I2S_GPIO_UNUSED; sc.gpio_cfg.bclk = I2S_BCLK_PIN; sc.gpio_cfg.ws = I2S_LRCLK_PIN;
  sc.gpio_cfg.dout = I2S_DOUT_PIN;    sc.gpio_cfg.din = I2S_GPIO_UNUSED;
  if (i2s_channel_init_std_mode(i2sTx, &sc) != ESP_OK || i2s_channel_enable(i2sTx) != ESP_OK) { i2s_del_channel(i2sTx); i2sTx = nullptr; return false; }
  return true;
}

static bool i2sWrite(const int16_t *frames, size_t nFrames) {        // blocks at the pace of the ADAU's clock
  size_t done = 0, bytes = nFrames * 4;
  while (done < bytes) {
    size_t w = 0;
    esp_err_t e = i2s_channel_write(i2sTx, (const uint8_t *)frames + done, bytes - done, &w, 200);
    done += w;
    if (e != ESP_OK && w == 0) return false;                            // no clock from the ADAU
  }
  return true;
}

static bool clockPresent() {                                          // DMA holds ~1440 frames: 3000 only fit if the clock runs
  static int16_t z[500 * 2]; memset(z, 0, sizeof z);
  for (int i = 0; i < 6; i++) if (!i2sWrite(z, 500)) return false;
  return true;
}

// ---- decoder callback: resample to ADAU_FS (linear interpolation) and send ----
static int16_t outBuf[256 * 2]; static int outN = 0;
static float rsPos = 1.0f; static int16_t prevL = 0, prevR = 0;
static void flushOut() { if (outN) { if (!i2sWrite(outBuf, outN)) noClock = true; outN = 0; } }
static void emit(int16_t l, int16_t r) { outBuf[outN * 2] = l; outBuf[outN * 2 + 1] = r; if (++outN == 256) flushOut(); }
static void pcmCallback(MP3FrameInfo &info, short *pcm, size_t len, void *ref) {
  int ch = info.nChans > 0 ? info.nChans : 2, rate = info.samprate > 0 ? info.samprate : 44100;
  float step = (float)rate / (float)ADAU_FS;                           // input samples per output sample
  size_t frames = len / ch;
  for (size_t i = 0; i < frames && !noClock; i++) {
    int16_t l = pcm[i * ch], r = (ch > 1) ? pcm[i * ch + 1] : l;
    while (rsPos < 1.0f) {
      emit((int16_t)(prevL + (l - prevL) * rsPos), (int16_t)(prevR + (r - prevR) * rsPos));
      rsPos += step;
    }
    rsPos -= 1.0f; prevL = l; prevR = r;
  }
}
static libhelix::MP3DecoderHelix mp3dec(pcmCallback);
static File mp3File;

static void stopTrack() { if (mp3File) mp3File.close(); mp3dec.end(); flushOut(); }
static bool startTrack(int idx) {                  // audio task, sdMutex held
  stopTrack();
  if (idx < 0 || idx >= (int)playlist.size()) { pState = 0; curIndex = -1; return false; }
  noClock = false;
  if (!i2sStart() || !clockPresent()) { pState = 2; curIndex = idx; return false; }
  mp3File = SD_MMC.open(playlist[idx]);
  if (!mp3File) { pState = 0; curIndex = -1; return false; }
  mp3dec.begin(); rsPos = 1.0f; prevL = prevR = 0; outN = 0;
  curIndex = idx; pState = 1;
  return true;
}

static void audioTask(void *) {
  static uint8_t buf[1024];
  for (;;) {
    if (reqStop) { reqStop = false; xSemaphoreTake(sdMutex, portMAX_DELAY); stopTrack(); xSemaphoreGive(sdMutex); pState = 0; curIndex = -1; }
    if (reqIndex >= 0) { int i = reqIndex; reqIndex = -1; xSemaphoreTake(sdMutex, portMAX_DELAY); startTrack(i); xSemaphoreGive(sdMutex); }
    if (pState == 1 && mp3File) {
      xSemaphoreTake(sdMutex, portMAX_DELAY);
      int n = mp3File.read(buf, sizeof buf);
      xSemaphoreGive(sdMutex);
      if (n > 0) mp3dec.write(buf, n);              // decodes and plays (blocks at the I2S clock rate)
      if (noClock) { xSemaphoreTake(sdMutex, portMAX_DELAY); stopTrack(); xSemaphoreGive(sdMutex); pState = 2; }
      else if (n <= 0) {                             // end of file: next entry (wraps around)
        xSemaphoreTake(sdMutex, portMAX_DELAY);
        startTrack(playlist.empty() ? -1 : (curIndex + 1) % (int)playlist.size());
        xSemaphoreGive(sdMutex);
      }
    } else vTaskDelay(pdMS_TO_TICKS(10));
  }
}

static void cmdMp3List(const String &a) {
  int c = a.indexOf(','); int first = a.substring(0, c).toInt(), n = a.substring(c + 1).toInt();
  xSemaphoreTake(sdMutex, portMAX_DELAY);
  if (SD_MMC.cardType() == CARD_NONE) { xSemaphoreGive(sdMutex); Serial.print("+SDERR:2\r\n"); err(); return; }
  if (!plBuilt) buildPlaylist();
  for (int i = first; i < (int)playlist.size() && i < first + n; i++) {
    File f = SD_MMC.open(playlist[i]);
    Serial.printf("+SDLS:f,%u,\"%s\"\r\n", f ? (unsigned)f.size() : 0u, baseName(playlist[i]).c_str());
  }
  Serial.printf("+SDLS:END,%d\r\n", (int)playlist.size());
  xSemaphoreGive(sdMutex); ok();
}

// ---- SD browsing ----
static void cmdLs(const String &args) {
  int q1 = args.indexOf('"'), q2 = args.indexOf('"', q1 + 1);
  if (q1 < 0 || q2 < 0) { err(); return; }
  String dir = args.substring(q1 + 1, q2);
  int c1 = args.indexOf(',', q2), c2 = args.indexOf(',', c1 + 1);
  int first = args.substring(c1 + 1, c2).toInt(), n = args.substring(c2 + 1).toInt();
  xSemaphoreTake(sdMutex, portMAX_DELAY);
  File d = SD_MMC.open(dir);
  if (!d || !d.isDirectory()) { xSemaphoreGive(sdMutex); err(); return; }
  int idx = 0, sent = 0;
  for (File f = d.openNextFile(); f; f = d.openNextFile(), idx++) {
    if (idx >= first && sent < n) {
      String nm = f.name(); int s = nm.lastIndexOf('/'); if (s >= 0) nm = nm.substring(s + 1);
      Serial.printf("+SDLS:%c,%u,\"%s\"\r\n", f.isDirectory() ? 'd' : 'f', (unsigned)f.size(), nm.c_str());
      sent++;
    }
  }
  xSemaphoreGive(sdMutex);
  Serial.printf("+SDLS:END,%d\r\n", idx); ok();
}

static void handle(String l) {
  l.trim();
  if (!l.length()) return;
  if (echoOn) Serial.print(l + "\r\n");
  if (l == "AT") ok();
  else if (l == "ATE0") { echoOn = false; ok(); }
  else if (l == "ATE1") { echoOn = true; ok(); }
  else if (l.startsWith("AT+CWMODE")) { WiFi.mode(WIFI_STA); ok(); }
  else if (l.startsWith("AT+CWAUTOCONN=")) { WiFi.setAutoReconnect(l.endsWith("1")); ok(); }
  else if (l.startsWith("AT+CWDHCP=")) ok();                      // DHCP client is the default
  else if (l.startsWith("AT+CIPMUX=")) ok();                      // single connection only
  else if (l == "AT+CWQAP") { WiFi.disconnect(false); ok(); }
  else if (l == "AT+CWLAP") cmdScan();
  else if (l.startsWith("AT+CWJAP=")) cmdJoin(l.substring(9));
  else if (l == "AT+CWJAP?") {
    if (WiFi.status() == WL_CONNECTED)
      Serial.printf("+CWJAP:\"%s\",\"%s\",%d,%d\r\n", WiFi.SSID().c_str(), WiFi.BSSIDstr().c_str(), WiFi.channel(), WiFi.RSSI());
    else Serial.print("No AP\r\n");
    ok();
  }
  else if (l == "AT+CIFSR") cmdIp();
  else if (l.startsWith("AT+CIPSNTPCFG=")) {
    int i = 0; String srv; readQuoted(l, i, srv); if (srv.length()) ntpServer = srv;
    configTime(0, 0, ntpServer.c_str()); ntpOn = true; ok();       // time is UTC; the STM32 applies the time zone
  }
  else if (l == "AT+CIPSNTPTIME?") cmdSntpTime();
  else if (l.startsWith("AT+CIPSTART=")) cmdTcpStart(l.substring(12));
  else if (l.startsWith("AT+CIPSEND=")) cmdTcpSend(l.substring(11));
  else if (l == "AT+CIPCLOSE") { if (tcpOpen) { tcp.stop(); tcpOpen = false; Serial.print("CLOSED\r\n"); ok(); } else err(); }
  else if (l == "AT+SDINFO") {
    if (pState != 1) { xSemaphoreTake(sdMutex, portMAX_DELAY); plBuilt = false; xSemaphoreGive(sdMutex); }   // card may have been swapped
    if (SD_MMC.cardType() == CARD_NONE) Serial.print("+SDINFO:0,0,0\r\n");
    else Serial.printf("+SDINFO:1,%llu,%llu\r\n", SD_MMC.totalBytes() >> 20, SD_MMC.usedBytes() >> 20);
    ok();
  }
  else if (l.startsWith("AT+SDLS=")) cmdLs(l.substring(8));
  else if (l.startsWith("AT+SDPLAY=")) {
    int a = l.indexOf('"'), b = l.lastIndexOf('"'); String p = (a >= 0 && b > a) ? l.substring(a + 1, b) : "";
    xSemaphoreTake(sdMutex, portMAX_DELAY); if (!plBuilt) buildPlaylist();
    int k = -1; for (int i = 0; i < (int)playlist.size(); i++) if (playlist[i] == p) k = i;
    xSemaphoreGive(sdMutex);
    if (k >= 0) { curIndex = k; pState = 1; reqIndex = k; ok(); } else err();
  }
  else if (l.startsWith("AT+SDMP3=")) cmdMp3List(l.substring(9));
  else if (l.startsWith("AT+SDPLAYN=")) {
    int k = l.substring(11).toInt();
    xSemaphoreTake(sdMutex, portMAX_DELAY); if (!plBuilt) buildPlaylist(); int total = playlist.size(); xSemaphoreGive(sdMutex);
    if (k >= 0 && k < total) { curIndex = k; pState = 1; reqIndex = k; ok(); } else err();
  }
  else if (l == "AT+SDSTAT?") { Serial.printf("+SDSTAT:%d,%d,%d\r\n", (int)pState, curIndex < 0 ? 0 : (int)curIndex, (int)playlist.size()); ok(); }
  else if (l == "AT+SDSTOP") { reqStop = true; pState = 0; ok(); }
  else err();
}

void setup() {
  Serial.begin(115200);
  WiFi.persistent(false); WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(false);
  sdMutex = xSemaphoreCreateMutex();
  SD_MMC.begin("/sdcard", true);              // 1-bit mode: frees GPIO4/12 conflicts; card is optional
  xTaskCreatePinnedToCore(audioTask, "audio", 8192, nullptr, 3, nullptr, 1);
  Serial.print("\r\nready\r\n");
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') { handle(line); line = ""; }
    else if (c != '\r' && line.length() < 160) line += c;
  }
  pumpTcp();
}
