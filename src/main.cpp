// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 D.Koch – Hermit OS
#ifndef HERMIT_LOADER   // Hermit Loader wird separat gebaut (src/loader.cpp)
// Hermit OS – Shelly-Gen2-kompatible Firmware fuer Shelly 1 / 1PM (ESP8266)
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

// Debug-Ausgaben ueber UART0 – beim Dimmer gehoert die Schnittstelle dem STM32
#if HAS_DIM
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

struct Cfg {
  String name;
  String ssid, pass;
  bool staticIp = false;
  bool roam = true;                // bei mehreren Access Points mit gleichem Namen den staerksten nehmen
  int roamRssi = -70;              // unter dieser Signalstaerke alle 5 min nach einem besseren AP suchen
  String ip, mask, gw, dns;
  String apPass;
  String swName, inName;
  String initialState = DEF_INITIAL;     // off | on | restore_last | match_input
  String inMode = DEF_IN_MODE;     // follow | flip | momentary | detached
  String inType = DEF_IN_TYPE;     // switch | button
  bool inInvert = false;
  String inFilter = DEF_IN_FILTER;          // ac = 50-Hz-Halbwellen ueberbruecken | dc = reiner Pegel
  String exName;                   // Add-on-Taster (input:100)
  String exType = "button";        // switch | button
  bool exInvert = false;
  bool exOutLow = false;           // Add-on-Ausgang invertiert (Leitung mit LOW freigeben)
  String exRelay = "none";         // none | momentary | flip | follow
  bool autoOn = false;  float autoOnDelay = 60;
  bool autoOff = false; float autoOffDelay = 60;
  float powerLimit = DEF_POWER_LIMIT;
  float vCal = V_CAL_DEFAULT, iCal = I_CAL_DEFAULT;   // Plug S: Spannung/Strom
  float tempLimit = DEF_TEMP_LIMIT;
  float pwrCal = PWR_CAL_DEFAULT;  // Watt pro Hz am CF-Pin (eigene Datei /calib)
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
  String updUrl = "https://raw.githubusercontent.com/oeltensack/hermit-os/main/update.json";
  String updToken;                 // optional: GitHub-Token fuer privates Repo
  String updMode = "check";        // off | check | install
  int updHour = 3;                 // taegliche Pruefung um diese Stunde
  // MQTT (Gen2-Format) – optional, fuer Hermit Cloud, Home Assistant, Node-RED …
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
#if !HAS_LIGHT
  EV_OVERPOWER, EV_OVERTEMP,
#endif
#if IS_METER
  "switch.reset",
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
#if HAS_DIM
  "Light.Set", "Light.Toggle", "Light.GetStatus", "Light.GetConfig", "Light.SetConfig",
  "Light.DimUp", "Light.DimDown", "Light.DimStop", "Light.ResetCounters", "Light.Calibrate", "Hermit.Stm",
#else
  "Switch.Set", "Switch.Toggle", "Switch.GetStatus", "Switch.GetConfig", "Switch.SetConfig",
#if HAS_PM
  "Switch.ResetCounters", "Switch.Calibrate",
#endif
#endif
  "Input.GetStatus", "Input.GetConfig", "Input.SetConfig", "Input.Diag",
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

bool relayOn = false;
String relaySrc = "init";
bool timerActive = false, timerTarget = false;
uint32_t timerStartMs = 0, timerDurMs = 0;
time_t timerStartedAt = 0;

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
float apower = 0, avolt = NAN, acurr = NAN;
volatile uint32_t cf1LastUs = 0, cf1SumUs = 0, cf1Cnt = 0;
volatile bool cf1Arm = false;
bool selVolt = true; uint32_t selSinceMs = 0;
double energyWh = 0, energySavedWh = 0, minuteAccWh = 0;
float byMinute[3] = {0, 0, 0};
uint32_t minuteStartMs = 0;
time_t minuteTs = 0;
float tempC = NAN;
bool overtemp = false, overpower = false;
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
    f.print(cfg.vCal, 6); f.print('\n'); f.print(cfg.iCal, 6); f.close();
  }
}
void loadCal() {
  File f = LittleFS.open("/calib", "r");
  if (!f) return;
  float v = f.readStringUntil('\n').toFloat();
  String src = f.readStringUntil('\n'); src.trim();
  float vv = f.readStringUntil('\n').toFloat(), ii = f.readStringUntil('\n').toFloat();
  f.close();
  if (vv > 0.001f && vv < 10) cfg.vCal = vv;
  if (ii > 0.0001f && ii < 1) cfg.iCal = ii;
  if (v > 0.05f && v < 100.0f) { cfg.pwrCal = v; cfg.calSrc = src.length() ? src : "manual"; }
}

void saveCfg() {
  cfg.cfgRev++;
  JsonDocument d;
  d["name"] = cfg.name; d["ssid"] = cfg.ssid; d["pass"] = cfg.pass;
  d["roam"] = cfg.roam; d["roamthr"] = cfg.roamRssi;
  d["static"] = cfg.staticIp; d["ip"] = cfg.ip; d["mask"] = cfg.mask; d["gw"] = cfg.gw; d["dns"] = cfg.dns;
  d["appass"] = cfg.apPass; d["swname"] = cfg.swName; d["inname"] = cfg.inName;
  d["initial"] = cfg.initialState; d["inmode"] = cfg.inMode; d["intype"] = cfg.inType; d["ininv"] = cfg.inInvert; d["infilt"] = cfg.inFilter;
  d["exname"] = cfg.exName; d["extype"] = cfg.exType; d["exinv"] = cfg.exInvert; d["exout"] = cfg.exOutLow; d["exrel"] = cfg.exRelay;
  d["aon"] = cfg.autoOn; d["aond"] = cfg.autoOnDelay; d["aoff"] = cfg.autoOff; d["aoffd"] = cfg.autoOffDelay;
  d["plim"] = cfg.powerLimit; d["tlim"] = cfg.tempLimit; d["led"] = cfg.led; d["trans"] = cfg.transition; d["ctinv"] = cfg.ctInvert;
  d["in2name"] = cfg.in2Name; d["in2type"] = cfg.in2Type; d["in2inv"] = cfg.in2Invert;
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
  cfg.apPass = jstr(d["appass"], ""); cfg.swName = jstr(d["swname"], ""); cfg.inName = jstr(d["inname"], "");
  cfg.initialState = jstr(d["initial"], cfg.initialState); cfg.inMode = jstr(d["inmode"], cfg.inMode);
  cfg.inType = jstr(d["intype"], cfg.inType); cfg.inInvert = d["ininv"] | false; cfg.inFilter = jstr(d["infilt"], cfg.inFilter);
  cfg.exName = jstr(d["exname"], ""); cfg.exType = jstr(d["extype"], cfg.exType);
  cfg.exInvert = d["exinv"] | false; cfg.exOutLow = d["exout"] | false; cfg.exRelay = jstr(d["exrel"], cfg.exRelay);
  cfg.autoOn = d["aon"] | false; cfg.autoOnDelay = d["aond"] | 60.0f;
  cfg.autoOff = d["aoff"] | false; cfg.autoOffDelay = d["aoffd"] | 60.0f;
  cfg.powerLimit = d["plim"] | cfg.powerLimit; cfg.tempLimit = d["tlim"] | cfg.tempLimit;
  cfg.led = d["led"] | true;
  cfg.transition = d["trans"] | cfg.transition; cfg.ctInvert = d["ctinv"] | false;
  // Uebernahme aus Version 1.0.0 (Faktor stand in config.json, Standard war falsch 12.53)
  float old = d["pcal"] | 0.0f;
  if (old > 0 && fabsf(old - 12.53f) > 0.001f && !LittleFS.exists("/calib")) { cfg.pwrCal = old; cfg.calSrc = "reference"; saveCal(); }
  cfg.in2Name = jstr(d["in2name"], ""); cfg.in2Type = jstr(d["in2type"], cfg.in2Type); cfg.in2Invert = d["in2inv"] | false;
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

void saveEnergy() {
#if HAS_PM
  File f = LittleFS.open("/energy", "w");
  if (f) { f.print(energyWh, 4); f.close(); energySavedWh = energyWh; }
  lastEnergySaveMs = millis();
#endif
}
void loadEnergy() {
#if HAS_PM
  File f = LittleFS.open("/energy", "r");
  if (f) { energyWh = f.readString().toFloat(); energySavedWh = energyWh; f.close(); }
#endif
}
void saveRelayState() {
  File f = LittleFS.open("/relay", "w");
  if (f) { f.print(relayOn ? '1' : '0'); f.close(); }
}
bool loadRelayState() {
  File f = LittleFS.open("/relay", "r");
  if (!f) return false;
  bool on = f.read() == '1';
  f.close();
  return on;
}

// ======================= Ereignisse / Webhooks =======================
#if HAS_DIM
extern int lBri;
#endif
int h_cid_tmp = 0;
String expandUrl(String u, const String& ev) {
  u.replace("${event}", ev);
  u.replace("${cid}", String(h_cid_tmp));
  u.replace("${output}", relayOn ? "true" : "false");
  u.replace("${input}", inState ? "true" : "false");
  u.replace("${id}", devId);
  u.replace("${name}", cfg.name);
  u.replace("${ip}", WiFi.localIP().toString());
  u.replace("${apower}", String(r1(apower), 1));
  u.replace("${aenergy}", String(energyWh, 3));
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
extern bool relayOn;

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
  toBri = relayOn ? lBri : 0; toCt = lCt;
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
extern bool relayOn;

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
  toBri = relayOn ? lBri : 0;
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
  rampBri = relayOn ? (curBri > 0 ? curBri : lBri) : 1;
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

bool setRelay(bool on, const String& src, float toggleAfter = -1) {
  if (on && overtemp) return false;
#if IS_METER
  // nur Start, Hand-Reset und Schutzabschaltung duerfen das Relais bewegen
  if (!(src == "init" || src == "reset" || src == "overpower" || src == "overtemp")) return false;
  toggleAfter = 0;
#endif
  bool was = relayOn;
  relayOn = on;
  relaySrc = src;
#if HAS_RELAY
  digitalWrite(PIN_RELAY, on ? HIGH : LOW);
#endif
#if HAS_LIGHT || HAS_DIM
  lightUpdate();
#endif
  if (on) overpower = false;
  timerActive = false;
  float ta = toggleAfter;
  if (ta < 0) {
    if (on && cfg.autoOff) ta = cfg.autoOffDelay;
    else if (!on && cfg.autoOn) ta = cfg.autoOnDelay;
    else ta = 0;
  }
  if (ta > 0) {
    timerActive = true; timerTarget = !on;
    timerStartMs = millis(); timerDurMs = (uint32_t)(ta * 1000);
    timerStartedAt = time(nullptr);
  }
  if (on && src == "reset") setTrip(false);
  if (was != on) {
    if (cfg.initialState == "restore_last") saveRelayState();
    fireEvent(on ? EV_ON : EV_OFF);
  }
  updateLed();
  return true;
}

void handleTimer() {
  if (timerActive && millis() - timerStartMs >= timerDurMs) {
    timerActive = false;
    setRelay(timerTarget, "timer");
  }
}

void updateLed() {
#if HAS_LED
  if (apActive) return;
  digitalWrite(PIN_LED, (cfg.led && relayOn) ? LED_ON_LEVEL : LED_OFF_LEVEL);
#endif
}

void handleLed() {
#if HAS_LED
  if (apActive) digitalWrite(PIN_LED, (millis() / 500) & 1 ? LED_ON_LEVEL : LED_OFF_LEVEL);
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
void inputAction(const String& mode, bool st, const char* src) {
  if (mode == "follow") setRelay(st, src);
  else if (mode == "flip") setRelay(!relayOn, src);
  else if (mode == "momentary") { if (st) setRelay(!relayOn, src); }
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

bool dimButtons(int cid) { return cfg.inMode == "dual_dim" || (cfg.inMode == "dim" && cid == 0); }

void dimEdge(int cid, bool st) {
  DimBtn& b = dbt[cid];
  if (dimButtons(cid)) {
    if (st) { b.down = true; b.ramping = false; b.downMs = millis(); return; }
    if (!b.down) return;
    b.down = false;
    if (b.ramping) { b.ramping = false; rampStop(); return; }
    if (cfg.inMode == "dim") setRelay(!relayOn, "button");
    else setRelay(cid == 0, "button");
    return;
  }
  if (cid == 0) inputAction(cfg.inMode, st, cfg.inMode == "momentary" ? "button" : "switch");
}

void dimTick() {
  for (int cid = 0; cid < 2; cid++) {
    DimBtn& b = dbt[cid];
    if (!b.down || b.ramping || millis() - b.downMs < 500 || !dimButtons(cid)) continue;
    int8_t dir;
    if (cfg.inMode == "dual_dim") dir = cid == 0 ? 1 : -1;
    else {
      dir = (!relayOn || dimUpNext) ? 1 : -1;
      if (relayOn && dir > 0 && lBri >= 100) dir = -1;
      if (relayOn && dir < 0 && lBri <= 1) dir = 1;
      dimUpNext = dir < 0;
    }
    if (!relayOn) {
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
  if ((cfg.inType != "button" || st) && !relayOn && setRelay(true, "reset")) fireEvent("switch.reset");
#elif HAS_DIM
  dimEdge(0, st);
#else
  inputAction(cfg.inMode, st, cfg.inMode == "momentary" ? "button" : "switch");
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
#if HAS_PM && !PM_STM
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
  if (selVolt) avolt = r1(cfg.vCal * hz);
  else acurr = (apower > 0.5f) ? roundf(cfg.iCal * hz * 1000) / 1000 : 0;
  selVolt = !selVolt;
  digitalWrite(PIN_SEL, selVolt ? SEL_VOLT_LEVEL : !SEL_VOLT_LEVEL);
  selSinceMs = now;
}
#endif

void handlePm() {
#if HAS_CF1
  handleCf1();
#endif
#if HAS_PM
  uint32_t now = millis();
  if (now - lastPmMs < 1000) return;
  float dt = (now - lastPmMs) / 1000.0f;
  lastPmMs = now;
#if PM_STM
  float p = 0;
  if (stmd.online) {
    if (relayOn || curBri > 0) p = stmd.pW * cfg.pwrCal;
    avolt = isnan(stmd.vV) ? NAN : r1(stmd.vV);
    acurr = isnan(stmd.iA) ? NAN : (p > 0.5f ? roundf(stmd.iA * 1000) / 1000 : 0);
  }
  if (p < 0) p = 0;
#else
  noInterrupts();
  uint32_t sum = cfSumUs, cnt = cfCnt, last = cfLastUs;
  cfSumUs = 0; cfCnt = 0;
  interrupts();
  float p = 0;
  if (relayOn) {
    if (cnt > 0 && sum > 0) p = cfg.pwrCal * (cnt * 1e6f / sum);
    else if (last) {
      uint32_t since = micros() - last;
      if (since < 10000000UL) { float ub = cfg.pwrCal * 1e6f / since; p = apower < ub ? apower : ub; }
    }
  }
#endif
  apower = p;
  double wh = p * dt / 3600.0;
  energyWh += wh; minuteAccWh += wh;
  if (now - minuteStartMs >= 60000) {
    byMinute[2] = byMinute[1]; byMinute[1] = byMinute[0]; byMinute[0] = minuteAccWh * 1000.0;
    minuteAccWh = 0; minuteStartMs = now;
    minuteTs = validTime() ? (time(nullptr) / 60) * 60 : 0;
  }
  if (cfg.powerLimit > 0 && relayOn && apower > cfg.powerLimit) {
    setRelay(false, "overpower", 0);
    overpower = true;
    setTrip(true);
    fireEvent(EV_OVERPOWER);
  }
  for (auto& h : cfg.hooks) {
    if (!h.enable || !h.hasThr) continue;
    if (h.event == EV_PABOVE) {
      if (h.armed && apower > h.thr) { h.armed = false; fireHook(h, h.event); }
      else if (apower < h.thr * 0.95f) h.armed = true;
    } else if (h.event == EV_PBELOW) {
      if (h.armed && apower < h.thr) { h.armed = false; fireHook(h, h.event); }
      else if (apower > h.thr * 1.05f + 0.5f) h.armed = true;
    }
  }
  if (now - lastEnergySaveMs > 900000UL && energyWh - energySavedWh > 0.01) saveEnergy();
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
    setRelay(false, "overtemp", 0);
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

String updInstall(const String& url, const String& sha) {
  if (UPD_RELAY_BLOCKS && relayOn) return "Relais ist an – Update nur bei ausgeschaltetem Relais";
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
    if (!updAvailVer.length()) updCheck();
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

void switchStatus(JsonObject o) {
  o["id"] = 0; o["source"] = relaySrc; o["output"] = relayOn;
#if IS_METER
  o["meter_only"] = true;
  o["tripped"] = !relayOn;
#endif
  if (timerActive) {
    o["timer_started_at"] = (long)timerStartedAt;
    o["timer_duration"] = timerDurMs / 1000.0;
  }
#if HAS_PM
  o["apower"] = r1(apower);
#if HAS_CF1
  if (!isnan(avolt)) o["voltage"] = avolt;
  if (!isnan(acurr)) o["current"] = acurr;
  if (!isnan(avolt) && !isnan(acurr) && avolt * acurr > 1) {
    float pf = apower / (avolt * acurr); o["pf"] = roundf((pf > 1 ? 1 : pf) * 100) / 100;
  }
#endif
  JsonObject e = o["aenergy"].to<JsonObject>();
  e["total"] = r3(energyWh);
  JsonArray bm = e["by_minute"].to<JsonArray>();
  for (int i = 0; i < 3; i++) bm.add(r3(byMinute[i]));
  e["minute_ts"] = (long)minuteTs;
#endif
#if HAS_TEMP
  if (!isnan(tempC)) {
    JsonObject t = o["temperature"].to<JsonObject>();
    t["tC"] = r1(tempC); t["tF"] = r1(tempC * 1.8f + 32);
  }
#endif
  if (overtemp || overpower) {
    JsonArray er = o["errors"].to<JsonArray>();
    if (overtemp) er.add("overtemp");
    if (overpower) er.add("overpower");
  }
}

void switchConfig(JsonObject o) {
  o["id"] = 0;
  if (cfg.swName.length()) o["name"] = cfg.swName; else o["name"] = nullptr;
  o["in_mode"] = cfg.inMode; o["initial_state"] = cfg.initialState;
#if IS_METER
  o["meter_only"] = true;
#endif
  o["auto_on"] = cfg.autoOn; o["auto_on_delay"] = cfg.autoOnDelay;
  o["auto_off"] = cfg.autoOff; o["auto_off_delay"] = cfg.autoOffDelay;
#if HAS_PM
  o["power_limit"] = cfg.powerLimit;
  JsonObject pc = o["pm_calibration"].to<JsonObject>();
  pc["factor"] = cfg.pwrCal; pc["source"] = cfg.calSrc; pc["default"] = PWR_CAL_DEFAULT;
#if HAS_CF1
  pc["voltage_factor"] = cfg.vCal; pc["current_factor"] = cfg.iCal;
#endif
#endif
#if HAS_TEMP
  o["temp_limit"] = cfg.tempLimit;
#endif
}

#if HAS_LIGHT
void cctStatus(JsonObject o) {
  o["id"] = 0; o["source"] = relaySrc; o["output"] = relayOn;
  o["brightness"] = lBri; o["ct"] = lCt;
  if (timerActive) { o["timer_started_at"] = (long)timerStartedAt; o["timer_duration"] = timerDurMs / 1000.0; }
}
void cctConfig(JsonObject o) {
  o["id"] = 0;
  if (cfg.swName.length()) o["name"] = cfg.swName; else o["name"] = nullptr;
  o["initial_state"] = cfg.initialState;
  o["auto_on"] = cfg.autoOn; o["auto_on_delay"] = cfg.autoOnDelay;
  o["auto_off"] = cfg.autoOff; o["auto_off_delay"] = cfg.autoOffDelay;
  o["transition_duration"] = cfg.transition;
  JsonObject r = o["ct_range"].to<JsonObject>(); r["min"] = 2700; r["max"] = 6500;
  o["ct_invert"] = cfg.ctInvert;
}
#endif
#if HAS_DIM
void lightStatus(JsonObject o) {
  o["id"] = 0; o["source"] = relaySrc; o["output"] = relayOn;
  o["brightness"] = lBri;
  if (timerActive) { o["timer_started_at"] = (long)timerStartedAt; o["timer_duration"] = timerDurMs / 1000.0; }
  if (fadeMs || rampDir) o["transition"]["target"]["brightness"] = relayOn ? lBri : 0;
  o["apower"] = r1(apower);
  if (!isnan(avolt)) o["voltage"] = avolt;
  if (!isnan(acurr)) o["current"] = acurr;
  JsonObject e = o["aenergy"].to<JsonObject>();
  e["total"] = r3(energyWh);
  JsonArray bm = e["by_minute"].to<JsonArray>();
  for (int i = 0; i < 3; i++) bm.add(r3(byMinute[i]));
  e["minute_ts"] = (long)minuteTs;
  if (!isnan(tempC)) { JsonObject t = o["temperature"].to<JsonObject>(); t["tC"] = r1(tempC); t["tF"] = r1(tempC * 1.8f + 32); }
  if (overtemp || overpower || !stmd.online) {
    JsonArray er = o["errors"].to<JsonArray>();
    if (overtemp) er.add("overtemp");
    if (overpower) er.add("overpower");
    if (!stmd.online) er.add("stm_no_response");
  }
}
void lightConfig(JsonObject o) {
  o["id"] = 0;
  if (cfg.swName.length()) o["name"] = cfg.swName; else o["name"] = nullptr;
  o["in_mode"] = cfg.inMode; o["initial_state"] = cfg.initialState;
  o["auto_on"] = cfg.autoOn; o["auto_on_delay"] = cfg.autoOnDelay;
  o["auto_off"] = cfg.autoOff; o["auto_off_delay"] = cfg.autoOffDelay;
  o["transition_duration"] = cfg.transition;
  JsonArray rm = o["range_map"].to<JsonArray>(); rm.add(cfg.dimMin); rm.add(cfg.dimMax);
  o["leading_edge"] = cfg.leadEdge;
  o["fade_rate"] = cfg.stmFade; o["button_fade_rate"] = cfg.btnFade;
  o["power_limit"] = cfg.powerLimit; o["temp_limit"] = cfg.tempLimit;
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
  } else h.cid = 0;
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
//   <prefix>/info                 Shelly.GetDeviceInfo + IP (retained, Hermit-Erweiterung)
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
#else
    switchStatus(o["switch:0"].to<JsonObject>());
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
#else
    switchConfig(o["switch:0"].to<JsonObject>());
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
    if (UPD_RELAY_BLOCKS && relayOn) ERR(-114, "Relais ist an – Update nur bei ausgeschaltetem Relais");
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
    LittleFS.remove("/light"); LittleFS.remove("/dim");
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
  if (m.startsWith("switch.") && pid != 0) ERR(-103, "Ungültige id – Schalter hat nur id 0");
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
  if (m == "cct.toggle") { setRelay(!relayOn, src); out.set(nullptr); return 0; }
  if (m == "cct.getstatus") { cctStatus(out.to<JsonObject>()); return 0; }
  if (m == "cct.getconfig") { cctConfig(out.to<JsonObject>()); return 0; }
  if (m == "cct.setconfig") {
    JsonVariantConst c = p["config"];
    if (!c.is<JsonObjectConst>()) ERR(-103, "config fehlt");
    if (c["name"].is<const char*>()) cfg.swName = c["name"].as<const char*>();
    if (c["initial_state"].is<const char*>()) {
      String v = c["initial_state"].as<const char*>();
      if (v != "off" && v != "on" && v != "restore_last") ERR(-103, "initial_state: off|on|restore_last");
      cfg.initialState = v;
    }
    if (c["auto_on"].is<bool>()) cfg.autoOn = c["auto_on"];
    if (c["auto_on_delay"].is<float>()) cfg.autoOnDelay = c["auto_on_delay"];
    if (c["auto_off"].is<bool>()) cfg.autoOff = c["auto_off"];
    if (c["auto_off_delay"].is<float>()) cfg.autoOffDelay = c["auto_off_delay"];
    if (c["transition_duration"].is<float>()) cfg.transition = c["transition_duration"];
    if (c["ct_invert"].is<bool>()) { cfg.ctInvert = c["ct_invert"]; lightWrite(); }
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
    if (!relayOn && overtemp) ERR(-114, "Übertemperatur – Einschalten gesperrt");
    setRelay(!relayOn, src); out.set(nullptr); return 0;
  }
  if (m == "light.dimup" || m == "light.dimdown") {
    int fr = p["fade_rate"] | cfg.btnFade; fr = fr < 1 ? 1 : (fr > 5 ? 5 : fr);
    static const float R[6] = {25, 10, 16, 25, 40, 60};
    bool up = m == "light.dimup";
    if (!relayOn) {
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
    if (c["name"].is<const char*>()) cfg.swName = c["name"].as<const char*>();
    if (c["in_mode"].is<const char*>()) {
      String v = c["in_mode"].as<const char*>();
      if (v != "dim" && v != "dual_dim" && v != "follow" && v != "flip" && v != "momentary" && v != "detached")
        ERR(-103, "in_mode: dim|dual_dim|follow|flip|momentary|detached");
      cfg.inMode = v;
    }
    if (c["initial_state"].is<const char*>()) {
      String v = c["initial_state"].as<const char*>();
      if (v != "off" && v != "on" && v != "restore_last") ERR(-103, "initial_state: off|on|restore_last");
      cfg.initialState = v;
    }
    if (c["auto_on"].is<bool>()) cfg.autoOn = c["auto_on"];
    if (c["auto_on_delay"].is<float>()) cfg.autoOnDelay = c["auto_on_delay"];
    if (c["auto_off"].is<bool>()) cfg.autoOff = c["auto_off"];
    if (c["auto_off_delay"].is<float>()) cfg.autoOffDelay = c["auto_off_delay"];
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
    if (c["power_limit"].is<float>()) cfg.powerLimit = c["power_limit"];
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
    bool was = relayOn;
    setRelay(on, src, ta);
    out["was_on"] = was;
    return 0;
  }
  if (m == "switch.toggle") {
    if (!relayOn && overtemp) ERR(-114, "Übertemperatur – Einschalten gesperrt");
    bool was = relayOn;
    setRelay(!relayOn, src);
    out["was_on"] = was;
    return 0;
  }
  if (m == "switch.getstatus") { switchStatus(out.to<JsonObject>()); return 0; }
  if (m == "switch.getconfig") { switchConfig(out.to<JsonObject>()); return 0; }
  if (m == "switch.setconfig") {
    JsonVariantConst c = p["config"];
    if (!c.is<JsonObjectConst>()) ERR(-103, "config fehlt");
    if (c["name"].is<const char*>()) cfg.swName = c["name"].as<const char*>();
    if (c["in_mode"].is<const char*>()) {
      String v = c["in_mode"].as<const char*>();
      if (v != "follow" && v != "flip" && v != "momentary" && v != "detached") ERR(-103, "in_mode: follow|flip|momentary|detached");
      cfg.inMode = v;
    }
    if (c["initial_state"].is<const char*>()) {
      String v = c["initial_state"].as<const char*>();
      if (v != "off" && v != "on" && v != "restore_last" && v != "match_input") ERR(-103, "initial_state: off|on|restore_last|match_input");
      cfg.initialState = v;
    }
    if (c["auto_on"].is<bool>()) cfg.autoOn = c["auto_on"];
    if (c["auto_on_delay"].is<float>()) cfg.autoOnDelay = c["auto_on_delay"];
    if (c["auto_off"].is<bool>()) cfg.autoOff = c["auto_off"];
    if (c["auto_off_delay"].is<float>()) cfg.autoOffDelay = c["auto_off_delay"];
    if (c["power_limit"].is<float>()) cfg.powerLimit = c["power_limit"];
    if (c["temp_limit"].is<float>()) cfg.tempLimit = c["temp_limit"];
    saveCfg();
    out["restart_required"] = false;
    return 0;
  }
#if HAS_PM
  if (m == "switch.resetcounters") {
    energyWh = 0; minuteAccWh = 0; byMinute[0] = byMinute[1] = byMinute[2] = 0;
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
#if HAS_CF1
    //   {"voltage": 231.5} / {"current": 4.35}   Spannung bzw. Strom mit Messgeraet (Plug S)
    if (p["voltage"].is<float>() || p["current"].is<float>()) {
      if (p["voltage"].is<float>()) {
        float ref = p["voltage"];
        if (isnan(avolt) || avolt < 10 || ref < 50) ERR(-114, "Keine Spannung gemessen");
        cfg.vCal = cfg.vCal * ref / avolt; avolt = ref;
      }
      if (p["current"].is<float>()) {
        float ref = p["current"];
        if (isnan(acurr) || acurr < 0.02f || ref <= 0) ERR(-114, "Kein Strom gemessen – Last anschließen, Relais an");
        cfg.iCal = cfg.iCal * ref / acurr; acurr = ref;
      }
      saveCal();
      out["voltage_factor"] = cfg.vCal; out["current_factor"] = cfg.iCal;
      return 0;
    }
#endif
    if (p["reset"].is<bool>() && p["reset"].as<bool>()) {
      cfg.pwrCal = PWR_CAL_DEFAULT; cfg.calSrc = "default";
      cfg.vCal = V_CAL_DEFAULT; cfg.iCal = I_CAL_DEFAULT;
    } else if (p["tasmota_powercal"].is<float>()) {
      if (PM_STM) ERR(-103, "Dimmer: Messung kommt vom STM32 – apower (Referenz) oder factor verwenden");
      float pc = p["tasmota_powercal"];
      if (pc < 1000 || pc > 100000) ERR(-103, "tasmota_powercal unplausibel (typ. 8000–20000)");
      cfg.pwrCal = PWR_TASMOTA_PREF * pc / 1e7f; cfg.calSrc = "tasmota";
    } else if (p["factor"].is<float>()) {
      float f = p["factor"];
      if (f < 0.05f || f > 100) ERR(-103, "factor unplausibel");
      cfg.pwrCal = f; cfg.calSrc = "manual";
    } else if (p["apower"].is<float>()) {
      float ref = p["apower"];
      if (ref <= 0 || apower < 1) ERR(-114, "Keine Leistung gemessen – Relais an und Last angeschlossen?");
      float nf = cfg.pwrCal * ref / apower;
      if (nf < 0.05f || nf > 100) ERR(-103, "Ergebnis unplausibel – Referenzwert prüfen");
      cfg.pwrCal = nf; cfg.calSrc = "reference";
    } else ERR(-103, "apower, tasmota_powercal, factor oder reset angeben");
    saveCal();
    out["factor"] = cfg.pwrCal;
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
void gen1Relay() {
  if (!checkAuth()) return;
  if (server.hasArg("turn")) {
    String t = server.arg("turn");
    float timer = server.hasArg("timer") ? server.arg("timer").toFloat() : -1;
    bool on = t == "on" ? true : t == "off" ? false : !relayOn;
    setRelay(on, "http", timer);   // Messfirmware: wird in setRelay ignoriert
  }
  JsonDocument d;
  d["ison"] = relayOn; d["has_timer"] = timerActive;
  d["timer_started"] = timerActive ? (long)timerStartedAt : 0;
  d["timer_duration"] = timerActive ? timerDurMs / 1000 : 0;
  d["timer_remaining"] = timerActive ? (timerDurMs - (millis() - timerStartMs)) / 1000 : 0;
  d["overpower"] = overpower; d["source"] = relaySrc;
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
  r["ison"] = relayOn; r["brightness"] = lBri; r["has_timer"] = timerActive; r["source"] = relaySrc;
  d["overtemperature"] = overtemp; d["loaderror"] = !stmd.online;
#else
  JsonObject r = d["relays"].to<JsonArray>().add<JsonObject>();
  r["ison"] = relayOn; r["has_timer"] = timerActive; r["overpower"] = overpower; r["source"] = relaySrc;
#endif
#if HAS_PM
  JsonObject mt = d["meters"].to<JsonArray>().add<JsonObject>();
  mt["power"] = r1(apower); mt["is_valid"] = true; mt["total"] = (long)(energyWh * 60);  // Watt-Minuten wie Gen1
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
  if (uri == "/relay/0" || uri == "/relay") { gen1Relay(); return; }
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
      setRelay(t == "on" ? true : t == "off" ? false : !relayOn, "http", timer);
    } else if (b >= 0 || c >= 0) lightUpdate();
    JsonDocument d;
    d["ison"] = relayOn; d["source"] = relaySrc; d["has_timer"] = timerActive;
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
      setRelay(t == "on" ? true : t == "off" ? false : !relayOn, "http", timer);
    } else if (b > 0) lightUpdate();
    JsonDocument d;
    d["ison"] = relayOn; d["source"] = relaySrc; d["has_timer"] = timerActive;
    d["mode"] = "white"; d["brightness"] = lBri;
    d["transition"] = (int)(cfg.transition * 1000);
    sendJson(200, d);
    return;
  }
#endif
  if (uri == "/meter/0") {
    if (!checkAuth()) return;
    JsonDocument d; d["power"] = r1(apower); d["total"] = (long)(energyWh * 60);
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
  for (int i = 0; i <= 16; ++i)
    if (!isFlashInterfacePin(i) && i != PIN_RELAY) pinMode(i, INPUT);
}
bool relayLatched = false;   // Relais war vor dem Neustart an
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
  {
    relayLatched = softReset && (GPE & (1UL << PIN_RELAY)) && (GPO & (1UL << PIN_RELAY));
  }
  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, relayLatched ? HIGH : LOW);
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
  if (!LittleFS.begin()) {
    if (!safeMode) mgosimport::readAll(mgCands);
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
  bool start = false;
  if (cfg.initialState == "on") start = true;
  else if (cfg.initialState == "restore_last") start = loadRelayState();
  else if (cfg.initialState == "match_input") start = inState;
#if HAS_RELAY
  if (softReset) start = relayLatched;      // Neustart ohne Stromausfall: Zustand unveraendert lassen
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
    if (r == "overtemp") overtemp = true; else overpower = true;
    start = false;
  }
#endif
  setRelay(start, "init");

#if HAS_PM && !PM_STM
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
#else
  else if (comp == "switch:0") switchStatus(o);
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
#else
    "switch:0",
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
  if (cfg.mqRpcNtf) {                       // NotifyStatus mit dem Hauptkomponenten-Status
    JsonDocument d;
    d["src"] = devId; d["dst"] = mqPre() + "/events"; d["method"] = "NotifyStatus";
    JsonObject p = d["params"].to<JsonObject>();
    p["ts"] = validTime() ? (double)time(nullptr) : (double)(millis() / 1000);
#if HAS_LIGHT
    cctStatus(p["cct:0"].to<JsonObject>());
#elif HAS_DIM
    lightStatus(p["light:0"].to<JsonObject>());
#else
    switchStatus(p["switch:0"].to<JsonObject>());
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
  handleLight();
#if HAS_LIGHT
  if (pcycleClearAt && millis() > pcycleClearAt) { pcycleClearAt = 0; LittleFS.remove("/pcycle"); }
#endif
  handleAddon();
  handleTimer();
  handlePm();
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
