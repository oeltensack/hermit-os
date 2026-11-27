// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 D.Koch – Pagurus OS
// Heizungsanzeige auf dem AZ-Touch MOD (2,4", ILI9341 + XPT2046) – Anzeige und Bedienung wie die
// Arduino-Sketche AZ_Touch_Heizung v12.1, aber ohne fest eingetragenes WLAN und feste Adressen:
// WLAN ueber den Zugangspunkt, Adressen von Warmwasser-Logik und Zirkulation in der Weboberflaeche.
//   display:0   Status: Werte der Heizung, Verbindung zu den Quellen; Config: Adressen, Drehung
// Bedienung am Display: unten links BOOST (boost_trigger der Logik), unten rechts ZIRKULATION (Switch.Set).
#pragma once
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>
#include <esp_task_wdt.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSans9pt7b.h>

// --- Pins AZ-Touch MOD (SPI an VSPI: SCK 18, MISO 19, MOSI 23) ---
#define TFT_CS    5
#define TFT_DC    4
#define TFT_RST   22
#define TFT_BL    15          // Hintergrundbeleuchtung, LOW = an
#define TOUCH_CS  14
#define TOUCH_IRQ 27
// Kalibrierung 2,4"
#define TS_MINX 472
#define TS_MAXX 3647
#define TS_MINY 544
#define TS_MAXY 3445

#define MIN_TEMP_SPEICHER 20
#define MAX_TEMP_SPEICHER 70
#define MIN_TEMP_KESSEL   20
#define MAX_TEMP_KESSEL   80
#define HTTP_CONNECT_MS   1500
#define HTTP_TIMEOUT_MS   2500
#define WDT_SEKUNDEN      30
#define MIN_HEAP          20000
#define STALE_MS          30000UL   // aelter: Kopfzeile orange, Werte gelten als veraltet

#define C_BG 0x0000
#define C_HEADER 0x0010
#define C_TEXT 0xFFFF
#define C_ACCENT 0x03EF
#define C_ORANGE 0xFD20
#define C_ALERT 0xF800
#define C_OK 0x07E0
#define C_BTN_OFF 0x2124
#define C_BAR_BG 0x10A2
#define C_LED_OFF 0x10A2
#define C_DIM 0xAD55

#define AC_METHODS "Display.GetStatus", "Display.GetConfig", "Display.SetConfig", "Display.Boost", "Display.Zirk",

Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

struct DispCfg {
  String wwIp; int wwSid = 1;          // Shelly mit der Warmwasser-Logik und Nummer des Logik-Scripts
  String zIp;  int zTid = 101;         // Zirkulations-Shelly und Nummer des Temperaturfuehlers (Add-on)
  int rot = 3;                         // Bilddrehung 3 (Standard) oder 1 (auf dem Kopf)
} dcfg;

struct DispData {
  float s = 0, k = 0, cons = 0, zt = 0;
  bool burn = false, pump = false, boost = false, zOn = false, zHasT = false;
  uint32_t wwAt = 0, zAt = 0;          // letzte gueltige Antwort (millis), 0 = nie
  uint32_t wwNext = 0, zNext = 0;      // naechste Abfrage
  String wwErr, zErr;
} dd;

enum Screen { SC_NONE, SC_SETUP, SC_MAIN, SC_UPDATE };
Screen screen = SC_NONE;
uint32_t clockAt = 0, touchLock = 0, drawAt = 0;
bool dataDirty = true;
String setupKey;

void dispLoadCfg() {
  prefs.begin("disp", true);
  dcfg.wwIp = prefs.getString("wwIp", "");
  dcfg.wwSid = prefs.getInt("wwSid", 1);
  dcfg.zIp = prefs.getString("zIp", "");
  dcfg.zTid = prefs.getInt("zTid", 101);
  dcfg.rot = prefs.getInt("rot", 3);
  prefs.end();
}

void dispSaveCfg() {
  prefs.begin("disp", false);
  prefs.putString("wwIp", dcfg.wwIp);
  prefs.putInt("wwSid", dcfg.wwSid);
  prefs.putString("zIp", dcfg.zIp);
  prefs.putInt("zTid", dcfg.zTid);
  prefs.putInt("rot", dcfg.rot);
  prefs.end();
}

// ======================= Netzwerk =======================
// HTTP-GET mit kurzen Timeouts; Antwort leer bei Fehler (err sagt warum)
String dGet(const String& url, String& err) {
  String body;
  if (WiFi.status() != WL_CONNECTED) { err = "kein WLAN"; return body; }
  HTTPClient http;
  http.setConnectTimeout(HTTP_CONNECT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  if (!http.begin(url)) { err = "Adresse ungültig"; return body; }
  int code = http.GET();
  if (code == 200 && http.getSize() < 2048) { body = http.getString(); err = ""; }
  else err = code > 0 ? "HTTP " + String(code) : "keine Antwort";
  http.end();
  esp_task_wdt_reset();
  return body;
}

String wwUrl(const char* ep) { return "http://" + dcfg.wwIp + "/script/" + String(dcfg.wwSid) + "/" + ep; }

void fetchWW() {
  if (!dcfg.wwIp.length()) return;
  String e, b = dGet(wwUrl("api_data"), e);
  JsonDocument d;
  if (b.length() && !deserializeJson(d, b)) {
    dd.s = d["temp_s"] | 0.0f; dd.k = d["temp_k"] | 0.0f;        // null (Fuehlerstoerung) -> 0,0 wie bisher
    dd.pump = (d["pump"] | 0) == 1; dd.burn = (d["burn"] | 0) == 1;
    dd.cons = d["cons_today"] | 0.0f; dd.boost = (d["boost"] | 0) == 1;
    dd.wwAt = millis(); dd.wwErr = ""; dd.wwNext = millis() + 5000;
  } else {
    dd.wwErr = e.length() ? e : "Antwort ungültig";
    dd.wwNext = millis() + 15000;                              // Quelle weg: seltener fragen, Bedienung bleibt flott
  }
  dataDirty = true;
}

void fetchZirk() {
  if (!dcfg.zIp.length()) return;
  String e, b = dGet("http://" + dcfg.zIp + "/rpc/Switch.GetStatus?id=0", e);
  JsonDocument d;
  bool ok = b.length() && !deserializeJson(d, b) && d["output"].is<bool>();
  if (ok) {
    dd.zOn = d["output"].as<bool>();
    if (dcfg.zTid >= 0) {
      String b2 = dGet("http://" + dcfg.zIp + "/rpc/Temperature.GetStatus?id=" + String(dcfg.zTid), e);
      JsonDocument t;
      if (b2.length() && !deserializeJson(t, b2) && t["tC"].is<float>()) {
        float v = t["tC"].as<float>();
        if (v > -50 && v < 150) { dd.zt = v; dd.zHasT = true; }
      }
    }
    dd.zAt = millis(); dd.zErr = ""; dd.zNext = millis() + 5000;
  } else {
    dd.zErr = e.length() ? e : "Antwort ungültig";
    dd.zNext = millis() + 15000;
  }
  dataDirty = true;
}

bool doBoost() {
  if (!dcfg.wwIp.length()) return false;
  dd.boost = !dd.boost; dataDirty = true;                       // sofort anzeigen
  String e; dGet(wwUrl("boost_trigger"), e);
  dd.wwNext = millis() + 1500;                                  // gleich den echten Zustand holen
  return !e.length();
}

bool doZirk(int on) {                                           // on: 1 an, 0 aus, -1 umschalten
  if (!dcfg.zIp.length()) return false;
  dd.zOn = on < 0 ? !dd.zOn : on == 1; dataDirty = true;
  String e; dGet("http://" + dcfg.zIp + "/rpc/Switch.Set?id=0&on=" + String(dd.zOn ? "true" : "false"), e);
  dd.zNext = millis() + 1500;
  return !e.length();
}

// ======================= Zeichnen =======================
void centered(int x, int y, int w, int h, const String& text, const GFXfont* f, uint16_t color) {
  tft.setFont(f); tft.setTextColor(color);
  int16_t x1, y1; uint16_t tw, th; tft.getTextBounds(text, 0, 0, &x1, &y1, &tw, &th);
  tft.setCursor(x + (w - tw) / 2, y + (h + th) / 2 - 4); tft.print(text);
}

void bar(int x, int y, int w, int h, float val, float mn, float mx, uint16_t color) {
  tft.drawRect(x, y, w, h, C_TEXT);
  tft.fillRect(x + 1, y + 1, w - 2, h - 2, C_BAR_BG);
  float c = constrain(val, mn, mx);
  int mw = map((long)(c * 10), (long)(mn * 10), (long)(mx * 10), 0, w - 2);
  if (mw > 0) tft.fillRect(x + 1, y + 1, mw, h - 2, color);
}

void led(int x, int y, char label, bool active, uint16_t col) {
  tft.fillCircle(x, y, 11, active ? col : C_LED_OFF);
  tft.drawCircle(x, y, 11, C_TEXT);
  tft.setFont(&FreeSansBold9pt7b); tft.setTextColor(C_TEXT);
  tft.setCursor(x - 5, y + 5); tft.print(label);
}

void value(int x, int y, const String& text, const GFXfont* f, uint16_t color, int w, int h) {
  tft.fillRect(x, y, w, h, C_BG);
  tft.setFont(f); tft.setTextColor(color);
  tft.setCursor(x, y + h - 5); tft.print(text);
}

void drawHeader() {
  bool stale = dcfg.wwIp.length() && (!dd.wwAt || millis() - dd.wwAt > STALE_MS);
  tft.fillRect(0, 0, 110, 30, C_HEADER);
  tft.setFont(&FreeSansBold9pt7b); tft.setTextColor(stale ? C_ORANGE : C_TEXT);
  tft.setCursor(5, 20); tft.print("HEIZUNG");
}

void drawClock() {
  struct tm ti;
  if (!getLocalTime(&ti, 10)) return;
  static const char* days[] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
  char t[6], d[16];
  strftime(t, sizeof t, "%H:%M", &ti);
  snprintf(d, sizeof d, "%s %d.%02d.", days[ti.tm_wday], ti.tm_mday, ti.tm_mon + 1);
  tft.fillRect(110, 0, 210, 29, C_HEADER);
  tft.setFont(&FreeSansBold9pt7b); tft.setTextColor(C_TEXT);
  tft.setCursor(140, 20); tft.print(d);
  tft.setCursor(250, 20); tft.print(t);
}

void drawBase() {
  tft.fillScreen(C_BG);
  tft.fillRect(0, 0, 320, 30, C_HEADER);
  tft.drawFastHLine(0, 30, 320, 0x7BEF);
  drawHeader(); drawClock();
  tft.setFont(&FreeSans9pt7b); tft.setTextColor(C_DIM);
  tft.setCursor(10, 50); tft.print("Speicher:");
  tft.setCursor(10, 100); tft.print("Kessel:");
  tft.setCursor(10, 155); tft.print("Verbrauch heute:");
}

void drawData() {
  drawHeader();
  value(180, 35, String(dd.s, 1) + " C", &FreeSansBold12pt7b, C_TEXT, 100, 20);
  bar(10, 60, 270, 12, dd.s, MIN_TEMP_SPEICHER, MAX_TEMP_SPEICHER, C_ACCENT);
  led(300, 66, 'P', dd.pump, C_ACCENT);
  value(180, 85, String(dd.k, 1) + " C", &FreeSansBold12pt7b, C_ORANGE, 100, 20);
  bar(10, 110, 270, 10, dd.k, MIN_TEMP_KESSEL, MAX_TEMP_KESSEL, C_ORANGE);
  led(300, 115, 'B', dd.burn, C_ALERT);
  value(180, 140, String(dd.cons, 1) + " L", &FreeSansBold12pt7b, C_TEXT, 100, 20);

  uint16_t bc = dd.boost ? C_ALERT : C_BTN_OFF, bt = dd.boost ? C_BG : C_TEXT;
  tft.fillRoundRect(10, 175, 145, 55, 8, bc); tft.drawRoundRect(10, 175, 145, 55, 8, C_TEXT);
  centered(10, 180, 145, 25, "BOOST", &FreeSansBold9pt7b, bt);
  centered(10, 205, 145, 25, dd.boost ? "AN" : "AUS", &FreeSans9pt7b, bt);

  uint16_t zc = dd.zOn ? C_OK : C_BTN_OFF, zt = dd.zOn ? C_BG : C_TEXT;
  tft.fillRoundRect(165, 175, 145, 55, 8, zc); tft.drawRoundRect(165, 175, 145, 55, 8, C_TEXT);
  centered(165, 180, 145, 25, "ZIRKULATION", &FreeSansBold9pt7b, zt);
  String zs = String(dd.zOn ? "AN" : "AUS");
  if (dd.zHasT) zs += " | " + String(dd.zt, 1) + " C";
  centered(165, 205, 145, 25, zs, &FreeSans9pt7b, zt);
}

// Einrichtung: Zugangspunkt bzw. Adresse der Weboberflaeche anzeigen
void drawSetup() {
  bool sta = WiFi.status() == WL_CONNECTED;
  String key = String(sta) + WiFi.localIP().toString() + apActive;
  if (screen == SC_SETUP && key == setupKey) return;
  setupKey = key; screen = SC_SETUP;
  tft.fillScreen(C_BG);
  tft.fillRect(0, 0, 320, 30, C_HEADER);
  tft.setFont(&FreeSansBold9pt7b); tft.setTextColor(C_TEXT);
  tft.setCursor(5, 20); tft.print("PAGURUS HEIZUNG  " FW_VERSION);
  tft.setFont(&FreeSans9pt7b); tft.setTextColor(C_TEXT);
  int y = 60;
  auto line = [&](const String& s, uint16_t c) { tft.setTextColor(c); tft.setCursor(10, y); tft.print(s); y += 24; };
  if (!sta) {
    if (apActive) {
      line("WLAN einrichten:", C_ORANGE);
      line("1. mit dem WLAN verbinden", C_TEXT);
      line("   " + String(DEV_PREFIX) + "-" + mac.substring(6), C_OK);
      line("2. im Browser 192.168.33.1", C_TEXT);
      line("   Reiter WLAN", C_TEXT);
    } else line("Verbinde mit " + cfg.ssid + " ...", C_TEXT);
  } else {
    line("Verbunden: " + WiFi.localIP().toString(), C_OK);
    line("Adressen eintragen:", C_ORANGE);
    line("http://" + WiFi.localIP().toString(), C_TEXT);
    line("Reiter Anzeige", C_TEXT);
  }
}

void drawUpdate() {
  screen = SC_UPDATE;
  tft.fillScreen(C_BG);
  centered(0, 90, 320, 30, "Firmware-Update ...", &FreeSansBold12pt7b, C_ORANGE);
  centered(0, 130, 320, 30, "nicht ausschalten", &FreeSans9pt7b, C_TEXT);
}

// Vor dem Update: Watchdog aus (Download dauert laenger), Hinweis anzeigen
void dispUpdating() {
  esp_task_wdt_delete(NULL);
  drawUpdate();
}

// ======================= Status / Config / RPC =======================
void dispStatus(JsonObject o) {
  o["id"] = 0;
  JsonObject w = o["ww"].to<JsonObject>();
  w["configured"] = dcfg.wwIp.length() > 0;
  w["ok"] = dd.wwAt && millis() - dd.wwAt < STALE_MS;
  if (dd.wwAt) {
    w["age_s"] = (millis() - dd.wwAt) / 1000;
    w["temp_s"] = dd.s; w["temp_k"] = dd.k; w["burn"] = dd.burn; w["pump"] = dd.pump;
    w["boost"] = dd.boost; w["cons_today"] = dd.cons;
  }
  if (dd.wwErr.length()) w["error"] = dd.wwErr;
  JsonObject z = o["zirk"].to<JsonObject>();
  z["configured"] = dcfg.zIp.length() > 0;
  z["ok"] = dd.zAt && millis() - dd.zAt < STALE_MS;
  if (dd.zAt) { z["age_s"] = (millis() - dd.zAt) / 1000; z["output"] = dd.zOn; if (dd.zHasT) z["tC"] = dd.zt; }
  if (dd.zErr.length()) z["error"] = dd.zErr;
}

void dispConfig(JsonObject o) {
  o["id"] = 0;
  o["ww_ip"] = dcfg.wwIp; o["ww_sid"] = dcfg.wwSid;
  o["zirk_ip"] = dcfg.zIp; o["zirk_tid"] = dcfg.zTid;
  o["rotation"] = dcfg.rot;
}

String cleanIp(String s) {
  s.trim();
  if (s.startsWith("http://")) s = s.substring(7);
  while (s.endsWith("/")) s.remove(s.length() - 1);
  return s;
}

String dispSetConfig(JsonVariantConst c) {
  if (c["ww_ip"].is<const char*>()) dcfg.wwIp = cleanIp(c["ww_ip"].as<const char*>());
  if (c["ww_sid"].is<int>()) { int v = c["ww_sid"]; if (v < 1 || v > 20) return "ww_sid 1–20"; dcfg.wwSid = v; }
  if (c["zirk_ip"].is<const char*>()) dcfg.zIp = cleanIp(c["zirk_ip"].as<const char*>());
  if (c["zirk_tid"].is<int>()) { int v = c["zirk_tid"]; if (v < -1 || v > 199) return "zirk_tid -1 oder 0–199"; dcfg.zTid = v; }
  if (c["rotation"].is<int>()) { int v = c["rotation"]; if (v != 1 && v != 3) return "rotation 1 oder 3"; dcfg.rot = v; }
  dispSaveCfg();
  tft.setRotation(dcfg.rot); ts.setRotation(dcfg.rot == 3 ? 1 : 3);
  dd = DispData(); screen = SC_NONE;                          // neu abfragen und neu zeichnen
  return "";
}

bool acRpc(const String& m, JsonVariantConst p, JsonObject o, String& e) {
  if (m == "display.getstatus") { dispStatus(o); return true; }
  if (m == "display.getconfig") { dispConfig(o); return true; }
  if (m == "display.setconfig") { e = dispSetConfig(p["config"]); return true; }
  if (m == "display.boost") { if (!doBoost()) e = "Warmwasser-Logik nicht erreichbar"; return true; }
  if (m == "display.zirk") {
    int on = p["on"].is<bool>() ? (p["on"].as<bool>() ? 1 : 0) : -1;
    if (!doZirk(on)) e = "Zirkulation nicht erreichbar";
    return true;
  }
  return false;
}

// ======================= Start / Schleife =======================
void dispBegin() {
  Serial.printf("\n[disp] Pagurus OS %s %s, Reset-Grund %d, Heap %u\n", FW_VERSION, DEV_VARIANT,
                (int)esp_reset_reason(), ESP.getFreeHeap());
  dispLoadCfg();
  Serial.printf("[disp] Quellen: ww %s/%d, zirk %s/%d, Drehung %d\n", dcfg.wwIp.c_str(), dcfg.wwSid,
                dcfg.zIp.c_str(), dcfg.zTid, dcfg.rot);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, LOW);
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  esp_task_wdt_config_t wc = { .timeout_ms = WDT_SEKUNDEN * 1000, .idle_core_mask = 0, .trigger_panic = true };
  if (esp_task_wdt_reconfigure(&wc) != ESP_OK) esp_task_wdt_init(&wc);
#else
  esp_task_wdt_init(WDT_SEKUNDEN, true);
#endif
  esp_task_wdt_add(NULL);
  Serial.println("[disp] tft.begin");
  tft.begin();
  Serial.printf("[disp] ILI9341 ID %02X %02X %02X\n", tft.readcommand8(ILI9341_RDMODE),
                tft.readcommand8(ILI9341_RDMADCTL), tft.readcommand8(ILI9341_RDSELFDIAG));
  tft.setRotation(dcfg.rot);
  ts.begin();
  ts.setRotation(dcfg.rot == 3 ? 1 : 3);
  tft.fillScreen(C_BG);
  centered(0, 100, 320, 30, "Pagurus Heizung", &FreeSansBold12pt7b, C_TEXT);
  Serial.println("[disp] Startbild gezeichnet");
}

void dispLoop() {
  esp_task_wdt_reset();
  if (ESP.getFreeHeap() < MIN_HEAP) { delay(100); ESP.restart(); }
  if (screen == SC_UPDATE) return;

  bool sta = WiFi.status() == WL_CONNECTED;
  if (!sta || (!dcfg.wwIp.length() && !dcfg.zIp.length())) {
    if (millis() - drawAt > 1000) { drawAt = millis(); drawSetup(); }
    return;
  }
  if (screen != SC_MAIN) { screen = SC_MAIN; drawBase(); dataDirty = true; }

  // Touch: unten links BOOST, unten rechts ZIRKULATION
  if (ts.touched() && (int32_t)(millis() - touchLock) >= 0) {
    TS_Point p = ts.getPoint();
    int x = map(p.x, TS_MINX, TS_MAXX, 0, 320);
    int y = map(p.y, TS_MINY, TS_MAXY, 0, 240);
    if (y > 160) {
      touchLock = millis() + 400;                               // entprellen
      if (x < 160) doBoost(); else doZirk(-1);
      drawData(); dataDirty = false;
    }
  }

  if ((int32_t)(millis() - dd.wwNext) >= 0) fetchWW();
  if ((int32_t)(millis() - dd.zNext) >= 0) fetchZirk();
  if (millis() - clockAt > 1000) { clockAt = millis(); drawClock(); drawHeader(); }
  if (dataDirty) { dataDirty = false; drawData(); }
}
