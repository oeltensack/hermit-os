// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 D.Koch – Hermit OS
#pragma once
// WLAN-Zugangsdaten aus den zurueckgelassenen Tasmota-Einstellungen lesen (ESP8266, 2 MB Flash).
// Tasmota speichert TSettings (4 KB, CRC32 am Ende) im "Core EEPROM"-Sektor (0x1FB, vor OTA immer
// aktuell) und rotierend in 0xF7..0xFD. Hermit OS und der Loader beschreiben diese Sektoren nicht.
// Text-Pool ab Offset 0x017 (699 Byte), nullterminierte Eintraege:
//   0 OTAURL, 1-3 MQTTPREFIX, 4 STASSID1, 5 STASSID2, 6 STAPWD1, 7 STAPWD2
#include <Arduino.h>

// Feste IP-Einstellungen (leer = DHCP), gemeinsam fuer Tasmota- und Original-Import
#ifndef HERMIT_IPINFO
#define HERMIT_IPINFO
struct ImportIp { String ip, gw, mask, dns; };
#endif

namespace tasimport {

static String ipStr(const uint8_t* b) {
  char t[16]; snprintf(t, sizeof t, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]); return String(t);
}

static uint32_t crc32(const uint8_t* b, uint32_t n) {
  uint32_t c = 0;
  while (n--) { c ^= *b++; for (int j = 0; j < 8; j++) c = (c >> 1) ^ (-int(c & 1) & 0xEDB88320); }
  return ~c;
}

static String poolEntry(const uint8_t* pool, uint32_t len, int idx) {
  uint32_t p = 0;
  for (int i = 0; i < idx && p < len; i++) { while (p < len && pool[p]) p++; p++; }
  String s;
  while (p < len && pool[p] && s.length() < 64) s += (char)pool[p++];
  return s;
}

// true, wenn gueltige Tasmota-Einstellungen mit SSID gefunden wurden
inline bool readWifi(String& ssid, String& pass, ImportIp* ip = nullptr) {
  // 2-MB-Layout: EEPROM 0x1FB, Rotation 0xF7..0xFD; 1-MB-Layout: EEPROM 0xFB, Rotation 0xF4..0xFA
  const uint32_t sectors[] = {0x1FB, 0xFD, 0xFC, 0xFB, 0xFA, 0xF9, 0xF8, 0xF7, 0xF6, 0xF5, 0xF4};
  uint32_t* buf = (uint32_t*)malloc(4096);
  if (!buf) return false;
  uint32_t best = 0;
  bool found = false;
  for (uint32_t sec : sectors) {
    if (!ESP.flashRead(sec * 4096, buf, 4096)) continue;
    const uint8_t* b = (const uint8_t*)buf;
    uint32_t stored = buf[1023];
    if (stored == 0xFFFFFFFF || stored == 0) continue;
    if (crc32(b, 4092) != stored) continue;
    uint32_t saveFlag = buf[1];                      // Offset 0x004
    if (found && saveFlag <= best) continue;
    const uint8_t* pool = b + 0x17;
    String s1 = poolEntry(pool, 699, 4), p1 = poolEntry(pool, 699, 6);
    String s2 = poolEntry(pool, 699, 5), p2 = poolEntry(pool, 699, 7);
    if (s1.length()) { ssid = s1; pass = p1; }
    else if (s2.length()) { ssid = s2; pass = p2; }
    else continue;
    best = saveFlag; found = true;
    if (ip) {                                        // ipv4_address[5] @ 0x544: IP, Gateway, Maske, DNS1, DNS2
      const uint8_t* a = b + 0x544;
      bool any = a[0] | a[1] | a[2] | a[3];
      *ip = ImportIp();
      if (any) { ip->ip = ipStr(a); ip->gw = ipStr(a + 4); ip->mask = ipStr(a + 8); ip->dns = ipStr(a + 12); }
    }
  }
  free(buf);
  return found;
}

}  // namespace tasimport
