// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 D.Koch – Hermit OS
#pragma once
// Treiber fuer den STM32-Koprozessor des Shelly Dimmer 2 (UART 115200 8N1).
//
// Rahmen: 0x01 | Zaehler | Befehl | Laenge | Nutzdaten | Pruefsumme (16 bit, big endian) | 0x04
// Pruefsumme = Summe der Bytes von Zaehler bis Ende der Nutzdaten.
//
// Zwei Befehlssaetze sind im Umlauf:
//  STOCK – STM-Firmware der Original-Shelly-Firmware (v2.x, z. B. 2.48 / 2.53 / 2.63)
//          0x01 Version, 0x02 Helligkeit, 0x10 Zustand, 0x20 Einstellungen (12 Byte), 0x30/0x31 Kalibrierung
//  OPEN  – quelloffene STM-Firmware (v51.x, von Tasmota/ESPHome aufgespielt)
//          0x01 Helligkeit, 0x10 Zustand, 0x11 Version, 0x20 Einstellungen (10 Byte)
// Erkennung: erst 0x11 senden (bei STOCK unbekannt und folgenlos), sonst 0x01 ohne Nutzdaten (STOCK: Version).
// Hermit spielt keine STM-Firmware auf – der Weg zurueck zur Original-Firmware bleibt offen.

#include <stdint.h>
#include <string.h>
#include <math.h>

template <class S>
class StmDimmer {
 public:
  enum Proto : uint8_t { NONE = 0, STOCK = 1, OPEN = 2 };

  S* io = nullptr;
  uint32_t (*clock)() = nullptr;        // Millisekunden

  Proto proto = NONE;
  uint8_t verMaj = 0, verMin = 0, hwVer = 0;
  bool online = false;                  // Antworten kommen
  uint32_t lastRxMs = 0, frames = 0, badFrames = 0;
  uint16_t bri = 0;                     // gemeldete Helligkeit 0..1000
  bool briValid = false;
  float pW = 0, vV = NAN, iA = NAN;     // Messwerte (ohne Korrekturfaktor)
  uint8_t pollRaw[24]; uint8_t pollLen = 0;
  bool pwrFmt20 = false;                // STOCK: Leistung als W*20 (ab STM 2.63) statt Periodendauer

  // Einstellungen (werden nach Erkennung bzw. Aenderung gesendet)
  bool leading = false;                 // false = Phasenabschnitt (LED, Standard), true = Phasenanschnitt
  uint8_t fadeRate = 3;                 // STM-interne Glaettung 1..5 (nur STOCK)
  uint16_t zcDebounce = 100;            // Nulldurchgangs-Entprellung 50..150 (nur STOCK)

  void begin(S* s, uint32_t (*ms)()) { io = s; clock = ms; }

  // ---- Senden ----
  uint8_t send(uint8_t cmd, const uint8_t* pl, uint8_t len) {
    uint8_t h[4] = {0x01, cnt, cmd, len};
    uint16_t c = cnt + cmd + len;
    for (uint8_t i = 0; i < len; i++) c += pl[i];
    uint8_t t[3] = {(uint8_t)(c >> 8), (uint8_t)c, 0x04};
    io->write(h, 4);
    if (len) io->write(pl, len);
    io->write(t, 3);
    return cnt++;
  }

  void setBrightness(uint16_t v) {      // 0..1000
    if (v > 1000) v = 1000;
    target = v; haveTarget = true;
    if (proto == NONE) return;          // wird nach der Erkennung gesendet
    uint8_t p[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
    send(proto == STOCK ? 0x02 : 0x01, p, 2);
    sentTarget = v;
  }

  void sendSettings(void (*pause)(uint32_t)) {
    uint16_t b = haveTarget ? target : bri;
    if (proto == STOCK) {
      uint8_t fr[6] = {5, 5, 10, 15, 20, 25};
      uint16_t f = fr[fadeRate > 5 ? 5 : fadeRate];
      uint16_t v[6] = {b, (uint16_t)(leading ? 1 : 2), f, zcDebounce, 0, 0};
      uint8_t p[12];
      for (int i = 0; i < 6; i++) { p[i * 2] = (uint8_t)v[i]; p[i * 2 + 1] = (uint8_t)(v[i] >> 8); }
      send(0x20, p, 12); pause(15);
      // Kalibriertabelle (leer = linear); laut Analyse noetig, damit ein Wechsel der Dimmart greift
      static uint8_t zero[200];
      send(0x30, zero, 200); pause(25);
      send(0x31, zero, 200); pause(25);
      send(0x20, p, 12); pause(15);
    } else if (proto == OPEN) {
      uint16_t v[5] = {b, (uint16_t)(leading ? 1 : 2), 0, 0, 0};
      uint8_t p[10];
      for (int i = 0; i < 5; i++) { p[i * 2] = (uint8_t)v[i]; p[i * 2 + 1] = (uint8_t)(v[i] >> 8); }
      send(0x20, p, 10); pause(15);
    }
  }

  void poll() { if (proto != NONE) send(0x10, nullptr, 0); }

  // ---- Empfangen: alle anstehenden Bytes verarbeiten; true, wenn ein gueltiger Rahmen kam ----
  bool receive() {
    bool got = false;
    while (io->available() > 0) {
      int c = io->read();
      if (c < 0) break;
      uint8_t b = (uint8_t)c;
      if (rxi == 0 && b != 0x01) continue;
      rx[rxi++] = b;
      if (rxi >= 4) {
        uint16_t need = 4 + rx[3] + 3;
        if (rxi == need) {
          rxi = 0;
          uint16_t cs = 0;
          for (uint16_t i = 1; i < 4 + rx[3]; i++) cs += rx[i];
          uint16_t got16 = ((uint16_t)rx[need - 3] << 8) | rx[need - 2];
          if (cs != got16 || rx[need - 1] != 0x04) { badFrames++; continue; }
          frames++; lastRxMs = clock(); online = true;
          handle(rx[2], rx + 4, rx[3]);
          got = true;
        }
      }
      if (rxi >= sizeof(rx)) rxi = 0;
    }
    return got;
  }

  // ---- Erkennung (nicht blockierend): regelmaessig aufrufen ----
  // resetFn: STM32 neu starten (nur wenn erlaubt), pause: Wartefunktion
  void tick(bool mayReset, void (*resetFn)(), void (*pause)(uint32_t)) {
    uint32_t now = clock();
    receive();
    if (proto == NONE) {
      if (now - detMs < 250) return;
      detMs = now;
      switch (detStep % 3) {
        case 0: send(0x11, nullptr, 0); break;          // OPEN-Version
        case 1: send(0x01, nullptr, 0); break;          // STOCK-Version
        case 2: if (mayReset && detStep >= 5 && resetFn && now - resetMs > 5000) { resetMs = now; skipInit = false; resetFn(); } break;
      }
      detStep++;
      return;
    }
    if (justDetected) {
      justDetected = false;
      if (!skipInit) sendSettings(pause);
      skipInit = false;
      if (haveTarget) setBrightness(target);
    }
    if (online && now - lastRxMs > 6000) {             // keine Antwort mehr: neu erkennen
      online = false; proto = NONE; detStep = 0; briValid = false;
    }
    if (now - pollMs >= 1000) { pollMs = now; poll(); }
  }

  // Nach Neustart ohne Stromausfall: STM32 laeuft weiter, Einstellungen nicht erneut senden
  void keepState() { skipInit = true; }

  const char* protoName() const { return proto == STOCK ? "stock" : proto == OPEN ? "open" : "none"; }

  // Teststuetze: Rahmen von Hand einspeisen
  void inject(uint8_t cmd, const uint8_t* pl, uint8_t len) { handle(cmd, pl, len); }

 private:
  uint8_t cnt = 0;
  uint8_t rx[256];
  uint16_t rxi = 0;
  uint32_t detMs = 0, pollMs = 0, resetMs = 0;
  uint8_t detStep = 0;
  bool justDetected = false, skipInit = false;
  uint16_t target = 0, sentTarget = 0xFFFF;
  bool haveTarget = false;

  static uint32_t u32(const uint8_t* p) { return p[0] | (p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

  void handle(uint8_t cmd, const uint8_t* p, uint8_t n) {
    if (proto == NONE) {
      // OPEN: Version v5x.y; STOCK: v2.x
      if (cmd == 0x11 && n >= 2 && p[1] >= 40) { proto = OPEN; verMin = p[0]; verMaj = p[1]; justDetected = true; return; }
      if (cmd == 0x01 && n >= 2) { proto = STOCK; verMin = p[0]; verMaj = p[1]; justDetected = true;
                                   pwrFmt20 = verMaj == 2 && verMin >= 0x3F; return; }
      return;
    }
    if (cmd == 0x10 && n >= 8) parseState(p, n);
  }

  void parseState(const uint8_t* p, uint8_t n) {
    pollLen = n > sizeof(pollRaw) ? sizeof(pollRaw) : n;
    memcpy(pollRaw, p, pollLen);
    bri = p[2] | (p[3] << 8);
    if (bri > 1000) bri = 1000;
    briValid = true;
    if (proto == OPEN) {
      hwVer = p[0];
      uint32_t r = u32(p + 4);
      pW = r ? 880373.0f / r : 0;
      if (n >= 16 && hwVer == 1) {
        uint32_t rv = u32(p + 8), ri = u32(p + 12);
        vV = rv ? 347800.0f / rv : NAN;
        iA = ri ? 1448.0f / ri : 0;
      }
    } else {
      uint16_t w20 = p[6] | (p[7] << 8);
      // ab STM 2.63: Leistung * 20 in Byte 6/7 (Byte 4/5 = 0); aeltere: Periodendauer in Byte 4..7
      bool f20 = pwrFmt20 || (p[4] == 0 && p[5] == 0 && w20 != 0);
      if (f20) pW = w20 / 20.0f;
      else { uint32_t r = u32(p + 4); pW = r ? 1925000.0f / r / 1.9845f : 0; }
    }
    if (pW < 0 || pW > 5000) pW = 0;
  }
};
