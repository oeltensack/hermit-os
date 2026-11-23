// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 D.Koch – Pagurus OS
#pragma once
// WLAN-Zugangsdaten aus der Original-Shelly-Firmware (Mongoose OS) lesen, nachdem Pagurus OS ueber
// tools/pagurus_flash.py (mg2hermit) aufgespielt wurde. Die Konfiguration (conf9.json) liegt im
// SPIFFS-Dateisystem der Original-Firmware: fs1 ab 0x1BB000 (von mg2x dorthin kopiert) bzw. fs0 ab
// 0xBB000, je 256 KB. Pagurus OS beschreibt fs0 nie; fs1 wird erst beim ersten Formatieren des
// eigenen LittleFS ueberschrieben – deshalb vor LittleFS.begin() aufrufen.
// SPIFFS: Seiten zu 256 Byte, Kopf 5 Byte (obj_id 2, span 2, flags 1), Daten 251 Byte.
#include <Arduino.h>
#include <vector>

#ifndef HERMIT_IPINFO
#define HERMIT_IPINFO
struct ImportIp { String ip, gw, mask, dns; };
#endif
#ifndef HERMIT_WIFICAND
#define HERMIT_WIFICAND
struct WifiCand { String ssid, pass; ImportIp ip; String src; };
// Kandidat nur aufnehmen, wenn SSID+Passwort noch nicht in der Liste stehen
inline void addCand(std::vector<WifiCand>& v, const WifiCand& c) {
  if (!c.ssid.length()) return;
  for (auto& x : v) if (x.ssid == c.ssid && x.pass == c.pass) return;
  v.push_back(c);
}
#endif

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
static bool scanText(const String& t, String& ssid, String& pass, ImportIp* ipi = nullptr) {
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
        int end = t.indexOf('}', i);              // nur innerhalb des "sta"-Objekts suchen
        String obj = end > i ? t.substring(i, end + 1) : t.substring(i);
        if (jsonValue(obj, 0, "ssid", s) && s.length()) {
          jsonValue(obj, 0, "pass", w);
          ssid = s; pass = w; ok = true;          // letzte gueltige Fundstelle gewinnt
          if (ipi) {                               // Mongoose OS: wifi.sta.ip/netmask/gw/nameserver
            *ipi = ImportIp();
            jsonValue(obj, 0, "ip", ipi->ip); jsonValue(obj, 0, "netmask", ipi->mask);
            jsonValue(obj, 0, "gw", ipi->gw); jsonValue(obj, 0, "nameserver", ipi->dns);
          }
        }
      }
    }
    p += 5;
  }
  return ok;
}

uint32_t lastFoundRegion = 0;

// Alle "sta"-Objekte mit SSID in einem Text sammeln
static void scanAll(const String& t, std::vector<WifiCand>& out, const String& src) {
  int p = 0;
  while ((p = t.indexOf("\"sta\"", p)) >= 0) {
    int i = p + 5;
    while (i < (int)t.length() && t[i] == ' ') i++;
    if (i < (int)t.length() && t[i] == ':') {
      i++;
      while (i < (int)t.length() && t[i] == ' ') i++;
      if (i < (int)t.length() && t[i] == '{') {
        int end = t.indexOf('}', i);
        String obj = end > i ? t.substring(i, end + 1) : t.substring(i);
        WifiCand c;
        if (jsonValue(obj, 0, "ssid", c.ssid) && c.ssid.length()) {
          jsonValue(obj, 0, "pass", c.pass);
          jsonValue(obj, 0, "ip", c.ip.ip); jsonValue(obj, 0, "netmask", c.ip.mask);
          jsonValue(obj, 0, "gw", c.ip.gw); jsonValue(obj, 0, "nameserver", c.ip.dns);
          c.src = src;
          addCand(out, c);
        }
      }
    }
    p += 5;
  }
}

// Saemtliche WLAN-Eintraege aus beiden Dateisystem-Bereichen der Original-Firmware. Mongoose OS hat zwei
// Bereiche, die sich bei Updates abwechseln; ein Werksreset leert nur den aktiven – im anderen kann ein
// alter Stand liegen. Deshalb alle Eintraege sammeln und spaeter der Reihe nach ausprobieren.
inline int readAll(std::vector<WifiCand>& out) {
  const uint32_t regions[] = {0xBB000, 0x1BB000};
  uint8_t* sec = (uint8_t*)malloc(4096);
  if (!sec) return 0;
  size_t before = out.size();
  for (uint32_t base : regions) {
    String tail;
    for (uint32_t off = 0; off < 0x40000; off += 4096) {
      if (!ESP.flashRead(base + off, (uint32_t*)sec, 4096)) break;
      String txt = tail;
      for (int pg = 0; pg < 16; pg++) {
        const uint8_t* h = sec + pg * 256;
        uint16_t obj = h[0] | (h[1] << 8);
        uint8_t fl = h[4];
        if (obj == 0xFFFF || obj == 0) continue;
        if (!(fl & 0x80)) continue;
        if (!(fl & 0x04)) continue;
        for (int i = 5; i < 256; i++) txt += (char)(h[i] == 0xFF || h[i] == 0 ? ' ' : h[i]);
      }
      scanAll(txt, out, base == 0xBB000 ? "mgos-fs0" : "mgos-fs1");
      tail = txt.length() > 600 ? txt.substring(txt.length() - 600) : txt;
      yield();
    }
  }
  free(sec);
  return out.size() - before;
}

inline bool readWifi(String& ssid, String& pass, ImportIp* ipi = nullptr) {
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
      if (scanText(txt, s, p, ipi)) { ssid = s; pass = p; found = true; lastFoundRegion = base + off; }
      tail = txt.length() > 600 ? txt.substring(txt.length() - 600) : txt;
    }
    if (found) break;
  }
  free(sec);
  return found;
}

}  // namespace mgosimport
