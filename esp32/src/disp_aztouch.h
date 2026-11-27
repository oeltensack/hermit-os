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
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>

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

constexpr uint16_t RGB565(uint8_t r, uint8_t g, uint8_t b) { return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); }
constexpr uint16_t C_BG = RGB565(0x0B, 0x0F, 0x14), C_CARD = RGB565(0x16, 0x1C, 0x24), C_TRACK = RGB565(0x26, 0x30, 0x3B),
  C_LINE = RGB565(0x3A, 0x44, 0x4F), C_TEXT = RGB565(0xE6, 0xE9, 0xEC), C_DIM = RGB565(0x8A, 0x96, 0xA3),
  C_BLUE = RGB565(0x2F, 0x9B, 0xE0), C_ORANGE = RGB565(0xF0, 0x88, 0x3E), C_TEAL = RGB565(0x1D, 0x9E, 0x75),
  C_TEAL_D = RGB565(0x04, 0x34, 0x2C), C_RED = RGB565(0xE2, 0x4B, 0x4A), C_RED_D = RGB565(0x50, 0x13, 0x13),
  C_AMBER = RGB565(0xEF, 0x9F, 0x27), C_AMBER_D = RGB565(0x41, 0x24, 0x02);

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
  float s = 0, k = 0, r = 0, soll = 0, cons = 0, zt = 0;
  bool sOk = false, kOk = false, rOk = false, sollOk = false;
  bool burn = false, pump = false, boost = false, zOn = false, zHasT = false;
  String fault;                        // Fuehlerstoerung der Logik (status: fault)
  uint32_t wwAt = 0, zAt = 0;          // letzte gueltige Antwort (millis), 0 = nie
  uint32_t wwNext = 0, zNext = 0;      // naechste Abfrage
  String wwErr, zErr;
} dd;

struct BadData {
  bool tempOk = false; float temp = 0; bool sollOk = false; float soll = 20; int boostMin = 60;
  int pwr = 0; bool boost = false, heat = false, fan = false, on = true; int rest = 0; float today = -1;
  bool win = false;                    // Fenster offen
  String status, err; uint16_t stCol = C_DIM;
  uint32_t at = 0, cfgAt = 0, next = 0, cmdUntil = 0, sollChg = 0; bool sollPend = false, onPend = false;
} bd;
int page = 0;                          // 0 = HEIZUNG, 1 = BAD
String num1(float v) { return String(v, 1); }
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
  // status (Logik ab V13) hat Soll, Ruecklauf und Stoerung; aeltere Logik: api_data
  static bool useStatus = true;
  String e, b = dGet(wwUrl(useStatus ? "status" : "api_data"), e);
  if (!b.length() && useStatus && e.startsWith("HTTP 404")) { useStatus = false; b = dGet(wwUrl("api_data"), e); }
  JsonDocument d;
  if (b.length() && !deserializeJson(d, b)) {
    dd.sOk = d["temp_s"].is<float>(); dd.s = d["temp_s"] | 0.0f;
    dd.kOk = d["temp_k"].is<float>(); dd.k = d["temp_k"] | 0.0f;
    dd.rOk = d["temp_r"].is<float>(); dd.r = d["temp_r"] | 0.0f;
    dd.sollOk = d["soll"].is<float>(); dd.soll = d["soll"] | 0.0f;
    dd.pump = (d["pump"] | 0) == 1; dd.burn = (d["burn"] | 0) == 1;
    dd.cons = d["cons_today"] | 0.0f;
    dd.boost = d["boost_manual"].is<bool>() ? d["boost_manual"].as<bool>() : (d["boost"] | 0) == 1;
    String f = d["fault"] | "";
    f.replace("speicher", "Speicher"); f.replace("kessel", "Kessel"); f.replace(",", ", ");
    dd.fault = f;
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
// Text vom Script fuer das Display: Umlaute und ° bleiben (dtext zeichnet sie), Emojis u. a. fallen weg
String asciiDe(const char* s) {
  String o;
  if (!s) return o;
  const unsigned char* p = (const unsigned char*)s;
  while (*p) {
    unsigned char c = *p;
    if (c < 0x80) { o += (char)c; p++; continue; }
    if ((c == 0xC3 || (c == 0xC2 && p[1] == 0xB0)) && p[1]) { o += (char)c; o += (char)p[1]; p += 2; continue; }
    p++;
    while ((*p & 0xC0) == 0x80) p++;                           // Rest eines Mehrbyte-Zeichens
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
  bd.win = d["winOpen"].is<bool>() && d["winOpen"].as<bool>();
  const char* st = d["status"] | "";
  bool vor = strstr(st, "VORLAUF"), nach = strstr(st, "NACHLAUF");
  bd.fan = bd.heat || vor || nach;
  if (bd.err.length())               { bd.status = "Störung: " + bd.err; bd.stCol = C_RED; }
  else if (strstr(st, "FENSTER"))    { bd.status = "Fenster offen";       bd.stCol = C_BLUE; }
  else if (strstr(st, "FROST"))      { bd.status = "Frostschutz heizt";   bd.stCol = C_BLUE; }
  else if (bd.heat)                  { bd.status = (d["isB"] | false) ? "Heizt (Boost)" : "Heizt auf " + num1(bd.soll) + " °C"; bd.stCol = C_ORANGE; }
  else if (vor)                      { bd.status = "Vorlauf (Lüfter)";   bd.stCol = C_TEXT; }
  else if (nach)                     { bd.status = "Nachlauf (Lüfter)";  bd.stCol = C_TEXT; }
  else if (strstr(st, "BEREIT"))     { bd.status = "Bereit, Soll erreicht"; bd.stCol = C_TEAL; }
  else if (strstr(st, "PAUSE"))      { bd.status = asciiDe(st);           bd.stCol = C_DIM; }
  else                               { bd.status = "Aus";                 bd.stCol = C_DIM; }
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
// Dunkles, flaches Design: Karten auf fast schwarzem Grund, grosse Zahlen, schlanke Balken, runde Tasten.
// Gezeichnet wird nur, was sich geaendert hat (kein Flackern). Umlaute und Gradzeichen zeichnet dtext()
// selbst, weil die GFX-Schriften nur ASCII enthalten.
String cache[24];                      // zuletzt gezeichneter Inhalt je Element
enum { K_TITLE, K_CLOCK, K_S, K_K, K_OIL, K_ZI, K_B1, K_B2, K_RING, K_LEFT, K_RIGHT, K_STAT, K_BB0, K_BB1, K_BB2, K_BB3 };
bool chg(int i, const String& k) { if (cache[i] == k) return false; cache[i] = k; return true; }
void cacheClear() { for (auto& c : cache) c = ""; }

// Breite eines Texts (UTF-8 mit Umlauten und °) in einer GFX-Schrift
char umlBase(uint8_t n) {               // zweites Byte von C3 xx -> Grundbuchstabe
  switch (n) { case 0x84: return 'A'; case 0x96: return 'O'; case 0x9C: return 'U';
               case 0xA4: return 'a'; case 0xB6: return 'o'; case 0xBC: return 'u'; default: return 0; }
}

int dwidth(const String& s, const GFXfont* f) {
  int w = 0;
  auto adv = [&](uint8_t c) { return (c >= f->first && c <= f->last) ? f->glyph[c - f->first].xAdvance : 0; };
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = s[i];
    if (c == 0xC3 && i + 1 < s.length()) {
      uint8_t n = s[++i];
      if (n == 0x9F) w += 2 * adv('s'); else if (umlBase(n)) w += adv(umlBase(n));
    } else if (c == 0xC2 && i + 1 < s.length() && (uint8_t)s[i + 1] == 0xB0) { i++; w += f->yAdvance / 3; }
    else if (c < 0x80) w += adv(c);
  }
  return w;
}

// Text zeichnen; align 0 = links, 1 = Mitte, 2 = rechts (x ist dann Mitte bzw. rechter Rand), y = Grundlinie
void dtext(int x, int y, const String& s, const GFXfont* f, uint16_t col, int align = 0) {
  if (align) x -= align == 1 ? dwidth(s, f) / 2 : dwidth(s, f);
  tft.setFont(f); tft.setTextColor(col); tft.setTextSize(1);
  int cap = f->yAdvance * 3 / 5, xh = f->yAdvance * 9 / 20, dot = f->yAdvance >= 40 ? 3 : 2;
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = s[i];
    bool uml = false, upper = false;
    if (c == 0xC3 && i + 1 < s.length()) {
      uint8_t n = s[++i];
      if (n == 0x9F) { tft.setCursor(x, y); tft.print("ss"); x = tft.getCursorX(); continue; }
      upper = n == 0x84 || n == 0x96 || n == 0x9C;
      c = umlBase(n);
      if (!c) continue;
      uml = true;
    } else if (c == 0xC2 && i + 1 < s.length() && (uint8_t)s[i + 1] == 0xB0) {
      i++;
      int r = f->yAdvance / 11 + 1;
      tft.drawCircle(x + r + 1, y - cap + r, r, col);
      if (r > 2) tft.drawCircle(x + r + 1, y - cap + r, r - 1, col);
      x += f->yAdvance / 3; continue;
    } else if (c >= 0x80) continue;
    tft.setCursor(x, y); tft.print((char)c);
    int nx = tft.getCursorX();
    if (uml) {
      int w = nx - x, top = y - (upper ? cap : xh) - dot - 2;
      tft.fillRect(x + w * 3 / 10, top, dot, dot, col);
      tft.fillRect(x + w * 6 / 10, top, dot, dot, col);
    }
    x = nx;
  }
}

String fitText(String s, const GFXfont* f, int maxW) {
  while (s.length() > 1 && dwidth(s, f) > maxW) s.remove(s.length() - 1);
  return s;
}

// Balken: Spur, Fuellung, optional Soll-Marke
void slim(int x, int y, int w, float v, float mn, float mx, uint16_t col, float mark = NAN) {
  tft.fillRoundRect(x, y, w, 4, 2, C_TRACK);
  int fw = (int)((constrain(v, mn, mx) - mn) / (mx - mn) * w);
  if (fw > 3) tft.fillRoundRect(x, y, fw, 4, 2, col);
  if (!isnan(mark)) tft.fillRect(x + (int)((constrain(mark, mn, mx) - mn) / (mx - mn) * w) - 1, y - 3, 2, 10, C_TEXT);
}

// Taste: an = gefuellt mit dunkler Schrift, aus = nur Rahmen
void pill(int x, int y, int w, int h, bool on, uint16_t fill, uint16_t dark, const String& l1, const String& l2,
          const GFXfont* f1 = &FreeSansBold12pt7b) {
  tft.fillRoundRect(x, y, w, h, 14, on ? fill : C_BG);
  if (!on) { tft.drawRoundRect(x, y, w, h, 14, C_LINE); tft.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 13, C_LINE); }
  uint16_t t1 = on ? dark : C_TEXT, t2 = on ? dark : C_DIM;
  if (l2.length()) { dtext(x + w / 2, y + h / 2 + 2, fitText(l1, f1, w - 10), f1, t1, 1);
                     dtext(x + w / 2, y + h / 2 + 19, fitText(l2, &FreeSans9pt7b, w - 10), &FreeSans9pt7b, t2, 1); }
  else dtext(x + w / 2, y + h / 2 + 9, l1, f1, t1, 1);
}

void card(int x, int y, int w, int h) { tft.fillRoundRect(x, y, w, h, 8, C_CARD); }

// Statuspunkt mit Text rechtsbuendig an x
void flag(int xr, int y, const String& s, bool on, uint16_t col) {
  dtext(xr, y, s, &FreeSans9pt7b, on ? col : C_LINE, 2);
}

bool wwStale() { return dcfg.wwIp.length() && (!dd.wwAt || millis() - dd.wwAt > STALE_MS); }

void drawHeader() {
  bool stale = page == 0 ? wwStale() : !badOnline();
  String t = page == 0 ? String("Heizung") : dcfg.hlName;
  if (!chg(K_TITLE, t + stale + page)) return;
  tft.fillRect(0, 0, 150, 26, C_BG);
  dtext(10, 18, t, &FreeSansBold9pt7b, stale ? C_ORANGE : C_TEXT);
  if (dcfg.hlIp.length()) {                                     // Seitenpunkte: Kopfzeile antippen wechselt
    int x = 10 + dwidth(t, &FreeSansBold9pt7b) + 12;
    tft.fillCircle(x, 13, 3, page == 0 ? C_TEXT : C_LINE);
    tft.fillCircle(x + 10, 13, 3, page == 1 ? C_TEXT : C_LINE);
  }
}

void drawClock() {
  struct tm ti;
  if (!getLocalTime(&ti, 10)) return;
  static const char* days[] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
  char t[6], d[16];
  strftime(t, sizeof t, "%H:%M", &ti);
  snprintf(d, sizeof d, "%s %d.%02d.", days[ti.tm_wday], ti.tm_mday, ti.tm_mon + 1);
  if (!chg(K_CLOCK, String(d) + t)) return;
  tft.fillRect(150, 0, 170, 26, C_BG);
  int tw = dwidth(t, &FreeSansBold9pt7b);
  dtext(310, 18, t, &FreeSansBold9pt7b, C_TEXT, 2);
  dtext(310 - tw - 8, 18, d, &FreeSans9pt7b, C_DIM, 2);
}

void drawBase() {
  cacheClear();
  tft.fillScreen(C_BG);
  drawHeader(); drawClock();
}

// Grosser Wert mit kleiner Einheit, z. B. 47.5 °C
void bigVal(int x, int y, const String& v, uint16_t col, const String& unit) {
  dtext(x, y, v, &FreeSansBold24pt7b, col);
  dtext(x + dwidth(v, &FreeSansBold24pt7b) + 3, y, unit, &FreeSans12pt7b, C_DIM);
}

void drawData() {
  drawHeader();
  bool ok = !wwStale();
  uint16_t vcol = ok ? C_TEXT : C_DIM;
  // Speicher
  String k = num1(dd.s) + dd.pump + dd.sollOk + dd.soll + ok + dd.sOk;
  if (chg(K_S, k)) {
    card(6, 30, 151, 96);
    dtext(16, 48, "Speicher", &FreeSans9pt7b, C_DIM);
    flag(148, 48, "Pumpe", dd.pump, C_BLUE);
    bigVal(14, 92, dd.sOk ? num1(dd.s) : String("--.-"), vcol, "°C");
    dtext(16, 110, dd.sollOk ? "Soll " + String((int)roundf(dd.soll)) + " °C" : String(""), &FreeSans9pt7b, C_DIM);
    slim(16, 116, 131, dd.s, MIN_TEMP_SPEICHER, MAX_TEMP_SPEICHER, C_BLUE, dd.sollOk ? dd.soll : NAN);
  }
  // Kessel
  k = num1(dd.k) + dd.burn + num1(dd.r) + dd.rOk + dd.fault + ok + dd.kOk;
  if (chg(K_K, k)) {
    card(163, 30, 151, 96);
    dtext(173, 48, "Kessel", &FreeSans9pt7b, C_DIM);
    flag(305, 48, "Brenner", dd.burn, C_ORANGE);
    bigVal(171, 92, dd.kOk ? num1(dd.k) : String("--.-"), ok ? C_ORANGE : C_DIM, "°C");
    if (dd.fault.length()) dtext(173, 110, "Fühlerstörung", &FreeSans9pt7b, C_RED);
    else if (dd.rOk) dtext(173, 110, "Rücklauf " + String((int)roundf(dd.r)) + " °C", &FreeSans9pt7b, C_DIM);
    slim(173, 116, 131, dd.k, MIN_TEMP_KESSEL, MAX_TEMP_KESSEL, C_ORANGE);
  }
  // Oel heute / Zirkulation
  if (chg(K_OIL, num1(dd.cons) + ok)) {
    card(6, 132, 151, 34);
    dtext(16, 154, "Öl heute", &FreeSans9pt7b, C_DIM);
    dtext(148, 155, num1(dd.cons) + " L", &FreeSansBold9pt7b, vcol, 2);
  }
  if (chg(K_ZI, String(dd.zHasT) + num1(dd.zt) + (dd.zAt != 0))) {
    card(163, 132, 151, 34);
    dtext(173, 154, "Leitung", &FreeSans9pt7b, C_DIM);
    dtext(305, 155, dd.zHasT ? num1(dd.zt) + " °C" : String("--"), &FreeSansBold9pt7b, C_TEXT, 2);
  }
  // Tasten
  if (chg(K_B1, String(dd.boost))) pill(6, 174, 151, 60, dd.boost, C_RED, C_RED_D, "Boost", dd.boost ? "läuft" : "aus");
  if (chg(K_B2, String(dd.zOn))) pill(163, 174, 151, 60, dd.zOn, C_TEAL, C_TEAL_D, "Zirkulation", dd.zOn ? "an" : "aus");
}

// Ring (270°-Bogen) aus Vierecken: cx/cy Mitte, r Radius aussen, th Dicke, von a0 bis a1 Grad (0 = rechts, im Uhrzeigersinn)
void arc(int cx, int cy, int r, int th, float a0, float a1, uint16_t col) {
  for (float a = a0; a < a1; a += 3) {
    float b = min(a + 3.5f, a1);
    float c0 = cosf(a * DEG_TO_RAD), s0 = sinf(a * DEG_TO_RAD), c1 = cosf(b * DEG_TO_RAD), s1 = sinf(b * DEG_TO_RAD);
    int x0 = cx + c0 * r, y0 = cy + s0 * r, x1 = cx + c1 * r, y1 = cy + s1 * r;
    int x2 = cx + c0 * (r - th), y2 = cy + s0 * (r - th), x3 = cx + c1 * (r - th), y3 = cy + s1 * (r - th);
    tft.fillTriangle(x0, y0, x1, y1, x2, y2, col);
    tft.fillTriangle(x1, y1, x3, y3, x2, y2, col);
  }
}

void drawBad() {
  drawHeader();
  bool on = badOnline();
  const int cx = 160, cy = 92, R = 54;
  float t = on && bd.tempOk ? bd.temp : NAN;
  String k = (isnan(t) ? String("-") : num1(t)) + bd.sollOk + num1(bd.soll) + bd.sollPend + bd.heat;
  if (chg(K_RING, k)) {
    tft.fillRect(cx - R - 4, 32, 2 * R + 8, 2 * R + 20, C_BG);
    arc(cx, cy, R, 8, 135, 405, C_TRACK);
    if (!isnan(t)) {
      float f = constrain((t - MIN_TEMP_BAD) / (MAX_TEMP_BAD - MIN_TEMP_BAD), 0.0f, 1.0f);
      if (f > 0.01f) arc(cx, cy, R, 8, 135, 135 + 270 * f, bd.heat ? C_ORANGE : C_BLUE);
    }
    if (bd.sollOk) {                                             // Soll-Marke
      float a = (135 + 270 * constrain((bd.soll - MIN_TEMP_BAD) / (MAX_TEMP_BAD - MIN_TEMP_BAD), 0.0f, 1.0f)) * DEG_TO_RAD;
      tft.drawLine(cx + cosf(a) * (R + 2), cy + sinf(a) * (R + 2), cx + cosf(a) * (R - 10), cy + sinf(a) * (R - 10), C_TEXT);
      tft.drawLine(cx + cosf(a) * (R + 2) + 1, cy + sinf(a) * (R + 2), cx + cosf(a) * (R - 10) + 1, cy + sinf(a) * (R - 10), C_TEXT);
    }
    String v = isnan(t) ? String("--.-") : num1(t);
    int w = dwidth(v, &FreeSansBold18pt7b);
    dtext(cx - w / 2 - 4, cy + 13, v, &FreeSansBold18pt7b, isnan(t) ? C_DIM : C_TEXT);
    dtext(cx + w / 2 - 2, cy + 13, "°", &FreeSansBold18pt7b, C_DIM);
    dtext(cx, cy + 56, bd.sollOk ? "Soll " + num1(bd.soll) + "°" : String("Soll --"), &FreeSans9pt7b,
          bd.sollPend ? C_ORANGE : C_DIM, 1);
  }
  if (chg(K_LEFT, String(on) + bd.pwr + num1(bd.today))) {
    card(6, 40, 92, 108);
    dtext(52, 62, "jetzt", &FreeSans9pt7b, C_DIM, 1);
    dtext(52, 84, on ? String(bd.pwr) + " W" : String("-- W"), &FreeSansBold9pt7b, C_TEXT, 1);
    dtext(52, 112, "heute", &FreeSans9pt7b, C_DIM, 1);
    dtext(52, 134, bd.today >= 0 ? String(bd.today, 2) + " kWh" : String("--"), &FreeSansBold9pt7b, C_TEXT, 1);
  }
  if (chg(K_RIGHT, String(on) + bd.fan + bd.heat + bd.win)) {
    card(222, 40, 92, 108);
    struct { const char* l; bool v; uint16_t c; } F[] = {{"Lüfter", on && bd.fan, C_BLUE}, {"Heizt", on && bd.heat, C_ORANGE},
                                                         {"Fenster", on && bd.win, C_BLUE}};
    for (int i = 0; i < 3; i++) {
      int y = 66 + i * 28;
      tft.fillCircle(236, y - 5, 4, F[i].v ? F[i].c : C_LINE);
      dtext(246, y, F[i].l, &FreeSans9pt7b, F[i].v ? C_TEXT : C_DIM);
    }
  }
  String st = on ? bd.status : String(bd.at ? "Keine Verbindung" : "Verbinde ...");
  if (chg(K_STAT, st + bd.stCol + on)) {
    tft.fillRect(0, 153, 320, 20, C_BG);
    dtext(160, 169, fitText(st, &FreeSansBold9pt7b, 300), &FreeSansBold9pt7b, on ? bd.stCol : C_RED, 1);
  }
  bool fault = on && bd.err.length();
  if (chg(K_BB0, "m")) pill(6, 176, 54, 58, false, 0, 0, "-", "", &FreeSansBold18pt7b);
  if (chg(K_BB1, String(bd.on))) pill(66, 176, 91, 58, bd.on, C_TEAL, C_TEAL_D, "Betrieb", bd.on ? "ein" : "aus", &FreeSansBold9pt7b);
  String b2 = fault ? String("Q") : String(bd.boost) + bd.rest;
  if (chg(K_BB2, b2)) {
    if (fault) pill(163, 176, 91, 58, true, C_AMBER, C_AMBER_D, "Störung", "quittieren", &FreeSansBold9pt7b);
    else pill(163, 176, 91, 58, bd.boost, C_RED, C_RED_D, "Boost",
              bd.boost ? (bd.rest > 0 ? String(bd.rest) + " min" : String("an")) : String("aus"), &FreeSansBold9pt7b);
  }
  if (chg(K_BB3, "p")) pill(260, 176, 54, 58, false, 0, 0, "+", "", &FreeSansBold18pt7b);
}

void showPage(int p) {
  if (p == 1 && !dcfg.hlIp.length()) p = 0;
  page = p; drawBase(); dataDirty = true;
  if (page == 1) { bd.next = 0; bd.cfgAt = 0; } else { dd.wwNext = 0; dd.zNext = 0; }
}

// Einrichtung: Zugangspunkt bzw. Adresse der Weboberflaeche anzeigen
void drawSetup() {
  bool sta = WiFi.status() == WL_CONNECTED;
  String key = String(sta) + WiFi.localIP().toString() + apActive;
  if (screen == SC_SETUP && key == setupKey) return;
  setupKey = key; screen = SC_SETUP; cacheClear();
  tft.fillScreen(C_BG);
  dtext(10, 22, "Pagurus Heizung", &FreeSansBold12pt7b, C_TEXT);
  dtext(310, 22, FW_VERSION, &FreeSans9pt7b, C_DIM, 2);
  int y = 58;
  auto line = [&](const String& s, uint16_t c, const GFXfont* f) { dtext(16, y, s, f, c); y += 25; };
  card(6, 36, 308, 196);
  if (!sta) {
    if (apActive) {
      line("WLAN einrichten", C_ORANGE, &FreeSansBold9pt7b);
      line("1. Mit diesem WLAN verbinden:", C_TEXT, &FreeSans9pt7b);
      line("   " + String(DEV_PREFIX) + "-" + mac.substring(6), C_TEAL, &FreeSansBold9pt7b);
      line("2. Im Browser 192.168.33.1", C_TEXT, &FreeSans9pt7b);
      line("   öffnen, Reiter WLAN", C_TEXT, &FreeSans9pt7b);
    } else {
      line("Verbinde mit", C_TEXT, &FreeSans9pt7b);
      line("   " + cfg.ssid, C_TEAL, &FreeSansBold9pt7b);
      line("Klappt es nicht (Name/Passwort),", C_DIM, &FreeSans9pt7b);
      line("kommt nach 1 min wieder der", C_DIM, &FreeSans9pt7b);
      line("Zugangspunkt zum Einrichten.", C_DIM, &FreeSans9pt7b);
    }
  } else {
    line("Verbunden: " + WiFi.localIP().toString(), C_TEAL, &FreeSansBold9pt7b);
    line("Adressen eintragen:", C_ORANGE, &FreeSans9pt7b);
    line("http://" + WiFi.localIP().toString(), C_TEXT, &FreeSansBold9pt7b);
    line("Reiter Anzeige", C_TEXT, &FreeSans9pt7b);
  }
}

void drawUpdate() {
  screen = SC_UPDATE;
  tft.fillScreen(C_BG);
  dtext(160, 110, "Firmware-Update", &FreeSansBold12pt7b, C_ORANGE, 1);
  dtext(160, 140, "nicht ausschalten", &FreeSans9pt7b, C_DIM, 1);
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
  dtext(160, 118, "Pagurus Heizung", &FreeSansBold12pt7b, C_TEXT, 1);
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
      else if (x < 63) badSoll(-HL_SOLL_STEP);
      else if (x < 160) badOnOff();
      else if (x < 257) badBoost();
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
