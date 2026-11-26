// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 D.Koch – Pagurus OS
// Anbindung Midea-Klimaanlage an den gemeinsamen Kern (main.cpp). Midea-Protokoll sprechen u. a. auch
// Dimstal, Comfee, Qlima, Electrolux, Artel, Inventor, Carrier – UART des WLAN-Sticks, 9600 8N1.
// Das Protokoll selbst kommt aus der Bibliothek dudanov/MideaUART (MIT-Lizenz), dieselbe, die ESPHome
// fuer `climate: platform: midea` verwendet. Autoconf fragt die Faehigkeiten der Anlage ab (0xB5).
// Wird von main.cpp nach Cfg, mqDirty und den Hilfsfunktionen eingebunden.
#pragma once
#include <Appliance/AirConditioner/AirConditioner.h>

namespace mda = dudanov::midea::ac;

// Zugriff auf "Befehl laeuft gerade" und den gemerkten Modus (bei ausgeschalteter Anlage)
class MideaAc : public mda::AirConditioner {
 public:
  bool busy() const { return m_sendControl; }
  mda::Mode rawMode() const { return m_status.getRawMode(); }
};

// Zwischen Bibliothek und UART: zaehlt Bytes und gueltige Rahmen fuer die Diagnose
class AcStream : public Stream {
 public:
  HardwareSerial* s = nullptr;
  uint32_t rxBytes = 0, txFrames = 0, frames = 0, bad = 0, lastFrame = 0;
  int available() override { return s ? s->available() : 0; }
  int read() override {
    int c = s ? s->read() : -1;
    if (c >= 0) { rxBytes++; feed(c); }
    return c;
  }
  int peek() override { return s ? s->peek() : -1; }
  size_t write(uint8_t b) override { return s ? s->write(b) : 0; }
  size_t write(const uint8_t* b, size_t n) override { txFrames++; return s ? s->write(b, n) : 0; }
  void flush() override { if (s) s->flush(); }

 private:
  // Rahmen: AA <Laenge> … <Pruefsumme>, Laenge = Bytes ab dem Laengenbyte bis einschliesslich Pruefsumme;
  // die Summe genau dieser Bytes ist 0 (mod 256)
  uint8_t len = 0, sum = 0; int pos = 0;
  void feed(uint8_t c) {
    if (pos == 0) { if (c == 0xAA) pos = 1; return; }
    if (pos == 1) { if (c <= 10) { pos = 0; return; } len = c; sum = c; pos = 2; return; }
    sum += c;
    if (++pos > len) { if (sum == 0) { frames++; lastFrame = millis(); } else bad++; pos = 0; }
  }
};

MideaAc ac;
AcStream acs;
bool acSeen = false;               // mindestens ein Zustand von der Anlage gelesen
int8_t acPendMode = -1;            // bei ausgeschalteter Anlage gewaehlter Modus (Index in AC_MODES)

static const char* const AC_MODES[] = {"cool", "heat", "dry", "fan", "auto"};
static const mda::Mode AC_MODE_V[] = {mda::MODE_COOL, mda::MODE_HEAT, mda::MODE_DRY, mda::MODE_FAN_ONLY, mda::MODE_AUTO};
static const char* const AC_FANS[] = {"auto", "low", "mid", "high"};

int acModeIdx(mda::Mode m) { for (int i = 0; i < 5; i++) if (AC_MODE_V[i] == m) return i; return -1; }

// Luefter der Anlage (Prozent bzw. 101/102 = Auto) auf die vier Stufen abbilden; Leise = low, Turbo = high
const char* acFanName(uint8_t f) {
  if (f > 100) return "auto";
  if (f >= 70) return "high";
  if (f >= 50) return "mid";
  return "low";
}

bool acCapsOk() { return ac.getAutoconfStatus() == dudanov::midea::AUTOCONF_OK; }

// Ohne Antwort auf die Faehigkeitsabfrage wird alles angeboten
bool acModeOk(int i) {
  if (!acCapsOk()) return true;
  const mda::Capabilities& c = ac.getCapabilities();
  switch (i) {
    case 1: return c.supportHeatMode();
    case 2: return c.supportDryMode();
    case 4: return c.supportAutoMode();
    default: return true;                          // Kuehlen und Lueften kann jede Anlage
  }
}
bool acHasSwingV() { return !acCapsOk() || ac.getCapabilities().supportVerticalSwing(); }
bool acHasSwingH() { return acCapsOk() && ac.getCapabilities().supportHorizontalSwing(); }
bool acHasEco() { return !acCapsOk() || ac.getCapabilities().supportEcoPreset(); }
bool acTurboPreset() { return acCapsOk() && ac.getCapabilities().supportTurboPreset(); }

bool acValid() { return acSeen; }
bool acLinkOk() { return acSeen && millis() - acs.lastFrame < 15000; }
bool acPower() { return acSeen && ac.getPowerState(); }

void acOpen() {
  Serial1.begin(9600, SERIAL_8N1, cfg.swap ? PIN_AC_TX : PIN_AC_RX, cfg.swap ? PIN_AC_RX : PIN_AC_TX);
  acs.s = &Serial1;
}

void acBegin() {
  acOpen();
  ac.setStream(&acs);
  ac.setPeriod(1000);
  ac.setTimeout(2000);
  ac.setNumAttempts(3);
  ac.setAutoconf(true);
  ac.setBeeper(cfg.beep);
  ac.addOnStateCallback([]() { acSeen = true; mqDirty = true; });
  ac.setup();
}

void acLoop() {
  ac.loop();
  static bool lastLink = false;
  static uint8_t lastAuto = 255;
  if (acLinkOk() != lastLink || ac.getAutoconfStatus() != lastAuto) {
    lastLink = acLinkOk(); lastAuto = ac.getAutoconfStatus(); mqDirty = true;
  }
}

// Bibliothek weiterlaufen lassen, bis der laufende Befehl beantwortet ist
bool acWaitIdle(uint32_t ms) {
  uint32_t t0 = millis();
  while (ac.busy() && millis() - t0 < ms) { ac.loop(); delay(2); }
  return !ac.busy();
}

int acCurMode() {
  if (ac.getPowerState()) return acModeIdx(ac.getMode());
  if (acPendMode >= 0) return acPendMode;
  return acModeIdx(ac.rawMode());
}

void acStatus(JsonObject o) {
  o["output"] = acPower();
  o["link"] = acLinkOk();
  JsonObject dg = o["diag"].to<JsonObject>();           // Fehlersuche UART
  dg["rx_bytes"] = acs.rxBytes; dg["tx_frames"] = acs.txFrames; dg["frames"] = acs.frames; dg["bad"] = acs.bad;
  dg["tx_pin"] = cfg.swap ? PIN_AC_RX : PIN_AC_TX; dg["rx_pin"] = cfg.swap ? PIN_AC_TX : PIN_AC_RX;
  static const char* const AUTOCONF[] = {"off", "running", "ok", "failed"};
  dg["autoconf"] = AUTOCONF[ac.getAutoconfStatus() & 3];
  if (!acSeen) return;
  int mi = acCurMode();
  if (mi >= 0) o["mode"] = AC_MODES[mi];
  o["target_C"] = ac.getTargetTemp();
  float t = ac.getIndoorTemp();
  if (t > -30 && t < 70) o["current_C"] = roundf(t * 10) / 10;
  float x = ac.getOutdoorTemp();                         // ohne Aussengeraet meist -25 oder 102,5
  if (x > -24.9f && x < 70) o["outdoor_C"] = roundf(x * 10) / 10;
  uint8_t f = ac.getFanMode();
  mda::Preset pr = ac.getPreset();
  o["fan"] = acFanName(f);
  uint8_t sw = ac.getSwingMode();
  o["swing_v"] = (sw & mda::SWING_VERTICAL) == mda::SWING_VERTICAL;
  o["swing_h"] = (sw & mda::SWING_HORIZONTAL) == mda::SWING_HORIZONTAL;
  o["eco"] = pr == mda::PRESET_ECO;
  o["turbo"] = pr == mda::PRESET_TURBO || f == mda::FAN_TURBO;
  o["mute"] = f == mda::FAN_SILENT;
  o["sleep"] = pr == mda::PRESET_SLEEP;
  if (pr == mda::PRESET_FREEZE_PROTECTION) o["frost"] = true;
  // Was die Anlage kann – Weboberflaeche, Cloud und Portal zeigen nur das
  JsonArray md = o["modes"].to<JsonArray>();
  for (int i = 0; i < 5; i++) if (acModeOk(i)) md.add(AC_MODES[i]);
  JsonArray fa = o["fans"].to<JsonArray>();
  for (const char* n : AC_FANS) fa.add(n);
  JsonArray ex = o["extras"].to<JsonArray>();
  if (acHasSwingV()) ex.add("swing_v");
  if (acHasSwingH()) ex.add("swing_h");
  if (acHasEco()) ex.add("eco");
  ex.add("turbo"); ex.add("mute"); ex.add("sleep"); ex.add("display");
  o["min_C"] = 16; o["max_C"] = 30; o["step_C"] = 0.5;
}

String acSet(JsonVariantConst p) {
  if (!acSeen) return "Keine Verbindung zur Klimaanlage (UART)";
  if (p["display_toggle"].is<bool>() && p["display_toggle"].as<bool>()) ac.displayToggle();   // Anzeige am Innengeraet
  const bool on = ac.getPowerState();
  const bool wantOn = p["on"].is<bool>() ? p["on"].as<bool>() : on;
  mda::Control c;

  int mi = -1;
  if (p["mode"].is<const char*>()) {
    mi = idxOf(AC_MODES, 5, p["mode"].as<const char*>());
    if (mi < 0) return "Modus: cool, heat, dry, fan, auto";
    if (!acModeOk(mi)) return "Diese Betriebsart kann die Anlage nicht";
  }
  int eff = -1;                                          // Modus, in dem die Anlage danach laeuft
  if (!wantOn) {
    if (on) c.mode = mda::MODE_OFF;
    if (mi >= 0) acPendMode = mi;                        // merken, gilt beim Einschalten
  } else {
    int cur = acCurMode();
    eff = mi >= 0 ? mi : (cur >= 0 ? cur : 0);
    if (!on || mi >= 0) c.mode = AC_MODE_V[eff];
  }

  if (p["target_C"].is<float>()) {
    float t = p["target_C"];
    if (t < 16 || t > 30) return "Solltemperatur 16–30 °C";
    c.targetTemp = roundf(t * 2) / 2;
  }

  mda::FanMode nf = ac.getFanMode();
  bool fanSet = false;
  if (p["fan"].is<const char*>()) {
    String f = p["fan"].as<const char*>();
    if (f == "auto") nf = mda::FAN_AUTO;
    else if (f == "low" || f == "low_mid") nf = mda::FAN_LOW;
    else if (f == "mid") nf = mda::FAN_MEDIUM;
    else if (f == "high" || f == "mid_high") nf = mda::FAN_HIGH;
    else return "Lüfter: auto, low, mid, high";
    fanSet = true;
  }
  if (p["mute"].is<bool>()) {
    if (p["mute"].as<bool>()) { nf = mda::FAN_SILENT; fanSet = true; }
    else if (nf == mda::FAN_SILENT) { nf = mda::FAN_AUTO; fanSet = true; }
  }

  mda::Preset np = ac.getPreset();
  bool preSet = false;
  if (p["eco"].is<bool>()) { preSet = true; if (p["eco"].as<bool>()) np = mda::PRESET_ECO; else if (np == mda::PRESET_ECO) np = mda::PRESET_NONE; }
  if (p["sleep"].is<bool>()) { preSet = true; if (p["sleep"].as<bool>()) np = mda::PRESET_SLEEP; else if (np == mda::PRESET_SLEEP) np = mda::PRESET_NONE; }
  if (p["turbo"].is<bool>()) {                           // Turbo-Preset, wenn die Anlage es meldet, sonst Turbo-Luefter
    if (p["turbo"].as<bool>()) {
      if (acTurboPreset()) { np = mda::PRESET_TURBO; preSet = true; } else { nf = mda::FAN_TURBO; fanSet = true; }
    } else {
      if (np == mda::PRESET_TURBO) { np = mda::PRESET_NONE; preSet = true; }
      if (nf == mda::FAN_TURBO) { nf = mda::FAN_HIGH; fanSet = true; }
    }
  }
  if (preSet && np != mda::PRESET_NONE) {
    if (!wantOn) return "Die Anlage ist aus";
    mda::Mode m = AC_MODE_V[eff];
    if (np == mda::PRESET_ECO && m != mda::MODE_COOL) return "Eco nur beim Kühlen";
    if (np == mda::PRESET_TURBO && m != mda::MODE_COOL && m != mda::MODE_HEAT) return "Turbo nur beim Kühlen oder Heizen";
    if (np == mda::PRESET_SLEEP && (m == mda::MODE_DRY || m == mda::MODE_FAN_ONLY)) return "Nachtmodus nicht beim Entfeuchten oder Lüften";
  }

  if (p["swing_v"].is<bool>() || p["swing_h"].is<bool>()) {
    uint8_t sw = ac.getSwingMode();
    bool v = p["swing_v"].is<bool>() ? p["swing_v"].as<bool>() : (sw & mda::SWING_VERTICAL) == mda::SWING_VERTICAL;
    bool h = p["swing_h"].is<bool>() ? p["swing_h"].as<bool>() : (sw & mda::SWING_HORIZONTAL) == mda::SWING_HORIZONTAL;
    c.swingMode = v && h ? mda::SWING_BOTH : v ? mda::SWING_VERTICAL : h ? mda::SWING_HORIZONTAL : mda::SWING_OFF;
  }
  if (fanSet) c.fanMode = nf;                            // im Automatikmodus und mit Preset regelt die Anlage selbst
  if (preSet) c.preset = np;
  if (wantOn) acPendMode = -1;

  ac.setBeeper(cfg.beep);
  if (!acWaitIdle(1500)) return "Klimaanlage antwortet nicht (voriger Befehl läuft noch)";
  ac.control(c);
  acWaitIdle(2500);                                      // Antwort abwarten: der Status danach ist der echte
  mqDirty = true;
  return "";
}

void acConfig(JsonObject o) { o["beep"] = cfg.beep; o["swap_pins"] = cfg.swap; }

void acSetConfig(JsonVariantConst c) {
  if (c["beep"].is<bool>()) { cfg.beep = c["beep"]; ac.setBeeper(cfg.beep); saveCfg(); }
  if (c["swap_pins"].is<bool>() && (bool)c["swap_pins"] != cfg.swap) {
    cfg.swap = c["swap_pins"]; saveCfg();
    Serial1.end();
    acOpen();
  }
}

// Weitere Climate-Methoden: Anzeige am Innengeraet an/aus
#define AC_METHODS "Climate.DisplayToggle",
bool acRpc(const String& m, JsonVariantConst, JsonObject, String& err) {
  if (m == "climate.displaytoggle") {
    if (!acSeen) err = "Keine Verbindung zur Klimaanlage (UART)";
    else ac.displayToggle();
    return true;
  }
  return false;
}
