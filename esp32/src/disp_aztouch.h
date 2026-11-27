// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 D.Koch – Pagurus OS
// Heizungsanzeige auf dem AZ-Touch MOD (2,4", ILI9341 + XPT2046) – Anzeige und Bedienung wie die
// Arduino-Sketche AZ_Touch_Heizung v12.1, aber ohne fest eingetragenes WLAN und feste Adressen:
// WLAN ueber den Zugangspunkt, Adressen von Warmwasser-Logik und Zirkulation in der Weboberflaeche.
//   display:0   Status: Werte der Heizung, Verbindung zu den Quellen; Config: Adressen, Drehung
// Seite HEIZUNG: unten links BOOST (boost_trigger der Logik), unten rechts ZIRKULATION (Switch.Set).
// Seite BAD (nur mit eingetragenem Heizluefter, Script ab v11): Raum, Soll, Status, Leistung, Verbrauch;
//   Tasten SOLL - | BETRIEB | BOOST (bei Stoerung QUITTIEREN) | SOLL +. Das Display schreibt nur in den KVS
//   (hl_cmd, hl_cfg) – die Regelung bleibt im Heizluefter-Script.
// Seitenwechsel: Tipp auf die Kopfzeile; nach 2 min ohne Beruehrung zurueck zu HEIZUNG.
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
#define MIN_TEMP_BAD      10
#define MAX_TEMP_BAD      30
#define HL_SOLL_MIN       5.0
#define HL_SOLL_MAX       28.0
#define HL_SOLL_STEP      0.5
#define HL_SOLL_DELAY_MS  1500       // Soll erst schreiben, wenn 1,5 s keine Taste mehr
#define HL_CFG_EVERY_MS   30000UL    // Soll/Verbrauch alle 30 s lesen
#define HL_CMD_HOLD_MS    8000UL     // nach Tastendruck so lange die eigene Anzeige behalten
#define PAGE_TIMEOUT_MS   120000UL

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
#define C_GREY 0xAD55

#define AC_METHODS "Display.GetStatus", "Display.GetConfig", "Display.SetConfig", "Display.Boost", "Display.Zirk", "Display.Page",

Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

struct DispCfg {
  String wwIp; int wwSid = 1;          // Shelly mit der Warmwasser-Logik und Nummer des Logik-Scripts
  String zIp;  int zTid = 101;         // Zirkulations-Shelly und Nummer des Temperaturfuehlers (Add-on)
  String hlIp; String hlName = "BAD";  // Heizluefter (Script ab v11, KVS hl_*) fuer die Seite BAD
  int rot = 3;                         // Bilddrehung 3 (Standard) oder 1 (auf dem Kopf)
} dcfg;

struct DispData {
  float s = 0, k = 0, cons = 0, zt = 0;
  bool burn = false, pump = false, boost = false, zOn = false, zHasT = false;
  uint32_t wwAt = 0, zAt = 0;          // letzte gueltige Antwort (millis), 0 = nie
  uint32_t wwNext = 0, zNext = 0;      // naechste Abfrage
  String wwErr, zErr;
} dd;

struct BadData {
  bool tempOk = false; float temp = 0; bool sollOk = false; float soll = 20; int boostMin = 60;
  int pwr = 0; bool boost = false, heat = false, fan = false, on = true; int rest = 0; float today = -1;
  String status, err; uint16_t stCol = C_GREY;
  uint32_t at = 0, cfgAt = 0, next = 0, cmdUntil = 0, sollChg = 0; bool sollPend = false, onPend = false;
} bd;
int page = 0;                          // 0 = HEIZUNG, 1 = BAD
uint32_t lastTouch = 0;

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
  dcfg.hlIp = prefs.getString("hlIp", "");
  dcfg.hlName = prefs.getString("hlName", "BAD");
  prefs.end();
}

void dispSaveCfg() {
  prefs.begin("disp", false);
  prefs.putString("wwIp", dcfg.wwIp);
  prefs.putInt("wwSid", dcfg.wwSid);
  prefs.putString("zIp", dcfg.zIp);
  prefs.putInt("zTid", dcfg.zTid);
  prefs.putInt("rot", dcfg.rot);
  prefs.putString("hlIp", dcfg.hlIp);
  prefs.putString("hlName", dcfg.hlName);
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

// ---- Heizluefter (KVS) ----
bool dPost(const String& url, const String& json) {
  if (WiFi.status() != WL_CONNECTED) return false;
  HTTPClient http;
  http.setConnectTimeout(HTTP_CONNECT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  bool ok = false;
  if (http.begin(url)) { http.addHeader("Content-Type", "application/json"); ok = http.POST(json) == 200; http.end(); }
  esp_task_wdt_reset();
  return ok;
}

// KVS.Get -> Text des Werts (Zahl als Text)
bool kvsGet(const char* key, String& out) {
  String e, b = dGet("http://" + dcfg.hlIp + "/rpc/KVS.Get?key=%22" + key + "%22", e);
  JsonDocument d;
  if (!b.length() || deserializeJson(d, b)) return false;
  if (d["value"].is<const char*>()) { out = d["value"].as<const char*>(); return true; }
  if (d["value"].is<float>()) { out = String(d["value"].as<float>(), 1); return true; }
  return false;
}

bool kvsSet(const char* key, const String& value) {
  JsonDocument d;
  d["id"] = 1; d["method"] = "KVS.Set"; d["params"]["key"] = key; d["params"]["value"] = value;
  String js; serializeJson(d, js);
  return dPost("http://" + dcfg.hlIp + "/rpc", js);
}

// Umlaute fuer die Display-Schrift ersetzen, Emojis und andere Sonderzeichen weglassen
String asciiDe(const char* s) {
  String o;
  if (!s) return o;
  const unsigned char* p = (const unsigned char*)s;
  while (*p) {
    unsigned char c = *p;
    if (c < 0x80) { o += (char)c; p++; continue; }
    if (c == 0xC3 && p[1]) {
      switch (p[1]) {
        case 0xA4: o += "ae"; break; case 0xB6: o += "oe"; break; case 0xBC: o += "ue"; break;
        case 0x84: o += "Ae"; break; case 0x96: o += "Oe"; break; case 0x9C: o += "Ue"; break;
        case 0x9F: o += "ss"; break; default: break;
      }
      p += 2; continue;
    }
    if (c == 0xC2 && p[1] == 0xB0) { p += 2; continue; }      // Gradzeichen
    p++;
  }
  o.trim();
  return o;
}

void fetchBadCfg() {
  bd.cfgAt = millis();
  String v;
  if (!bd.sollPend && !bd.onPend && kvsGet("hl_cfg", v)) {
    JsonDocument c;
    if (!deserializeJson(c, v)) {
      if (c.is<float>()) { bd.soll = c.as<float>(); bd.sollOk = true; }
      else {
        if (c["t"].is<float>()) { bd.soll = c["t"].as<float>(); bd.sollOk = true; }
        if (c["boost"].is<int>() && c["boost"].as<int>() > 0) bd.boostMin = c["boost"].as<int>();
      }
    }
  }
  if (kvsGet("hl_stats", v)) {
    JsonDocument s;
    if (!deserializeJson(s, v) && s["d0"].is<float>()) bd.today = s["d0"].as<float>();
  }
}

void fetchBad() {
  bd.next = millis() + 5000;
  String v;
  if (!kvsGet("hl_state", v)) { bd.next = millis() + 15000; dataDirty = true; return; }
  JsonDocument d;
  if (deserializeJson(d, v)) return;
  bd.tempOk = d["temp"].is<float>();
  if (bd.tempOk) bd.temp = d["temp"].as<float>();
  bd.pwr = d["pwr"].is<const char*>() ? atoi(d["pwr"].as<const char*>()) : (d["pwr"] | 0);
  bd.heat = d["isH"] | false;
  if (millis() > bd.cmdUntil) {                                 // kurz nach Tastendruck eigene Anzeige behalten
    bd.boost = d["isB"] | false; bd.rest = d["rest"] | 0;
    if (d["on"].is<bool>()) bd.on = d["on"].as<bool>();
  }
  bd.err = asciiDe(d["err"] | "");
  const char* st = d["status"] | "";
  bool vor = strstr(st, "VORLAUF"), nach = strstr(st, "NACHLAUF");
  bd.fan = bd.heat || vor || nach;
  if (bd.err.length())               { bd.status = "STOERUNG: " + bd.err; bd.stCol = C_ALERT; }
  else if (strstr(st, "FENSTER"))    { bd.status = "Fenster offen";       bd.stCol = C_ACCENT; }
  else if (strstr(st, "FROST"))      { bd.status = "Frostschutz heizt";   bd.stCol = C_ACCENT; }
  else if (bd.heat)                  { bd.status = d["isB"] | false ? "HEIZT (Boost)" : "HEIZT"; bd.stCol = C_ORANGE; }
  else if (vor)                      { bd.status = "Vorlauf (Luefter)";   bd.stCol = C_TEXT; }
  else if (nach)                     { bd.status = "Nachlauf (Luefter)";  bd.stCol = C_TEXT; }
  else if (strstr(st, "BEREIT"))     { bd.status = "Bereit, Soll erreicht"; bd.stCol = C_OK; }
  else if (strstr(st, "PAUSE"))      { bd.status = asciiDe(st);           bd.stCol = C_GREY; }
  else                               { bd.status = "AUS";                 bd.stCol = C_GREY; }
  bd.at = millis(); if (!bd.at) bd.at = 1;
  dataDirty = true;
}

bool badOnline() { return bd.at && millis() - bd.at < STALE_MS; }

void writeBadCfg() {
  // aktuelle Einstellungen lesen und nur t bzw. on aendern – nichts anderes ueberschreiben
  String v; JsonDocument c;
  if (!kvsGet("hl_cfg", v) || deserializeJson(c, v) || !c.is<JsonObject>()) {
    bd.sollChg = millis() + 3500;                                // nicht lesbar: nichts ueberschreiben, in 5 s nochmal
    return;
  }
  if (bd.sollPend) c["t"] = roundf(bd.soll * 10) / 10;
  if (bd.onPend) c["on"] = bd.on;
  bd.sollPend = bd.onPend = false;
  String js; serializeJson(c, js);
  kvsSet("hl_cfg", js);
  bd.cfgAt = millis(); bd.next = millis() + 2000;
}

void badSoll(float delta) {
  if (!bd.sollOk) { bd.soll = 20; bd.sollOk = true; }
  bd.soll = constrain(bd.soll + delta, HL_SOLL_MIN, HL_SOLL_MAX);
  bd.sollPend = true; bd.sollChg = millis();
}

void badOnOff() {
  // ueber hl_cfg statt hl_cmd: ein gleichzeitig geaenderter Sollwert geht so nicht verloren
  bd.on = !bd.on; bd.cmdUntil = millis() + HL_CMD_HOLD_MS; bd.onPend = true;
  writeBadCfg();                                               // Script uebernimmt in 5 s
}

void badBoost() {
  if (badOnline() && bd.err.length()) {
    kvsSet("hl_cmd", "ack"); bd.err = ""; bd.status = "Quittiert ..."; bd.stCol = C_TEXT;
    return;
  }
  bd.boost = !bd.boost; bd.rest = bd.boost ? bd.boostMin : 0; bd.cmdUntil = millis() + HL_CMD_HOLD_MS;
  kvsSet("hl_cmd", bd.boost ? "boost_on" : "boost_off");
  bd.next = millis() + 2000;
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
  bool stale = page == 0 ? dcfg.wwIp.length() && (!dd.wwAt || millis() - dd.wwAt > STALE_MS) : !badOnline();
  tft.fillRect(0, 0, 110, 30, C_HEADER);
  tft.setFont(&FreeSansBold9pt7b); tft.setTextColor(stale ? C_ORANGE : C_TEXT);
  tft.setCursor(5, 20);
  String t = page == 0 ? String("HEIZUNG") : dcfg.hlName.substring(0, 9);
  tft.print(t);
  if (dcfg.hlIp.length()) {                                     // Pfeil: Kopfzeile antippen wechselt die Seite
    int16_t x1, y1; uint16_t tw, th; tft.getTextBounds(t, 0, 0, &x1, &y1, &tw, &th);
    int x = 5 + tw + 8; if (x > 100) x = 100;
    tft.fillTriangle(x, 9, x, 21, x + 8, 15, C_GREY);
  }
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
  if (page == 0) {
    tft.setCursor(10, 50); tft.print("Speicher:");
    tft.setCursor(10, 100); tft.print("Kessel:");
    tft.setCursor(10, 155); tft.print("Verbrauch heute:");
  } else {
    tft.setCursor(10, 50); tft.print("Raum:");
    tft.setCursor(10, 100); tft.print("Soll:");
  }
}

String fitText(String text, const GFXfont* f, int maxW) {
  tft.setFont(f);
  int16_t x1, y1; uint16_t tw, th;
  tft.getTextBounds(text, 0, 0, &x1, &y1, &tw, &th);
  while (text.length() > 1 && tw > maxW) { text.remove(text.length() - 1); tft.getTextBounds(text, 0, 0, &x1, &y1, &tw, &th); }
  return text;
}

void btn(int x, int w, uint16_t bg, uint16_t fg, const String& l1, const String& l2, const GFXfont* f2) {
  tft.fillRoundRect(x, 175, w, 55, 8, bg); tft.drawRoundRect(x, 175, w, 55, 8, C_TEXT);
  centered(x, 180, w, 25, l1, &FreeSansBold9pt7b, fg);
  centered(x, f2 == &FreeSansBold12pt7b ? 202 : 205, w, f2 == &FreeSansBold12pt7b ? 28 : 25, l2, f2, fg);
}

void drawBad() {
  drawHeader();
  bool on = badOnline();
  value(180, 35, on && bd.tempOk ? String(bd.temp, 1) + " C" : String("--.- C"), &FreeSansBold12pt7b, C_TEXT, 100, 20);
  bar(10, 60, 270, 12, on && bd.tempOk ? bd.temp : MIN_TEMP_BAD, MIN_TEMP_BAD, MAX_TEMP_BAD, C_ACCENT);
  led(300, 66, 'L', on && bd.fan, C_ACCENT);
  value(180, 85, bd.sollOk ? String(bd.soll, 1) + " C" : String("--.- C"), &FreeSansBold12pt7b, bd.sollPend ? C_TEXT : C_ORANGE, 100, 20);
  led(300, 100, 'H', on && bd.heat, C_ALERT);
  String st = on ? bd.status : String(bd.at ? "Keine Verbindung" : "Verbinde ...");
  value(10, 112, fitText(st, &FreeSansBold9pt7b, 272), &FreeSansBold9pt7b, on ? bd.stCol : C_ALERT, 280, 22);
  String info = on ? String(bd.pwr) + " W" : String("-- W");
  if (bd.today >= 0) info += "   |   heute " + String(bd.today, 2) + " kWh";
  value(10, 138, info, &FreeSans9pt7b, C_GREY, 300, 22);
  // Tasten: SOLL - | BETRIEB | BOOST/QUITTIEREN | SOLL +   (je 72 px, 6 px Abstand)
  btn(6, 72, C_BTN_OFF, C_TEXT, "SOLL", "-", &FreeSansBold12pt7b);
  btn(84, 72, bd.on ? C_OK : C_BTN_OFF, bd.on ? C_BG : C_TEXT, "BETRIEB", bd.on ? "EIN" : "AUS", &FreeSans9pt7b);
  bool fault = on && bd.err.length();
  uint16_t bc = fault ? C_ORANGE : (bd.boost ? C_ALERT : C_BTN_OFF), tc = (fault || bd.boost) ? C_BG : C_TEXT;
  if (fault) btn(162, 72, bc, tc, "STOER.", "QUITT.", &FreeSans9pt7b);
  else btn(162, 72, bc, tc, "BOOST", bd.boost ? (bd.rest > 0 ? String(bd.rest) + " min" : String("AN")) : String("AUS"), &FreeSans9pt7b);
  btn(240, 72, C_BTN_OFF, C_TEXT, "SOLL", "+", &FreeSansBold12pt7b);
}

void showPage(int p) {
  if (p == 1 && !dcfg.hlIp.length()) p = 0;
  page = p; drawBase(); dataDirty = true;
  if (page == 1) { bd.next = 0; bd.cfgAt = 0; } else { dd.wwNext = 0; dd.zNext = 0; }
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
    } else {
      line("Verbinde mit", C_TEXT);
      line("   " + cfg.ssid, C_OK);
      line("Klappt es nicht (Name/Passwort),", C_DIM);
      line("kommt nach 1 min wieder der", C_DIM);
      line("Zugangspunkt zum Einrichten.", C_DIM);
    }
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

// Update gescheitert (kein Neustart): Watchdog wieder an, normale Anzeige zurueck
void dispUpdateFailed(const String& e) {
  Serial.printf("[disp] Update abgebrochen: %s\n", e.c_str());
  esp_task_wdt_add(NULL);
  screen = SC_NONE; setupKey = ""; dataDirty = true;
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
  o["page"] = page == 0 ? "heizung" : "bad";
  JsonObject h = o["bad"].to<JsonObject>();
  h["configured"] = dcfg.hlIp.length() > 0;
  h["ok"] = badOnline();
  if (bd.at) { h["age_s"] = (millis() - bd.at) / 1000; if (bd.tempOk) h["temp"] = bd.temp; h["status"] = bd.status;
    h["boost"] = bd.boost; h["on"] = bd.on; h["pwr"] = bd.pwr; }
  if (bd.sollOk) h["soll"] = bd.soll;
}

void dispConfig(JsonObject o) {
  o["id"] = 0;
  o["ww_ip"] = dcfg.wwIp; o["ww_sid"] = dcfg.wwSid;
  o["zirk_ip"] = dcfg.zIp; o["zirk_tid"] = dcfg.zTid;
  o["rotation"] = dcfg.rot;
  o["hl_ip"] = dcfg.hlIp; o["hl_name"] = dcfg.hlName;
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
  if (c["hl_ip"].is<const char*>()) dcfg.hlIp = cleanIp(c["hl_ip"].as<const char*>());
  if (c["hl_name"].is<const char*>()) { dcfg.hlName = c["hl_name"].as<const char*>(); dcfg.hlName.trim(); if (!dcfg.hlName.length()) dcfg.hlName = "BAD"; }
  if (c["rotation"].is<int>()) { int v = c["rotation"]; if (v != 1 && v != 3) return "rotation 1 oder 3"; dcfg.rot = v; }
  dispSaveCfg();
  tft.setRotation(dcfg.rot); ts.setRotation(dcfg.rot == 3 ? 1 : 3);
  dd = DispData(); bd = BadData(); screen = SC_NONE;          // neu abfragen und neu zeichnen
  if (!dcfg.hlIp.length()) page = 0;
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
  if (m == "display.page") {
    String pg = p["page"] | "";
    showPage(pg == "bad" ? 1 : pg == "heizung" ? 0 : (page ? 0 : 1)); lastTouch = millis();
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

  // Touch: Kopfzeile = Seite wechseln; unten die Tasten der Seite
  if (ts.touched() && (int32_t)(millis() - touchLock) >= 0) {
    TS_Point p = ts.getPoint();
    int x = map(p.x, TS_MINX, TS_MAXX, 0, 320);
    int y = map(p.y, TS_MINY, TS_MAXY, 0, 240);
    lastTouch = millis();
    if (y < 35 && dcfg.hlIp.length()) {
      touchLock = millis() + 400;
      if (bd.sollPend || bd.onPend) writeBadCfg();
      showPage(page ? 0 : 1);
    } else if (y > 160) {
      touchLock = millis() + (page ? 250 : 400);                // entprellen (Soll +/- etwas schneller)
      if (page == 0) { if (x < 160) doBoost(); else doZirk(-1); }
      else if (x < 81) badSoll(-HL_SOLL_STEP);
      else if (x < 159) badOnOff();
      else if (x < 237) badBoost();
      else badSoll(HL_SOLL_STEP);
      dataDirty = true;
    }
  }

  if (page == 0) {
    if ((int32_t)(millis() - dd.wwNext) >= 0) fetchWW();
    if ((int32_t)(millis() - dd.zNext) >= 0) fetchZirk();
  } else {
    if ((bd.sollPend || bd.onPend) && (int32_t)(millis() - bd.sollChg) > (int32_t)HL_SOLL_DELAY_MS) writeBadCfg();
    if ((int32_t)(millis() - bd.next) >= 0) fetchBad();
    if (!bd.cfgAt || millis() - bd.cfgAt > HL_CFG_EVERY_MS) fetchBadCfg();
    if (millis() - lastTouch > PAGE_TIMEOUT_MS) { if (bd.sollPend || bd.onPend) writeBadCfg(); showPage(0); }
  }
  if (millis() - clockAt > 1000) { clockAt = millis(); drawClock(); drawHeader(); }
  if (dataDirty) { dataDirty = false; if (page == 0) drawData(); else drawBad(); }
}
