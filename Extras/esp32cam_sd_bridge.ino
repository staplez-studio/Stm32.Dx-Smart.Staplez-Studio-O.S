// UNTESTED on hardware. ESP32-CAM sketch answering the Staplez Studio OS SD commands on UART2 (115200).
//   AT+SDINFO            -> +SDINFO:<mounted>,<total_mb>,<used_mb>  then OK
//   AT+SDLS="/dir",first,n -> +SDLS:<d|f>,<size>,"name" lines, +SDLS:END,<total>, OK   (ERROR if folder missing)
//   AT+SDPLAY="/path.mp3"  -> OK / ERROR   (decoding MP3 -> I2S to the ADAU1401 is YOUR audio code: see playMp3())
//   AT+SDSTOP              -> OK
// Wi-Fi AT commands are NOT implemented here: use a separate ESP-01S, or merge this with an AT-compatible layer.
#include <SD_MMC.h>
#include <FS.h>

static String line;
static void ok()  { Serial.print("OK\r\n"); }
static void err() { Serial.print("ERROR\r\n"); }
static void playMp3(const String &path) { /* TODO: start decoder (e.g. ESP8266Audio) -> I2S -> ADAU1401 */ }
static void stopMp3() { /* TODO */ }

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

static void handle(String l) {
  l.trim();
  if (l == "AT") ok();
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

void setup() { Serial.begin(115200); SD_MMC.begin("/sdcard", true); }
void loop() {
  while (Serial.available()) { char c = Serial.read(); if (c == '\n') { handle(line); line = ""; } else if (c != '\r') line += c; }
}
