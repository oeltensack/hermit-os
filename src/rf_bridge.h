// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 D.Koch – Hermit OS
#pragma once
// Sonoff RF Bridge: Protokoll zum EFM8BB1 (UART 19200 8N1), Original- und Portisch-Firmware.
//
// Rahmen: 0xAA | Befehl | Daten | 0x55
//  A0          Quittung
//  A4 (9 Byte) empfangener Code, Standard 24 Bit: Sync-Low, Bit-0-High, Bit-1-High (je 2 Byte, us, big endian), 3 Byte Daten
//  A5 (9 Byte) senden, gleicher Aufbau
//  A6 / A7     Portisch: Erkennung aller bekannten Protokolle an / aus
//  A6 Antwort  Laenge | Protokoll-Index | Daten
//  A8          Portisch: senden mit Protokoll-Index (Laenge | Index | Daten)
//  B1          Portisch: Rohdaten-Erkennung an; Antwort: Anzahl Zeitstufen (inkl. Sync) | Zeitstufen je 2 Byte | Daten (Nibbles)
//  B0          Portisch: Rohdaten senden: Laenge | Anzahl Zeitstufen | Wiederholungen | Zeitstufen | Daten
//  C0 (2 Byte) Summer, Dauer in ms
// B1-Daten koennen 0x55 enthalten: ein Rahmen endet erst mit 0x55 und anschliessender Pause.

#include <stdint.h>
#include <string.h>

enum RfKind : uint8_t { RF_STD = 1, RF_PROTO = 2, RF_RAW = 3 };

struct RfCode {
  RfKind kind = RF_STD;
  uint8_t len = 0;
  uint8_t d[120];          // STD: 9 Byte; PROTO: Index + Daten; RAW: B1-Nutzdaten
};

template <class S>
class RfEfm {
 public:
  S* io = nullptr;
  uint32_t (*clock)() = nullptr;
  void (*onCode)(const RfCode&) = nullptr;
  uint32_t frames = 0, acks = 0, bad = 0, lastRxMs = 0;

  void begin(S* s, uint32_t (*ms)(), void (*cb)(const RfCode&)) { io = s; clock = ms; onCode = cb; }

  // Empfangsmodus: 0 = Standard (A4), 1 = alle Protokolle (A6), 2 = Rohdaten (B1)
  void setMode(uint8_t m) {
    if (m == 1) cmd(0xA6);
    else if (m == 2) cmd(0xB1);
    else cmd(0xA7);                    // Portisch: zurueck auf Standard; Original ignoriert es
  }
  void beep(uint16_t ms) { uint8_t p[2] = {(uint8_t)(ms >> 8), (uint8_t)ms}; frame(0xC0, p, 2, false); }

  bool send(const RfCode& c, uint8_t repeats = 8) {
    if (c.kind == RF_STD && c.len == 9) { frame(0xA5, c.d, 9, false); return true; }
    if (c.kind == RF_PROTO && c.len >= 2) { frame(0xA8, c.d, c.len, true); return true; }
    if (c.kind == RF_RAW && c.len >= 3) {
      // B1 -> B0: Wiederholungen hinter der Anzahl der Zeitstufen einfuegen
      uint8_t n = c.d[0];
      if (c.len < 1 + 2 * n) return false;
      uint8_t buf[124];
      if (c.len + 1 > (int)sizeof(buf)) return false;
      buf[0] = n; buf[1] = repeats;
      memcpy(buf + 2, c.d + 1, c.len - 1);
      frame(0xB0, buf, c.len + 1, true);
      return true;
    }
    return false;
  }

  void poll() {
    uint32_t now = clock();
    // B1: 0x55 gefolgt von Pause = Ende (nur wenn nichts mehr im Puffer liegt)
    if (st == ST_RAWDATA && pend55 && io->available() <= 0 && now - lastByteMs >= 6) { code.len = pos; deliver(); }
    while (io->available() > 0) {
      int c = io->read();
      if (c < 0) break;
      lastByteMs = now;
      feed((uint8_t)c);
    }
    if (st != ST_IDLE && !(st == ST_RAWDATA && pend55) && now - lastByteMs > 200) { bad++; st = ST_IDLE; pend55 = false; }
  }

  // Teststuetze
  void feed(uint8_t b) {
    switch (st) {
      case ST_IDLE:
        if (b == 0xAA) st = ST_CMD;
        break;
      case ST_CMD:
        cmdb = b; pos = 0;
        if (b == 0xA0 || b == 0xA2) { st = ST_END; }
        else if (b == 0xA4 || b == 0xA3) { code.kind = RF_STD; need = 9; st = ST_DATA; }
        else if (b == 0xA6 || b == 0xAB) { code.kind = RF_PROTO; st = ST_LEN; }
        else if (b == 0xB1) { code.kind = RF_RAW; st = ST_RAWN; }
        else if (b == 0xAA) st = ST_CMD;
        else { bad++; st = ST_IDLE; }
        break;
      case ST_LEN:
        need = b; pos = 0;
        if (need == 0 || need > sizeof(code.d)) { bad++; st = ST_IDLE; } else st = ST_DATA;
        break;
      case ST_DATA:
        code.d[pos++] = b;
        if (pos >= need) st = ST_END;
        break;
      case ST_END:
        if (b == 0x55) {
          if (cmdb == 0xA0) acks++;
          else if (cmdb != 0xA2) { code.len = pos; deliver(); return; }
          frames++;
        } else bad++;
        st = ST_IDLE;
        break;
      case ST_RAWN:                                   // Anzahl Zeitstufen
        if (b == 0 || b > 8) { bad++; st = ST_IDLE; break; }
        code.d[0] = b; pos = 1; need = 1 + 2 * b; pend55 = false;
        st = ST_RAWDATA;
        break;
      case ST_RAWDATA:
        if (pend55 && b == 0xAA) {                     // naechster Rahmen beginnt: 0x55 war das Ende
          code.len = pos; deliver(); st = ST_CMD; break;
        }
        if (pos >= need && pend55) {                   // 0x55 war Datenbyte
          if (pos >= sizeof(code.d)) { bad++; st = ST_IDLE; break; }
          code.d[pos++] = 0x55; pend55 = false;
        }
        if (pos >= need && b == 0x55) { pend55 = true; break; }
        if (pos >= sizeof(code.d)) { bad++; st = ST_IDLE; break; }
        code.d[pos++] = b;
        break;
    }
  }

  uint32_t lastByteMs = 0;

 private:
  enum { ST_IDLE, ST_CMD, ST_LEN, ST_DATA, ST_END, ST_RAWN, ST_RAWDATA } st = ST_IDLE;
  uint8_t cmdb = 0, need = 0, pos = 0;
  bool pend55 = false;
  RfCode code;

  void deliver() {
    frames++; lastRxMs = clock(); st = ST_IDLE; pend55 = false;
    if (onCode) onCode(code);
  }
  void cmd(uint8_t c) { uint8_t f[3] = {0xAA, c, 0x55}; io->write(f, 3); }
  void frame(uint8_t c, const uint8_t* p, uint8_t n, bool withLen) {
    uint8_t h[3] = {0xAA, c, n};
    io->write(h, withLen ? 3 : 2);
    io->write(p, n);
    uint8_t e = 0x55; io->write(&e, 1);
  }
};
