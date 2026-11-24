// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 D.Koch – Pagurus OS
#ifndef HERMIT_LOADER   // Pagurus Loader wird separat gebaut (src/loader.cpp)
// Pagurus OS – Shelly-Gen2-kompatible Firmware fuer Shelly 1 / 1PM (ESP8266)
// RPC-API wie Gen2 (/rpc/Switch.Set, POST /rpc JSON-RPC), Webhooks, Gen1-Kompat-API,
// WLAN mit AP-Fallback (192.168.33.1), OTA, Energiezaehler, Uebertemperatur/-leistung.

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266HTTPClient.h>
#include <bearssl/bearssl_hash.h>
#include <ESP8266mDNS.h>
#include <WiFiClientSecureBearSSL.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <time.h>
#include <vector>
#include "device.h"
#include "webui.h"
#include "tasmota_import.h"
#include "mgos_import.h"
#if HAS_DIM
#include "stm_dimmer.h"
#endif
#if PM_ADE
#include "ade7953.h"
#endif

// Debug-Ausgaben ueber UART0 – beim Dimmer gehoert die Schnittstelle dem STM32
#if SERIAL_BUSY
  #define LOGLN(x) do {} while (0)
  #define LOGF(...) do {} while (0)
#else
  #define LOGLN(x) Serial.println(x)
  #define LOGF(...) Serial.printf(__VA_ARGS__)
#endif

#ifndef FW_VERSION
#define FW_VERSION "1.8.7"
#endif

// ======================= Konfiguration =======================
struct Hook {
  int id = 0;
  int cid = 0;
  bool enable = true;
  String event, name;
  std::vector<String> urls;
  bool hasThr = false;
  float thr = 0;
  bool armed = true;
};

// Einstellungen je Ausgangskanal (Shelly 2.5: zwei, sonst einer)
struct ChCfg {
  String name;
  String initial = DEF_INITIAL;     // off | on | restore_last | match_input
  String inMode = DEF_IN_MODE;      // follow | flip | momentary | detached
  bool autoOn = false;  float autoOnDelay = 60;
  bool autoOff = false; float autoOffDelay = 60;
  float powerLimit = DEF_POWER_LIMIT;
};

struct Cfg {
  String name;
  String ssid, pass;
  bool staticIp = false;
  bool roam = true;                // bei mehreren Access Points mit gleichem Namen den staerksten nehmen
  int roamRssi = -70;              // unter dieser Signalstaerke alle 5 min nach einem besseren AP suchen
  String ip, mask, gw, dns;
  String apPass;
  ChCfg ch[NCH];
  String inName;
  String inType = DEF_IN_TYPE;     // switch | button
  bool inInvert = false;
  String inFilter = DEF_IN_FILTER;          // ac = 50-Hz-Halbwellen ueberbruecken | dc = reiner Pegel
  String exName;                   // Add-on-Taster (input:100)
  String exType = "button";        // switch | button
  bool exInvert = false;
  bool exOutLow = false;           // Add-on-Ausgang invertiert (Leitung mit LOW freigeben)
  String exRelay = "none";         // none | momentary | flip | follow
  float vCal = V_CAL_DEFAULT, iCal = I_CAL_DEFAULT;   // Plug S: Spannung/Strom
  float tempLimit = DEF_TEMP_LIMIT;
  float pwrCal = PWR_CAL_DEFAULT;  // Watt pro Hz am CF-Pin (eigene Datei /calib)
  float pwrCal1 = PWR_CAL_DEFAULT, iCal1 = I_CAL_DEFAULT;   // zweiter Kanal (Shelly 2.5)
  // Rollladen (cover:0)
  float cvMaxOpen = 60, cvMaxClose = 60;  // Hoechstfahrzeit in s
  float cvTOpen = 0, cvTClose = 0;        // kalibrierte Fahrzeiten (eigene Datei /cover), 0 = keine Positionssteuerung
  bool cvSwapIn = false, cvInvert = false;
  float cvIdleThr = 5, cvIdleConf = 0.5f; // Endlage: unter … W fuer … s
  bool cvObst = false; float cvObstThr = 1000, cvObstHold = 1; String cvObstAction = "stop";
  String calSrc = "default";       // default | reference | tasmota | manual
  bool led = true;
  float transition = HAS_DIM ? 1.0f : 0.5f;   // Licht: Ueberblendzeit in s
  // Dimmer
  String in2Name, in2Type = "button";     // SW2 (input:1)
  bool in2Invert = false;
  int dimMin = 0, dimMax = 100;           // range_map in %
  bool leadEdge = false;                  // Phasenanschnitt statt -abschnitt
  int stmFade = 3, btnFade = 3;           // STM-Glaettung 1..5, Dimmgeschwindigkeit Taster 1..5
  bool ctInvert = false;           // Licht: Farbtemperatur-Kanal umkehren
  String authPass;
  String tz = "CET-1CEST,M3.5.0,M10.5.0/3";
  String sntp = "pool.ntp.org";
  // Auto-Update
  String updUrl = "https://raw.githubusercontent.com/oeltensack/pagurus-os/main/update.json";
  String updToken;                 // optional: GitHub-Token fuer privates Repo
  String updMode = "check";        // off | check | install
  int updHour = 3;                 // taegliche Pruefung um diese Stunde
  // MQTT (Gen2-Format) – optional, fuer Pagurus Cloud, Home Assistant, Node-RED …
  bool mqEnable = false;
  String mqServer;                 // host:port
  String mqUser, mqPass;
  String mqClientId, mqPrefix;     // leer = Geraete-ID
  bool mqRpcNtf = true, mqStatusNtf = true;
  std::vector<Hook> hooks;
  int nextHookId = 1;
  int hookRev = 0;
  int cfgRev = 0;
} cfg;

static const char* EVENTS[] = {
  EV_ON, EV_OFF,
#if HAS_RF
  "rf.button", "rf.open", "rf.closed", "rf.unknown",
#elif !HAS_LIGHT
  EV_OVERPOWER, EV_OVERTEMP,
#endif
#if IS_METER
  "switch.reset",
#endif
#if IS_COVER
  "cover.opening", "cover.closing", "cover.stopped", "cover.calibrating", "cover.calibration_failed", "cover.obstruction",
#endif
#if HAS_PM
  EV_PABOVE, EV_PBELOW,
#endif
#if HAS_INPUT || HAS_ADDON
  "input.toggle_on", "input.toggle_off",
  "input.button_down", "input.button_up", "input.button_push",
  "input.button_longpush", "input.button_doublepush",
#endif
  "sys.boot"
};

static const char* METHODS[] = {
  "Shelly.GetDeviceInfo", "Shelly.GetStatus", "Shelly.GetConfig", "Shelly.ListMethods",
  "Shelly.Reboot", "Shelly.FactoryReset", "Shelly.SetAuth",
  "Shelly.CheckForUpdate", "Shelly.Update",
#if IS_COVER
  "Cover.Open", "Cover.Close", "Cover.Stop", "Cover.GoToPosition", "Cover.Calibrate",
  "Cover.GetStatus", "Cover.GetConfig", "Cover.SetConfig", "Cover.ResetCounters",
  "Switch.Calibrate",
#elif HAS_RF
  "RF.GetStatus", "RF.GetConfig", "RF.SetConfig", "RF.Send", "RF.Add", "RF.Update", "RF.Delete", "RF.Beep",
  "RF.ClearRecent",
  "Switch.Set", "Switch.Toggle", "Switch.GetStatus",
#elif HAS_DIM
  "Light.Set", "Light.Toggle", "Light.GetStatus", "Light.GetConfig", "Light.SetConfig",
  "Light.DimUp", "Light.DimDown", "Light.DimStop", "Light.ResetCounters", "Light.Calibrate", "Pagurus.Stm",
#else
  "Switch.Set", "Switch.Toggle", "Switch.GetStatus", "Switch.GetConfig", "Switch.SetConfig",
#if HAS_PM
  "Switch.ResetCounters", "Switch.Calibrate",
#endif
#endif
#if HAS_INPUT || HAS_ADDON
  "Input.GetStatus", "Input.GetConfig", "Input.SetConfig", "Input.Diag",
#endif
#if HAS_ADDON
  "SensorAddon.GetPeripherals",
#endif
  "Sys.GetStatus", "Sys.GetConfig", "Sys.SetConfig",
  "WiFi.GetStatus", "WiFi.GetConfig", "WiFi.SetConfig", "WiFi.Scan",
  "Webhook.List", "Webhook.Create", "Webhook.Update", "Webhook.Delete",
  "Webhook.DeleteAll", "Webhook.ListSupported",
  "MQTT.GetStatus", "MQTT.GetConfig", "MQTT.SetConfig"
};

// ======================= Laufzeit =======================
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer updater;
DNSServer dnsServer;

String devId, macStr;
bool apActive = false, wifiWasUp = false, mdnsUp = false, bootEventSent = false, pendingReconnect = false;
uint32_t wifiLostMs = 0, wifiUpMs = 0, rebootAt = 0, staTryMs = 0;
bool staOn = false;          // STA sucht gerade das Heim-WLAN (bei aktivem AP nur zeitweise)
// Access-Point-Wahl: bei mehreren APs mit gleichem Namen gezielt den staerksten (BSSID) verwenden
uint8_t apBssid[6]; int32_t apChan = 0; bool apTarget = false;
uint32_t staStartMs = 0, lastRoamCheck = 0, roamCount = 0;
volatile bool roamPending = false;
bool safeMode = false;       // nach mehreren Abstuerzen in Folge: keine Uebernahme, AP sofort
// Erster Start: alle gefundenen WLAN-Zugangsdaten werden der Reihe nach probiert (sichtbare Netze zuerst)
std::vector<WifiCand> wifiCands;
size_t candIdx = 0;
bool candActive = false;
uint32_t candStartMs = 0;

// Laufzustand je Ausgangskanal: Relais/Licht, Timer, Messung
struct Chan {
  bool on = false;
  String src = "init";
  bool tAct = false, tTarget = false;            // Timer (toggle_after, Auto-An/Aus)
  uint32_t tStartMs = 0, tDurMs = 0;
  time_t tStartedAt = 0;
  float apower = 0, avolt = NAN, acurr = NAN, pf = NAN, freq = NAN;
  double energyWh = 0, energySavedWh = 0, minuteAccWh = 0;
  float byMinute[3] = {0, 0, 0};
  bool overpower = false;
};
Chan CH[NCH];

volatile bool inPulse = false;
volatile uint8_t inActiveLevel = HIGH;
uint32_t inLastActiveMs = 0, inCandMs = 0;
bool inState = false, inCand = false;
#if HAS_IN2
volatile bool in2Pulse = false;
volatile uint8_t in2ActiveLevel = HIGH;
uint32_t in2LastActiveMs = 0, in2CandMs = 0;
bool in2State = false, in2Cand = false;
#endif
int bootEdges = 0;

volatile uint32_t cfLastUs = 0, cfSumUs = 0, cfCnt = 0;
volatile uint32_t cf1LastUs = 0, cf1SumUs = 0, cf1Cnt = 0;
volatile bool cf1Arm = false;
bool selVolt = true; uint32_t selSinceMs = 0;
uint32_t minuteStartMs = 0;
time_t minuteTs = 0;
float tempC = NAN;
bool overtemp = false;
uint32_t lastPmMs = 0, lastTempMs = 0, lastEnergySaveMs = 0;

std::vector<String> httpQueue;
bool resetWifiPending = false;
uint32_t pcycleClearAt = 6000;

// ======================= Hilfen =======================
static String jstr(JsonVariantConst v, const String& def) {
  return v.is<const char*>() ? String(v.as<const char*>()) : def;
}
static bool validTime() { return time(nullptr) > 1600000000; }
static float r1(float v) { return roundf(v * 10) / 10; }
static double r3(double v) { return round(v * 1000) / 1000; }

// ======================= Speichern / Laden =======================
// Kalibrierung liegt in eigener Datei und uebersteht Werkseinstellungen
void saveCal() {
  File f = LittleFS.open("/calib", "w");
  if (f) {
    f.print(cfg.pwrCal, 6); f.print('\n'); f.print(cfg.calSrc); f.print('\n');
    f.print(cfg.vCal, 6); f.print('\n'); f.print(cfg.iCal, 6);
#if NCH > 1
    f.print('\n'); f.print(cfg.pwrCal1, 6); f.print('\n'); f.print(cfg.iCal1, 6);
#endif
    f.close();
  }
}
void loadCal() {
  File f = LittleFS.open("/calib", "r");
  if (!f) return;
  float v = f.readStringUntil('\n').toFloat();
  String src = f.readStringUntil('\n'); src.trim();
  float vv = f.readStringUntil('\n').toFloat(), ii = f.readStringUntil('\n').toFloat();
#if NCH > 1
  float p1 = f.readStringUntil('\n').toFloat(), i1 = f.readStringUntil('\n').toFloat();
  if (p1 > 0.05f && p1 < 100.0f) cfg.pwrCal1 = p1;
  if (i1 > 0.0001f && i1 < 10) cfg.iCal1 = i1;
#endif
  f.close();
  if (vv > 0.001f && vv < 10) cfg.vCal = vv;
  if (ii > 0.0001f && ii < (PM_ADE ? 10 : 1)) cfg.iCal = ii;
  if (v > 0.05f && v < 100.0f) { cfg.pwrCal = v; cfg.calSrc = src.length() ? src : "manual"; }
}

void saveCfg() {
  cfg.cfgRev++;
  JsonDocument d;
  d["name"] = cfg.name; d["ssid"] = cfg.ssid; d["pass"] = cfg.pass;
  d["roam"] = cfg.roam; d["roamthr"] = cfg.roamRssi;
  d["static"] = cfg.staticIp; d["ip"] = cfg.ip; d["mask"] = cfg.mask; d["gw"] = cfg.gw; d["dns"] = cfg.dns;
  d["appass"] = cfg.apPass; d["swname"] = cfg.ch[0].name; d["inname"] = cfg.inName;
  d["initial"] = cfg.ch[0].initial; d["inmode"] = cfg.ch[0].inMode; d["intype"] = cfg.inType; d["ininv"] = cfg.inInvert; d["infilt"] = cfg.inFilter;
  d["exname"] = cfg.exName; d["extype"] = cfg.exType; d["exinv"] = cfg.exInvert; d["exout"] = cfg.exOutLow; d["exrel"] = cfg.exRelay;
  d["aon"] = cfg.ch[0].autoOn; d["aond"] = cfg.ch[0].autoOnDelay; d["aoff"] = cfg.ch[0].autoOff; d["aoffd"] = cfg.ch[0].autoOffDelay;
  for (int c = 1; c < NCH; c++) {            // weitere Kanaele: eigenes Objekt ch1, ch2 …
    JsonObject k = d[String("ch") + c].to<JsonObject>(); ChCfg& kc = cfg.ch[c];
    k["name"] = kc.name; k["initial"] = kc.initial; k["inmode"] = kc.inMode;
    k["aon"] = kc.autoOn; k["aond"] = kc.autoOnDelay; k["aoff"] = kc.autoOff; k["aoffd"] = kc.autoOffDelay; k["plim"] = kc.powerLimit;
  }
  d["plim"] = cfg.ch[0].powerLimit; d["tlim"] = cfg.tempLimit; d["led"] = cfg.led; d["trans"] = cfg.transition; d["ctinv"] = cfg.ctInvert;
  d["in2name"] = cfg.in2Name; d["in2type"] = cfg.in2Type; d["in2inv"] = cfg.in2Invert;
#if IS_COVER
  { JsonObject v = d["cv"].to<JsonObject>();
    v["mo"] = cfg.cvMaxOpen; v["mc"] = cfg.cvMaxClose; v["si"] = cfg.cvSwapIn; v["inv"] = cfg.cvInvert;
    v["it"] = cfg.cvIdleThr; v["ic"] = cfg.cvIdleConf; v["ob"] = cfg.cvObst; v["ot"] = cfg.cvObstThr; v["oh"] = cfg.cvObstHold; v["oa"] = cfg.cvObstAction; }
#endif
  d["dmin"] = cfg.dimMin; d["dmax"] = cfg.dimMax; d["lead"] = cfg.leadEdge; d["sfade"] = cfg.stmFade; d["bfade"] = cfg.btnFade;
  d["auth"] = cfg.authPass; d["tz"] = cfg.tz; d["sntp"] = cfg.sntp;
  d["uurl"] = cfg.updUrl; d["utok"] = cfg.updToken; d["umode"] = cfg.updMode; d["uhour"] = cfg.updHour;
  { JsonObject q = d["mq"].to<JsonObject>();
    q["en"] = cfg.mqEnable; q["srv"] = cfg.mqServer; q["u"] = cfg.mqUser; q["p"] = cfg.mqPass;
    q["cid"] = cfg.mqClientId; q["pre"] = cfg.mqPrefix; q["rn"] = cfg.mqRpcNtf; q["sn"] = cfg.mqStatusNtf; }
  d["hnext"] = cfg.nextHookId; d["hrev"] = cfg.hookRev; d["rev"] = cfg.cfgRev;
  JsonArray ha = d["hooks"].to<JsonArray>();
  for (auto& h : cfg.hooks) {
    JsonObject o = ha.add<JsonObject>();
    o["id"] = h.id; o["cid"] = h.cid; o["en"] = h.enable; o["ev"] = h.event; o["name"] = h.name;
    JsonArray u = o["urls"].to<JsonArray>();
    for (auto& s : h.urls) u.add(s);
    if (h.hasThr) o["thr"] = h.thr;
  }
  File f = LittleFS.open("/config.json", "w");
  if (f) { serializeJson(d, f); f.close(); }
}

void loadCfg() {
  File f = LittleFS.open("/config.json", "r");
  if (!f) return;
  JsonDocument d;
  DeserializationError e = deserializeJson(d, f);
  f.close();
  if (e) return;
  cfg.name = jstr(d["name"], cfg.name); cfg.ssid = jstr(d["ssid"], ""); cfg.pass = jstr(d["pass"], "");
  cfg.staticIp = d["static"] | false;
  cfg.roam = d["roam"] | true; cfg.roamRssi = d["roamthr"] | -70;
  cfg.ip = jstr(d["ip"], ""); cfg.mask = jstr(d["mask"], ""); cfg.gw = jstr(d["gw"], ""); cfg.dns = jstr(d["dns"], "");
  cfg.apPass = jstr(d["appass"], ""); cfg.ch[0].name = jstr(d["swname"], ""); cfg.inName = jstr(d["inname"], "");
  cfg.ch[0].initial = jstr(d["initial"], cfg.ch[0].initial); cfg.ch[0].inMode = jstr(d["inmode"], cfg.ch[0].inMode);
  cfg.inType = jstr(d["intype"], cfg.inType); cfg.inInvert = d["ininv"] | false; cfg.inFilter = jstr(d["infilt"], cfg.inFilter);
  cfg.exName = jstr(d["exname"], ""); cfg.exType = jstr(d["extype"], cfg.exType);
  cfg.exInvert = d["exinv"] | false; cfg.exOutLow = d["exout"] | false; cfg.exRelay = jstr(d["exrel"], cfg.exRelay);
  cfg.ch[0].autoOn = d["aon"] | false; cfg.ch[0].autoOnDelay = d["aond"] | 60.0f;
  cfg.ch[0].autoOff = d["aoff"] | false; cfg.ch[0].autoOffDelay = d["aoffd"] | 60.0f;
  cfg.ch[0].powerLimit = d["plim"] | cfg.ch[0].powerLimit; cfg.tempLimit = d["tlim"] | cfg.tempLimit;
  for (int c = 1; c < NCH; c++) {
    JsonObjectConst k = d[String("ch") + c]; ChCfg& kc = cfg.ch[c];
    if (k.isNull()) continue;
    kc.name = jstr(k["name"], ""); kc.initial = jstr(k["initial"], kc.initial); kc.inMode = jstr(k["inmode"], kc.inMode);
    kc.autoOn = k["aon"] | false; kc.autoOnDelay = k["aond"] | 60.0f; kc.autoOff = k["aoff"] | false; kc.autoOffDelay = k["aoffd"] | 60.0f;
    kc.powerLimit = k["plim"] | kc.powerLimit;
  }
  cfg.led = d["led"] | true;
  cfg.transition = d["trans"] | cfg.transition; cfg.ctInvert = d["ctinv"] | false;
  // Uebernahme aus Version 1.0.0 (Faktor stand in config.json, Standard war falsch 12.53)
  float old = d["pcal"] | 0.0f;
  if (old > 0 && fabsf(old - 12.53f) > 0.001f && !LittleFS.exists("/calib")) { cfg.pwrCal = old; cfg.calSrc = "reference"; saveCal(); }
  cfg.in2Name = jstr(d["in2name"], ""); cfg.in2Type = jstr(d["in2type"], cfg.in2Type); cfg.in2Invert = d["in2inv"] | false;
#if IS_COVER
  { JsonObjectConst v = d["cv"];
    cfg.cvMaxOpen = v["mo"] | 60.0f; cfg.cvMaxClose = v["mc"] | 60.0f; cfg.cvSwapIn = v["si"] | false; cfg.cvInvert = v["inv"] | false;
    cfg.cvIdleThr = v["it"] | 5.0f; cfg.cvIdleConf = v["ic"] | 0.5f; cfg.cvObst = v["ob"] | false;
    cfg.cvObstThr = v["ot"] | 1000.0f; cfg.cvObstHold = v["oh"] | 1.0f; cfg.cvObstAction = jstr(v["oa"], "stop"); }
#endif
  cfg.dimMin = d["dmin"] | 0; cfg.dimMax = d["dmax"] | 100; cfg.leadEdge = d["lead"] | false;
  cfg.stmFade = d["sfade"] | 3; cfg.btnFade = d["bfade"] | 3;
  cfg.authPass = jstr(d["auth"], ""); cfg.tz = jstr(d["tz"], cfg.tz); cfg.sntp = jstr(d["sntp"], cfg.sntp);
  cfg.updUrl = jstr(d["uurl"], cfg.updUrl); cfg.updToken = jstr(d["utok"], "");
  cfg.updMode = jstr(d["umode"], cfg.updMode); cfg.updHour = d["uhour"] | cfg.updHour;
  { JsonObjectConst q = d["mq"];
    cfg.mqEnable = q["en"] | false; cfg.mqServer = jstr(q["srv"], ""); cfg.mqUser = jstr(q["u"], ""); cfg.mqPass = jstr(q["p"], "");
    cfg.mqClientId = jstr(q["cid"], ""); cfg.mqPrefix = jstr(q["pre"], "");
    cfg.mqRpcNtf = q["rn"] | true; cfg.mqStatusNtf = q["sn"] | true; }
  cfg.nextHookId = d["hnext"] | 1; cfg.hookRev = d["hrev"] | 0; cfg.cfgRev = d["rev"] | 0;
  cfg.hooks.clear();
  for (JsonObjectConst o : d["hooks"].as<JsonArrayConst>()) {
    Hook h;
    h.id = o["id"] | 0; h.cid = o["cid"] | 0; h.enable = o["en"] | true; h.event = jstr(o["ev"], ""); h.name = jstr(o["name"], "");
    for (JsonVariantConst u : o["urls"].as<JsonArrayConst>()) h.urls.push_back(jstr(u, ""));
    if (o["thr"].is<float>()) { h.hasThr = true; h.thr = o["thr"]; }
    h.armed = (h.event != EV_PBELOW);
    cfg.hooks.push_back(h);
  }
}

// Energiezaehler: Kanal 0 in /energy, weitere in /energy1 …
static String energyFile(int c) { return c ? String("/energy") + c : String("/energy"); }
void saveEnergy() {
#if HAS_PM
  for (int c = 0; c < NCH; c++) {
    File f = LittleFS.open(energyFile(c), "w");
    if (f) { f.print(CH[c].energyWh, 4); f.close(); CH[c].energySavedWh = CH[c].energyWh; }
  }
  lastEnergySaveMs = millis();
#endif
}
void loadEnergy() {
#if HAS_PM
  for (int c = 0; c < NCH; c++) {
    File f = LittleFS.open(energyFile(c), "r");
    if (f) { CH[c].energyWh = f.readString().toFloat(); CH[c].energySavedWh = CH[c].energyWh; f.close(); }
  }
#endif
}
// Letzter Schaltzustand: ein Zeichen je Kanal ("1", "10" …)
void saveRelayState() {
  File f = LittleFS.open("/relay", "w");
  if (f) { for (int c = 0; c < NCH; c++) f.print(CH[c].on ? '1' : '0'); f.close(); }
}
bool loadRelayState(int c = 0) {
  File f = LittleFS.open("/relay", "r");
  if (!f) return false;
  String v = f.readString();
  f.close();
  return c < (int)v.length() && v[c] == '1';
}

// ======================= Ereignisse / Webhooks =======================
#if HAS_DIM
extern int lBri;
#endif
int h_cid_tmp = 0;
String expandUrl(String u, const String& ev) {
  u.replace("${event}", ev);
  u.replace("${cid}", String(h_cid_tmp));
  int hc = (h_cid_tmp >= 0 && h_cid_tmp < NCH) ? h_cid_tmp : 0;
  u.replace("${output}", CH[hc].on ? "true" : "false");
#if HAS_IN2
  u.replace("${input}", (h_cid_tmp == 1 ? in2State : inState) ? "true" : "false");
#else
  u.replace("${input}", inState ? "true" : "false");
#endif
  u.replace("${id}", devId);
  u.replace("${name}", cfg.name);
  u.replace("${ip}", WiFi.localIP().toString());
  u.replace("${apower}", String(r1(CH[hc].apower), 1));
  u.replace("${aenergy}", String(CH[hc].energyWh, 3));
  u.replace("${tC}", isnan(tempC) ? String("null") : String(tempC, 1));
#if HAS_DIM
  u.replace("${brightness}", String(lBri));
#endif
  return u;
}

void fireHook(const Hook& h, const String& ev) {
  h_cid_tmp = h.cid;
  for (auto& u : h.urls)
    if (httpQueue.size() < 12) httpQueue.push_back(expandUrl(u, ev));
}

void mqttEvent(const String& ev, int cid);
void fireEvent(const String& ev, int cid = 0) {
  mqttEvent(ev, cid);
  for (auto& h : cfg.hooks)
    if (h.enable && h.event == ev && h.cid == cid) fireHook(h, ev);
}

void processQueue() {
  if (httpQueue.empty() || WiFi.status() != WL_CONNECTED) return;
  String url = httpQueue.front();
  httpQueue.erase(httpQueue.begin());
  HTTPClient http;
  http.setTimeout(3000);
  if (url.startsWith("https://")) {
    BearSSL::WiFiClientSecure sc;
    sc.setInsecure();
    sc.setBufferSizes(1024, 512);
    if (http.begin(sc, url)) { http.GET(); http.end(); }
  } else {
    WiFiClient wc;
    if (http.begin(wc, url)) { http.GET(); http.end(); }
  }
}

// ======================= Licht (Duo) =======================
#if HAS_LIGHT
int lBri = 100, lCt = 4000;                  // Sollwerte (Helligkeit %, Farbtemperatur K)
float curBri = 0, curCt = 4000, fromBri = 0, fromCt = 4000, toBri = 0, toCt = 4000;
uint32_t fadeStart = 0, fadeMs = 0, lightSaveAt = 0;
float nextTransition = -1;                   // einmalige Ueberblendzeit (CCT.Set transition_duration)

void lightWrite() {
  // Helligkeit (Gamma) auf Kalt- und Warmkanal verteilen; Summe = Gesamthelligkeit
  float b = curBri / 100.0f;
  float d = b <= 0 ? 0 : 1023.0f * powf(b, 2.2f);
  if (curBri > 0 && d < 3) d = 3;
  float warm = (6500.0f - curCt) / 3800.0f;
  if (warm < 0) warm = 0;
  if (warm > 1) warm = 1;
  if (cfg.ctInvert) warm = 1 - warm;
  analogWrite(PIN_PWM_WARM, (int)(d * warm + 0.5f));
  analogWrite(PIN_PWM_COLD, (int)(d * (1 - warm) + 0.5f));
}

void lightUpdate() {                          // neues Ziel aus relayOn/lBri/lCt, sanft ueberblenden
  fromBri = curBri; fromCt = curCt;
  toBri = CH[0].on ? lBri : 0; toCt = lCt;
  float t = nextTransition >= 0 ? nextTransition : cfg.transition;
  nextTransition = -1;
  fadeMs = (uint32_t)(t * 1000); fadeStart = millis();
  if (!fadeMs) { curBri = toBri; curCt = toCt; lightWrite(); }
}

void saveLight() {
  File f = LittleFS.open("/light", "w");
  if (f) { f.printf("%d %d", lBri, lCt); f.close(); }
}
void loadLight() {
  File f = LittleFS.open("/light", "r");
  if (!f) return;
  int b = f.parseInt(), c = f.parseInt();
  f.close();
  if (b >= 1 && b <= 100) lBri = b;
  if (c >= 2700 && c <= 6500) lCt = c;
  curCt = fromCt = toCt = lCt;
}

void handleLight() {
  if (fadeMs) {
    float k = (millis() - fadeStart) / (float)fadeMs;
    if (k >= 1) { k = 1; fadeMs = 0; }
    curBri = fromBri + (toBri - fromBri) * k;
    curCt = fromCt + (toCt - fromCt) * k;
    lightWrite();
  }
  if (lightSaveAt && (int32_t)(millis() - lightSaveAt) >= 0) { lightSaveAt = 0; saveLight(); }
}

// Werte setzen (ohne Ein/Aus); speichert verzoegert, um den Flash zu schonen
void lightSet(int bri, int ct) {
  if (bri >= 0) lBri = bri < 1 ? 1 : (bri > 100 ? 100 : bri);
  if (ct >= 0) lCt = ct < 2700 ? 2700 : (ct > 6500 ? 6500 : ct);
  lightSaveAt = millis() + 5000;
}

// Bei Lampen ohne Taster: WLAN-Reset durch 5x schnelles Aus/Ein am Wandschalter
void powerCycleCheck() {
  // Nur echte Stromunterbrechungen zaehlen (nicht Neustart nach Update, Absturz, Watchdog)
  if (ESP.getResetInfoPtr()->reason != REASON_DEFAULT_RST) { LittleFS.remove("/pcycle"); return; }
  int n = 0;
  File f = LittleFS.open("/pcycle", "r");
  if (f) { n = f.parseInt(); f.close(); }
  n++;
  if (n >= 5) { LittleFS.remove("/pcycle"); resetWifiPending = true; return; }
  f = LittleFS.open("/pcycle", "w");
  if (f) { f.print(n); f.close(); }
}
#elif HAS_DIM
// ======================= Dimmer (Shelly Dimmer 2) =======================
// Helligkeit 1..100 % wird ueber range_map [min,max] auf 0..1000 des STM32 abgebildet.
// Ueberblenden und Dimmen per Taster rechnet der ESP und schickt die Zwischenwerte (max. alle 30 ms).
StmDimmer<HardwareSerial> stmd;
int lBri = 50;                               // Sollhelligkeit in %
float curBri = 0, fromBri = 0, toBri = 0;
uint32_t fadeStart = 0, fadeMs = 0, lightSaveAt = 0, lastHwMs = 0;
uint16_t lastHw = 0xFFFF;
float nextTransition = -1;
int8_t rampDir = 0; float rampBri = 0; uint32_t rampLastMs = 0; float rampRate = 25;

static uint32_t msClock() { return millis(); }
static void msPause(uint32_t ms) { delay(ms); }
static void stmReset() {
  digitalWrite(PIN_STM_BOOT0, LOW);
  digitalWrite(PIN_STM_NRST, LOW);
  delay(50);
  while (Serial.available()) Serial.read();
  digitalWrite(PIN_STM_NRST, HIGH);
  delay(50);
}

uint16_t dimHw(float pct) {
  if (pct <= 0) return 0;
  float lo = cfg.dimMin * 10.0f, hi = cfg.dimMax * 10.0f;
  float v = lo + (hi - lo) * (pct > 100 ? 100 : pct) / 100.0f;
  if (v < 1) v = 1;
  return (uint16_t)(v > 1000 ? 1000 : v + 0.5f);
}
float dimPct(uint16_t hw) {                  // Umkehrung fuer die Uebernahme nach Neustart
  if (!hw) return 0;
  float lo = cfg.dimMin * 10.0f, hi = cfg.dimMax * 10.0f;
  float p = hi > lo ? (hw - lo) * 100.0f / (hi - lo) : 100;
  return p < 1 ? 1 : (p > 100 ? 100 : p);
}

void dimWrite(bool force) {
  uint16_t v = dimHw(curBri);
  if (v == lastHw) return;
  if (!force && millis() - lastHwMs < 30) return;
  lastHw = v; lastHwMs = millis();
  stmd.setBrightness(v);
}

void lightUpdate() {                          // neues Ziel aus relayOn/lBri, sanft ueberblenden
  rampDir = 0;
  fromBri = curBri;
  toBri = CH[0].on ? lBri : 0;
  float t = nextTransition >= 0 ? nextTransition : cfg.transition;
  nextTransition = -1;
  fadeMs = (uint32_t)(t * 1000); fadeStart = millis();
  if (!fadeMs) { curBri = toBri; dimWrite(true); }
}

void saveLight() {
  File f = LittleFS.open("/dim", "w");
  if (f) { f.print(lBri); f.close(); }
}
void loadLight() {
  File f = LittleFS.open("/dim", "r");
  if (!f) return;
  int b = f.parseInt();
  f.close();
  if (b >= 1 && b <= 100) lBri = b;
}

void lightSet(int bri) {
  if (bri >= 0) lBri = bri < 1 ? 1 : (bri > 100 ? 100 : bri);
  lightSaveAt = millis() + 5000;
}

// Dauerdimmen (Taster gehalten bzw. Light.DimUp/DimDown), dir +1/-1
void rampBegin(int8_t dir, float ratePct) {
  fadeMs = 0;
  rampBri = CH[0].on ? (curBri > 0 ? curBri : lBri) : 1;
  rampDir = dir; rampRate = ratePct; rampLastMs = millis();
}
void rampStop() {
  if (!rampDir) return;
  rampDir = 0;
  lBri = (int)lroundf(rampBri);
  if (lBri < 1) lBri = 1;
  toBri = curBri = lBri;
  dimWrite(true);
  lightSaveAt = millis() + 3000;
}

void handleLight() {
  stmd.tick(true, stmReset, msPause);
  uint32_t now = millis();
  if (rampDir) {
    float dt = (now - rampLastMs) / 1000.0f; rampLastMs = now;
    rampBri += rampDir * rampRate * dt;
    bool end = (rampDir > 0 && rampBri >= 100) || (rampDir < 0 && rampBri <= 1);
    if (rampBri >= 100) rampBri = 100;
    if (rampBri <= 1) rampBri = 1;
    curBri = toBri = rampBri;
    if (end) rampStop(); else dimWrite(false);
  } else if (fadeMs) {
    float k = (now - fadeStart) / (float)fadeMs;
    if (k >= 1) { k = 1; fadeMs = 0; }
    curBri = fromBri + (toBri - fromBri) * k;
    dimWrite(fadeMs == 0);
  } else dimWrite(true);                      // ausstehenden Endwert sicher senden
  if (lightSaveAt && (int32_t)(now - lightSaveAt) >= 0) { lightSaveAt = 0; saveLight(); }
}
#else
void handleLight() {}
#endif

// ======================= Relais =======================
void updateLed();

// Messfirmware: Abschaltung bleibt gespeichert (/trip), bis am Geraet quittiert wird
void setTrip(bool t) {
#if IS_METER
  if (t) { File f = LittleFS.open("/trip", "w"); if (f) { f.print(overtemp ? "overtemp" : "overpower"); f.close(); } }
  else LittleFS.remove("/trip");
#endif
}

#if HAS_RELAY
#ifdef PIN_RELAY2
static const uint8_t RELAY_PIN[NCH] = {PIN_RELAY, PIN_RELAY2};
#else
static const uint8_t RELAY_PIN[NCH] = {PIN_RELAY};
#endif
#endif

bool setRelayC(int c, bool on, const String& src, float toggleAfter = -1) {
  if (c < 0 || c >= NCH) return false;
  if (on && overtemp) return false;
#if IS_METER
  // nur Start, Hand-Reset und Schutzabschaltung duerfen das Relais bewegen
  if (!(src == "init" || src == "reset" || src == "overpower" || src == "overtemp")) return false;
  toggleAfter = 0;
#endif
  Chan& k = CH[c];
  ChCfg& kc = cfg.ch[c];
  bool was = k.on;
  k.on = on;
  k.src = src;
#if HAS_RELAY
  digitalWrite(RELAY_PIN[c], on ? HIGH : LOW);
#endif
#if HAS_LIGHT || HAS_DIM
  lightUpdate();
#endif
  if (on) k.overpower = false;
  k.tAct = false;
  float ta = toggleAfter;
  if (ta < 0) {
    if (on && kc.autoOff) ta = kc.autoOffDelay;
    else if (!on && kc.autoOn) ta = kc.autoOnDelay;
    else ta = 0;
  }
  if (ta > 0) {
    k.tAct = true; k.tTarget = !on;
    k.tStartMs = millis(); k.tDurMs = (uint32_t)(ta * 1000);
    k.tStartedAt = time(nullptr);
  }
  if (on && src == "reset") setTrip(false);
  if (was != on) {
    if (kc.initial == "restore_last") saveRelayState();
    fireEvent(on ? EV_ON : EV_OFF, c);
  }
  updateLed();
  return true;
}
bool setRelay(bool on, const String& src, float toggleAfter = -1) { return setRelayC(0, on, src, toggleAfter); }

void handleTimer() {
  for (int c = 0; c < NCH; c++)
    if (CH[c].tAct && millis() - CH[c].tStartMs >= CH[c].tDurMs) {
      CH[c].tAct = false;
      setRelayC(c, CH[c].tTarget, "timer");
    }
}

static bool anyOn() { for (int c = 0; c < NCH; c++) if (CH[c].on) return true; return false; }

void updateLed() {
#if HAS_LED
  if (apActive) return;
  digitalWrite(PIN_LED, (cfg.led && anyOn()) ? LED_ON_LEVEL : LED_OFF_LEVEL);
#endif
}


// ======================= Rollladen (Shelly 2.5, cover:0) =======================
// Relais 1 = auf, Relais 2 = zu (tauschbar). Nie beide zugleich; beim Richtungswechsel 0,5 s Pause.
// Endlagen: Motor schaltet ab -> Leistung faellt unter idle_power_thr. Position ueber Fahrzeit (nach Kalibrierung).
#if IS_COVER
enum CvState : uint8_t { CV_STOPPED, CV_OPENING, CV_CLOSING, CV_OPEN, CV_CLOSED, CV_CALIBRATING };
const char* CV_NAMES[] = {"stopped", "opening", "closing", "open", "closed", "calibrating"};
CvState cvState = CV_STOPPED;
String cvSrc = "init";
int8_t cvDir = 0, cvLastDir = 0;            // +1 auf, -1 zu
float cvPos = NAN, cvStartPos = NAN, cvTarget = NAN;
uint32_t cvStartMs = 0, cvMaxMs = 0, cvIdleMs = 0, cvPendMs = 0;
int8_t cvPendDir = 0; float cvPendTarget = NAN; String cvPendSrc;
time_t cvStartedAt = 0;
bool cvObstructed = false, cvOverpower = false, cvSeenPw = false;
bool pmOk();
// Kalibrierfahrt: 1 = zu bis Endlage, 2 = auf (Zeit messen), 3 = zu (Zeit messen)
uint8_t cvCal = 0; float cvCalOpen = 0, cvCalClose = 0;

bool coverMoving() { return cvDir != 0; }
float coverPower() { return CH[0].apower + CH[1].apower; }

void cvSave() {
  File f = LittleFS.open("/cover", "w");
  if (f) { f.printf("%.2f %.2f %.1f %d", cfg.cvTOpen, cfg.cvTClose, isnan(cvPos) ? -1.0f : cvPos, (int)cvLastDir); f.close(); }
}
void cvLoad() {
  File f = LittleFS.open("/cover", "r");
  if (!f) return;
  float to = f.parseFloat(), tc = f.parseFloat(), p = f.parseFloat(); int ld = f.parseInt();
  f.close();
  if (to > 0.5f && to < 600) cfg.cvTOpen = to;
  if (tc > 0.5f && tc < 600) cfg.cvTClose = tc;
  if (cfg.cvTOpen > 0 && cfg.cvTClose > 0 && p >= 0 && p <= 100) cvPos = p;
  cvLastDir = ld > 0 ? 1 : (ld < 0 ? -1 : 0);
}
bool cvPosControl() { return cfg.cvTOpen > 0 && cfg.cvTClose > 0; }

void cvRelays(int8_t dir) {
  uint8_t pOpen = RELAY_PIN[cfg.cvInvert ? 1 : 0], pClose = RELAY_PIN[cfg.cvInvert ? 0 : 1];
  // erst abschalten, dann einschalten – nie beide an
  if (dir <= 0) digitalWrite(pOpen, LOW);
  if (dir >= 0) digitalWrite(pClose, LOW);
  if (dir > 0) digitalWrite(pOpen, HIGH);
  if (dir < 0) digitalWrite(pClose, HIGH);
  CH[cfg.cvInvert ? 1 : 0].on = dir > 0;
  CH[cfg.cvInvert ? 0 : 1].on = dir < 0;
  updateLed();
}

// aktuelle Position waehrend der Fahrt
float cvLivePos() {
  if (!cvDir || isnan(cvStartPos) || !cvPosControl()) return cvPos;
  float t = (millis() - cvStartMs) / 1000.0f;
  float full = cvDir > 0 ? cfg.cvTOpen : cfg.cvTClose;
  float p = cvStartPos + cvDir * t / full * 100.0f;
  return p < 0 ? 0 : (p > 100 ? 100 : p);
}

void cvEvent(const char* ev) { fireEvent(String("cover.") + ev, 0); }

void coverStop(const String& src) {
  cvPendDir = 0;
  if (!cvDir) { cvRelays(0); return; }
  float p = cvLivePos();
  cvRelays(0);
  cvDir = 0; cvSrc = src; cvTarget = NAN;
  if (cvCal) { cvCal = 0; }                  // Kalibrierung abgebrochen
  cvPos = p;
  if (!isnan(p) && p >= 99.5f) { cvPos = 100; cvState = CV_OPEN; cvEvent("open"); }
  else if (!isnan(p) && p <= 0.5f) { cvPos = 0; cvState = CV_CLOSED; cvEvent("closed"); }
  else { cvState = CV_STOPPED; cvEvent("stopped"); }
  cvSave();
}

void cvStart(int8_t dir, const String& src, float target, float maxSec) {
  cvRelays(dir);
  cvDir = dir; cvLastDir = dir; cvSrc = src; cvTarget = target;
  cvStartPos = cvPos; cvStartMs = millis(); cvStartedAt = time(nullptr); cvIdleMs = 0;
  float mx = maxSec > 0 ? maxSec : (dir > 0 ? cfg.cvMaxOpen : cfg.cvMaxClose);
  cvMaxMs = (uint32_t)(mx * 1000);
  cvObstructed = false; cvOverpower = false; cvSeenPw = false;
  if (cvCal) cvState = CV_CALIBRATING;
  else { cvState = dir > 0 ? CV_OPENING : CV_CLOSING; cvEvent(dir > 0 ? "opening" : "closing"); }
}

// Fahrt anfordern; laeuft der Motor gerade andersherum, erst stoppen und kurz warten
bool coverMove(int8_t dir, const String& src, float target = NAN, float maxSec = 0) {
  if (overtemp) return false;
  if (cvDir == dir && isnan(target) && isnan(cvTarget)) return true;
  if (cvDir && cvDir != dir) {
    coverStop(src);
    cvPendDir = dir; cvPendTarget = target; cvPendSrc = src; cvPendMs = millis() + 500;
    return true;
  }
  if (cvDir == dir) { cvTarget = target; cvSrc = src; return true; }
  cvStart(dir, src, target, maxSec);
  return true;
}

bool coverGoTo(float pos, const String& src) {
  if (!cvPosControl() || isnan(cvPos)) return false;
  pos = pos < 0 ? 0 : (pos > 100 ? 100 : pos);
  float cur = cvLivePos();
  if (fabsf(pos - cur) < 0.5f && !cvDir) return true;
  if (pos >= 100) return coverMove(1, src);            // Endlagen anfahren: bis der Motor abschaltet
  if (pos <= 0) return coverMove(-1, src);
  return coverMove(pos > cur ? 1 : -1, src, pos);
}

void coverCalibrate(const String& src) {
  coverStop(src);
  cvCal = 1; cvPos = NAN;
  cvStart(-1, src, NAN, cfg.cvMaxClose);
  cvEvent("calibrating");
}

// Endlage erreicht (Motor hat abgeschaltet)
void cvEndReached() {
  int8_t d = cvDir;
  // Fahrzeit bis zum Abschalten des Motors (nicht bis zum Ende der Bestaetigungszeit)
  float t = ((cvIdleMs ? cvIdleMs : millis()) - cvStartMs) / 1000.0f;
  cvRelays(0);
  cvDir = 0; cvTarget = NAN;
  cvPos = d > 0 ? 100 : 0;
  if (cvCal == 1) { cvCal = 2; cvStart(1, cvSrc, NAN, cfg.cvMaxOpen); return; }
  if (cvCal == 2) { cvCalOpen = t; cvCal = 3; cvStart(-1, cvSrc, NAN, cfg.cvMaxClose); return; }
  if (cvCal == 3) {
    cvCalClose = t; cvCal = 0;
    if (cvCalOpen > 1 && cvCalClose > 1) { cfg.cvTOpen = cvCalOpen; cfg.cvTClose = cvCalClose; }
  }
  cvState = d > 0 ? CV_OPEN : CV_CLOSED;
  cvEvent(d > 0 ? "open" : "closed");
  cvSave();
}

void handleCover() {
  uint32_t now = millis();
  if (cvPendDir && (int32_t)(now - cvPendMs) >= 0) {
    int8_t d = cvPendDir; cvPendDir = 0;
    cvStart(d, cvPendSrc, cvPendTarget, 0);
  }
  if (!cvDir) return;
  uint32_t run = now - cvStartMs;
  float pw = coverPower();
  // Ziel erreicht (nur mit Positionssteuerung)
  if (!isnan(cvTarget) && !cvCal) {
    float p = cvLivePos();
    if ((cvDir > 0 && p >= cvTarget) || (cvDir < 0 && p <= cvTarget)) { coverStop(cvSrc); return; }
  }
  // Schutz: Leistungsgrenze, Hindernis
  if (cfg.ch[0].powerLimit > 0 && pw > cfg.ch[0].powerLimit) {
    coverStop("overpower"); cvOverpower = true; fireEvent(EV_OVERPOWER, 0); return;
  }
  if (cfg.cvObst && run > (uint32_t)(cfg.cvObstHold * 1000) && pw > cfg.cvObstThr) {
    int8_t d = cvDir;
    coverStop("obstruction"); cvObstructed = true; cvEvent("obstruction");
    if (cfg.cvObstAction == "reverse") coverMove(-d, "obstruction", NAN, 2);
    return;
  }
  // Endlage: Motor zieht nach dem Anlaufen keinen Strom mehr (nur wenn vorher Leistung gemessen wurde)
  if (pw > cfg.cvIdleThr * 3) cvSeenPw = true;
  if (run > 1500 && pmOk() && cvSeenPw) {
    if (pw < cfg.cvIdleThr) { if (!cvIdleMs) cvIdleMs = now; else if (now - cvIdleMs >= (uint32_t)(cfg.cvIdleConf * 1000)) { cvEndReached(); return; } }
    else cvIdleMs = 0;
  }
  // Hoechstfahrzeit
  if (run >= cvMaxMs) {
    if (cvCal) { cvCal = 0; coverStop("timeout"); cvEvent("calibration_failed"); return; }
    if (isnan(cvTarget)) { cvEndReached(); return; }   // ohne Leistungsabfall: Fahrzeit gilt als Endlage
    coverStop("timeout");
  }
}

// Eingaenge: dual (SW1 auf, SW2 zu), single (SW1 auf/stopp/zu/stopp), detached
void coverInput(int in, bool st) {
  const String& mode = cfg.ch[0].inMode;
  if (mode == "detached") return;
  bool button = (in == 0 ? cfg.inType : cfg.in2Type) == "button";
  if (cfg.cvSwapIn) in = 1 - in;
  if (mode == "single") {
    if (in != 0) return;
    if (button && !st) return;
    if (cvDir) coverStop(button ? "button" : "switch");
    else coverMove(cvLastDir > 0 ? -1 : 1, button ? "button" : "switch");
    return;
  }
  int8_t dir = in == 0 ? 1 : -1;
  if (button) {
    if (!st) return;
    if (cvDir) coverStop("button"); else coverMove(dir, "button");
  } else {
    if (st) coverMove(dir, "switch");
    else if (cvDir == dir) coverStop("switch");
  }
}
#else
bool coverMoving() { return false; }
void handleCover() {}
#endif


// ======================= RF-Bridge (Sonoff RF Bridge 433) =======================
// Empfangene Codes erscheinen in „Zuletzt empfangen“ und koennen gespeichert werden als
//  - Taste   (Ereignis rf.button, cid = Eintrag)
//  - Kontakt (zwei Codes: offen/zu -> Ereignisse rf.open / rf.closed, Zustand)
//  - Schalter (zwei Codes: an/aus -> Gen2-Komponente switch:<id>, schaltbar, Fernbedienung aktualisiert den Zustand)
#if HAS_RF
#include "rf_bridge.h"
RfEfm<HardwareSerial> efm;

struct RfEntry {
  int id = 0;
  String name, type = "button";        // button | contact | switch
  String code1, code2;                 // Taste/Kontakt offen/Schalter an · Kontakt zu/Schalter aus
  int state = -1;                      // Kontakt: 1 offen, 0 zu; Schalter: 1 an, 0 aus; -1 unbekannt
  String src = "init";
  uint32_t lastMs = 0;
};
std::vector<RfEntry> rfList;
struct RfSeen { String code; uint32_t ms; time_t ts; int id; int count; };
std::vector<RfSeen> rfRecent;           // neueste zuerst, max. 16
uint8_t rfMode = 0;                     // 0 Standard, 1 alle Protokolle (Portisch), 2 Rohdaten (Portisch)
uint8_t rfRepeats = 8;
String rfLastCode; uint32_t rfLastCodeMs = 0;
uint32_t rfLedMs = 0;                   // LED kurz an bei Empfang

String rfToStr(const RfCode& c) {
  String s = c.kind == RF_STD ? "S:" : c.kind == RF_PROTO ? "P:" : "R:";
  char b[3];
  for (int i = 0; i < c.len; i++) { snprintf(b, sizeof b, "%02X", c.d[i]); s += b; }
  return s;
}
bool rfFromStr(String s, RfCode& c) {
  s.trim(); s.toUpperCase(); s.replace(" ", "");
  if (s.length() < 4 || s[1] != ':') return false;
  c.kind = s[0] == 'S' ? RF_STD : s[0] == 'P' ? RF_PROTO : s[0] == 'R' ? RF_RAW : (RfKind)0;
  if (!c.kind) return false;
  String h = s.substring(2);
  if (h.length() % 2 || h.length() / 2 > sizeof(c.d)) return false;
  c.len = h.length() / 2;
  for (int i = 0; i < c.len; i++) {
    char t[3] = {h[i * 2], h[i * 2 + 1], 0};
    char* e; long v = strtol(t, &e, 16);
    if (*e) return false;
    c.d[i] = (uint8_t)v;
  }
  return (c.kind != RF_STD || c.len == 9) && c.len >= 2;
}
// Rohdaten sind nie zweimal gleich: Vergleich nur ueber die Daten (ohne Zeitstufen)
String rfKey(const String& code) {
  if (code.startsWith("R:") && code.length() > 4) {
    int n = strtol(code.substring(2, 4).c_str(), nullptr, 16);
    return "R:" + code.substring(4 + n * 4);
  }
  if (code.startsWith("S:") && code.length() == 20) return "S:" + code.substring(14);   // nur die 24 Bit
  return code;
}
String rfShort(const String& code) {
  if (code.startsWith("S:") && code.length() == 20) return code.substring(14);
  return code.length() > 26 ? code.substring(0, 24) + "…" : code;
}

void rfSave() {
  JsonDocument d;
  d["mode"] = rfMode; d["rep"] = rfRepeats;
  JsonArray a = d["list"].to<JsonArray>();
  for (auto& e : rfList) {
    JsonObject o = a.add<JsonObject>();
    o["id"] = e.id; o["n"] = e.name; o["t"] = e.type; o["c1"] = e.code1; o["c2"] = e.code2; o["s"] = e.state;
  }
  File f = LittleFS.open("/rf.json", "w");
  if (f) { serializeJson(d, f); f.close(); }
}
void rfLoad() {
  File f = LittleFS.open("/rf.json", "r");
  if (!f) return;
  JsonDocument d;
  if (deserializeJson(d, f)) { f.close(); return; }
  f.close();
  rfMode = d["mode"] | 0; rfRepeats = d["rep"] | 8;
  rfList.clear();
  for (JsonObjectConst o : d["list"].as<JsonArrayConst>()) {
    RfEntry e; e.id = o["id"] | 0; e.name = jstr(o["n"], ""); e.type = jstr(o["t"], "button");
    e.code1 = jstr(o["c1"], ""); e.code2 = jstr(o["c2"], ""); e.state = o["s"] | -1;
    rfList.push_back(e);
  }
}
RfEntry* rfFind(int id) { for (auto& e : rfList) if (e.id == id) return &e; return nullptr; }
int rfFreeId() { for (int i = 0; i < 64; i++) if (!rfFind(i)) return i; return -1; }

bool rfSendStr(const String& code, uint8_t rep = 0) {
  RfCode c;
  if (!rfFromStr(code, c)) return false;
  return efm.send(c, rep ? rep : rfRepeats);
}

// Schalter (Funksteckdose) schalten: Code senden, Zustand merken
bool rfSwitch(RfEntry& e, bool on, const String& src) {
  const String& code = on ? e.code1 : e.code2;
  if (!code.length() || !rfSendStr(code)) return false;
  bool was = e.state == 1;
  e.state = on ? 1 : 0; e.src = src;
  rfLastCode = rfKey(code); rfLastCodeMs = millis();   // eigenes Echo nicht als Fernbedienung werten
  if (was != on) fireEvent(on ? "switch.on" : "switch.off", e.id);
  rfSave();
  return true;
}

void rfOnCode(const RfCode& c) {
  String code = rfToStr(c), key = rfKey(code);
  uint32_t now = millis();
  rfLedMs = now | 1;
  bool repeat = key == rfLastCode && now - rfLastCodeMs < 700;   // Fernbedienungen senden mehrfach
  rfLastCode = key; rfLastCodeMs = now;
  int match = -1;
  for (auto& e : rfList) {
    bool m1 = e.code1.length() && rfKey(e.code1) == key, m2 = e.code2.length() && rfKey(e.code2) == key;
    if (!m1 && !m2) continue;
    match = e.id;
    if (repeat) break;
    e.lastMs = now;
    if (e.type == "button") fireEvent("rf.button", e.id);
    else if (e.type == "contact") {
      int s = m1 ? 1 : 0;
      if (e.state != s) { e.state = s; fireEvent(s ? "rf.open" : "rf.closed", e.id); }
    } else if (e.type == "switch") {
      int s = m1 ? 1 : 0;
      e.src = "rf";
      if (e.state != s) { e.state = s; fireEvent(s ? "switch.on" : "switch.off", e.id); rfSave(); }
    }
    break;
  }
  if (!repeat && match < 0) fireEvent("rf.unknown", 0);
  for (auto it = rfRecent.begin(); it != rfRecent.end(); ++it)
    if (rfKey(it->code) == key) { RfSeen s = *it; rfRecent.erase(it); s.ms = now; s.ts = time(nullptr); s.count++; s.id = match; s.code = code; rfRecent.insert(rfRecent.begin(), s); return; }
  rfRecent.insert(rfRecent.begin(), RfSeen{code, now, time(nullptr), match, 1});
  if (rfRecent.size() > 16) rfRecent.pop_back();
}

void rfStatus(JsonObject o) {
  o["mode"] = rfMode == 1 ? "protocols" : rfMode == 2 ? "raw" : "standard";
  o["frames"] = efm.frames; o["acks"] = efm.acks; o["errors"] = efm.bad;
  if (efm.lastRxMs) o["last_rx_ago"] = (millis() - efm.lastRxMs) / 1000;
  JsonArray r = o["recent"].to<JsonArray>();
  for (auto& s : rfRecent) {
    JsonObject x = r.add<JsonObject>();
    x["code"] = s.code; x["short"] = rfShort(s.code); x["ago"] = (millis() - s.ms) / 1000; x["count"] = s.count;
    if (s.id >= 0) x["id"] = s.id;
  }
  JsonArray l = o["entries"].to<JsonArray>();
  for (auto& e : rfList) {
    JsonObject x = l.add<JsonObject>();
    x["id"] = e.id; x["name"] = e.name; x["type"] = e.type;
    if (e.state >= 0) x["state"] = e.state == 1; else x["state"] = nullptr;
    if (e.lastMs) x["last_ago"] = (millis() - e.lastMs) / 1000;
  }
}
void rfEntryJson(JsonObject x, const RfEntry& e) {
  x["id"] = e.id; x["name"] = e.name; x["type"] = e.type; x["code1"] = e.code1; x["code2"] = e.code2;
}
void rfSwitchStatus(JsonObject o, const RfEntry& e) {
  o["id"] = e.id; o["source"] = e.src; o["output"] = e.state == 1;
}

void handleRf() { efm.poll(); }
#endif

void handleLed() {
#if HAS_LED
  if (apActive) digitalWrite(PIN_LED, (millis() / 500) & 1 ? LED_ON_LEVEL : LED_OFF_LEVEL);
#if HAS_RF
  else digitalWrite(PIN_LED, (rfLedMs && millis() - rfLedMs < 150) ? LED_ON_LEVEL : LED_OFF_LEVEL);
#endif
#endif
#ifdef PIN_LED_LINK
  // WLAN-LED: AP blinkt, getrennt an, verbunden aus
  bool on = apActive ? ((millis() / 250) & 1) : (WiFi.status() != WL_CONNECTED);
  digitalWrite(PIN_LED_LINK, on ? LED_ON_LEVEL : LED_OFF_LEVEL);
#endif
}

// ======================= Eingang (AC-tauglich) =======================
#if HAS_INPUT
IRAM_ATTR void inIsr() {
  if (((GPI >> PIN_INPUT) & 1) == inActiveLevel) inPulse = true;
}
#endif
#if HAS_IN2
IRAM_ATTR void in2Isr() {
  if (((GPI >> PIN_INPUT2) & 1) == in2ActiveLevel) in2Pulse = true;
}
#endif

void resetWifiAndReboot() {
  cfg.ssid = ""; cfg.pass = ""; cfg.staticIp = false;
  ESP.eraseConfig();                 // auch WLAN-Speicher des ESP leeren (sonst Uebernahme beim Start)
  saveCfg();
  rebootAt = millis() + 500;
}

struct BtnState { uint32_t pressMs = 0, relMs = 0; uint8_t pushes = 0; };
BtnState bs0, bs100, bs1;

// Ereignisse eines Eingangs (Schalter: toggle_on/off, Taster: down/up/push/doublepush/longpush)
void btnEdge(int cid, const String& type, bool st, BtnState& b) {
  uint32_t now = millis();
  if (type == "button") {
    if (st) { b.pressMs = now; fireEvent("input.button_down", cid); }
    else {
      fireEvent("input.button_up", cid);
      if (now - b.pressMs >= 1000) { b.pushes = 0; fireEvent("input.button_longpush", cid); }
      else { b.pushes++; b.relMs = now; }
    }
  } else {
    fireEvent(st ? "input.toggle_on" : "input.toggle_off", cid);
  }
}

void btnTick(int cid, bool st, BtnState& b) {
  if (b.pushes && !st && millis() - b.relMs > 350) {
    fireEvent(b.pushes >= 2 ? "input.button_doublepush" : "input.button_push", cid);
    b.pushes = 0;
  }
}

// Relaisaktion eines Eingangs: follow | flip | momentary | detached/none
void inputAction(const String& mode, bool st, const char* src, int c = 0) {
  if (mode == "follow") setRelayC(c, st, src);
  else if (mode == "flip") setRelayC(c, !CH[c].on, src);
  else if (mode == "momentary") { if (st) setRelayC(c, !CH[c].on, src); }
}

#if HAS_DIM
// Taster am Dimmer: kurz = schalten, gehalten (ab 0,5 s) = dimmen
//  dim      : SW1 kurz an/aus, lang abwechselnd heller/dunkler (aus: ab Minimum heller)
//  dual_dim : SW1 kurz an, lang heller · SW2 kurz aus, lang dunkler
//  follow/flip/momentary/detached: SW1 wie beim Schalter, SW2 nur Ereignisse
struct DimBtn { bool down = false, ramping = false; uint32_t downMs = 0; };
DimBtn dbt[2];
bool dimUpNext = true;
static const float BTN_RATE[6] = {25, 10, 16, 25, 40, 60};   // %/s je Stufe 1..5

bool dimButtons(int cid) { return cfg.ch[0].inMode == "dual_dim" || (cfg.ch[0].inMode == "dim" && cid == 0); }

void dimEdge(int cid, bool st) {
  DimBtn& b = dbt[cid];
  if (dimButtons(cid)) {
    if (st) { b.down = true; b.ramping = false; b.downMs = millis(); return; }
    if (!b.down) return;
    b.down = false;
    if (b.ramping) { b.ramping = false; rampStop(); return; }
    if (cfg.ch[0].inMode == "dim") setRelay(!CH[0].on, "button");
    else setRelay(cid == 0, "button");
    return;
  }
  if (cid == 0) inputAction(cfg.ch[0].inMode, st, cfg.ch[0].inMode == "momentary" ? "button" : "switch");
}

void dimTick() {
  for (int cid = 0; cid < 2; cid++) {
    DimBtn& b = dbt[cid];
    if (!b.down || b.ramping || millis() - b.downMs < 500 || !dimButtons(cid)) continue;
    int8_t dir;
    if (cfg.ch[0].inMode == "dual_dim") dir = cid == 0 ? 1 : -1;
    else {
      dir = (!CH[0].on || dimUpNext) ? 1 : -1;
      if (CH[0].on && dir > 0 && lBri >= 100) dir = -1;
      if (CH[0].on && dir < 0 && lBri <= 1) dir = 1;
      dimUpNext = dir < 0;
    }
    if (!CH[0].on) {
      if (dir < 0) { b.down = false; continue; }
      int keep = lBri;
      lBri = 1; nextTransition = 0;
      if (!setRelay(true, "button")) { lBri = keep; b.down = false; continue; }   // z. B. Uebertemperatur
    }
    b.ramping = true;
    int k = cfg.btnFade < 1 ? 3 : (cfg.btnFade > 5 ? 5 : cfg.btnFade);
    rampBegin(dir, BTN_RATE[k]);
  }
}
#endif

void onInputEdge(bool st) {
  uint32_t now = millis();
  if (now < 60000 && ++bootEdges >= 10) { resetWifiAndReboot(); return; }
  btnEdge(0, cfg.inType, st, bs0);
#if IS_METER
  // Taster: Druck, Schalter/SW: jede Flanke – quittiert eine Schutzabschaltung
  if ((cfg.inType != "button" || st) && !CH[0].on && setRelay(true, "reset")) fireEvent("switch.reset");
#elif HAS_DIM
  dimEdge(0, st);
#elif IS_COVER
  coverInput(0, st);
#else
  inputAction(cfg.ch[0].inMode, st, cfg.ch[0].inMode == "momentary" ? "button" : "switch");
#endif
}

bool sampleInput(uint32_t now) {
#if !HAS_INPUT
  return false;
#else
  if (cfg.inFilter == "dc") { inPulse = false; return ((GPI >> PIN_INPUT) & 1) == inActiveLevel; }
  if (inPulse || (((GPI >> PIN_INPUT) & 1) == inActiveLevel)) { inPulse = false; inLastActiveMs = now; }
  return (now - inLastActiveMs) < 35;   // haelt bei 50-Hz-Halbwellen durch
#endif
}

void handleInput() {
#if !HAS_INPUT
  return;
#endif
  uint32_t now = millis();
  bool raw = sampleInput(now);
  if (raw != inCand) { inCand = raw; inCandMs = now; }
  if (inCand != inState && now - inCandMs >= 40) { inState = inCand; onInputEdge(inState); }
  btnTick(0, inState, bs0);
}

// ---- Taster auf der Platine (Shelly 2.5): 10 s halten = WLAN zuruecksetzen ----
#ifdef PIN_BTN
uint32_t pbDownMs = 0;
void handleBoardBtn() {
  bool down = digitalRead(PIN_BTN) == LOW;
  if (!down) { pbDownMs = 0; return; }
  if (!pbDownMs) pbDownMs = millis() | 1;
  else if (millis() - pbDownMs > 10000) { pbDownMs = 0; resetWifiAndReboot(); }
}
#else
void handleBoardBtn() {}
#endif

// ---- Zweiter Eingang (SW2, input:1) – gleicher Filter wie SW1 ----
#if HAS_IN2
bool sampleInput2(uint32_t now) {
  if (cfg.inFilter == "dc") { in2Pulse = false; return ((GPI >> PIN_INPUT2) & 1) == in2ActiveLevel; }
  if (in2Pulse || (((GPI >> PIN_INPUT2) & 1) == in2ActiveLevel)) { in2Pulse = false; in2LastActiveMs = now; }
  return (now - in2LastActiveMs) < 35;
}
void handleInput2() {
  uint32_t now = millis();
  bool raw = sampleInput2(now);
  if (raw != in2Cand) { in2Cand = raw; in2CandMs = now; }
  if (in2Cand != in2State && now - in2CandMs >= 40) {
    in2State = in2Cand;
    btnEdge(1, cfg.in2Type, in2State, bs1);
#if HAS_DIM
    dimEdge(1, in2State);
#elif IS_COVER
    coverInput(1, in2State);
#elif NCH > 1
    inputAction(cfg.ch[1].inMode, in2State, cfg.ch[1].inMode == "momentary" ? "button" : "switch", 1);
#endif
  }
  btnTick(1, in2State, bs1);
#if HAS_DIM
  dimTick();
#endif
}
#else
void handleInput2() {}
#endif

// ---- Add-on-Taster (input:100) ----
#if HAS_ADDON
bool exState = false, exCand = false;
uint32_t exCandMs = 0;

void addonRelease() { digitalWrite(PIN_ADDON_OUT, cfg.exOutLow ? LOW : HIGH); }

bool readAddon() {
  bool pressed = digitalRead(PIN_ADDON_IN) == LOW;   // Taster zieht DATA nach GND
  return cfg.exInvert ? !pressed : pressed;
}

void handleAddon() {
  uint32_t now = millis();
  bool raw = readAddon();
  if (raw != exCand) { exCand = raw; exCandMs = now; }
  if (exCand != exState && now - exCandMs >= 30) {
    exState = exCand;
    btnEdge(100, cfg.exType, exState, bs100);
    inputAction(cfg.exRelay, exState, "addon");
  }
  btnTick(100, exState, bs100);
}
#else
void handleAddon() {}
#endif

// ======================= Leistungsmessung / Temperatur =======================
#if HAS_PM && !PM_STM && !PM_ADE
IRAM_ATTR void cfIsr() {
  uint32_t now = micros();
  if (cfLastUs) { cfSumUs += now - cfLastUs; cfCnt++; }
  cfLastUs = now;
}
#endif

#if HAS_CF1
IRAM_ATTR void cf1Isr() {
  uint32_t now = micros();
  if (cf1Arm && cf1LastUs) { cf1SumUs += now - cf1LastUs; cf1Cnt++; }
  cf1LastUs = now;
}

// BL0937 CF1: abwechselnd 2 s Spannung (SEL LOW) und 2 s Strom (SEL HIGH), erste 300 ms verwerfen
void handleCf1() {
  uint32_t now = millis();
  if (!cf1Arm && now - selSinceMs > 300) { noInterrupts(); cf1SumUs = 0; cf1Cnt = 0; cf1LastUs = 0; cf1Arm = true; interrupts(); }
  if (now - selSinceMs < 2000) return;
  noInterrupts();
  uint32_t sum = cf1SumUs, cnt = cf1Cnt;
  cf1Arm = false;
  interrupts();
  float hz = (cnt > 0 && sum > 0) ? cnt * 1e6f / sum : 0;
  if (selVolt) CH[0].avolt = r1(cfg.vCal * hz);
  else CH[0].acurr = (CH[0].apower > 0.5f) ? roundf(cfg.iCal * hz * 1000) / 1000 : 0;
  selVolt = !selVolt;
  digitalWrite(PIN_SEL, selVolt ? SEL_VOLT_LEVEL : !SEL_VOLT_LEVEL);
  selSinceMs = now;
}
#endif

#if PM_ADE
bool pmOk() { return ade::ok; }
#else
bool pmOk() { return true; }
#endif
// Kalibrierfaktoren je Kanal (Kanal 0 wie bisher, Kanal 1 nur beim Shelly 2.5)
float& pcalOf(int c) { return c ? cfg.pwrCal1 : cfg.pwrCal; }
float& icalOf(int c) { return c ? cfg.iCal1 : cfg.iCal; }

void handlePm() {
#if HAS_CF1
  handleCf1();
#endif
#if HAS_PM
  uint32_t now = millis();
  if (now - lastPmMs < (coverMoving() ? 200u : 1000u)) return;   // Rollladen: waehrend der Fahrt 5x/s messen
  float dt = (now - lastPmMs) / 1000.0f;
  lastPmMs = now;
  float p[NCH];
  for (int c = 0; c < NCH; c++) p[c] = 0;
#if PM_STM
  if (stmd.online) {
    if (CH[0].on || curBri > 0) p[0] = stmd.pW * cfg.pwrCal;
    CH[0].avolt = isnan(stmd.vV) ? NAN : r1(stmd.vV);
    CH[0].acurr = isnan(stmd.iA) ? NAN : (p[0] > 0.5f ? roundf(stmd.iA * 1000) / 1000 : 0);
  }
#elif PM_ADE
  ade::Val v[2];
  if (millis() > 2000 && ade::sample(v)) {
    for (int c = 0; c < NCH; c++) {
      p[c] = v[c].w * pcalOf(c);
      CH[c].avolt = r1(v[c].v * cfg.vCal);
      CH[c].acurr = roundf(v[c].a * icalOf(c) * 1000) / 1000;
      float va = v[c].va * pcalOf(c);
      CH[c].pf = (va > 1 && p[c] > 0.5f) ? roundf(fminf(p[c] / va, 1) * 100) / 100 : NAN;
      CH[c].freq = isnan(v[c].hz) ? NAN : roundf(v[c].hz * 10) / 10;
    }
  }
#else
  noInterrupts();
  uint32_t sum = cfSumUs, cnt = cfCnt, last = cfLastUs;
  cfSumUs = 0; cfCnt = 0;
  interrupts();
  if (CH[0].on) {
    if (cnt > 0 && sum > 0) p[0] = cfg.pwrCal * (cnt * 1e6f / sum);
    else if (last) {
      uint32_t since = micros() - last;
      if (since < 10000000UL) { float ub = cfg.pwrCal * 1e6f / since; p[0] = CH[0].apower < ub ? CH[0].apower : ub; }
    }
  }
#endif
  bool minute = now - minuteStartMs >= 60000;
  for (int c = 0; c < NCH; c++) {
    Chan& k = CH[c];
    k.apower = p[c] < 0 ? 0 : p[c];
    double wh = k.apower * dt / 3600.0;
    k.energyWh += wh; k.minuteAccWh += wh;
    if (minute) {
      k.byMinute[2] = k.byMinute[1]; k.byMinute[1] = k.byMinute[0]; k.byMinute[0] = k.minuteAccWh * 1000.0;
      k.minuteAccWh = 0;
    }
#if !IS_COVER
    if (cfg.ch[c].powerLimit > 0 && k.on && k.apower > cfg.ch[c].powerLimit) {
      setRelayC(c, false, "overpower", 0);
      k.overpower = true;
      setTrip(true);
      fireEvent(EV_OVERPOWER, c);
    }
#endif
  }
  if (minute) { minuteStartMs = now; minuteTs = validTime() ? (time(nullptr) / 60) * 60 : 0; }
  for (auto& h : cfg.hooks) {
    if (!h.enable || !h.hasThr) continue;
    float ap = CH[(h.cid >= 0 && h.cid < NCH) ? h.cid : 0].apower;
#if IS_COVER
    ap = CH[0].apower + CH[1].apower;
#endif
    if (h.event == EV_PABOVE) {
      if (h.armed && ap > h.thr) { h.armed = false; fireHook(h, h.event); }
      else if (ap < h.thr * 0.95f) h.armed = true;
    } else if (h.event == EV_PBELOW) {
      if (h.armed && ap < h.thr) { h.armed = false; fireHook(h, h.event); }
      else if (ap > h.thr * 1.05f + 0.5f) h.armed = true;
    }
  }
  bool dirty = false;
  for (int c = 0; c < NCH; c++) if (CH[c].energyWh - CH[c].energySavedWh > 0.01) dirty = true;
  if (now - lastEnergySaveMs > 900000UL && dirty) saveEnergy();
#endif
}
void handleTemp() {
#if HAS_TEMP
  if (millis() - lastTempMs < 2000) return;
  lastTempMs = millis();
  int adc = analogRead(A0);
  if (adc <= 0 || adc >= 1023) return;
  double rt = adc * 32000.0 / (3379.2 - adc);   // 32k-Bruecke, Formel wie Tasmota
  if (rt <= 0) return;
  double t = 1.0 / (1.0 / 298.15 + log(rt / 10000.0) / 3350.0) - 273.15;
  tempC = isnan(tempC) ? t : tempC * 0.7f + t * 0.3f;
  if (!overtemp && tempC > cfg.tempLimit) {
#if IS_COVER
    coverStop("overtemp");
#else
    for (int c = 0; c < NCH; c++) setRelayC(c, false, "overtemp", 0);
#endif
    overtemp = true;
    setTrip(true);
    fireEvent(EV_OVERTEMP);
  } else if (overtemp && tempC < cfg.tempLimit - 10) overtemp = false;
#endif
}

// ======================= WLAN =======================
void startAP() {
  if (apActive) return;
  WiFi.mode(cfg.ssid.length() ? WIFI_AP_STA : WIFI_AP);
  staOn = cfg.ssid.length() > 0; staTryMs = millis();
  WiFi.softAPConfig(IPAddress(192, 168, 33, 1), IPAddress(192, 168, 33, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP(devId.c_str(), cfg.apPass.length() >= 8 ? cfg.apPass.c_str() : nullptr);
  dnsServer.start(53, "*", IPAddress(192, 168, 33, 1));
  apActive = true;
}

void stopAP() {
  if (!apActive) return;
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apActive = false;
  updateLed();
}

// Staerksten AP mit unserem Namen suchen (blockiert ca. 2 s)
bool pickBestAp() {
  int n = WiFi.scanNetworks(false, false);
  int best = -1; int32_t br = -1000;
  for (int i = 0; i < n; i++)
    if (WiFi.SSID(i) == cfg.ssid && WiFi.RSSI(i) > br) { best = i; br = WiFi.RSSI(i); }
  if (best >= 0) { memcpy(apBssid, WiFi.BSSID(best), 6); apChan = WiFi.channel(best); }
  WiFi.scanDelete();
  return best >= 0;
}

// Ergebnis der Hintergrundsuche bei schwachem Empfang: deutlich besseren AP merken
void onRoamScan(int n) {
  int best = -1; int32_t br = -1000;
  for (int i = 0; i < n; i++)
    if (WiFi.SSID(i) == cfg.ssid && WiFi.RSSI(i) > br) { best = i; br = WiFi.RSSI(i); }
  if (best >= 0 && WiFi.status() == WL_CONNECTED && memcmp(WiFi.BSSID(best), WiFi.BSSID(), 6) != 0
      && br >= WiFi.RSSI() + 8) {
    memcpy(apBssid, WiFi.BSSID(best), 6); apChan = WiFi.channel(best); apTarget = true;
    roamPending = true;
  }
  WiFi.scanDelete();
}

void startSTA(bool scan = true) {
  if (!cfg.ssid.length()) return;
  WiFi.hostname(devId);
  staStartMs = millis();
  if (cfg.staticIp) {
    IPAddress ip, gw, m, d;
    ip.fromString(cfg.ip); gw.fromString(cfg.gw); m.fromString(cfg.mask);
    if (!d.fromString(cfg.dns)) d = gw;
    WiFi.config(ip, gw, m, d);
  } else {
    WiFi.config(0U, 0U, 0U);
  }
  if (cfg.roam && (!scan || pickBestAp()) && apChan) {
    apTarget = true;
    WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str(), apChan, apBssid);
  } else {
    apTarget = false;
    WiFi.begin(cfg.ssid, cfg.pass);
  }
}

void onWifiUp() {
  configTime(cfg.tz.c_str(), cfg.sntp.c_str());
  if (!mdnsUp && MDNS.begin(devId)) {
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("shelly", "tcp", 80);
    MDNS.addServiceTxt("shelly", "tcp", "gen", "2");
    MDNS.addServiceTxt("shelly", "tcp", "app", DEV_APP);
    MDNS.addServiceTxt("shelly", "tcp", "ver", FW_VERSION);
    mdnsUp = true;
  }
  if (!bootEventSent) { bootEventSent = true; fireEvent("sys.boot"); }
}

void applyCand(size_t i);
void handleWifi() {
  uint32_t now = millis();
  if (pendingReconnect) {
    pendingReconnect = false;
    WiFi.disconnect();
    wifiWasUp = false; wifiLostMs = now;
    if (apActive) WiFi.mode(WIFI_AP_STA);
    startSTA();
  }
  bool up = WiFi.status() == WL_CONNECTED;
  if (candActive) {                          // erster Start: Kandidaten der Reihe nach probieren
    if (!candStartMs) candStartMs = now;
    if (up) { candActive = false; saveCfg(); wifiCands.clear(); }
    else if (now - candStartMs > 25000UL) {
      if (candIdx + 1 < wifiCands.size()) { applyCand(++candIdx); candStartMs = now; pendingReconnect = true; return; }
      candActive = false; applyCand(0); saveCfg();   // keiner passte: ersten behalten, Zugangspunkt folgt
    }
  }
  if (roamPending) {                         // deutlich staerkeren AP gefunden: direkt wechseln
    roamPending = false; roamCount++;
    wifiWasUp = false; wifiLostMs = now;
    startSTA(false);
    return;
  }
  if (up && cfg.roam && cfg.ssid.length() && !apActive && now - wifiUpMs > 60000UL && now - lastRoamCheck > 300000UL) {
    lastRoamCheck = now;
    if (WiFi.RSSI() < cfg.roamRssi) WiFi.scanNetworksAsync(onRoamScan, false);
  }
  if (up) {
    if (!wifiWasUp) { wifiWasUp = true; wifiUpMs = now; onWifiUp(); }
    if (apActive && now - wifiUpMs > 120000UL) stopAP();
  } else {
    if (wifiWasUp) { wifiWasUp = false; wifiLostMs = now; }
    // An einen bestimmten AP gebunden und der ist seit 30 s weg: neu suchen (sonst bliebe er an diesem AP
    // haengen). wifiLostMs bleibt stehen, damit nach 60 s ohne WLAN trotzdem der Zugangspunkt kommt.
    if (apTarget && !apActive && !candActive && cfg.ssid.length() && now - wifiLostMs > 30000UL
        && now - staStartMs > 30000UL) {
      WiFi.disconnect();
      startSTA();
      return;
    }
    if (!apActive && (!cfg.ssid.length() || (now - wifiLostMs > 60000UL && !candActive))) startAP();
    // Zugangspunkt erreichbar halten: Beim Suchen nach dem Heim-WLAN springt der ESP8266 durch die Kanaele,
    // der AP ist dann kaum zu finden. Deshalb STA nur alle 3 min fuer 30 s versuchen – und nie, solange
    // jemand mit dem AP verbunden ist.
    if (apActive && cfg.ssid.length()) {
      bool client = WiFi.softAPgetStationNum() > 0;
      if (staOn && (client || now - staTryMs > 30000UL)) { WiFi.mode(WIFI_AP); staOn = false; staTryMs = now; }
      else if (!staOn && !client && now - staTryMs > 180000UL) { WiFi.mode(WIFI_AP_STA); startSTA(); staOn = true; staTryMs = now; }
    }
  }
}

// ======================= Auto-Update =======================
// update.json (im Repo-Hauptverzeichnis):
// {"version":"1.4.0","files":{"hermit-1pm":{"url":"bin/hermit-1pm.bin","sha256":"…"}, …}}
// Relative URLs werden relativ zur update.json aufgeloest.
String updAvailVer, updAvailUrl, updAvailSha, updLastErr, updState = "idle";
#if FLASH_1M
#include "upd_handoff.h"
String updLoaderUrl, updLoaderSha;      // 1 MB: Zwischenschritt ueber den Loader
#endif
time_t updLastCheck = 0;
bool updPendingInstall = false, updPendingCheck = false;
String updManualUrl;
uint32_t updNextTryMs = 0, updBootCheckMs = 120000;
int updDoneDay = -1;

int verCmp(const String& a, const String& b) {
  int ia = 0, ib = 0;
  for (int k = 0; k < 3; k++) {
    long va = 0, vb = 0;
    while (ia < (int)a.length() && isdigit(a[ia])) va = va * 10 + (a[ia++] - '0');
    while (ib < (int)b.length() && isdigit(b[ib])) vb = vb * 10 + (b[ib++] - '0');
    if (va != vb) return va < vb ? -1 : 1;
    if (ia < (int)a.length()) ia++;
    if (ib < (int)b.length()) ib++;
  }
  return 0;
}

String resolveUrl(const String& base, const String& rel) {
  if (rel.startsWith("http://") || rel.startsWith("https://")) return rel;
  int q = base.lastIndexOf('/');
  return (q > 8 ? base.substring(0, q + 1) : base + "/") + rel;
}

// Oeffnet HTTP(S)-Verbindung; Client muss bis http.end() leben
bool updOpen(HTTPClient& http, std::unique_ptr<WiFiClient>& cl, const String& url) {
  if (url.startsWith("https://")) {
    auto* sc = new BearSSL::WiFiClientSecure();
    sc->setInsecure();            // Zertifikat wird nicht geprueft – Integritaet ueber SHA-256
    sc->setBufferSizes(16384, 512);
    cl.reset(sc);
  } else cl.reset(new WiFiClient());
  http.setTimeout(15000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(*cl, url)) return false;
  http.setUserAgent("HermitOS/" FW_VERSION);
  if (cfg.updToken.length()) http.addHeader("Authorization", "token " + cfg.updToken);
  return true;
}

bool updCheck() {
  updLastErr = "";
  if (!cfg.updUrl.length()) { updLastErr = "Keine Update-URL"; return false; }
  if (WiFi.status() != WL_CONNECTED) { updLastErr = "Kein WLAN"; return false; }
  HTTPClient http; std::unique_ptr<WiFiClient> cl;
  if (!updOpen(http, cl, cfg.updUrl)) { updLastErr = "URL ungültig"; return false; }
  int code = http.GET();
  if (code != 200) { updLastErr = "update.json: HTTP " + String(code); http.end(); return false; }
  String body = http.getString();
  http.end();
  JsonDocument d;
  if (deserializeJson(d, body)) { updLastErr = "update.json ungültig"; return false; }
  JsonVariantConst f = d["files"][DEV_VARIANT];
  updLastCheck = time(nullptr);
  updAvailVer = ""; updAvailUrl = ""; updAvailSha = "";
  String ver = jstr(d["version"], "");
  if (!ver.length() || !f["url"].is<const char*>()) { updLastErr = "Keine Datei für " DEV_VARIANT; return false; }
#if FLASH_1M
  if (f["loader"].is<const char*>()) {
    updLoaderUrl = resolveUrl(cfg.updUrl, f["loader"].as<const char*>());
    updLoaderSha = jstr(f["loader_sha256"], ""); updLoaderSha.toLowerCase();
  }
#endif
  if (verCmp(ver, FW_VERSION) > 0) {
    updAvailVer = ver;
    updAvailUrl = resolveUrl(cfg.updUrl, f["url"].as<const char*>());
    updAvailSha = jstr(f["sha256"], "");
    updAvailSha.toLowerCase();
  }
  return true;
}

// Laedt und flasht; prueft SHA-256 vor dem Aktivieren. Gibt Fehlertext zurueck oder "".
// Update auch bei eingeschaltetem Relais: der Relais-Pin bleibt ueber den Neustart gehalten (resetPins)
#define UPD_RELAY_BLOCKS 0

String updFlash(const String& url, const String& sha);
String updInstall(const String& url, const String& sha) {
  if (UPD_RELAY_BLOCKS && CH[0].on) return "Relais ist an – Update nur bei ausgeschaltetem Relais";
#if FLASH_1M
  // 1 MB Flash: neue Firmware passt nicht neben die laufende. Erst den Pagurus Loader installieren;
  // er laedt nach dem Neustart die eigentliche Datei (Adresse und SHA-256 im RTC-Speicher).
  // Einstellungen (LittleFS) bleiben dabei erhalten.
  if (!updLoaderUrl.length()) updCheck();
  if (!updLoaderUrl.length()) return "update.json nennt keinen Loader für 1-MB-Geräte";
  if (url.length() >= sizeof(UpdHandoff::url)) return "URL zu lang";
  UpdHandoff h; memset(&h, 0, sizeof h);
  h.magic = UPD_MAGIC;
  updHexToSha(sha, h.sha);
  strncpy(h.url, url.c_str(), sizeof h.url - 1);
  String e = updFlash(updLoaderUrl, updLoaderSha);
  if (e.length()) return "Loader: " + e;
  updHandoffWrite(h);
  // Der Loader kennt nur den WLAN-Speicher des ESP – aktuelle Zugangsdaten dort ablegen
  if (cfg.ssid.length()) {
    struct station_config sc; memset(&sc, 0, sizeof sc);
    strncpy((char*)sc.ssid, cfg.ssid.c_str(), sizeof sc.ssid);
    strncpy((char*)sc.password, cfg.pass.c_str(), sizeof sc.password - 1);
    wifi_station_set_config(&sc);
  }
  updState = "loader";
  return "";
#else
  return updFlash(url, sha);
#endif
}

String updFlash(const String& url, const String& sha) {
  updState = "downloading";
  HTTPClient http; std::unique_ptr<WiFiClient> cl;
  if (!updOpen(http, cl, url)) { updState = "idle"; return "URL ungültig"; }
  int code = http.GET();
  if (code != 200) { http.end(); updState = "idle"; return "Download: HTTP " + String(code); }
  int len = http.getSize();
  if (len <= 0) { http.end(); updState = "idle"; return "Unbekannte Dateigröße"; }
  if (!Update.begin(len)) { http.end(); updState = "idle"; return "Zu wenig Platz für Update"; }
  br_sha256_context ctx; br_sha256_init(&ctx);
  WiFiClient* st = http.getStreamPtr();
  uint8_t buf[1024];
  int left = len;
  uint32_t last = millis();
  while (left > 0 && millis() - last < 20000) {
    size_t av = st->available();
    if (!av) { if (!http.connected()) break; delay(1); continue; }
    int n = st->readBytes(buf, av > sizeof(buf) ? sizeof(buf) : av);
    if (n <= 0) continue;
    br_sha256_update(&ctx, buf, n);
    if (Update.write(buf, n) != (size_t)n) break;
    left -= n; last = millis();
    yield();
  }
  http.end();
  if (left > 0) { Update.end(false); updState = "idle"; return "Download abgebrochen"; }
  uint8_t h[32]; br_sha256_out(&ctx, h);
  char hex[65]; for (int i = 0; i < 32; i++) sprintf(hex + i * 2, "%02x", h[i]);
  if (sha.length() && sha != hex) { Update.end(false); updState = "idle"; return "SHA-256 stimmt nicht – Update verworfen"; }
  if (!Update.end(true)) { updState = "idle"; return "Flash-Fehler " + String(Update.getError()); }
  updState = "done";
  rebootAt = millis() + 500;
  return "";
}

// Zeitplan: Pruefung kurz nach dem Start und einmal taeglich zur eingestellten Stunde
void handleUpdate() {
  uint32_t now = millis();
  if (updPendingCheck) { updPendingCheck = false; updCheck(); }
  if (updPendingInstall) {
    updPendingInstall = false;
    // Vor dem Installieren immer frisch pruefen: alte Treffer zeigen sonst auf eine inzwischen ersetzte
    // Datei in bin/ (SHA-256 passt nicht mehr -> Update wird jedes Mal verworfen)
    if (!updManualUrl.length() && updCheck() && !updAvailVer.length()) { updLastErr = "Keine neuere Version"; return; }
    String url = updManualUrl.length() ? updManualUrl : updAvailUrl;
    String sha = updManualUrl.length() ? String("") : updAvailSha;
    updManualUrl = "";
    if (url.length()) { updLastErr = updInstall(url, sha); }
    return;
  }
  if (cfg.updMode == "off" || WiFi.status() != WL_CONNECTED) return;
  if (updBootCheckMs && now > updBootCheckMs) { updBootCheckMs = 0; updCheck(); }
  if (!validTime()) return;
  time_t t = time(nullptr); struct tm lt; localtime_r(&t, &lt);
  if (lt.tm_hour == cfg.updHour && lt.tm_yday != updDoneDay && (int32_t)(now - updNextTryMs) >= 0) {
    updNextTryMs = now + 600000UL;              // alle 10 min erneut, solange die Stunde laeuft
    updCheck();                                 // immer neu: update.json kann sich seit dem letzten Mal geaendert haben
    if (updAvailVer.length() && cfg.updMode == "install") {
      updLastErr = updInstall(updAvailUrl, updAvailSha);
    }
    if (!updAvailVer.length() || cfg.updMode != "install") updDoneDay = lt.tm_yday;
  }
}

// ======================= Status-Objekte =======================
void deviceInfo(JsonObject o) {
  if (cfg.name.length()) o["name"] = cfg.name; else o["name"] = nullptr;
  o["id"] = devId; o["mac"] = macStr; o["slot"] = 0;
  o["model"] = DEV_MODEL_STR; o["gen"] = 2;
  o["fw_id"] = String("hermit-os/") + FW_VERSION;
  o["ver"] = FW_VERSION; o["app"] = DEV_APP;
  o["auth_en"] = cfg.authPass.length() > 0;
  if (cfg.authPass.length()) o["auth_domain"] = devId; else o["auth_domain"] = nullptr;
  o["type"] = GEN1_TYPE;   // fuer Gen1-Clients
}

void switchStatus(JsonObject o, int c = 0) {
  Chan& k = CH[c];
  o["id"] = c; o["source"] = k.src; o["output"] = k.on;
#if IS_METER
  o["meter_only"] = true;
  o["tripped"] = !k.on;
#endif
  if (k.tAct) {
    o["timer_started_at"] = (long)k.tStartedAt;
    o["timer_duration"] = k.tDurMs / 1000.0;
  }
#if HAS_PM
  o["apower"] = r1(k.apower);
#if HAS_CF1 || PM_ADE
  if (!isnan(k.avolt)) o["voltage"] = k.avolt;
  if (!isnan(k.acurr)) o["current"] = k.acurr;
  if (!isnan(k.avolt) && !isnan(k.acurr) && k.avolt * k.acurr > 1) {
    float pf = isnan(k.pf) ? k.apower / (k.avolt * k.acurr) : k.pf; o["pf"] = roundf((pf > 1 ? 1 : pf) * 100) / 100;
  }
  if (!isnan(k.freq)) o["freq"] = k.freq;
#endif
  JsonObject e = o["aenergy"].to<JsonObject>();
  e["total"] = r3(k.energyWh);
  JsonArray bm = e["by_minute"].to<JsonArray>();
  for (int i = 0; i < 3; i++) bm.add(r3(k.byMinute[i]));
  e["minute_ts"] = (long)minuteTs;
#endif
#if HAS_TEMP
  if (!isnan(tempC)) {
    JsonObject t = o["temperature"].to<JsonObject>();
    t["tC"] = r1(tempC); t["tF"] = r1(tempC * 1.8f + 32);
  }
#endif
  if (overtemp || k.overpower || (PM_ADE && !pmOk())) {
    JsonArray er = o["errors"].to<JsonArray>();
    if (overtemp) er.add("overtemp");
    if (k.overpower) er.add("overpower");
    if (PM_ADE && !pmOk()) er.add("pm_no_response");
  }
}

void switchConfig(JsonObject o, int c = 0) {
  ChCfg& kc = cfg.ch[c];
  o["id"] = c;
  if (kc.name.length()) o["name"] = kc.name; else o["name"] = nullptr;
  o["in_mode"] = kc.inMode; o["initial_state"] = kc.initial;
#if IS_METER
  o["meter_only"] = true;
#endif
  o["auto_on"] = kc.autoOn; o["auto_on_delay"] = kc.autoOnDelay;
  o["auto_off"] = kc.autoOff; o["auto_off_delay"] = kc.autoOffDelay;
#if HAS_PM
  o["power_limit"] = kc.powerLimit;
  JsonObject pc = o["pm_calibration"].to<JsonObject>();
  pc["factor"] = pcalOf(c); pc["source"] = cfg.calSrc; pc["default"] = PWR_CAL_DEFAULT;
#if HAS_CF1 || PM_ADE
  pc["voltage_factor"] = cfg.vCal; pc["current_factor"] = icalOf(c);
#endif
#endif
#if HAS_TEMP
  o["temp_limit"] = cfg.tempLimit;
#endif
}

#if HAS_LIGHT
void cctStatus(JsonObject o) {
  o["id"] = 0; o["source"] = CH[0].src; o["output"] = CH[0].on;
  o["brightness"] = lBri; o["ct"] = lCt;
  if (CH[0].tAct) { o["timer_started_at"] = (long)CH[0].tStartedAt; o["timer_duration"] = CH[0].tDurMs / 1000.0; }
}
void cctConfig(JsonObject o) {
  o["id"] = 0;
  if (cfg.ch[0].name.length()) o["name"] = cfg.ch[0].name; else o["name"] = nullptr;
  o["initial_state"] = cfg.ch[0].initial;
  o["auto_on"] = cfg.ch[0].autoOn; o["auto_on_delay"] = cfg.ch[0].autoOnDelay;
  o["auto_off"] = cfg.ch[0].autoOff; o["auto_off_delay"] = cfg.ch[0].autoOffDelay;
  o["transition_duration"] = cfg.transition;
  JsonObject r = o["ct_range"].to<JsonObject>(); r["min"] = 2700; r["max"] = 6500;
  o["ct_invert"] = cfg.ctInvert;
}
#endif
#if HAS_DIM
void lightStatus(JsonObject o) {
  o["id"] = 0; o["source"] = CH[0].src; o["output"] = CH[0].on;
  o["brightness"] = lBri;
  if (CH[0].tAct) { o["timer_started_at"] = (long)CH[0].tStartedAt; o["timer_duration"] = CH[0].tDurMs / 1000.0; }
  if (fadeMs || rampDir) o["transition"]["target"]["brightness"] = CH[0].on ? lBri : 0;
  o["apower"] = r1(CH[0].apower);
  if (!isnan(CH[0].avolt)) o["voltage"] = CH[0].avolt;
  if (!isnan(CH[0].acurr)) o["current"] = CH[0].acurr;
  JsonObject e = o["aenergy"].to<JsonObject>();
  e["total"] = r3(CH[0].energyWh);
  JsonArray bm = e["by_minute"].to<JsonArray>();
  for (int i = 0; i < 3; i++) bm.add(r3(CH[0].byMinute[i]));
  e["minute_ts"] = (long)minuteTs;
  if (!isnan(tempC)) { JsonObject t = o["temperature"].to<JsonObject>(); t["tC"] = r1(tempC); t["tF"] = r1(tempC * 1.8f + 32); }
  if (overtemp || CH[0].overpower || !stmd.online) {
    JsonArray er = o["errors"].to<JsonArray>();
    if (overtemp) er.add("overtemp");
    if (CH[0].overpower) er.add("overpower");
    if (!stmd.online) er.add("stm_no_response");
  }
}
void lightConfig(JsonObject o) {
  o["id"] = 0;
  if (cfg.ch[0].name.length()) o["name"] = cfg.ch[0].name; else o["name"] = nullptr;
  o["in_mode"] = cfg.ch[0].inMode; o["initial_state"] = cfg.ch[0].initial;
  o["auto_on"] = cfg.ch[0].autoOn; o["auto_on_delay"] = cfg.ch[0].autoOnDelay;
  o["auto_off"] = cfg.ch[0].autoOff; o["auto_off_delay"] = cfg.ch[0].autoOffDelay;
  o["transition_duration"] = cfg.transition;
  JsonArray rm = o["range_map"].to<JsonArray>(); rm.add(cfg.dimMin); rm.add(cfg.dimMax);
  o["leading_edge"] = cfg.leadEdge;
  o["fade_rate"] = cfg.stmFade; o["button_fade_rate"] = cfg.btnFade;
  o["power_limit"] = cfg.ch[0].powerLimit; o["temp_limit"] = cfg.tempLimit;
  JsonObject pc = o["pm_calibration"].to<JsonObject>();
  pc["factor"] = cfg.pwrCal; pc["source"] = cfg.calSrc; pc["default"] = PWR_CAL_DEFAULT;
}
void stmInfo(JsonObject o) {
  o["proto"] = stmd.protoName(); o["online"] = stmd.online;
  if (stmd.proto != stmd.NONE) o["version"] = String(stmd.verMaj) + "." + String(stmd.verMin);
  o["brightness_raw"] = stmd.bri; o["power_raw_w"] = r1(stmd.pW);
  o["power_format"] = stmd.proto == stmd.OPEN ? "open" : (stmd.pwrFmt20 ? "w20" : "auto");
  String hex;
  for (int i = 0; i < stmd.pollLen; i++) { char b[4]; snprintf(b, sizeof b, "%02X ", stmd.pollRaw[i]); hex += b; }
  hex.trim();
  o["state_raw"] = hex;
  o["frames"] = stmd.frames; o["bad_frames"] = stmd.badFrames;
}
#endif
#if IS_COVER
void coverStatus(JsonObject o) {
  o["id"] = 0; o["source"] = cvSrc; o["state"] = CV_NAMES[cvState];
  o["apower"] = r1(coverPower());
  if (!isnan(CH[0].avolt)) o["voltage"] = CH[0].avolt;
  float cur = (isnan(CH[0].acurr) ? 0 : CH[0].acurr) + (isnan(CH[1].acurr) ? 0 : CH[1].acurr);
  o["current"] = roundf(cur * 1000) / 1000;
  float pf = !isnan(CH[0].pf) ? CH[0].pf : CH[1].pf;
  if (!isnan(pf) && coverMoving()) o["pf"] = pf;
  if (!isnan(CH[0].freq)) o["freq"] = CH[0].freq;
  JsonObject e = o["aenergy"].to<JsonObject>();
  e["total"] = r3(CH[0].energyWh + CH[1].energyWh);
  JsonArray bm = e["by_minute"].to<JsonArray>();
  for (int i = 0; i < 3; i++) bm.add(r3(CH[0].byMinute[i] + CH[1].byMinute[i]));
  e["minute_ts"] = (long)minuteTs;
  if (!isnan(tempC)) { JsonObject t = o["temperature"].to<JsonObject>(); t["tC"] = r1(tempC); t["tF"] = r1(tempC * 1.8f + 32); }
  o["pos_control"] = cvPosControl();
  if (cvPosControl()) {
    float p = cvLivePos();
    if (isnan(p)) o["current_pos"] = nullptr; else o["current_pos"] = (int)lroundf(p);
    if (!isnan(cvTarget)) o["target_pos"] = (int)lroundf(cvTarget);
  }
  if (coverMoving()) {
    o["move_timeout"] = cvMaxMs / 1000.0;
    o["move_started_at"] = (long)cvStartedAt;
  }
  o["last_direction"] = cvLastDir > 0 ? "open" : (cvLastDir < 0 ? "close" : (const char*)nullptr);
  if (overtemp || cvOverpower || cvObstructed || !pmOk()) {
    JsonArray er = o["errors"].to<JsonArray>();
    if (overtemp) er.add("overtemp");
    if (cvOverpower) er.add("overpower");
    if (cvObstructed) er.add("obstruction");
    if (!pmOk()) er.add("pm_no_response");
  }
}
void coverConfig(JsonObject o) {
  o["id"] = 0;
  if (cfg.ch[0].name.length()) o["name"] = cfg.ch[0].name; else o["name"] = nullptr;
  o["in_mode"] = cfg.ch[0].inMode; o["initial_state"] = cfg.ch[0].initial;
  o["power_limit"] = cfg.ch[0].powerLimit;
  o["maxtime_open"] = cfg.cvMaxOpen; o["maxtime_close"] = cfg.cvMaxClose;
  o["swap_inputs"] = cfg.cvSwapIn; o["invert_directions"] = cfg.cvInvert;
  JsonObject m = o["motor"].to<JsonObject>();
  m["idle_power_thr"] = cfg.cvIdleThr; m["idle_confirm_period"] = cfg.cvIdleConf;
  JsonObject ob = o["obstruction_detection"].to<JsonObject>();
  ob["enable"] = cfg.cvObst; ob["direction"] = "both"; ob["action"] = cfg.cvObstAction;
  ob["power_thr"] = cfg.cvObstThr; ob["holdoff"] = cfg.cvObstHold;
  JsonObject pc = o["pos_calibration"].to<JsonObject>();
  pc["open_time"] = cfg.cvTOpen; pc["close_time"] = cfg.cvTClose;
  o["temp_limit"] = cfg.tempLimit;
  JsonArray cal = o["pm_calibration"].to<JsonArray>();
  for (int c = 0; c < 2; c++) {
    JsonObject k = cal.add<JsonObject>();
    k["factor"] = pcalOf(c); k["current_factor"] = icalOf(c);
  }
}
#endif
void inputStatus(JsonObject o) { o["id"] = 0; o["state"] = inState; }
#if HAS_IN2
void input2Status(JsonObject o) { o["id"] = 1; o["state"] = in2State; }
void input2Config(JsonObject o) {
  o["id"] = 1;
  if (cfg.in2Name.length()) o["name"] = cfg.in2Name; else o["name"] = nullptr;
  o["type"] = cfg.in2Type; o["invert"] = cfg.in2Invert; o["filter"] = cfg.inFilter;
}
#endif
#if HAS_ADDON
void addonStatus(JsonObject o) { o["id"] = 100; o["state"] = exState; }
void addonConfig(JsonObject o) {
  o["id"] = 100;
  if (cfg.exName.length()) o["name"] = cfg.exName; else o["name"] = nullptr;
  o["type"] = cfg.exType; o["invert"] = cfg.exInvert;
  o["addon_out_invert"] = cfg.exOutLow; o["relay_action"] = cfg.exRelay;
}
#endif
void inputConfig(JsonObject o) {
  o["id"] = 0;
  if (cfg.inName.length()) o["name"] = cfg.inName; else o["name"] = nullptr;
  o["type"] = cfg.inType; o["invert"] = cfg.inInvert; o["filter"] = cfg.inFilter;
}

void sysStatus(JsonObject o) {
  o["mac"] = macStr; o["restart_required"] = false;
  if (validTime()) {
    time_t t = time(nullptr); struct tm lt; localtime_r(&t, &lt);
    char b[6]; snprintf(b, sizeof b, "%02d:%02d", lt.tm_hour, lt.tm_min);
    o["time"] = b; o["unixtime"] = (long)t;
  } else { o["time"] = nullptr; o["unixtime"] = nullptr; }
  o["uptime"] = millis() / 1000;
  o["reset_reason"] = ESP.getResetReason();
  o["ram_size"] = 81920; o["ram_free"] = ESP.getFreeHeap();
  FSInfo fi; LittleFS.info(fi);
  o["fs_size"] = fi.totalBytes; o["fs_free"] = fi.totalBytes - fi.usedBytes;
  o["flash_size"] = ESP.getFlashChipSize();          // laut Firmware-Header
  o["flash_real"] = ESP.getFlashChipRealSize();      // tatsaechlicher Chip (JEDEC-ID)
  o["flash_id"] = String(ESP.getFlashChipId(), HEX);
  o["ota_free"] = ESP.getFreeSketchSpace();
  o["cfg_rev"] = cfg.cfgRev; o["webhook_rev"] = cfg.hookRev;
  JsonObject au = o["available_updates"].to<JsonObject>();
  if (updAvailVer.length()) au["stable"]["version"] = updAvailVer;
}

void sysConfig(JsonObject o) {
  JsonObject d = o["device"].to<JsonObject>();
  if (cfg.name.length()) d["name"] = cfg.name; else d["name"] = nullptr;
  d["mac"] = macStr; d["fw_id"] = String("hermit-os/") + FW_VERSION;
  o["location"]["tz"] = cfg.tz;
  o["sntp"]["server"] = cfg.sntp;
  o["ui"]["led"] = cfg.led;
  JsonObject u = o["updates"].to<JsonObject>();
  u["url"] = cfg.updUrl; u["mode"] = cfg.updMode; u["hour"] = cfg.updHour;
  u["token_set"] = cfg.updToken.length() > 0; u["variant"] = DEV_VARIANT;
  u["state"] = updState; u["last_error"] = updLastErr;
  u["last_check"] = (long)updLastCheck; u["available"] = updAvailVer;
}

void wifiStatus(JsonObject o) {
  bool up = WiFi.status() == WL_CONNECTED;
  if (up) { o["sta_ip"] = WiFi.localIP().toString(); o["status"] = "got ip"; o["ssid"] = WiFi.SSID(); o["rssi"] = WiFi.RSSI();
            o["bssid"] = WiFi.BSSIDstr(); o["channel"] = WiFi.channel(); }
  else { o["sta_ip"] = nullptr; o["status"] = cfg.ssid.length() ? "connecting" : "disconnected"; o["ssid"] = nullptr; o["rssi"] = nullptr; }
  o["ap_active"] = apActive;
  o["roams"] = roamCount;
}

void wifiConfig(JsonObject o) {
  JsonObject s = o["sta"].to<JsonObject>();
  s["ssid"] = cfg.ssid; s["is_open"] = cfg.pass.length() == 0; s["enable"] = cfg.ssid.length() > 0;
  s["ipv4mode"] = cfg.staticIp ? "static" : "dhcp";
  s["ip"] = cfg.ip; s["netmask"] = cfg.mask; s["gw"] = cfg.gw; s["nameserver"] = cfg.dns;
  JsonObject r = o["roam"].to<JsonObject>();
  r["enable"] = cfg.roam; r["rssi_thr"] = cfg.roamRssi; r["interval"] = 300;
  JsonObject a = o["ap"].to<JsonObject>();
  a["ssid"] = devId; a["is_open"] = cfg.apPass.length() < 8;
}

void hookJson(JsonObject o, const Hook& h) {
  o["id"] = h.id; o["cid"] = h.cid; o["enable"] = h.enable; o["event"] = h.event; o["name"] = h.name;
  JsonArray u = o["urls"].to<JsonArray>();
  for (auto& s : h.urls) u.add(s);
  if (h.hasThr) o["threshold"] = h.thr;
}

bool eventSupported(const String& ev) {
  for (auto e : EVENTS) if (ev == e) return true;
  return false;
}

// Gemeinsame Felder fuer Webhook.Create/Update; gibt Fehlertext zurueck oder ""
String applyHook(Hook& h, JsonVariantConst p) {
  if (p["event"].is<const char*>()) {
    String ev = p["event"].as<const char*>();
    if (!eventSupported(ev)) return "Unbekanntes Ereignis: " + ev;
    h.event = ev;
  }
  if (p["cid"].is<int>()) h.cid = p["cid"];
  if (h.event.startsWith("input.")) {
    if (h.cid != 0 && !(HAS_ADDON && h.cid == 100) && !(HAS_IN2 && h.cid == 1))
      return HAS_ADDON ? "cid: 0 (SW) oder 100 (Add-on)" : HAS_IN2 ? "cid: 0 (SW1) oder 1 (SW2)" : "cid muss 0 sein";
  } else if (HAS_RF && (h.event.startsWith("rf.") || h.event.startsWith("switch."))) {
    if (h.event == "rf.unknown") h.cid = 0;
    else if (h.cid < 0 || h.cid > 63) return "cid: Nummer des gespeicherten Codes";
  } else if (h.cid < 0 || h.cid >= NCH || h.event.startsWith("sys.")) h.cid = 0;   // Schalter-Ereignisse: Kanal
  if (p["enable"].is<bool>()) h.enable = p["enable"];
  if (p["name"].is<const char*>()) h.name = p["name"].as<const char*>();
  if (p["urls"].is<JsonArrayConst>()) {
    h.urls.clear();
    for (JsonVariantConst u : p["urls"].as<JsonArrayConst>()) {
      String s = jstr(u, "");
      if (!s.startsWith("http://") && !s.startsWith("https://")) return "URL muss mit http:// oder https:// beginnen";
      if (h.urls.size() < 5) h.urls.push_back(s);
    }
  }
  JsonVariantConst t = p["threshold"].isNull() ? p["condition"] : p["threshold"];
  if (t.is<float>()) { h.hasThr = true; h.thr = t; }
  else if (t.is<const char*>() && String(t.as<const char*>()).length()) { h.hasThr = true; h.thr = String(t.as<const char*>()).toFloat(); }
  if ((h.event == EV_PABOVE || h.event == EV_PBELOW) && !h.hasThr) return "threshold (Watt) fehlt";
  h.armed = (h.event != EV_PBELOW);
  return "";
}

// ======================= MQTT (Gen2-Format) =======================
// Topics wie bei Shelly Plus/Pro:
//   <prefix>/online               "true"/"false" (retained, Last Will)
//   <prefix>/rpc                  eingehende JSON-RPC-Anfragen, Antwort an <src>/rpc
//   <prefix>/events/rpc           NotifyStatus / NotifyEvent
//   <prefix>/status/<komponente>  Status je Komponente (retained)
//   <prefix>/info                 Shelly.GetDeviceInfo + IP (retained, Pagurus-Erweiterung)
WiFiClient mqNet;
PubSubClient mq(mqNet);
uint32_t mqNextTry = 0, mqLastStatus = 0, mqBackoff = 5000;
bool mqDirty = false;

String mqPre() { return cfg.mqPrefix.length() ? cfg.mqPrefix : devId; }
void mqttStatus(JsonObject o) { o["connected"] = mq.connected(); }
void mqttConfig(JsonObject o) {
  o["enable"] = cfg.mqEnable; o["server"] = cfg.mqServer.length() ? cfg.mqServer : (const char*)nullptr;
  o["client_id"] = cfg.mqClientId.length() ? cfg.mqClientId : devId;
  o["user"] = cfg.mqUser.length() ? cfg.mqUser : (const char*)nullptr;
  o["pass_set"] = cfg.mqPass.length() > 0;
  o["topic_prefix"] = mqPre();
  o["rpc_ntf"] = cfg.mqRpcNtf; o["status_ntf"] = cfg.mqStatusNtf;
  o["ssl_ca"] = nullptr;
}
void mqttRestart() { if (mq.connected()) mq.disconnect(); mqNextTry = 0; mqBackoff = 5000; }

bool mqPub(const String& t, JsonDocument& d, bool retain = false) {
  if (!mq.connected()) return false;
  String s; serializeJson(d, s);
  return mq.publish(t.c_str(), (const uint8_t*)s.c_str(), s.length(), retain);
}

// ======================= RPC =======================
#define ERR(c, m) do { err = m; return c; } while (0)

int rpcCall(String m, JsonVariantConst p, JsonDocument& out, String& err, const String& src) {
  m.toLowerCase();

  // ---- Shelly ----
  if (m == "shelly.getdeviceinfo") { deviceInfo(out.to<JsonObject>()); return 0; }
  if (m == "shelly.getstatus") {
    JsonObject o = out.to<JsonObject>();
    sysStatus(o["sys"].to<JsonObject>()); wifiStatus(o["wifi"].to<JsonObject>());
#if HAS_LIGHT
    cctStatus(o["cct:0"].to<JsonObject>());
#elif HAS_DIM
    lightStatus(o["light:0"].to<JsonObject>());
#elif IS_COVER
    coverStatus(o["cover:0"].to<JsonObject>());
#elif HAS_RF
    rfStatus(o["rf"].to<JsonObject>());
    for (auto& e : rfList) if (e.type == "switch") rfSwitchStatus(o[String("switch:") + e.id].to<JsonObject>(), e);
#else
    for (int c = 0; c < NCH; c++) switchStatus(o[String("switch:") + c].to<JsonObject>(), c);
#endif
#if HAS_INPUT
    inputStatus(o["input:0"].to<JsonObject>());
#endif
#if HAS_IN2
    input2Status(o["input:1"].to<JsonObject>());
#endif
#if HAS_ADDON
    addonStatus(o["input:100"].to<JsonObject>());
#endif
    mqttStatus(o["mqtt"].to<JsonObject>());
    return 0;
  }
  if (m == "mqtt.getstatus") { mqttStatus(out.to<JsonObject>()); return 0; }
  if (m == "mqtt.getconfig") { mqttConfig(out.to<JsonObject>()); return 0; }
  if (m == "mqtt.setconfig") {
    JsonVariantConst c = p["config"];
    if (!c.is<JsonObjectConst>()) ERR(-103, "config fehlt");
    if (c["enable"].is<bool>()) cfg.mqEnable = c["enable"];
    if (c["server"].is<const char*>()) cfg.mqServer = c["server"].as<const char*>();
    if (c["user"].is<const char*>()) cfg.mqUser = c["user"].as<const char*>();
    else if (c["user"].isNull() && c.containsKey("user")) cfg.mqUser = "";
    if (c["pass"].is<const char*>()) cfg.mqPass = c["pass"].as<const char*>();
    else if (c["pass"].isNull() && c.containsKey("pass")) cfg.mqPass = "";
    if (c["client_id"].is<const char*>()) cfg.mqClientId = c["client_id"].as<const char*>();
    if (c["topic_prefix"].is<const char*>()) cfg.mqPrefix = c["topic_prefix"].as<const char*>();
    if (c["rpc_ntf"].is<bool>()) cfg.mqRpcNtf = c["rpc_ntf"];
    if (c["status_ntf"].is<bool>()) cfg.mqStatusNtf = c["status_ntf"];
    if (cfg.mqEnable && !cfg.mqServer.length()) ERR(-103, "server (host:port) fehlt");
    saveCfg();
    mqttRestart();
    out["restart_required"] = false;
    return 0;
  }
  if (m == "shelly.getconfig") {
    JsonObject o = out.to<JsonObject>();
    sysConfig(o["sys"].to<JsonObject>()); wifiConfig(o["wifi"].to<JsonObject>());
#if HAS_LIGHT
    cctConfig(o["cct:0"].to<JsonObject>());
#elif HAS_DIM
    lightConfig(o["light:0"].to<JsonObject>());
#elif IS_COVER
    coverConfig(o["cover:0"].to<JsonObject>());
#elif HAS_RF
    { JsonObject r = o["rf"].to<JsonObject>(); r["mode"] = rfMode == 1 ? "protocols" : rfMode == 2 ? "raw" : "standard"; r["repeats"] = rfRepeats;
      JsonArray a = r["entries"].to<JsonArray>(); for (auto& e : rfList) rfEntryJson(a.add<JsonObject>(), e);
      for (auto& e : rfList) if (e.type == "switch") { JsonObject k = o[String("switch:") + e.id].to<JsonObject>(); k["id"] = e.id; k["name"] = e.name; } }
#else
    for (int c = 0; c < NCH; c++) switchConfig(o[String("switch:") + c].to<JsonObject>(), c);
#endif
#if HAS_INPUT
    inputConfig(o["input:0"].to<JsonObject>());
#endif
#if HAS_IN2
    input2Config(o["input:1"].to<JsonObject>());
#endif
#if HAS_ADDON
    addonConfig(o["input:100"].to<JsonObject>());
#endif
    mqttConfig(o["mqtt"].to<JsonObject>());
    return 0;
  }
  if (m == "shelly.listmethods") {
    JsonArray a = out["methods"].to<JsonArray>();
    for (auto s : METHODS) a.add(s);
    return 0;
  }
  if (m == "hermit.gpio") {
    // Diagnose: {"pin":5,"value":512} PWM 0..1023 ausgeben | {"pin":5} Pegel als Eingang lesen
    // | {"scan":true} alle freien Pins als Eingang lesen
    if (p["scan"] | false) {
      JsonObject o = out.to<JsonObject>();
#if HAS_DIM
      const int pins[] = {0, 2, 12, 13, 14, 15};          // 1/3 = UART, 4/5 = STM32, 16 = LED
#else
      const int pins[] = {0, 1, 2, 3, 4, 5, 12, 13, 14, 15, 16};
#endif
      for (int pn : pins) { pinMode(pn, INPUT); o[String("gpio") + pn] = digitalRead(pn); }
      return 0;
    }
    int pin = p["pin"] | -1;
    if (pin < 0 || pin > 16 || (pin >= 6 && pin <= 11)) ERR(-103, "pin: 0-5 oder 12-16");
#if HAS_DIM
    if (pin == 1 || pin == 3 || pin == PIN_STM_BOOT0 || pin == PIN_STM_NRST) ERR(-103, "Pin gehört dem STM32");
#endif
    if (p["value"].is<int>()) {
      int v = p["value"]; v = v < 0 ? 0 : (v > 1023 ? 1023 : v);
      pinMode(pin, OUTPUT); analogWrite(pin, v);
      out["pin"] = pin; out["pwm"] = v;
    } else {
      pinMode(pin, INPUT);
      out["pin"] = pin; out["level"] = digitalRead(pin);
    }
    return 0;
  }
  if (m == "hermit.testimport") {
    // Diagnose: WLAN-Uebernahme erneut ausfuehren (aendert nichts). Passwort wird nie ausgegeben.
    String ss, pw;
    JsonObject mg = out["mgos"].to<JsonObject>();
    ImportIp di;
    bool f = mgosimport::readWifi(ss, pw, &di);
    mg["found"] = f;
    if (f) { mg["ssid"] = ss; mg["pass_len"] = pw.length(); mg["sector"] = String(mgosimport::lastFoundRegion, HEX);
             mg["ip"] = di.ip; mg["netmask"] = di.mask; mg["gw"] = di.gw; mg["dns"] = di.dns; }
    ss = ""; pw = "";
    JsonObject ta = out["tasmota"].to<JsonObject>();
    ImportIp ti;
    f = tasimport::readWifi(ss, pw, &ti);
    ta["found"] = f;
    if (f) { ta["ssid"] = ss; ta["pass_len"] = pw.length(); ta["ip"] = ti.ip; ta["netmask"] = ti.mask; ta["gw"] = ti.gw; }
    struct station_config sc;
    JsonObject sd = out["sdk"].to<JsonObject>();
    if (wifi_station_get_config_default(&sc) && sc.ssid[0]) { char b[33] = {0}; memcpy(b, sc.ssid, 32); sd["ssid"] = b; }
    {
      std::vector<WifiCand> all;
      mgosimport::readAll(all);
      JsonArray a = out["mgos_all"].to<JsonArray>();
      for (auto& c : all) { JsonObject o = a.add<JsonObject>(); o["ssid"] = c.ssid; o["pass_len"] = c.pass.length(); o["src"] = c.src; o["ip"] = c.ip.ip; }
      JsonArray t = out["tried"].to<JsonArray>();
      for (auto& c : wifiCands) { JsonObject o = t.add<JsonObject>(); o["ssid"] = c.ssid; o["pass_len"] = c.pass.length(); o["src"] = c.src; }
    }
    // Rohtext-Probe: kommt "ssid" ueberhaupt in fs0/fs1 vor?
    JsonArray hits = out["raw_ssid_hits"].to<JsonArray>();
    uint8_t* buf = (uint8_t*)malloc(4096);
    if (buf) {
      const uint32_t regs[] = {0xBB000, 0x1BB000};
      for (uint32_t base : regs)
        for (uint32_t off = 0; off < 0x40000 && hits.size() < 8; off += 4096) {
          if (!ESP.flashRead(base + off, (uint32_t*)buf, 4096)) break;
          for (int i = 0; i + 6 < 4096 && hits.size() < 8; i++)
            if (!memcmp(buf + i, "\"ssid\"", 6)) {
              char ctx[40] = {0}; int n = 0;
              for (int k = i; k < 4096 && n < 38; k++) {     // nur bis Ende des SSID-Werts (kein Passwort)
                char c = buf[k]; if (c == ',' || c == '}') break;
                ctx[n++] = (c >= 32 && c < 127) ? c : '.';
              }
              JsonObject h = hits.add<JsonObject>(); h["addr"] = String(base + off + i, HEX); h["text"] = ctx;
            }
        }
      free(buf);
    }
    return 0;
  }
  if (m == "shelly.checkforupdate") {
    if (!updCheck()) ERR(-114, updLastErr);
    if (updAvailVer.length()) out["stable"]["version"] = updAvailVer;
    else out.to<JsonObject>();
    return 0;
  }
  if (m == "shelly.update") {
    // {"stage":"stable"} oder {"url":"http(s)://…/hermit-1pm.bin"}
    if (UPD_RELAY_BLOCKS && CH[0].on) ERR(-114, "Relais ist an – Update nur bei ausgeschaltetem Relais");
    if (p["url"].is<const char*>()) updManualUrl = p["url"].as<const char*>();
    else {
      if (!updAvailVer.length() && !updCheck()) ERR(-114, updLastErr);
      if (!updAvailVer.length()) ERR(-114, "Firmware ist aktuell");
    }
    updPendingInstall = true;
    out.set(nullptr);
    return 0;
  }
  if (m == "shelly.reboot") {
    int d = p["delay_ms"] | 1000;
    rebootAt = millis() + (d < 200 ? 200 : d);
    out.set(nullptr);
    return 0;
  }
  if (m == "shelly.factoryreset") {
    LittleFS.remove("/config.json"); LittleFS.remove("/energy"); LittleFS.remove("/relay");
    LittleFS.remove("/light"); LittleFS.remove("/dim"); LittleFS.remove("/cover");
    { File nf = LittleFS.open("/noimport", "w"); if (nf) nf.close(); }   // kein WLAN-Import nach Werksreset
    ESP.eraseConfig();
    rebootAt = millis() + 500;
    out.set(nullptr);
    return 0;
  }
  if (m == "shelly.setauth") {
    if (p["pass"].isNull()) cfg.authPass = "";
    else if (p["pass"].is<const char*>()) cfg.authPass = p["pass"].as<const char*>();
    else ERR(-103, "pass (String oder null) fehlt");
    saveCfg();
    updater.updateCredentials("admin", cfg.authPass);
    out.set(nullptr);
    return 0;
  }

  // ---- Switch / Input: nur id 0 ----
  int pid = p["id"] | 0;
  if (!HAS_RF && m.startsWith("switch.") && (pid < 0 || pid >= NCH)) ERR(-103, NCH > 1 ? "Ungültige id – Schalter: 0 oder 1" : "Ungültige id – Schalter hat nur id 0");
  if (!HAS_INPUT && m.startsWith("input.")) ERR(404, "Gerät hat keinen Eingang");
  if (m.startsWith("input.") && pid != 0 && !(HAS_ADDON && pid == 100) && !(HAS_IN2 && pid == 1))
    ERR(-103, HAS_ADDON ? "Ungültige id – Eingänge: 0 (SW), 100 (Add-on)" : HAS_IN2 ? "Ungültige id – Eingänge: 0 (SW1), 1 (SW2)" : "Ungültige id – Eingang hat nur id 0");
#if HAS_LIGHT
  if (m.startsWith("switch.")) ERR(404, "Lampe: CCT.* verwenden (wie Shelly Duo Bulb Gen3)");
  if (m.startsWith("cct.") && pid != 0) ERR(-103, "Ungültige id – Lampe hat nur cct:0");
  if (m == "cct.set") {
    // {id, on?, brightness?, ct?, transition_duration?, toggle_after?}
    if (p["on"].isNull() && p["brightness"].isNull() && p["ct"].isNull()) ERR(-103, "on, brightness oder ct angeben");
    int b = p["brightness"].is<float>() ? (int)lroundf(p["brightness"].as<float>()) : -1;
    int c = p["ct"].is<float>() ? (int)lroundf(p["ct"].as<float>()) : -1;
    if (p["transition_duration"].is<float>()) nextTransition = p["transition_duration"].as<float>();
    lightSet(b, c);
    float ta = p["toggle_after"].is<float>() ? p["toggle_after"].as<float>() : -1;
    if (p["on"].is<bool>()) setRelay(p["on"].as<bool>(), src, ta);
    else lightUpdate();
    out.set(nullptr);
    return 0;
  }
  if (m == "cct.toggle") { setRelay(!CH[0].on, src); out.set(nullptr); return 0; }
  if (m == "cct.getstatus") { cctStatus(out.to<JsonObject>()); return 0; }
  if (m == "cct.getconfig") { cctConfig(out.to<JsonObject>()); return 0; }
  if (m == "cct.setconfig") {
    JsonVariantConst c = p["config"];
    if (!c.is<JsonObjectConst>()) ERR(-103, "config fehlt");
    if (c["name"].is<const char*>()) cfg.ch[0].name = c["name"].as<const char*>();
    if (c["initial_state"].is<const char*>()) {
      String v = c["initial_state"].as<const char*>();
      if (v != "off" && v != "on" && v != "restore_last") ERR(-103, "initial_state: off|on|restore_last");
      cfg.ch[0].initial = v;
    }
    if (c["auto_on"].is<bool>()) cfg.ch[0].autoOn = c["auto_on"];
    if (c["auto_on_delay"].is<float>()) cfg.ch[0].autoOnDelay = c["auto_on_delay"];
    if (c["auto_off"].is<bool>()) cfg.ch[0].autoOff = c["auto_off"];
    if (c["auto_off_delay"].is<float>()) cfg.ch[0].autoOffDelay = c["auto_off_delay"];
    if (c["transition_duration"].is<float>()) cfg.transition = c["transition_duration"];
    if (c["ct_invert"].is<bool>()) { cfg.ctInvert = c["ct_invert"]; lightWrite(); }
    saveCfg();
    out["restart_required"] = false;
    return 0;
  }
#endif
#if HAS_RF
  if (m == "rf.getstatus") { rfStatus(out.to<JsonObject>()); return 0; }
  if (m == "rf.getconfig" || m == "rf.setconfig") {
    if (m == "rf.setconfig") {
      JsonVariantConst c = p["config"];
      if (c["mode"].is<const char*>()) {
        String v = c["mode"].as<const char*>();
        if (v != "standard" && v != "protocols" && v != "raw") ERR(-103, "mode: standard|protocols|raw");
        rfMode = v == "protocols" ? 1 : v == "raw" ? 2 : 0;
        efm.setMode(rfMode);
      }
      if (c["repeats"].is<int>()) rfRepeats = constrain(c["repeats"].as<int>(), 1, 40);
      rfSave();
    }
    JsonObject o = out.to<JsonObject>();
    o["mode"] = rfMode == 1 ? "protocols" : rfMode == 2 ? "raw" : "standard"; o["repeats"] = rfRepeats;
    JsonArray a = o["entries"].to<JsonArray>(); for (auto& e : rfList) rfEntryJson(a.add<JsonObject>(), e);
    return 0;
  }
  if (m == "rf.send") {
    // {code} oder {id, on?}: Taste/Kontakt -> code1, Schalter -> an/aus
    if (p["code"].is<const char*>()) {
      if (!rfSendStr(p["code"].as<const char*>(), p["repeats"] | 0)) ERR(-103, "Code ungültig (S:/P:/R: + Hex)");
      out.set(nullptr); return 0;
    }
    RfEntry* e = rfFind(p["id"] | -1);
    if (!e) ERR(-103, "id unbekannt");
    if (e->type == "switch") { if (!rfSwitch(*e, p["on"].is<bool>() ? p["on"].as<bool>() : e->state != 1, src)) ERR(-114, "Kein Code für diesen Zustand"); }
    else if (!rfSendStr(p["on"].is<bool>() && !p["on"].as<bool>() && e->code2.length() ? e->code2 : e->code1)) ERR(-114, "Code ungültig");
    out.set(nullptr); return 0;
  }
  if (m == "rf.add" || m == "rf.update") {
    RfEntry ne, *e = &ne;
    if (m == "rf.update") { e = rfFind(p["id"] | -1); if (!e) ERR(-103, "id unbekannt"); }
    else { if (rfList.size() >= 40) ERR(-114, "Höchstens 40 Einträge"); ne.id = rfFreeId(); }
    if (p["name"].is<const char*>()) e->name = p["name"].as<const char*>();
    if (p["type"].is<const char*>()) {
      String t = p["type"].as<const char*>();
      if (t != "button" && t != "contact" && t != "switch") ERR(-103, "type: button|contact|switch");
      e->type = t;
    }
    RfCode tc;
    if (p["code1"].is<const char*>()) { e->code1 = p["code1"].as<const char*>(); e->code1.toUpperCase(); if (e->code1.length() && !rfFromStr(e->code1, tc)) ERR(-103, "code1 ungültig"); }
    if (p["code2"].is<const char*>()) { e->code2 = p["code2"].as<const char*>(); e->code2.toUpperCase(); if (e->code2.length() && !rfFromStr(e->code2, tc)) ERR(-103, "code2 ungültig"); }
    if (!e->code1.length()) ERR(-103, "code1 fehlt");
    if (m == "rf.add") rfList.push_back(ne);
    rfSave();
    out["id"] = e->id;
    return 0;
  }
  if (m == "rf.delete") {
    int id = p["id"] | -1;
    for (auto it = rfList.begin(); it != rfList.end(); ++it) if (it->id == id) { rfList.erase(it); rfSave(); out.set(nullptr); return 0; }
    ERR(-103, "id unbekannt");
  }
  if (m == "rf.clearrecent") { rfRecent.clear(); out.set(nullptr); return 0; }   // Liste „Zuletzt empfangen“ leeren
  if (m == "rf.beep") { efm.beep(constrain((int)(p["ms"] | 200), 10, 2000)); out.set(nullptr); return 0; }
  if (m.startsWith("switch.")) {
    RfEntry* e = rfFind(pid);
    if (!e || e->type != "switch") ERR(-103, "Kein Funk-Schalter mit dieser id");
    if (m == "switch.getstatus") { rfSwitchStatus(out.to<JsonObject>(), *e); return 0; }
    if (m == "switch.getconfig") { out["id"] = e->id; out["name"] = e->name; return 0; }
    if (m == "switch.setconfig") {
      if (p["config"]["name"].is<const char*>()) { e->name = p["config"]["name"].as<const char*>(); rfSave(); }
      out["restart_required"] = false; return 0;
    }
    bool was = e->state == 1;
    bool on = m == "switch.toggle" ? !was : (p["on"] | false);
    if (m != "switch.toggle" && !p["on"].is<bool>()) ERR(-103, "on fehlt");
    if (!rfSwitch(*e, on, src)) ERR(-114, "Kein Code für diesen Zustand");
    out["was_on"] = was;
    return 0;
  }
#endif
#if IS_COVER
  if (m.startsWith("cover.") && pid != 0) ERR(-103, "Ungültige id – Rollladen hat nur cover:0");
  if (m.startsWith("switch.") && m != "switch.calibrate" && m != "switch.resetcounters")
    ERR(404, "Rollladen-Firmware: Cover.* verwenden (wie Shelly Plus 2PM)");
  if (m == "cover.open" || m == "cover.close") {
    float dur = p["duration"].is<float>() ? p["duration"].as<float>() : 0;
    if (overtemp) ERR(-114, "Übertemperatur – Fahrt gesperrt");
    coverMove(m == "cover.open" ? 1 : -1, src, NAN, dur);
    out.set(nullptr); return 0;
  }
  if (m == "cover.stop") { coverStop(src); out.set(nullptr); return 0; }
  if (m == "cover.gotoposition") {
    if (!cvPosControl()) ERR(-114, "Nicht kalibriert – erst Cover.Calibrate");
    if (isnan(cvPos)) ERR(-114, "Position unbekannt – einmal ganz auf oder zu fahren");
    float pos;
    if (p["pos"].is<float>()) pos = p["pos"];
    else if (p["rel"].is<float>()) pos = cvLivePos() + p["rel"].as<float>();
    else ERR(-103, "pos (0–100) oder rel angeben");
    if (overtemp) ERR(-114, "Übertemperatur – Fahrt gesperrt");
    coverGoTo(pos, src);
    out.set(nullptr); return 0;
  }
  if (m == "cover.calibrate") {
    if (overtemp) ERR(-114, "Übertemperatur – Fahrt gesperrt");
    coverCalibrate(src); out.set(nullptr); return 0;
  }
  if (m == "cover.getstatus") { coverStatus(out.to<JsonObject>()); return 0; }
  if (m == "cover.getconfig") { coverConfig(out.to<JsonObject>()); return 0; }
  if (m == "cover.resetcounters") {
    for (int c = 0; c < NCH; c++) { CH[c].energyWh = 0; CH[c].minuteAccWh = 0; CH[c].byMinute[0] = CH[c].byMinute[1] = CH[c].byMinute[2] = 0; }
    saveEnergy();
    out["aenergy"]["total"] = 0;
    return 0;
  }
  if (m == "cover.setconfig") {
    JsonVariantConst c = p["config"];
    if (!c.is<JsonObjectConst>()) ERR(-103, "config fehlt");
    if (c["name"].is<const char*>()) cfg.ch[0].name = c["name"].as<const char*>();
    if (c["in_mode"].is<const char*>()) {
      String v = c["in_mode"].as<const char*>();
      if (v != "dual" && v != "single" && v != "detached") ERR(-103, "in_mode: dual|single|detached");
      cfg.ch[0].inMode = v;
    }
    if (c["initial_state"].is<const char*>()) {
      String v = c["initial_state"].as<const char*>();
      if (v != "stopped" && v != "open" && v != "closed") ERR(-103, "initial_state: stopped|open|closed");
      cfg.ch[0].initial = v;
    }
    if (c["power_limit"].is<float>()) cfg.ch[0].powerLimit = c["power_limit"];
    if (c["maxtime_open"].is<float>()) cfg.cvMaxOpen = constrain(c["maxtime_open"].as<float>(), 1.0f, 600.0f);
    if (c["maxtime_close"].is<float>()) cfg.cvMaxClose = constrain(c["maxtime_close"].as<float>(), 1.0f, 600.0f);
    if (c["swap_inputs"].is<bool>()) cfg.cvSwapIn = c["swap_inputs"];
    if (c["invert_directions"].is<bool>()) { if (coverMoving()) coverStop(src); cfg.cvInvert = c["invert_directions"]; }
    JsonVariantConst mo = c["motor"];
    if (mo["idle_power_thr"].is<float>()) cfg.cvIdleThr = constrain(mo["idle_power_thr"].as<float>(), 1.0f, 200.0f);
    if (mo["idle_confirm_period"].is<float>()) cfg.cvIdleConf = constrain(mo["idle_confirm_period"].as<float>(), 0.2f, 10.0f);
    JsonVariantConst ob = c["obstruction_detection"];
    if (ob["enable"].is<bool>()) cfg.cvObst = ob["enable"];
    if (ob["action"].is<const char*>()) {
      String v = ob["action"].as<const char*>();
      if (v != "stop" && v != "reverse") ERR(-103, "obstruction_detection.action: stop|reverse");
      cfg.cvObstAction = v;
    }
    if (ob["power_thr"].is<float>()) cfg.cvObstThr = ob["power_thr"];
    if (ob["holdoff"].is<float>()) cfg.cvObstHold = constrain(ob["holdoff"].as<float>(), 0.5f, 30.0f);
    JsonVariantConst pc = c["pos_calibration"];         // Fahrzeiten von Hand (0 = keine Positionssteuerung)
    if (pc["open_time"].is<float>() || pc["close_time"].is<float>()) {
      float to = pc["open_time"] | cfg.cvTOpen, tc = pc["close_time"] | cfg.cvTClose;
      if ((to != 0 && (to < 1 || to > 600)) || (tc != 0 && (tc < 1 || tc > 600))) ERR(-103, "Fahrzeiten 1–600 s oder 0");
      cfg.cvTOpen = to; cfg.cvTClose = tc;
      if (!cvPosControl()) cvPos = NAN;
      cvSave();
    }
    if (c["temp_limit"].is<float>()) cfg.tempLimit = c["temp_limit"];
    saveCfg();
    out["restart_required"] = false;
    return 0;
  }
#endif
#if HAS_DIM
  if (m == "hermit.stm") { stmInfo(out.to<JsonObject>()); return 0; }
  if (m.startsWith("switch.")) ERR(404, "Dimmer: Light.* verwenden (wie Shelly Dimmer Gen3)");
  if (m.startsWith("light.") && pid != 0) ERR(-103, "Ungültige id – Dimmer hat nur light:0");
  if (m == "light.set") {
    // {id, on?, brightness?, transition_duration?, toggle_after?}
    if (p["on"].isNull() && p["brightness"].isNull()) ERR(-103, "on oder brightness angeben");
    bool hasB = p["brightness"].is<float>();
    int b = hasB ? (int)lroundf(p["brightness"].as<float>()) : -1;
    if (p["transition_duration"].is<float>()) nextTransition = p["transition_duration"].as<float>();
    float ta = p["toggle_after"].is<float>() ? p["toggle_after"].as<float>() : -1;
    if (hasB && b <= 0) { setRelay(false, src, ta); out.set(nullptr); return 0; }   // Helligkeit 0 = aus
    if (b > 0) lightSet(b);
    if (p["on"].is<bool>()) {
      bool on = p["on"].as<bool>();
      if (on && overtemp) ERR(-114, "Übertemperatur – Einschalten gesperrt");
      setRelay(on, src, ta);
    } else lightUpdate();
    out.set(nullptr);
    return 0;
  }
  if (m == "light.toggle") {
    if (!CH[0].on && overtemp) ERR(-114, "Übertemperatur – Einschalten gesperrt");
    setRelay(!CH[0].on, src); out.set(nullptr); return 0;
  }
  if (m == "light.dimup" || m == "light.dimdown") {
    int fr = p["fade_rate"] | cfg.btnFade; fr = fr < 1 ? 1 : (fr > 5 ? 5 : fr);
    static const float R[6] = {25, 10, 16, 25, 40, 60};
    bool up = m == "light.dimup";
    if (!CH[0].on) {
      if (!up) { out.set(nullptr); return 0; }
      if (overtemp) ERR(-114, "Übertemperatur – Einschalten gesperrt");
      lBri = 1; nextTransition = 0;
      if (!setRelay(true, src)) ERR(-114, "Einschalten nicht möglich");
    }
    rampBegin(up ? 1 : -1, R[fr]);
    out.set(nullptr);
    return 0;
  }
  if (m == "light.dimstop") { rampStop(); out.set(nullptr); return 0; }
  if (m == "light.getstatus") { lightStatus(out.to<JsonObject>()); return 0; }
  if (m == "light.getconfig") { lightConfig(out.to<JsonObject>()); return 0; }
  if (m == "light.setconfig") {
    JsonVariantConst c = p["config"];
    if (!c.is<JsonObjectConst>()) ERR(-103, "config fehlt");
    if (c["name"].is<const char*>()) cfg.ch[0].name = c["name"].as<const char*>();
    if (c["in_mode"].is<const char*>()) {
      String v = c["in_mode"].as<const char*>();
      if (v != "dim" && v != "dual_dim" && v != "follow" && v != "flip" && v != "momentary" && v != "detached")
        ERR(-103, "in_mode: dim|dual_dim|follow|flip|momentary|detached");
      cfg.ch[0].inMode = v;
    }
    if (c["initial_state"].is<const char*>()) {
      String v = c["initial_state"].as<const char*>();
      if (v != "off" && v != "on" && v != "restore_last") ERR(-103, "initial_state: off|on|restore_last");
      cfg.ch[0].initial = v;
    }
    if (c["auto_on"].is<bool>()) cfg.ch[0].autoOn = c["auto_on"];
    if (c["auto_on_delay"].is<float>()) cfg.ch[0].autoOnDelay = c["auto_on_delay"];
    if (c["auto_off"].is<bool>()) cfg.ch[0].autoOff = c["auto_off"];
    if (c["auto_off_delay"].is<float>()) cfg.ch[0].autoOffDelay = c["auto_off_delay"];
    if (c["transition_duration"].is<float>()) cfg.transition = constrain(c["transition_duration"].as<float>(), 0.0f, 30.0f);
    if (c["range_map"].is<JsonArrayConst>()) {
      int a = c["range_map"][0] | cfg.dimMin, b = c["range_map"][1] | cfg.dimMax;
      if (a < 0 || b > 100 || a >= b) ERR(-103, "range_map: [min, max] in %, 0 ≤ min < max ≤ 100");
      cfg.dimMin = a; cfg.dimMax = b;
    }
    bool stmChg = false;
    if (c["leading_edge"].is<bool>()) { cfg.leadEdge = c["leading_edge"]; stmChg = true; }
    if (c["fade_rate"].is<int>()) { cfg.stmFade = constrain(c["fade_rate"].as<int>(), 1, 5); stmChg = true; }
    if (c["button_fade_rate"].is<int>()) cfg.btnFade = constrain(c["button_fade_rate"].as<int>(), 1, 5);
    if (c["power_limit"].is<float>()) cfg.ch[0].powerLimit = c["power_limit"];
    if (c["temp_limit"].is<float>()) cfg.tempLimit = c["temp_limit"];
    saveCfg();
    stmd.leading = cfg.leadEdge; stmd.fadeRate = cfg.stmFade;
    if (stmChg) stmd.sendSettings(msPause);
    lastHw = 0xFFFF;                         // Helligkeit mit neuer range_map neu senden
    out["restart_required"] = false;
    return 0;
  }
  if (m == "light.resetcounters") m = "switch.resetcounters";
  if (m == "light.calibrate") m = "switch.calibrate";
#endif
#if IS_METER
  if (m == "switch.set" || m == "switch.toggle")
    ERR(-114, "Messfirmware: Relais wird nicht ferngeschaltet, Reset nach Abschaltung nur am Taster/SW");
#endif
  if (m == "switch.set") {
    if (!p["on"].is<bool>()) ERR(-103, "Parameter on (true/false) fehlt");
    bool on = p["on"];
    float ta = p["toggle_after"].is<float>() ? p["toggle_after"].as<float>() : -1;
    if (on && overtemp) ERR(-114, "Übertemperatur – Einschalten gesperrt");
    bool was = CH[pid].on;
    setRelayC(pid, on, src, ta);
    out["was_on"] = was;
    return 0;
  }
  if (m == "switch.toggle") {
    if (!CH[pid].on && overtemp) ERR(-114, "Übertemperatur – Einschalten gesperrt");
    bool was = CH[pid].on;
    setRelayC(pid, !CH[pid].on, src);
    out["was_on"] = was;
    return 0;
  }
  if (m == "switch.getstatus") { switchStatus(out.to<JsonObject>(), pid); return 0; }
  if (m == "switch.getconfig") { switchConfig(out.to<JsonObject>(), pid); return 0; }
  if (m == "switch.setconfig") {
    JsonVariantConst c = p["config"];
    if (!c.is<JsonObjectConst>()) ERR(-103, "config fehlt");
    if (c["name"].is<const char*>()) cfg.ch[pid].name = c["name"].as<const char*>();
    if (c["in_mode"].is<const char*>()) {
      String v = c["in_mode"].as<const char*>();
      if (v != "follow" && v != "flip" && v != "momentary" && v != "detached") ERR(-103, "in_mode: follow|flip|momentary|detached");
      cfg.ch[pid].inMode = v;
    }
    if (c["initial_state"].is<const char*>()) {
      String v = c["initial_state"].as<const char*>();
      if (v != "off" && v != "on" && v != "restore_last" && v != "match_input") ERR(-103, "initial_state: off|on|restore_last|match_input");
      cfg.ch[pid].initial = v;
    }
    if (c["auto_on"].is<bool>()) cfg.ch[pid].autoOn = c["auto_on"];
    if (c["auto_on_delay"].is<float>()) cfg.ch[pid].autoOnDelay = c["auto_on_delay"];
    if (c["auto_off"].is<bool>()) cfg.ch[pid].autoOff = c["auto_off"];
    if (c["auto_off_delay"].is<float>()) cfg.ch[pid].autoOffDelay = c["auto_off_delay"];
    if (c["power_limit"].is<float>()) cfg.ch[pid].powerLimit = c["power_limit"];
    if (c["temp_limit"].is<float>()) cfg.tempLimit = c["temp_limit"];
    saveCfg();
    out["restart_required"] = false;
    return 0;
  }
#if HAS_PM
  if (m == "switch.resetcounters") {
    CH[pid].energyWh = 0; CH[pid].minuteAccWh = 0; CH[pid].byMinute[0] = CH[pid].byMinute[1] = CH[pid].byMinute[2] = 0;
    saveEnergy();
    out["aenergy"]["total"] = 0;
    return 0;
  }
  if (m == "switch.calibrate") {
    // Moeglichkeiten:
    //   {"apower": 1985.4}          Referenzlast laeuft, gemessene Wirkleistung in W
    //   {"tasmota_powercal": 12530} Wert von Tasmota "PowerCal" (BL0937 bzw. HLW8012 beim Plug) uebernehmen
    //   {"factor": 1.7066}          Faktor in W/Hz direkt setzen
    //   {"reset": true}             zurueck auf Standard
#if HAS_CF1 || PM_ADE
    //   {"voltage": 231.5} / {"current": 4.35}   Spannung bzw. Strom mit Messgeraet (Plug S)
    if (p["voltage"].is<float>() || p["current"].is<float>()) {
      if (p["voltage"].is<float>()) {
        float ref = p["voltage"];
        if (isnan(CH[pid].avolt) || CH[pid].avolt < 10 || ref < 50) ERR(-114, "Keine Spannung gemessen");
        cfg.vCal = cfg.vCal * ref / CH[pid].avolt; CH[pid].avolt = ref;
      }
      if (p["current"].is<float>()) {
        float ref = p["current"];
        if (isnan(CH[pid].acurr) || CH[pid].acurr < 0.02f || ref <= 0) ERR(-114, "Kein Strom gemessen – Last anschließen, Relais an");
        icalOf(pid) = icalOf(pid) * ref / CH[pid].acurr; CH[pid].acurr = ref;
      }
      saveCal();
      out["voltage_factor"] = cfg.vCal; out["current_factor"] = icalOf(pid);
      return 0;
    }
#endif
    if (p["reset"].is<bool>() && p["reset"].as<bool>()) {
      pcalOf(pid) = PWR_CAL_DEFAULT; cfg.calSrc = "default";
      cfg.vCal = V_CAL_DEFAULT; icalOf(pid) = I_CAL_DEFAULT;
    } else if (p["tasmota_powercal"].is<float>()) {
      if (PM_STM || PM_ADE) ERR(-103, "Hier nicht möglich – apower (Referenzlast) oder factor verwenden");
      float pc = p["tasmota_powercal"];
      if (pc < 1000 || pc > 100000) ERR(-103, "tasmota_powercal unplausibel (typ. 8000–20000)");
      pcalOf(pid) = PWR_TASMOTA_PREF * pc / 1e7f; cfg.calSrc = "tasmota";
    } else if (p["factor"].is<float>()) {
      float f = p["factor"];
      if (f < 0.05f || f > 100) ERR(-103, "factor unplausibel");
      pcalOf(pid) = f; cfg.calSrc = "manual";
    } else if (p["apower"].is<float>()) {
      float ref = p["apower"];
      if (ref <= 0 || CH[pid].apower < 1) ERR(-114, "Keine Leistung gemessen – Relais an und Last angeschlossen?");
      float nf = pcalOf(pid) * ref / CH[pid].apower;
      if (nf < 0.05f || nf > 100) ERR(-103, "Ergebnis unplausibel – Referenzwert prüfen");
      pcalOf(pid) = nf; cfg.calSrc = "reference";
    } else ERR(-103, "apower, tasmota_powercal, factor oder reset angeben");
    saveCal();
    out["factor"] = pcalOf(pid);
    out["source"] = cfg.calSrc;
    out["default"] = PWR_CAL_DEFAULT;
    return 0;
  }
#endif
#if HAS_ADDON
  if (pid == 100) {
    if (m == "input.getstatus") { addonStatus(out.to<JsonObject>()); return 0; }
    if (m == "input.getconfig") { addonConfig(out.to<JsonObject>()); return 0; }
    if (m == "input.setconfig") {
      JsonVariantConst c = p["config"];
      if (!c.is<JsonObjectConst>()) ERR(-103, "config fehlt");
      if (c["name"].is<const char*>()) cfg.exName = c["name"].as<const char*>();
      if (c["type"].is<const char*>()) {
        String v = c["type"].as<const char*>();
        if (v != "switch" && v != "button") ERR(-103, "type: switch|button");
        cfg.exType = v;
      }
      if (c["relay_action"].is<const char*>()) {
        String v = c["relay_action"].as<const char*>();
        if (v != "none" && v != "momentary" && v != "flip" && v != "follow") ERR(-103, "relay_action: none|momentary|flip|follow");
        cfg.exRelay = v;
      }
      if (c["invert"].is<bool>()) cfg.exInvert = c["invert"];
      if (c["addon_out_invert"].is<bool>()) { cfg.exOutLow = c["addon_out_invert"]; addonRelease(); }
      saveCfg();
      exCand = exState = readAddon();
      out["restart_required"] = false;
      return 0;
    }
  }
  if (m == "sensoraddon.getperipherals") {
    out["digital_in"]["input:100"].to<JsonObject>();
    return 0;
  }
#endif
#if HAS_IN2
  if (pid == 1) {
    if (m == "input.getstatus") { input2Status(out.to<JsonObject>()); return 0; }
    if (m == "input.getconfig") { input2Config(out.to<JsonObject>()); return 0; }
    if (m == "input.setconfig") {
      JsonVariantConst c = p["config"];
      if (!c.is<JsonObjectConst>()) ERR(-103, "config fehlt");
      if (c["name"].is<const char*>()) cfg.in2Name = c["name"].as<const char*>();
      if (c["type"].is<const char*>()) {
        String v = c["type"].as<const char*>();
        if (v != "switch" && v != "button") ERR(-103, "type: switch|button");
        cfg.in2Type = v;
      }
      if (c["invert"].is<bool>()) { cfg.in2Invert = c["invert"]; in2ActiveLevel = cfg.in2Invert ? LOW : HIGH; }
      if (c["filter"].is<const char*>()) {
        String v = c["filter"].as<const char*>();
        if (v != "ac" && v != "dc") ERR(-103, "filter: ac|dc");
        cfg.inFilter = v;
      }
      saveCfg();
      out["restart_required"] = false;
      return 0;
    }
  }
#endif
  if (m == "input.getstatus") { inputStatus(out.to<JsonObject>()); return 0; }
  if (m == "input.getconfig") { inputConfig(out.to<JsonObject>()); return 0; }
#if HAS_INPUT
  if (m == "input.diag") {
    // 200 ms den rohen Pin abtasten (ca. alle 100 µs): Anteil HIGH, Flanken -> Netzfrequenz?
    uint32_t hi = 0, n = 0, edges = 0;
#if HAS_IN2
    const int dpin = pid == 1 ? PIN_INPUT2 : PIN_INPUT;
#else
    const int dpin = PIN_INPUT;
#endif
    int last = (GPI >> dpin) & 1;
    uint32_t t0 = micros();
    while (micros() - t0 < 200000UL) {
      int v = (GPI >> dpin) & 1;
      if (v != last) { edges++; last = v; }
      hi += v; n++;
      delayMicroseconds(100);
    }
    out["samples"] = n; out["high_pct"] = r1(100.0f * hi / n);
    out["edges"] = edges; out["approx_hz"] = r1(edges / 2.0f / 0.2f);
#if HAS_IN2
    if (pid == 1) { out["state"] = in2State; out["filter"] = cfg.inFilter; out["invert"] = cfg.in2Invert; return 0; }
#endif
    out["state"] = inState; out["filter"] = cfg.inFilter; out["invert"] = cfg.inInvert;
    return 0;
  }
#endif
  if (m == "input.setconfig") {
    JsonVariantConst c = p["config"];
    if (!c.is<JsonObjectConst>()) ERR(-103, "config fehlt");
    if (c["name"].is<const char*>()) cfg.inName = c["name"].as<const char*>();
    if (c["type"].is<const char*>()) {
      String v = c["type"].as<const char*>();
      if (v != "switch" && v != "button") ERR(-103, "type: switch|button");
      cfg.inType = v;
    }
    if (c["invert"].is<bool>()) { cfg.inInvert = c["invert"]; inActiveLevel = (cfg.inInvert != (bool)INPUT_ACTIVE_LOW) ? LOW : HIGH; }
    if (c["filter"].is<const char*>()) {
      String v = c["filter"].as<const char*>();
      if (v != "ac" && v != "dc") ERR(-103, "filter: ac|dc");
      cfg.inFilter = v;
    }
    saveCfg();
    out["restart_required"] = false;
    return 0;
  }

  // ---- Sys ----
  if (m == "sys.getstatus") { sysStatus(out.to<JsonObject>()); return 0; }
  if (m == "sys.getconfig") { sysConfig(out.to<JsonObject>()); return 0; }
  if (m == "sys.setconfig") {
    JsonVariantConst c = p["config"];
    if (!c.is<JsonObjectConst>()) ERR(-103, "config fehlt");
    if (c["device"]["name"].is<const char*>()) cfg.name = c["device"]["name"].as<const char*>();
    bool timeChg = false;
    if (c["location"]["tz"].is<const char*>()) { cfg.tz = c["location"]["tz"].as<const char*>(); timeChg = true; }
    if (c["sntp"]["server"].is<const char*>()) { cfg.sntp = c["sntp"]["server"].as<const char*>(); timeChg = true; }
    if (c["ui"]["led"].is<bool>()) { cfg.led = c["ui"]["led"]; updateLed(); }
    JsonVariantConst uc = c["updates"];
    if (uc["url"].is<const char*>()) cfg.updUrl = uc["url"].as<const char*>();
    if (uc["token"].is<const char*>()) cfg.updToken = uc["token"].as<const char*>();
    if (uc["mode"].is<const char*>()) {
      String v = uc["mode"].as<const char*>();
      if (v != "off" && v != "check" && v != "install") ERR(-103, "updates.mode: off|check|install");
      cfg.updMode = v;
    }
    if (uc["hour"].is<int>()) { int h = uc["hour"]; if (h < 0 || h > 23) ERR(-103, "updates.hour: 0–23"); cfg.updHour = h; }
    saveCfg();
    if (timeChg && WiFi.status() == WL_CONNECTED) configTime(cfg.tz.c_str(), cfg.sntp.c_str());
    out["restart_required"] = false;
    return 0;
  }

  // ---- WiFi ----
  if (m == "wifi.getstatus") { wifiStatus(out.to<JsonObject>()); return 0; }
  if (m == "wifi.getconfig") { wifiConfig(out.to<JsonObject>()); return 0; }
  if (m == "wifi.setconfig") {
    JsonVariantConst c = p["config"];
    if (!c.is<JsonObjectConst>()) ERR(-103, "config fehlt");
    JsonVariantConst s = c["sta"];
    if (s.is<JsonObjectConst>()) {
      if (s["ssid"].is<const char*>()) cfg.ssid = s["ssid"].as<const char*>();
      if (s["pass"].is<const char*>()) cfg.pass = s["pass"].as<const char*>();
      if (s["enable"].is<bool>() && !s["enable"].as<bool>()) cfg.ssid = "";
      if (s["ipv4mode"].is<const char*>()) cfg.staticIp = String(s["ipv4mode"].as<const char*>()) == "static";
      if (s["ip"].is<const char*>()) cfg.ip = s["ip"].as<const char*>();
      if (s["netmask"].is<const char*>()) cfg.mask = s["netmask"].as<const char*>();
      if (s["gw"].is<const char*>()) cfg.gw = s["gw"].as<const char*>();
      if (s["nameserver"].is<const char*>()) cfg.dns = s["nameserver"].as<const char*>();
      if (cfg.staticIp) {
        IPAddress t;
        if (!t.fromString(cfg.ip) || !t.fromString(cfg.mask) || !t.fromString(cfg.gw)) ERR(-103, "Statische IP: ip, netmask und gw nötig");
      }
      pendingReconnect = true;
    }
    if (c["ap"]["pass"].is<const char*>()) cfg.apPass = c["ap"]["pass"].as<const char*>();
    if (c["roam"]["enable"].is<bool>()) cfg.roam = c["roam"]["enable"];
    if (c["roam"]["rssi_thr"].is<int>()) cfg.roamRssi = constrain(c["roam"]["rssi_thr"].as<int>(), -90, -50);
    saveCfg();
    out["restart_required"] = false;
    return 0;
  }
  if (m == "wifi.scan") {
    int n = WiFi.scanNetworks();
    JsonArray a = out["results"].to<JsonArray>();
    for (int i = 0; i < n; i++) {
      JsonObject o = a.add<JsonObject>();
      o["ssid"] = WiFi.SSID(i); o["rssi"] = WiFi.RSSI(i); o["channel"] = WiFi.channel(i);
      o["auth"] = WiFi.encryptionType(i) == ENC_TYPE_NONE ? 0 : 1;
    }
    WiFi.scanDelete();
    return 0;
  }

  // ---- Webhook ----
  if (m == "webhook.listsupported") {
    JsonObject t = out["types"].to<JsonObject>();
    for (auto e : EVENTS) t[e].to<JsonObject>();
    return 0;
  }
  if (m == "webhook.list") {
    JsonArray a = out["hooks"].to<JsonArray>();
    for (auto& h : cfg.hooks) hookJson(a.add<JsonObject>(), h);
    out["rev"] = cfg.hookRev;
    return 0;
  }
  if (m == "webhook.create") {
    if (cfg.hooks.size() >= 20) ERR(-109, "Maximal 20 Aktionen");
    if (!p["event"].is<const char*>()) ERR(-103, "event fehlt");
    if (!p["urls"].is<JsonArrayConst>()) ERR(-103, "urls (Array) fehlt");
    Hook h;
    String e = applyHook(h, p);
    if (e.length()) ERR(-103, e);
    h.id = cfg.nextHookId++;
    cfg.hooks.push_back(h);
    cfg.hookRev++;
    saveCfg();
    out["id"] = h.id; out["rev"] = cfg.hookRev;
    return 0;
  }
  if (m == "webhook.update") {
    int id = p["id"] | -1;
    for (auto& h : cfg.hooks) if (h.id == id) {
      Hook tmp = h;
      String e = applyHook(tmp, p);
      if (e.length()) ERR(-103, e);
      h = tmp;
      cfg.hookRev++;
      saveCfg();
      out["rev"] = cfg.hookRev;
      return 0;
    }
    ERR(-105, "Aktion nicht gefunden");
  }
  if (m == "webhook.delete") {
    int id = p["id"] | -1;
    for (size_t i = 0; i < cfg.hooks.size(); i++) if (cfg.hooks[i].id == id) {
      cfg.hooks.erase(cfg.hooks.begin() + i);
      cfg.hookRev++;
      saveCfg();
      out["rev"] = cfg.hookRev;
      return 0;
    }
    ERR(-105, "Aktion nicht gefunden");
  }
  if (m == "webhook.deleteall") {
    cfg.hooks.clear(); cfg.hookRev++; saveCfg();
    out["rev"] = cfg.hookRev;
    return 0;
  }

  err = "No handler for " + m;
  return 404;
}

// ======================= HTTP =======================
void sendJson(int code, const JsonDocument& d) {
  String s;
  serializeJson(d, s);
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(code, "application/json", s);
}

bool checkAuth() {
  if (!cfg.authPass.length()) return true;
  if (server.authenticate("admin", cfg.authPass.c_str())) return true;
  server.requestAuthentication(DIGEST_AUTH, devId.c_str());
  return false;
}

void setArg(JsonVariant dst, const String& s) {
  if (s == "true") { dst.set(true); return; }
  if (s == "false") { dst.set(false); return; }
  if (s == "null") { dst.set(nullptr); return; }
  if (s.length() && (s[0] == '{' || s[0] == '[' || s[0] == '"')) {
    JsonDocument t;
    if (!deserializeJson(t, s)) { dst.set(t.as<JsonVariantConst>()); return; }
  }
  char* end = nullptr;
  const char* c = s.c_str();
  if (s.length()) {
    long l = strtol(c, &end, 10);
    if (*end == 0) { dst.set(l); return; }
    double d = strtod(c, &end);
    if (*end == 0) { dst.set(d); return; }
  }
  dst.set(s);
}

void handleRpcPost() {
  if (!checkAuth()) return;
  JsonDocument req, res, out;
  if (deserializeJson(req, server.arg("plain"))) {
    res["id"] = nullptr; res["src"] = devId;
    res["error"]["code"] = -32700; res["error"]["message"] = "Ungültiges JSON";
    sendJson(200, res);
    return;
  }
  String method = jstr(req["method"], "");
  String err;
  int code = rpcCall(method, req["params"], out, err, "http");
  res["id"] = req["id"];
  res["src"] = devId;
  if (req["src"].is<const char*>()) res["dst"] = req["src"];
  if (code == 0) res["result"] = out;
  else { res["error"]["code"] = code; res["error"]["message"] = err; }
  sendJson(200, res);
}

void handleRpcGet(const String& method) {
  if (!checkAuth()) return;
  JsonDocument params, out, res;
  if (server.method() == HTTP_POST && server.hasArg("plain") && server.arg("plain").length()) {
    deserializeJson(params, server.arg("plain"));
  } else {
    for (int i = 0; i < server.args(); i++) {
      String n = server.argName(i);
      if (n == "plain") continue;
      setArg(params[n], server.arg(i));
    }
  }
  String err;
  int code = rpcCall(method, params, out, err, "http");
  if (code == 0) { sendJson(200, out); return; }
  res["code"] = code; res["message"] = err;
  sendJson(code == 404 ? 404 : (code == -103 ? 400 : 500), res);
}

// ---- Gen1-Kompatibilitaet ----
void gen1Relay(int c = 0) {
  if (!checkAuth()) return;
  Chan& k = CH[c];
  if (server.hasArg("turn")) {
    String t = server.arg("turn");
    float timer = server.hasArg("timer") ? server.arg("timer").toFloat() : -1;
    bool on = t == "on" ? true : t == "off" ? false : !k.on;
    setRelayC(c, on, "http", timer);   // Messfirmware: wird in setRelay ignoriert
  }
  JsonDocument d;
  d["ison"] = k.on; d["has_timer"] = k.tAct;
  d["timer_started"] = k.tAct ? (long)k.tStartedAt : 0;
  d["timer_duration"] = k.tAct ? k.tDurMs / 1000 : 0;
  d["timer_remaining"] = k.tAct ? (k.tDurMs - (millis() - k.tStartMs)) / 1000 : 0;
  d["overpower"] = k.overpower; d["source"] = k.src;
  sendJson(200, d);
}

void gen1Status() {
  if (!checkAuth()) return;
  JsonDocument d;
  JsonObject w = d["wifi_sta"].to<JsonObject>();
  w["connected"] = WiFi.status() == WL_CONNECTED; w["ssid"] = WiFi.SSID();
  w["ip"] = WiFi.localIP().toString(); w["rssi"] = WiFi.RSSI();
#if HAS_DIM
  JsonObject r = d["lights"].to<JsonArray>().add<JsonObject>();
  r["ison"] = CH[0].on; r["brightness"] = lBri; r["has_timer"] = CH[0].tAct; r["source"] = CH[0].src;
  d["overtemperature"] = overtemp; d["loaderror"] = !stmd.online;
#elif HAS_RF
  JsonArray ra = d["relays"].to<JsonArray>();
  for (auto& e : rfList) if (e.type == "switch") { JsonObject r = ra.add<JsonObject>(); r["id"] = e.id; r["ison"] = e.state == 1; r["source"] = e.src; }
  if (!rfRecent.empty()) { JsonObject lc = d["rfcode"].to<JsonObject>(); lc["code"] = rfRecent[0].code; lc["ago"] = (millis() - rfRecent[0].ms) / 1000; }
#elif IS_COVER
  JsonObject ro = d["rollers"].to<JsonArray>().add<JsonObject>();
  ro["state"] = cvDir > 0 ? "open" : (cvDir < 0 ? "close" : "stop");
  ro["power"] = r1(coverPower()); ro["is_valid"] = pmOk(); ro["safety_switch"] = false; ro["overtemperature"] = overtemp;
  ro["stop_reason"] = cvObstructed ? "obstacle" : (cvOverpower ? "overpower" : "normal");
  ro["last_direction"] = cvLastDir > 0 ? "open" : "close";
  ro["current_pos"] = (cvPosControl() && !isnan(cvPos)) ? (int)lroundf(cvLivePos()) : 101;
  ro["calibrating"] = cvCal != 0; ro["positioning"] = cvPosControl();
#else
  JsonArray ra = d["relays"].to<JsonArray>();
  for (int c = 0; c < NCH; c++) {
    JsonObject r = ra.add<JsonObject>();
    r["ison"] = CH[c].on; r["has_timer"] = CH[c].tAct; r["overpower"] = CH[c].overpower; r["source"] = CH[c].src;
  }
#endif
#if HAS_PM
  JsonArray ma = d["meters"].to<JsonArray>();
  for (int c = 0; c < NCH; c++) {
    JsonObject mt = ma.add<JsonObject>();
    mt["power"] = r1(CH[c].apower); mt["is_valid"] = true; mt["total"] = (long)(CH[c].energyWh * 60);  // Watt-Minuten wie Gen1
  }
#endif
#if HAS_TEMP
  if (!isnan(tempC)) { d["temperature"] = r1(tempC); d["overtemperature"] = overtemp; }
#endif
  JsonArray ins = d["inputs"].to<JsonArray>();
#if HAS_INPUT
  ins.add<JsonObject>()["input"] = inState ? 1 : 0;
#endif
#if HAS_IN2
  ins.add<JsonObject>()["input"] = in2State ? 1 : 0;
#endif
#if HAS_ADDON
  ins.add<JsonObject>()["input"] = exState ? 1 : 0;
#endif
  d["mac"] = macStr; d["uptime"] = millis() / 1000;
  d["ram_free"] = ESP.getFreeHeap();
  sendJson(200, d);
}

void handleNotFound() {
  String uri = server.uri();
  if (uri.startsWith("/rpc/")) { handleRpcGet(uri.substring(5)); return; }
#if IS_COVER
  if (uri == "/roller/0" || uri == "/roller") {
    // Gen1: go=open|close|stop|to_pos (roller_pos=0..100), duration=s
    if (!checkAuth()) return;
    String go = server.arg("go");
    float dur = server.hasArg("duration") ? server.arg("duration").toFloat() : 0;
    if (go == "open") coverMove(1, "http", NAN, dur);
    else if (go == "close") coverMove(-1, "http", NAN, dur);
    else if (go == "stop") coverStop("http");
    else if (go == "to_pos" && server.hasArg("roller_pos")) coverGoTo(server.arg("roller_pos").toFloat(), "http");
    else if (server.hasArg("roller_pos")) coverGoTo(server.arg("roller_pos").toFloat(), "http");
    JsonDocument d;
    d["state"] = cvDir > 0 ? "open" : (cvDir < 0 ? "close" : "stop");
    d["power"] = r1(coverPower()); d["last_direction"] = cvLastDir > 0 ? "open" : "close";
    d["current_pos"] = (cvPosControl() && !isnan(cvPos)) ? (int)lroundf(cvLivePos()) : 101;
    d["positioning"] = cvPosControl(); d["calibrating"] = cvCal != 0;
    sendJson(200, d);
    return;
  }
#else
  if (uri == "/relay/0" || uri == "/relay") { gen1Relay(0); return; }
#if NCH > 1
  if (uri == "/relay/1") { gen1Relay(1); return; }
#endif
#endif
#if HAS_LIGHT
  if (uri == "/light/0" || uri == "/light") {
    if (!checkAuth()) return;
    int b = server.hasArg("brightness") ? server.arg("brightness").toInt() : -1;
    int c = server.hasArg("temp") ? server.arg("temp").toInt() : -1;
    if (server.hasArg("transition")) nextTransition = server.arg("transition").toFloat() / 1000.0f;   // Gen1: ms
    if (b >= 0 || c >= 0) lightSet(b, c);
    float timer = server.hasArg("timer") ? server.arg("timer").toFloat() : -1;
    if (server.hasArg("turn")) {
      String t = server.arg("turn");
      setRelay(t == "on" ? true : t == "off" ? false : !CH[0].on, "http", timer);
    } else if (b >= 0 || c >= 0) lightUpdate();
    JsonDocument d;
    d["ison"] = CH[0].on; d["source"] = CH[0].src; d["has_timer"] = CH[0].tAct;
    d["mode"] = "white"; d["brightness"] = lBri; d["white"] = 0; d["temp"] = lCt;
    d["transition"] = (int)(cfg.transition * 1000);
    sendJson(200, d);
    return;
  }
#endif
#if HAS_DIM
  if (uri == "/light/0" || uri == "/light") {
    if (!checkAuth()) return;
    int b = server.hasArg("brightness") ? server.arg("brightness").toInt() : -1;
    if (server.hasArg("transition")) nextTransition = server.arg("transition").toFloat() / 1000.0f;   // Gen1: ms
    if (b > 0) lightSet(b);
    float timer = server.hasArg("timer") ? server.arg("timer").toFloat() : -1;
    if (server.hasArg("turn")) {
      String t = server.arg("turn");
      setRelay(t == "on" ? true : t == "off" ? false : !CH[0].on, "http", timer);
    } else if (b > 0) lightUpdate();
    JsonDocument d;
    d["ison"] = CH[0].on; d["source"] = CH[0].src; d["has_timer"] = CH[0].tAct;
    d["mode"] = "white"; d["brightness"] = lBri;
    d["transition"] = (int)(cfg.transition * 1000);
    sendJson(200, d);
    return;
  }
#endif
  if (uri == "/meter/0" || (NCH > 1 && uri == "/meter/1")) {
    if (!checkAuth()) return;
    int c = uri.endsWith("1") ? 1 : 0;
    JsonDocument d; d["power"] = r1(CH[c].apower); d["total"] = (long)(CH[c].energyWh * 60);
    sendJson(200, d);
    return;
  }
  if (apActive) {   // Captive Portal
    server.sendHeader("Location", "http://192.168.33.1/", true);
    server.send(302, "text/plain", "");
    return;
  }
  server.send(404, "text/plain", "Nicht gefunden");
}

void setupServer() {
  server.on("/", HTTP_GET, []() {
    if (!checkAuth()) return;
    server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
  });
  server.on("/rpc", HTTP_POST, handleRpcPost);
  server.on("/rpc", HTTP_OPTIONS, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    server.send(204);
  });
  server.on("/shelly", HTTP_GET, []() {
    JsonDocument d; deviceInfo(d.to<JsonObject>()); d["auth"] = cfg.authPass.length() > 0; d["fw"] = FW_VERSION;
    sendJson(200, d);
  });
  server.on("/status", HTTP_GET, gen1Status);
  server.onNotFound(handleNotFound);
  updater.setup(&server, "/update", "admin", cfg.authPass);
  server.begin();
}

// Kandidat i als WLAN uebernehmen (feste IP nur, wenn IP, Maske und Gateway zusammenpassen)
void applyCand(size_t i) {
  const WifiCand& c = wifiCands[i];
  cfg.ssid = c.ssid; cfg.pass = c.pass; cfg.staticIp = false;
  IPAddress a, m, g;
  bool ok = c.ip.ip.length() && a.fromString(c.ip.ip) && m.fromString(c.ip.mask) && g.fromString(c.ip.gw)
            && (uint32_t)m != 0 && (uint32_t)a != 0 && (uint32_t)a != (uint32_t)g
            && (((uint32_t)a & (uint32_t)m) == ((uint32_t)g & (uint32_t)m));
  if (ok) {
    cfg.staticIp = true; cfg.ip = c.ip.ip; cfg.mask = c.ip.mask; cfg.gw = c.ip.gw;
    cfg.dns = c.ip.dns.length() ? c.ip.dns : c.ip.gw;
  }
  LOGF("WLAN-Kandidat %u/%u: %s (%s)\n", (unsigned)i + 1, (unsigned)wifiCands.size(), c.ssid.c_str(), c.src.c_str());
}

// ======================= Setup / Loop =======================
bool softReset = false;      // Neustart ohne Stromausfall (Update, Restart, Absturz)
#if HAS_DIM
// STM32-Reset- und Boot-Pins nicht anfassen: der Dimmer leuchtet ueber einen Neustart des ESP weiter
extern "C" void resetPins() {
  for (int i = 0; i <= 16; ++i)
    if (!isFlashInterfacePin(i) && i != PIN_STM_NRST && i != PIN_STM_BOOT0) pinMode(i, INPUT);
}
#endif
#if HAS_RELAY
// Bei einem Software-Neustart (Update, Restart, Absturz) behaelt der ESP8266 seine GPIO-Ausgaenge.
// Der Arduino-Core setzt sie in initPins() aber alle auf INPUT -> Relais faellt ab. Die Original-
// Firmware macht das nicht; deshalb wird der Relais-Pin hier ausgelassen.
extern "C" void resetPins() {
  for (int i = 0; i <= 16; ++i) {
    bool rel = false;
    for (int c = 0; c < NCH; c++) if (i == RELAY_PIN[c]) rel = true;
    if (!isFlashInterfacePin(i) && !rel) pinMode(i, INPUT);
  }
}
bool relayLatched[NCH];      // Relais war vor dem Neustart an
#endif

void setup() {
  {
    uint32_t r = ESP.getResetInfoPtr()->reason;
    softReset = r == REASON_SOFT_RESTART || r == REASON_EXCEPTION_RST || r == REASON_SOFT_WDT_RST;
  }
#if HAS_DIM
  digitalWrite(PIN_STM_BOOT0, LOW);  pinMode(PIN_STM_BOOT0, OUTPUT);   // erst Pegel, dann Ausgang:
  digitalWrite(PIN_STM_NRST, HIGH);  pinMode(PIN_STM_NRST, OUTPUT);    // kein kurzer Reset-Impuls
#endif
#if HAS_RELAY
  for (int c = 0; c < NCH; c++) {
    uint8_t pin = RELAY_PIN[c];
    relayLatched[c] = softReset && (GPE & (1UL << pin)) && (GPO & (1UL << pin));
#if IS_COVER
    relayLatched[c] = false;                 // Rollladen: nach Neustart steht der Motor (Position waere unbekannt)
#endif
    digitalWrite(pin, relayLatched[c] ? HIGH : LOW);
    pinMode(pin, OUTPUT);
  }
#endif
#if HAS_LIGHT
  analogWriteRange(1023); analogWriteFreq(1000);
  pinMode(PIN_PWM_COLD, OUTPUT); pinMode(PIN_PWM_WARM, OUTPUT);
  analogWrite(PIN_PWM_COLD, 0); analogWrite(PIN_PWM_WARM, 0);
#endif
#if HAS_LED
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LED_OFF_LEVEL);
#endif
#ifdef PIN_LED_LINK
  pinMode(PIN_LED_LINK, OUTPUT);
  digitalWrite(PIN_LED_LINK, LED_OFF_LEVEL);
#endif
#ifdef PIN_LED_AUX
  pinMode(PIN_LED_AUX, OUTPUT);
  digitalWrite(PIN_LED_AUX, LED_OFF_LEVEL);
#endif
#if HAS_ADDON
  Serial.begin(115200, SERIAL_8N1, SERIAL_TX_ONLY);   // RX (GPIO3) gehoert dem Add-on
#elif HAS_RF
  Serial.begin(19200);                                 // UART0 gehoert dem EFM8 (Portisch)
#else
  Serial.begin(115200);
#endif
  LOGLN(F("\nHermit OS " FW_VERSION));
  LOGF("Flash: Header %u KB, Chip %u KB, OTA frei %u KB\n",
                ESP.getFlashChipSize() / 1024, ESP.getFlashChipRealSize() / 1024, ESP.getFreeSketchSpace() / 1024);
  if (ESP.getFlashChipRealSize() < ESP.getFlashChipSize())
    LOGLN(F("WARNUNG: Chip kleiner als Firmware-Layout!"));

  // Erster Start (Dateisystem noch nicht formatiert): vorher WLAN der Original-Shelly-Firmware sichern
  std::vector<WifiCand> mgCands;
  // Absturzschutz: zaehlt Neustarts durch Absturz/Watchdog in Folge (RTC-Speicher ueberlebt Soft-Resets)
  {
    struct { uint32_t magic, n; } rb;
    ESP.rtcUserMemoryRead(96, (uint32_t*)&rb, sizeof rb);
    uint32_t r = ESP.getResetInfoPtr()->reason;
    if (rb.magic != 0x48524D54) rb.n = 0;
    rb.magic = 0x48524D54;
    rb.n = (r == REASON_EXCEPTION_RST || r == REASON_SOFT_WDT_RST || r == REASON_WDT_RST) ? rb.n + 1 : 0;
    ESP.rtcUserMemoryWrite(96, (uint32_t*)&rb, sizeof rb);
    safeMode = rb.n >= 3;
    if (safeMode) LOGLN(F("Abgesicherter Start: mehrere Abstuerze in Folge"));
  }
#if FLASH_1M
  // 1 MB: das Dateisystem liegt ueber den Tasmota-Einstellungen – WLAN vor dem Formatieren lesen
  if (!safeMode) { WifiCand c; if (tasimport::readWifi(c.ssid, c.pass, &c.ip)) { c.src = "tasmota"; mgCands.push_back(c); } }
#endif
  if (!LittleFS.begin()) {
#if HAS_MGOS
    if (!safeMode) mgosimport::readAll(mgCands);
#endif
    LittleFS.format(); LittleFS.begin();
  }

  uint8_t mac[6];
  WiFi.macAddress(mac);
  char b[13];
  snprintf(b, sizeof b, "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  macStr = b;
  String lower = macStr; lower.toLowerCase();
  devId = String(DEV_PREFIX) + "-" + lower;

  loadCal();
  bool firstBoot = !LittleFS.exists("/config.json") && !LittleFS.exists("/noimport");
  loadCfg();
  if (firstBoot && !cfg.ssid.length() && !safeMode) {   // nur beim allerersten Start: WLAN (und feste IP) uebernehmen
    // Kandidaten: SDK-Speicher (zuletzt vom Chip benutzt) -> Original-Firmware (beide Bereiche) -> Tasmota
    struct station_config sc;
    if (wifi_station_get_config_default(&sc) && sc.ssid[0]) {
      char ss[33] = {0}, pw[65] = {0};
      memcpy(ss, sc.ssid, 32); memcpy(pw, sc.password, 64);
      WifiCand c; c.ssid = ss; c.pass = pw; c.src = "sdk";
      addCand(wifiCands, c);
    }
    for (auto& c : mgCands) addCand(wifiCands, c);
    { WifiCand c; if (tasimport::readWifi(c.ssid, c.pass, &c.ip)) { c.src = "tasmota"; addCand(wifiCands, c); } }
    if (wifiCands.size() > 1) {
      // Netze, die gerade zu sehen sind, nach vorn (Reihenfolge sonst beibehalten)
      WiFi.mode(WIFI_STA);
      int n = WiFi.scanNetworks(false, true);
      std::vector<WifiCand> vis, rest;
      for (auto& c : wifiCands) {
        bool seen = false;
        for (int k = 0; k < n && !seen; k++) seen = WiFi.SSID(k) == c.ssid;
        (seen ? vis : rest).push_back(c);
      }
      WiFi.scanDelete();
      wifiCands = vis; wifiCands.insert(wifiCands.end(), rest.begin(), rest.end());
    }
    if (!wifiCands.empty()) {
      applyCand(0);
      saveCfg();
      if (wifiCands.size() > 1) { candActive = true; candIdx = 0; }
      else wifiCands.clear();
    }
  }
  loadEnergy();
#if PM_ADE
  ade::init(PIN_SDA, PIN_SCL);
#endif
#ifdef PIN_BTN
  pinMode(PIN_BTN, INPUT_PULLUP);
#endif

#if HAS_ADDON
  pinMode(PIN_ADDON_OUT, OUTPUT);
  addonRelease();
  pinMode(PIN_ADDON_IN, INPUT_PULLUP);
  delay(20);
  exState = exCand = readAddon();
#endif

#if HAS_INPUT
  // Eingang
  inActiveLevel = (cfg.inInvert != (bool)INPUT_ACTIVE_LOW) ? LOW : HIGH;
  pinMode(PIN_INPUT, INPUT_ACTIVE_LOW ? INPUT_PULLUP : INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_INPUT), inIsr, CHANGE);
  inLastActiveMs = millis() - 1000;
  uint32_t t0 = millis();
  while (millis() - t0 < 80) { inState = inCand = sampleInput(millis()); delay(1); }
  inCandMs = millis();
#endif
#if HAS_IN2
  in2ActiveLevel = cfg.in2Invert ? LOW : HIGH;
  pinMode(PIN_INPUT2, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_INPUT2), in2Isr, CHANGE);
  in2LastActiveMs = millis() - 1000;
  { uint32_t t1 = millis(); while (millis() - t1 < 80) { in2State = in2Cand = sampleInput2(millis()); delay(1); } }
  in2CandMs = millis();
#endif
#if HAS_DIM
  // STM32: nach Stromausfall neu starten; nach Neustart des ESP laeuft er weiter und behaelt die Helligkeit
  loadLight();
  bool stmAdopt = false, stmOn = false;
  stmd.begin(&Serial, msClock);
  stmd.leading = cfg.leadEdge; stmd.fadeRate = cfg.stmFade;
  if (softReset) stmd.keepState(); else stmReset();
  {
    uint32_t t1 = millis();
    while (millis() - t1 < 1500 && (stmd.proto == stmd.NONE || !stmd.briValid)) { stmd.tick(true, stmReset, msPause); delay(2); }
  }
  if (softReset && stmd.briValid) {
    stmAdopt = true; stmOn = stmd.bri > 0;
    if (stmOn) { lBri = (int)lroundf(dimPct(stmd.bri)); curBri = lBri; lastHw = stmd.bri; }
  }
#endif
#if HAS_LIGHT
  loadLight();
  powerCycleCheck();
  if (resetWifiPending) { resetWifiPending = false; cfg.ssid = ""; cfg.pass = ""; cfg.staticIp = false; ESP.eraseConfig(); saveCfg(); }
#endif

  // Startzustand Relais
#if IS_COVER
  cvLoad();
  cvRelays(0);
  if (cvPosControl() && !isnan(cvPos)) cvState = cvPos >= 100 ? CV_OPEN : (cvPos <= 0 ? CV_CLOSED : CV_STOPPED);
  if (!softReset) {                          // nach Stromausfall
    if (cfg.ch[0].initial == "open") coverMove(1, "init");
    else if (cfg.ch[0].initial == "closed") coverMove(-1, "init");
  }
#endif
#if HAS_RF
  rfLoad();
  efm.begin(&Serial, []() -> uint32_t { return millis(); }, rfOnCode);
  delay(50);
  efm.setMode(rfMode);
#endif
#if !IS_COVER && !HAS_RF
  for (int c = NCH - 1; c >= 0; c--) {      // Kanal 0 zuletzt (setRelay unten)
  bool start = false;
  const String& ini = cfg.ch[c].initial;
  if (ini == "on") start = true;
  else if (ini == "restore_last") start = loadRelayState(c);
#if HAS_IN2
  else if (ini == "match_input") start = c == 0 ? inState : in2State;
#else
  else if (ini == "match_input") start = inState;
#endif
#if HAS_RELAY
  if (softReset) start = relayLatched[c];   // Neustart ohne Stromausfall: Zustand unveraendert lassen
#endif
#if HAS_DIM
  if (stmAdopt) start = stmOn;
#endif
#if IS_METER
  start = true;
  if (LittleFS.exists("/trip")) {           // Abschaltung vor dem Stromausfall noch nicht quittiert
    File f = LittleFS.open("/trip", "r");
    String r = f ? f.readString() : "";
    if (f) f.close();
    if (r == "overtemp") overtemp = true; else CH[0].overpower = true;
    start = false;
  }
#endif
  setRelayC(c, start, "init");
  }
#endif

#if HAS_PM && !PM_STM && !PM_ADE
  pinMode(PIN_CF, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_CF), cfIsr, RISING);
#if HAS_CF1
  pinMode(PIN_SEL, OUTPUT); digitalWrite(PIN_SEL, SEL_VOLT_LEVEL);   // mit Spannung beginnen
  pinMode(PIN_CF1, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_CF1), cf1Isr, RISING);
  selSinceMs = millis();
#endif
#endif
#if HAS_PM
  lastPmMs = minuteStartMs = lastEnergySaveMs = millis();
#endif

  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.mode(WIFI_STA);
  wifiLostMs = millis();
  if (cfg.ssid.length()) startSTA();
  if (!cfg.ssid.length() || safeMode) startAP();

  setupServer();
  LOGLN("ID: " + devId);
}


// ---- MQTT: Status, Ereignisse, RPC ----
void mqStatusComp(const String& comp) {
  if (!cfg.mqStatusNtf) return;
  JsonDocument d; JsonObject o = d.to<JsonObject>();
  if (comp == "sys") sysStatus(o);
  else if (comp == "wifi") wifiStatus(o);
#if HAS_LIGHT
  else if (comp == "cct:0") cctStatus(o);
#elif HAS_DIM
  else if (comp == "light:0") lightStatus(o);
#elif IS_COVER
  else if (comp == "cover:0") coverStatus(o);
#elif HAS_RF
  else if (comp == "rf") rfStatus(o);
  else if (comp.startsWith("switch:")) { RfEntry* e = rfFind(comp.substring(7).toInt()); if (e) rfSwitchStatus(o, *e); else return; }
#else
  else if (comp.startsWith("switch:") && comp.substring(7).toInt() < NCH) switchStatus(o, comp.substring(7).toInt());
#endif
#if HAS_INPUT
  else if (comp == "input:0") inputStatus(o);
#endif
#if HAS_IN2
  else if (comp == "input:1") input2Status(o);
#endif
#if HAS_ADDON
  else if (comp == "input:100") addonStatus(o);
#endif
  else return;
  mqPub(mqPre() + "/status/" + comp, d, true);
}

void mqStatusAll() {
  static const char* comps[] = {
#if HAS_LIGHT
    "cct:0",
#elif HAS_DIM
    "light:0",
#elif IS_COVER
    "cover:0",
#elif HAS_RF
    "rf",
#else
    "switch:0",
#if NCH > 1
    "switch:1",
#endif
#endif
#if HAS_INPUT
    "input:0",
#endif
#if HAS_IN2
    "input:1",
#endif
#if HAS_ADDON
    "input:100",
#endif
    "sys", "wifi" };
  for (auto c : comps) mqStatusComp(c);
#if HAS_RF
  for (auto& e : rfList) if (e.type == "switch") mqStatusComp(String("switch:") + e.id);
#endif
  if (cfg.mqRpcNtf) {                       // NotifyStatus mit dem Hauptkomponenten-Status
    JsonDocument d;
    d["src"] = devId; d["dst"] = mqPre() + "/events"; d["method"] = "NotifyStatus";
    JsonObject p = d["params"].to<JsonObject>();
    p["ts"] = validTime() ? (double)time(nullptr) : (double)(millis() / 1000);
#if HAS_LIGHT
    cctStatus(p["cct:0"].to<JsonObject>());
#elif HAS_DIM
    lightStatus(p["light:0"].to<JsonObject>());
#elif IS_COVER
    coverStatus(p["cover:0"].to<JsonObject>());
#elif HAS_RF
    for (auto& e : rfList) if (e.type == "switch") rfSwitchStatus(p[String("switch:") + e.id].to<JsonObject>(), e);
#else
    for (int c = 0; c < NCH; c++) switchStatus(p[String("switch:") + c].to<JsonObject>(), c);
#endif
    mqPub(mqPre() + "/events/rpc", d);
  }
  mqLastStatus = millis();
}

void mqInfo() {
  JsonDocument d; JsonObject o = d.to<JsonObject>();
  deviceInfo(o);
  o["ip"] = WiFi.localIP().toString();
  o["name"] = cfg.name.length() ? cfg.name : (const char*)nullptr;
  mqPub(mqPre() + "/info", d, true);
}

void mqttEvent(const String& ev, int cid) {
  if (!cfg.mqEnable || !mq.connected()) return;
  if (cfg.mqRpcNtf) {
    int dot = ev.indexOf('.');
    String comp = dot > 0 ? ev.substring(0, dot) : ev;
    String evn = dot > 0 ? ev.substring(dot + 1) : ev;
    JsonDocument d;
    d["src"] = devId; d["dst"] = mqPre() + "/events"; d["method"] = "NotifyEvent";
    double ts = validTime() ? (double)time(nullptr) : (double)(millis() / 1000);
    d["params"]["ts"] = ts;
    JsonObject e = d["params"]["events"].to<JsonArray>().add<JsonObject>();
    e["component"] = comp == "sys" ? comp : comp + ":" + String(cid);
    e["id"] = cid; e["event"] = evn; e["ts"] = ts;
    mqPub(mqPre() + "/events/rpc", d);
  }
  mqDirty = true;                           // Status zeitnah nachschieben
}

void mqCallback(char* topic, uint8_t* payload, unsigned int len) {
  JsonDocument req, out, res;
  if (deserializeJson(req, payload, len)) return;
  String method = jstr(req["method"], "");
  String src = jstr(req["src"], "");
  if (!method.length()) return;
  String err;
  int code = rpcCall(method, req["params"], out, err, "mqtt");
  if (!src.length()) return;                // ohne src keine Antwort (wie Gen2)
  res["id"] = req["id"]; res["src"] = devId; res["dst"] = src;
  if (code == 0) res["result"] = out;
  else { res["error"]["code"] = code; res["error"]["message"] = err; }
  mqPub(src + "/rpc", res);
  mqDirty = true;
}

void handleMqtt() {
  if (!cfg.mqEnable || safeMode) { if (mq.connected()) mq.disconnect(); return; }
  if (WiFi.status() != WL_CONNECTED) return;
  if (!mq.connected()) {
    if ((int32_t)(millis() - mqNextTry) < 0) return;
    String host = cfg.mqServer; int port = 1883;
    int c = host.lastIndexOf(':');
    if (c > 0) { port = host.substring(c + 1).toInt(); host = host.substring(0, c); }
    if (!host.length()) { mqNextTry = millis() + 30000; return; }
    mq.setServer(host.c_str(), port);
    mq.setBufferSize(2048);
    mq.setKeepAlive(30);            // Broker meldet ein abgezogenes Geraet nach ca. 45 s offline
    mq.setSocketTimeout(5);
    mq.setCallback(mqCallback);
    mqNet.setTimeout(2000);
    String cid = cfg.mqClientId.length() ? cfg.mqClientId : devId;
    String will = mqPre() + "/online";
    bool ok = cfg.mqUser.length()
      ? mq.connect(cid.c_str(), cfg.mqUser.c_str(), cfg.mqPass.c_str(), will.c_str(), 0, true, "false")
      : mq.connect(cid.c_str(), nullptr, nullptr, will.c_str(), 0, true, "false");
    if (!ok) { mqNextTry = millis() + mqBackoff; mqBackoff = min<uint32_t>(mqBackoff * 2, 120000); return; }
    mqBackoff = 5000;
    mq.publish(will.c_str(), "true", true);
    mq.subscribe((mqPre() + "/rpc").c_str());
    mqInfo();
    mqStatusAll();
    return;
  }
  mq.loop();
#if HAS_PM
  const uint32_t iv = 15000;
#else
  const uint32_t iv = 60000;
#endif
  if (mqDirty || millis() - mqLastStatus > iv) { mqDirty = false; mqStatusAll(); }
}

void loop() {
  static bool bootOk = false;
  if (!bootOk && millis() > 60000) {        // eine Minute stabil: Absturzzaehler zuruecksetzen
    bootOk = true;
    struct { uint32_t magic, n; } rb = {0x48524D54, 0};
    ESP.rtcUserMemoryWrite(96, (uint32_t*)&rb, sizeof rb);
  }
  server.handleClient();
  if (apActive) dnsServer.processNextRequest();
  if (mdnsUp) MDNS.update();
  handleWifi();
  handleInput();
  handleInput2();
  handleBoardBtn();
  handleLight();
#if HAS_LIGHT
  if (pcycleClearAt && millis() > pcycleClearAt) { pcycleClearAt = 0; LittleFS.remove("/pcycle"); }
#endif
  handleAddon();
  handleTimer();
  handlePm();
  handleCover();
#if HAS_RF
  handleRf();
#endif
  handleTemp();
  handleLed();
  processQueue();
  handleMqtt();
  handleUpdate();
  if (rebootAt && (int32_t)(millis() - rebootAt) >= 0) {
    saveEnergy();
    delay(100);
    ESP.restart();
  }
}

#endif  // HERMIT_LOADER
