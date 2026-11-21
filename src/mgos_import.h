#pragma once
// WLAN-Zugangsdaten aus der Original-Shelly-Firmware (Mongoose OS) lesen, nachdem Hermit OS ueber
// tools/hermit_flash.py (mg2hermit) aufgespielt wurde. Die Konfiguration (conf9.json) liegt im
// SPIFFS-Dateisystem der Original-Firmware: fs1 ab 0x1BB000 (von mg2x dorthin kopiert) bzw. fs0 ab
// 0xBB000, je 256 KB. Hermit OS beschreibt fs0 nie; fs1 wird erst beim ersten Formatieren des
// eigenen LittleFS ueberschrieben – deshalb vor LittleFS.begin() aufrufen.
// SPIFFS: Seiten zu 256 Byte, Kopf 5 Byte (obj_id 2, span 2, flags 1), Daten 251 Byte.
#include <Arduino.h>

namespace mgosimport {

// Sucht ab 'from' (max. 600 Zeichen) nach "key" : "wert" – Leerzeichen erlaubt, wie Mongoose OS sie schreibt
static bool jsonValue(const String& t, int from, const char* key, String& out) {
  String k = String("\"") + key + "\"";
  int p = from;
  while ((p = t.indexOf(k, p)) >= 0 && p - from < 600) {
    int i = p + k.length();
    while (i < (int)t.length() && t[i] == ' ') i++;
    if (i < (int)t.length() && t[i] == ':') {
      i++;
      while (i < (int)t.length() && t[i] == ' ') i++;
      if (i < (int)t.length() && t[i] == '"') {
        out = "";
        for (i++; i < (int)t.length() && out.length() < 64; i++) {
          char c = t[i];
          if (c == '\\' && i + 1 < (int)t.length()) { out += t[++i]; continue; }
          if (c == '"') return true;
          out += c;
        }
        return false;
      }
    }
    p += k.length();
  }
  return false;
}

// Findet "sta" : { ... "ssid": "...", "pass": "..." }
static bool scanText(const String& t, String& ssid, String& pass, int* where = nullptr) {
  bool ok = false;
  int p = 0;
  while ((p = t.indexOf("\"sta\"", p)) >= 0) {
    int i = p + 5;
    while (i < (int)t.length() && t[i] == ' ') i++;
    if (i < (int)t.length() && t[i] == ':') {
      i++;
      while (i < (int)t.length() && t[i] == ' ') i++;
      if (i < (int)t.length() && t[i] == '{') {
        String s, w;
        if (jsonValue(t, i, "ssid", s) && s.length()) {
          jsonValue(t, i, "pass", w);
          ssid = s; pass = w; ok = true;          // letzte gueltige Fundstelle gewinnt
          if (where) *where = p;
        }
      }
    }
    p += 5;
  }
  return ok;
}

uint32_t lastFoundRegion = 0;

inline bool readWifi(String& ssid, String& pass) {
  const uint32_t regions[] = {0x1BB000, 0xBB000};
  uint8_t* sec = (uint8_t*)malloc(4096);
  if (!sec) return false;
  bool found = false;
  for (uint32_t base : regions) {
    String tail;
    for (uint32_t off = 0; off < 0x40000 && !found; off += 4096) {
      if (!ESP.flashRead(base + off, (uint32_t*)sec, 4096)) break;
      String txt = tail;
      for (int pg = 0; pg < 16; pg++) {
        const uint8_t* h = sec + pg * 256;
        uint16_t obj = h[0] | (h[1] << 8);
        uint8_t fl = h[4];
        if (obj == 0xFFFF || obj == 0) continue;     // frei / Lookup
        if (!(fl & 0x80)) continue;                  // geloescht
        if (!(fl & 0x04)) continue;                  // Index-Seite
        for (int i = 5; i < 256; i++) txt += (char)(h[i] == 0xFF || h[i] == 0 ? ' ' : h[i]);
      }
      String s, p;
      if (scanText(txt, s, p)) { ssid = s; pass = p; found = true; lastFoundRegion = base + off; }
      tail = txt.length() > 600 ? txt.substring(txt.length() - 600) : txt;
    }
    if (found) break;
  }
  free(sec);
  return found;
}

}  // namespace mgosimport
