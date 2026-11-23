// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 D.Koch – Hermit OS
#pragma once
// Uebergabe Hermit OS -> Hermit Loader im RTC-Speicher (ueberlebt den Neustart, nicht den Stromausfall).
// Fuer Geraete mit 1 MB Flash: Hermit OS passt dort nicht zweimal hinein. Hermit installiert deshalb erst
// den kleinen Loader und hinterlegt hier, welche Datei der Loader danach laden soll.
// RTC-Bloecke 0..31 belegt eboot (OTA-Befehl), 96..97 der Absturzzaehler.
#include <Arduino.h>

struct UpdHandoff {
  uint32_t magic;
  uint32_t tries;
  uint8_t sha[32];          // alles 0 = nicht pruefen
  char url[180];
};
#define UPD_RTC_BLOCK 40
#define UPD_MAGIC 0x44505548UL   // "HUPD"

inline bool updHexToSha(const String& hex, uint8_t* out) {
  memset(out, 0, 32);
  if (hex.length() != 64) return false;
  for (int i = 0; i < 32; i++) out[i] = (uint8_t)strtol(hex.substring(i * 2, i * 2 + 2).c_str(), nullptr, 16);
  return true;
}
inline bool updHandoffRead(UpdHandoff& h) {
  ESP.rtcUserMemoryRead(UPD_RTC_BLOCK, (uint32_t*)&h, sizeof h);
  h.url[sizeof h.url - 1] = 0;
  return h.magic == UPD_MAGIC && h.url[0];
}
inline void updHandoffWrite(const UpdHandoff& h) { ESP.rtcUserMemoryWrite(UPD_RTC_BLOCK, (uint32_t*)&h, sizeof h); }
inline void updHandoffClear() { UpdHandoff h; memset(&h, 0, sizeof h); updHandoffWrite(h); }
