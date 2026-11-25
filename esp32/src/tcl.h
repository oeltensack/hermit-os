// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 D.Koch – Pagurus OS
// TCL-/Kesser-Klimaanlage ueber den UART des WLAN-Moduls (9600 8E1).
// Eigene Umsetzung nach der Protokollbeschreibung in github.com/adaasch/AC-hack (Readme):
//   Rahmen: BB <von AC> <vom Modul> <Befehl> <Laenge> <Nutzdaten …> <XOR aller vorherigen Bytes>
//   0x04 = Zustand abfragen, 0x03 = Werte setzen (Antwort jeweils 55 Byte Zustand), 0x06 = Quittung
#pragma once
#include <Arduino.h>

namespace tcl {

// Modus/Luefter als Text – so stehen sie in Status und RPC
static const char* const MODES[] = {"cool", "fan", "dry", "heat", "auto"};
static const char* const FANS[]  = {"auto", "low", "mid", "high", "low_mid", "mid_high"};

struct State {
  bool valid = false;        // mindestens ein Zustand von der Anlage empfangen
  bool power = false;
  uint8_t mode = 0;          // Index in MODES
  float target = 24;         // Soll °C
  float room = NAN;          // Raumtemperatur am Innengeraet
  float outlet = NAN;        // Ausblastemperatur
  uint8_t fan = 0;           // Index in FANS
  bool swingV = false, swingH = false;
  bool eco = false, turbo = false, mute = false, display = true;
  bool compressor = false, indoorFan = false;
};

// Status-Rahmen: Modus 1..5 (cool, fan, dry, heat, auto)
// Setz-Rahmen:   Modus heat 1, dry 2, cool 3, fan 7, auto 8
static const uint8_t SET_MODE[] = {3, 7, 2, 1, 8};
// Luefter Status: 0 auto, 1 low, 2 mid, 3 high, 4 low_mid, 5 mid_high – auf dem Draht beim Setzen:
static const uint8_t SET_FAN[]  = {0, 2, 3, 5, 6, 7};

class Ac {
 public:
  State st;
  uint32_t lastRx = 0, frames = 0, badFrames = 0, rxBytes = 0, txFrames = 0;
  bool beep = true;

  void begin(HardwareSerial& s, int rx, int tx) {
    ser = &s;
    ser->begin(9600, SERIAL_8E1, rx, tx);
  }

  bool linkOk() const { return st.valid && millis() - lastRx < 15000; }

  // Gewuenschten Zustand setzen (immer kompletter Satz – die Anlage uebernimmt alles)
  void set(const State& want) {
    uint8_t p[29] = {0};
    p[2] = (want.power ? 0x04 : 0) | (beep ? 0x20 : 0) | 0x40 /* Anzeige an */ | (want.eco ? 0x80 : 0);
    p[3] = SET_MODE[want.mode < 5 ? want.mode : 0] | (want.turbo ? 0x40 : 0) | (want.mute ? 0x80 : 0);
    int t = (int)lroundf(want.target);
    if (t < 16) t = 16;
    if (t > 31) t = 31;
    p[4] = 0xF0 | ((15 - (t - 16)) & 0x0F);
    p[5] = SET_FAN[want.fan < 6 ? want.fan : 0] | (want.swingV ? (0x07 << 3) : 0);
    p[6] = want.swingH ? 0x08 : 0;
    send(0x03, p, sizeof p);
    pendingSet = true;
    nextPoll = millis() + 1500;              // Antwort auf das Setzen kommt von selbst
  }

  void loop() {
    while (ser && ser->available()) { rxBytes++; feed(ser->read()); }
    uint32_t now = millis();
    if ((int32_t)(now - nextPoll) >= 0) {
      static const uint8_t q[2] = {0x01, 0x00};
      send(0x04, q, 2);
      nextPoll = now + 3000;
      pendingSet = false;
    }
  }

  // Rahmen pruefen ohne Hardware (Tests)
  bool parseFrame(const uint8_t* f, int n) {
    if (n < 6 || f[0] != 0xBB) return false;
    uint8_t x = 0;
    for (int i = 0; i < n - 1; i++) x ^= f[i];
    if (x != f[n - 1] || f[4] != n - 6) { badFrames++; return false; }
    handle(f[3], f + 5, f[4], f[1] == 0x01);
    return true;
  }

 private:
  HardwareSerial* ser = nullptr;
  uint8_t buf[260];
  int pos = 0, need = 0;
  uint32_t nextPoll = 2000;
  bool pendingSet = false;

  void send(uint8_t cmd, const uint8_t* p, int len) {
    uint8_t f[40];
    f[0] = 0xBB; f[1] = 0x00; f[2] = 0x01; f[3] = cmd; f[4] = len;
    memcpy(f + 5, p, len);
    uint8_t x = 0;
    for (int i = 0; i < 5 + len; i++) x ^= f[i];
    f[5 + len] = x;
    if (ser) { ser->write(f, 6 + len); txFrames++; }
  }

  // Byteweise einlesen; das Modul schreibt teils Text dazwischen – neu aufsetzen am naechsten 0xBB
  void feed(int c) {
    if (c < 0) return;
    if (pos == 0) { if (c != 0xBB) return; buf[pos++] = c; need = 0; return; }
    buf[pos++] = c;
    if (pos == 5) {
      if (c > 0xF9) { pos = 0; return; }
      need = 5 + c + 1;
    }
    if (need && pos >= need) {
      if (!parseFrame(buf, pos)) {
        // Fehler: ab dem naechsten 0xBB im Puffer weitersuchen
        int k = 1;
        while (k < pos && buf[k] != 0xBB) k++;
        int rest = pos - k;
        memmove(buf, buf + k, rest);
        pos = 0;
        for (int i = 0; i < rest; i++) feed(buf[i]);
        return;
      }
      pos = 0; need = 0;
    }
    if (pos >= (int)sizeof buf) pos = 0;
  }

  void handle(uint8_t cmd, const uint8_t* p, int len, bool fromAc) {
    if (!fromAc || len < 0x23 || p[0] != 0x04 || (cmd != 0x03 && cmd != 0x04)) return;
    frames++;
    lastRx = millis();
    State& s = st;
    uint8_t m = p[2] & 0x0F;
    if (m >= 1 && m <= 5) s.mode = m - 1;
    s.power = p[2] & 0x10;
    s.display = p[2] & 0x20;
    s.eco = p[2] & 0x40;
    s.turbo = p[2] & 0x80;
    s.target = 16 + (p[3] & 0x0F) + ((p[4] & 0x02) ? 0.5f : 0);
    uint8_t fn = (p[3] >> 4) & 0x07;
    s.fan = fn < 6 ? fn : 0;
    s.swingH = p[5] & 0x20;
    s.swingV = p[5] & 0x40;
    s.room = (p[0x0C] * 4 + ((p[0x0D] >> 2) & 3) - 200) / 10.0f;
    if (len > 0x19) s.outlet = (p[0x19] * 4 - 200) / 10.0f;
    if (len > 0x1D) s.indoorFan = p[0x1D] != 0;
    if (len > 0x21) s.mute = p[0x21] & 0x80;
    if (len > 0x22) s.compressor = p[0x22] != 0;
    s.valid = true;
    if (p[0x0A] & 0x80) {                     // Anlage will eine Quittung
      uint8_t a[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0x80};
      send(0x06, a, 9);
    }
  }
};

}  // namespace tcl
