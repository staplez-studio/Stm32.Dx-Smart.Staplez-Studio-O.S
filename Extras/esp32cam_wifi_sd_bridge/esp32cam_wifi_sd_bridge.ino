// ESP32-CAM Wi-Fi + SD bridge for Staplez Studio OS (DX-SMART PJ26 v1.2).  UNTESTED on hardware.
// Talks ESP-AT-style commands on UART0 (GPIO1 TX / GPIO3 RX) at 115200 8N1 -> STM32 PA3 / PA2.
// Primary job: Wi-Fi so the clock can fetch weather (HTTP to wttr.in) and network time.
// Secondary: browse SD card names (AT+SDINFO / AT+SDLS / AT+SDPLAY / AT+SDSTOP).
// Arduino IDE: board "AI Thinker ESP32-CAM", esp32 core 2.x or 3.x. Disconnect STM32 while uploading
// (GPIO0 to GND to flash, remove for running). Do not print debug text to Serial: it is the AT link.
#include <WiFi.h>
#include <SD_MMC.h>
#include <FS.h>
#include <time.h>

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

// ---- SD browsing ----
static void cmdLs(const String &args) {
  int q1 = args.indexOf('"'), q2 = args.indexOf('"', q1 + 1);
  if (q1 < 0 || q2 < 0) { err(); return; }
  String dir = args.substring(q1 + 1, q2);
  int c1 = args.indexOf(',', q2), c2 = args.indexOf(',', c1 + 1);
  int first = args.substring(c1 + 1, c2).toInt(), n = args.substring(c2 + 1).toInt();
  File d = SD_MMC.open(dir);
  if (!d || !d.isDirectory()) { err(); return; }
  int idx = 0, sent = 0;
  for (File f = d.openNextFile(); f; f = d.openNextFile(), idx++) {
    if (idx >= first && sent < n) {
      String nm = f.name(); int s = nm.lastIndexOf('/'); if (s >= 0) nm = nm.substring(s + 1);
      Serial.printf("+SDLS:%c,%u,\"%s\"\r\n", f.isDirectory() ? 'd' : 'f', (unsigned)f.size(), nm.c_str());
      sent++;
    }
  }
  Serial.printf("+SDLS:END,%d\r\n", idx); ok();
}
static void playMp3(const String &path) { /* TODO: MP3 decode -> I2S -> ADAU1401 (not implemented) */ }
static void stopMp3() { /* TODO */ }

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
    if (SD_MMC.cardType() == CARD_NONE) Serial.print("+SDINFO:0,0,0\r\n");
    else Serial.printf("+SDINFO:1,%llu,%llu\r\n", SD_MMC.totalBytes() >> 20, SD_MMC.usedBytes() >> 20);
    ok();
  }
  else if (l.startsWith("AT+SDLS=")) cmdLs(l.substring(8));
  else if (l.startsWith("AT+SDPLAY=")) {
    int a = l.indexOf('"'), b = l.lastIndexOf('"'); String p = (a >= 0 && b > a) ? l.substring(a + 1, b) : "";
    if (p.length() && SD_MMC.exists(p)) { playMp3(p); ok(); } else err();
  }
  else if (l == "AT+SDSTOP") { stopMp3(); ok(); }
  else err();
}

void setup() {
  Serial.begin(115200);
  WiFi.persistent(false); WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(false);
  SD_MMC.begin("/sdcard", true);              // 1-bit mode: frees GPIO4/12 conflicts; card is optional
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
