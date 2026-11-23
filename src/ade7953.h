// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 D.Koch – Pagurus OS
#pragma once
// ADE7953 (Shelly 2.5) ueber I2C, Adresse 0x38. Umrechnung und Startwerte wie Tasmota (xnrg_07_ade7953):
//   Leistung  W = AENERGY (Lesen setzt zurueck) / 6,577 / Sekunden   (1540 / 10 / 23,41494)
//   Spannung  V = VRMS / 26000      Strom  A = IRMS / 100000      Frequenz Hz = 223750 / (PERIOD + 1)
// Beim Shelly 2.5 sind die Kanaele vertauscht: Relais 1 = Kanal B, Relais 2 = Kanal A.

#include <Wire.h>

namespace ade {

const uint8_t ADDR = 0x38;
bool ok = false;               // Chip antwortet
uint32_t lastMs = 0, errors = 0;

int regSize(uint16_t reg) {
  switch ((reg >> 8) & 0x0F) {
    case 0x03: return 4;
    case 0x02: return 3;
    case 0x01: return 2;
    default: return 1;
  }
}

bool write(uint16_t reg, uint32_t val) {
  int n = regSize(reg);
  Wire.beginTransmission(ADDR);
  Wire.write((reg >> 8) & 0xFF);
  Wire.write(reg & 0xFF);
  while (n--) Wire.write((val >> (8 * n)) & 0xFF);
  bool r = Wire.endTransmission() == 0;
  delayMicroseconds(5);
  return r;
}

bool read(uint16_t reg, int32_t& out) {
  int n = regSize(reg);
  Wire.beginTransmission(ADDR);
  Wire.write((reg >> 8) & 0xFF);
  Wire.write(reg & 0xFF);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(ADDR, (uint8_t)n) != n) return false;
  uint32_t v = 0;
  for (int i = 0; i < n; i++) v = (v << 8) | Wire.read();
  out = (int32_t)v;
  return true;
}

bool init(int sda, int scl) {
  Wire.begin(sda, scl);
  Wire.setClock(100000);
  ok = write(0x102, 0x0004);                 // CONFIG: Schnittstelle sperren (I2C), Hochpass an
  write(0x0FE, 0x00AD);                      // Register 0x120 freischalten
  write(0x120, 0x0030);                      // empfohlene Einstellung laut Datenblatt
  write(0x001, 0x07);                        // DISNOLOAD: Leerlauferkennung aus, damit Schwellen gesetzt werden koennen
  write(0x303, 29196);                       // AP_NOLOAD (halber Standard, misst ab ca. 5 W)
  write(0x304, 29196);                       // VAR_NOLOAD
  write(0x001, 0x00);                        // Leerlauferkennung wieder an
  const uint16_t gains[] = {0x380, 0x381, 0x382, 0x383, 0x384, 0x38C, 0x38E, 0x38F, 0x390};
  for (uint16_t g : gains) write(g, 0x400000);   // Verstaerkungen auf Standard
  write(0x108, 0); write(0x109, 0);          // Phasenkorrektur aus
  int32_t dummy;
  read(0x31E, dummy); read(0x31F, dummy);    // Energiezaehler leeren
  read(0x322, dummy); read(0x323, dummy);
  lastMs = millis();
  return ok;
}

struct Val { float w = 0, va = 0, v = NAN, a = NAN, hz = NAN; };

// Werte fuer Relais-Kanal 0 und 1 lesen (seit letztem Aufruf gemittelt)
bool sample(Val out[2]) {
  uint32_t now = millis();
  float dt = (now - lastMs) / 1000.0f;
  lastMs = now;
  if (dt <= 0.05f) dt = 1;
  int32_t irms[2], aen[2], apen[2], vrms, per;
  bool r = read(0x31B, irms[0]) && read(0x31A, irms[1])       // Relais 1 = B, Relais 2 = A
        && read(0x31F, aen[0]) && read(0x31E, aen[1])
        && read(0x323, apen[0]) && read(0x322, apen[1])
        && read(0x31C, vrms) && read(0x10E, per);
  if (!r) { errors++; ok = false; return false; }
  ok = true;
  const float PDIV = 1540.0f / 10.0f / 23.41494f;
  for (int c = 0; c < 2; c++) {
    out[c].w = fabsf((float)aen[c]) / PDIV / dt;
    out[c].va = fabsf((float)apen[c]) / PDIV / dt;
    out[c].v = vrms / 26000.0f;
    out[c].a = out[c].w > 0 ? irms[c] / 100000.0f : 0;
    out[c].hz = per > 0 ? 223750.0f / (per + 1) : NAN;
  }
  return true;
}

}  // namespace ade
