// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 D.Koch – Pagurus OS
// Anbindung TCL-/Kesser-Klimaanlage an den gemeinsamen Kern (main.cpp): Status, Setzen, Einstellungen.
// Wird von main.cpp nach Cfg, mqDirty und den Hilfsfunktionen eingebunden.
#pragma once
#include "tcl.h"

tcl::Ac ac;

bool acValid() { return ac.st.valid; }
bool acLinkOk() { return ac.linkOk(); }
bool acPower() { return ac.st.power; }

void acOpen() { ac.begin(Serial2, cfg.swap ? PIN_AC_TX : PIN_AC_RX, cfg.swap ? PIN_AC_RX : PIN_AC_TX); }
void acBegin() { ac.beep = cfg.beep; acOpen(); }

void acLoop() {
  ac.loop();
  static bool lastLink = false, lastPower = false; static uint8_t lastMode = 255; static float lastTarget = 0;
  if (ac.linkOk() != lastLink || ac.st.power != lastPower || ac.st.mode != lastMode || ac.st.target != lastTarget) {
    lastLink = ac.linkOk(); lastPower = ac.st.power; lastMode = ac.st.mode; lastTarget = ac.st.target; mqDirty = true;
  }
}

void acStatus(JsonObject o) {
  const tcl::State& s = ac.st;
  o["output"] = s.power;
  o["link"] = ac.linkOk();
  JsonObject dg = o["diag"].to<JsonObject>();           // Fehlersuche UART
  dg["rx_bytes"] = ac.rxBytes; dg["tx_frames"] = ac.txFrames; dg["frames"] = ac.frames; dg["bad"] = ac.badFrames;
  dg["ignored"] = ac.ignored;
  dg["tx_pin"] = cfg.swap ? PIN_AC_RX : PIN_AC_TX; dg["rx_pin"] = cfg.swap ? PIN_AC_TX : PIN_AC_RX;
  if (!s.valid) return;
  o["mode"] = tcl::MODES[s.mode];
  o["target_C"] = s.target;
  if (!isnan(s.room)) o["current_C"] = s.room;
  if (!isnan(s.outlet)) o["outlet_C"] = s.outlet;
  o["fan"] = tcl::FANS[s.fan];
  o["swing_v"] = s.swingV; o["swing_h"] = s.swingH;
  o["eco"] = s.eco; o["turbo"] = s.turbo; o["mute"] = s.mute;
  o["compressor"] = s.compressor;
}

String acSet(JsonVariantConst p) {
  if (!ac.st.valid) return "Keine Verbindung zur Klimaanlage (UART)";
  tcl::State w = ac.st;
  if (p["on"].is<bool>()) w.power = p["on"];
  if (p["mode"].is<const char*>()) {
    int i = idxOf(tcl::MODES, 5, p["mode"].as<const char*>());
    if (i < 0) return "Modus: cool, fan, dry, heat, auto";
    w.mode = i;
  }
  if (p["target_C"].is<float>()) {
    float t = p["target_C"];
    if (t < 16 || t > 31) return "Solltemperatur 16–31 °C";
    w.target = t;
  }
  if (p["fan"].is<const char*>()) {
    int i = idxOf(tcl::FANS, 6, p["fan"].as<const char*>());
    if (i < 0) return "Lüfter: auto, low, low_mid, mid, mid_high, high";
    w.fan = i;
  }
  if (p["swing_v"].is<bool>()) w.swingV = p["swing_v"];
  if (p["swing_h"].is<bool>()) w.swingH = p["swing_h"];
  if (p["eco"].is<bool>()) w.eco = p["eco"];
  if (p["turbo"].is<bool>()) w.turbo = p["turbo"];
  if (p["mute"].is<bool>()) w.mute = p["mute"];
  ac.beep = cfg.beep;
  ac.set(w);
  ac.st = w;                                 // sofort anzeigen; die Antwort der Anlage bestaetigt
  mqDirty = true;
  return "";
}

void acConfig(JsonObject o) { o["beep"] = cfg.beep; o["swap_pins"] = cfg.swap; }

void acSetConfig(JsonVariantConst c) {
  if (c["beep"].is<bool>()) { cfg.beep = c["beep"]; saveCfg(); }
  if (c["swap_pins"].is<bool>() && (bool)c["swap_pins"] != cfg.swap) {
    cfg.swap = c["swap_pins"]; saveCfg();
    Serial2.end();
    acOpen();
  }
}

// Weitere Climate-Methoden dieser Anlage (keine)
#define AC_METHODS
bool acRpc(const String&, JsonVariantConst, JsonObject, String&) { return false; }
