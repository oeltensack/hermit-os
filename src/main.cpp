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
#include <time.h>
#include <vector>
#include "device.h"
#include "webui.h"

#ifndef FW_VERSION
#define FW_VERSION "1.4.0"
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
  String ip, mask, gw, dns;
  String apPass;
  String swName, inName;
  String initialState = "off";     // off | on | restore_last | match_input
  String inMode = "follow";        // follow | flip | momentary | detached
  String inType = "switch";        // switch | button
  bool inInvert = false;
  String exName;                   // Add-on-Taster (input:100)
  String exType = "button";        // switch | button
  bool exInvert = false;
  bool exOutLow = false;           // Add-on-Ausgang invertiert (Leitung mit LOW freigeben)
  String exRelay = "none";         // none | momentary | flip | follow
  bool autoOn = false;  float autoOnDelay = 60;
  bool autoOff = false; float autoOffDelay = 60;
  float powerLimit = 3500;
  float tempLimit = 95;
  float pwrCal = PWR_CAL_DEFAULT;  // Watt pro Hz am CF-Pin (eigene Datei /calib)
  String calSrc = "default";       // default | reference | tasmota | manual
  bool led = true;
  String authPass;
  String tz = "CET-1CEST,M3.5.0,M10.5.0/3";
  String sntp = "pool.ntp.org";
  // Auto-Update
  String updUrl = "https://raw.githubusercontent.com/oeltensack/hermit-os/main/update.json";
  String updToken;                 // optional: GitHub-Token fuer privates Repo
  String updMode = "check";        // off | check | install
  int updHour = 3;                 // taegliche Pruefung um diese Stunde
  std::vector<Hook> hooks;
  int nextHookId = 1;
  int hookRev = 0;
  int cfgRev = 0;
} cfg;

static const char* EVENTS[] = {
  "switch.on", "switch.off", "switch.overpower", "switch.overtemperature",
#if HAS_PM
  "switch.power_above", "switch.power_below",
#endif
  "input.toggle_on", "input.toggle_off",
  "input.button_down", "input.button_up", "input.button_push",
  "input.button_longpush", "input.button_doublepush",
  "sys.boot"
};

static const char* METHODS[] = {
  "Shelly.GetDeviceInfo", "Shelly.GetStatus", "Shelly.GetConfig", "Shelly.ListMethods",
  "Shelly.Reboot", "Shelly.FactoryReset", "Shelly.SetAuth",
  "Shelly.CheckForUpdate", "Shelly.Update",
  "Switch.Set", "Switch.Toggle", "Switch.GetStatus", "Switch.GetConfig", "Switch.SetConfig",
#if HAS_PM
  "Switch.ResetCounters", "Switch.Calibrate",
#endif
  "Input.GetStatus", "Input.GetConfig", "Input.SetConfig",
#if HAS_ADDON
  "SensorAddon.GetPeripherals",
#endif
  "Sys.GetStatus", "Sys.GetConfig", "Sys.SetConfig",
  "WiFi.GetStatus", "WiFi.GetConfig", "WiFi.SetConfig", "WiFi.Scan",
  "Webhook.List", "Webhook.Create", "Webhook.Update", "Webhook.Delete",
  "Webhook.DeleteAll", "Webhook.ListSupported"
};

// ======================= Laufzeit =======================
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer updater;
DNSServer dnsServer;

String devId, macStr;
bool apActive = false, wifiWasUp = false, mdnsUp = false, bootEventSent = false, pendingReconnect = false;
uint32_t wifiLostMs = 0, wifiUpMs = 0, rebootAt = 0;

bool relayOn = false;
String relaySrc = "init";
bool timerActive = false, timerTarget = false;
uint32_t timerStartMs = 0, timerDurMs = 0;
time_t timerStartedAt = 0;

volatile bool inPulse = false;
volatile uint8_t inActiveLevel = HIGH;
uint32_t inLastActiveMs = 0, inCandMs = 0;
bool inState = false, inCand = false;
int bootEdges = 0;

volatile uint32_t cfLastUs = 0, cfSumUs = 0, cfCnt = 0;
float apower = 0;
double energyWh = 0, energySavedWh = 0, minuteAccWh = 0;
float byMinute[3] = {0, 0, 0};
uint32_t minuteStartMs = 0;
time_t minuteTs = 0;
float tempC = NAN;
bool overtemp = false, overpower = false;
uint32_t lastPmMs = 0, lastTempMs = 0, lastEnergySaveMs = 0;

std::vector<String> httpQueue;

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
  if (f) { f.print(cfg.pwrCal, 6); f.print('\n'); f.print(cfg.calSrc); f.close(); }
}
void loadCal() {
  File f = LittleFS.open("/calib", "r");
  if (!f) return;
  float v = f.readStringUntil('\n').toFloat();
  String src = f.readStringUntil('\n'); src.trim();
  f.close();
  if (v > 0.05f && v < 100.0f) { cfg.pwrCal = v; cfg.calSrc = src.length() ? src : "manual"; }
}

void saveCfg() {
  cfg.cfgRev++;
  JsonDocument d;
  d["name"] = cfg.name; d["ssid"] = cfg.ssid; d["pass"] = cfg.pass;
  d["static"] = cfg.staticIp; d["ip"] = cfg.ip; d["mask"] = cfg.mask; d["gw"] = cfg.gw; d["dns"] = cfg.dns;
  d["appass"] = cfg.apPass; d["swname"] = cfg.swName; d["inname"] = cfg.inName;
  d["initial"] = cfg.initialState; d["inmode"] = cfg.inMode; d["intype"] = cfg.inType; d["ininv"] = cfg.inInvert;
  d["exname"] = cfg.exName; d["extype"] = cfg.exType; d["exinv"] = cfg.exInvert; d["exout"] = cfg.exOutLow; d["exrel"] = cfg.exRelay;
  d["aon"] = cfg.autoOn; d["aond"] = cfg.autoOnDelay; d["aoff"] = cfg.autoOff; d["aoffd"] = cfg.autoOffDelay;
  d["plim"] = cfg.powerLimit; d["tlim"] = cfg.tempLimit; d["led"] = cfg.led;
  d["auth"] = cfg.authPass; d["tz"] = cfg.tz; d["sntp"] = cfg.sntp;
  d["uurl"] = cfg.updUrl; d["utok"] = cfg.updToken; d["umode"] = cfg.updMode; d["uhour"] = cfg.updHour;
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
  cfg.ip = jstr(d["ip"], ""); cfg.mask = jstr(d["mask"], ""); cfg.gw = jstr(d["gw"], ""); cfg.dns = jstr(d["dns"], "");
  cfg.apPass = jstr(d["appass"], ""); cfg.swName = jstr(d["swname"], ""); cfg.inName = jstr(d["inname"], "");
  cfg.initialState = jstr(d["initial"], cfg.initialState); cfg.inMode = jstr(d["inmode"], cfg.inMode);
  cfg.inType = jstr(d["intype"], cfg.inType); cfg.inInvert = d["ininv"] | false;
  cfg.exName = jstr(d["exname"], ""); cfg.exType = jstr(d["extype"], cfg.exType);
  cfg.exInvert = d["exinv"] | false; cfg.exOutLow = d["exout"] | false; cfg.exRelay = jstr(d["exrel"], cfg.exRelay);
  cfg.autoOn = d["aon"] | false; cfg.autoOnDelay = d["aond"] | 60.0f;
  cfg.autoOff = d["aoff"] | false; cfg.autoOffDelay = d["aoffd"] | 60.0f;
  cfg.powerLimit = d["plim"] | cfg.powerLimit; cfg.tempLimit = d["tlim"] | cfg.tempLimit;
  cfg.led = d["led"] | true;
  // Uebernahme aus Version 1.0.0 (Faktor stand in config.json, Standard war falsch 12.53)
  float old = d["pcal"] | 0.0f;
  if (old > 0 && fabsf(old - 12.53f) > 0.001f && !LittleFS.exists("/calib")) { cfg.pwrCal = old; cfg.calSrc = "reference"; saveCal(); }
  cfg.authPass = jstr(d["auth"], ""); cfg.tz = jstr(d["tz"], cfg.tz); cfg.sntp = jstr(d["sntp"], cfg.sntp);
  cfg.updUrl = jstr(d["uurl"], cfg.updUrl); cfg.updToken = jstr(d["utok"], "");
  cfg.updMode = jstr(d["umode"], cfg.updMode); cfg.updHour = d["uhour"] | cfg.updHour;
  cfg.nextHookId = d["hnext"] | 1; cfg.hookRev = d["hrev"] | 0; cfg.cfgRev = d["rev"] | 0;
  cfg.hooks.clear();
  for (JsonObjectConst o : d["hooks"].as<JsonArrayConst>()) {
    Hook h;
    h.id = o["id"] | 0; h.cid = o["cid"] | 0; h.enable = o["en"] | true; h.event = jstr(o["ev"], ""); h.name = jstr(o["name"], "");
    for (JsonVariantConst u : o["urls"].as<JsonArrayConst>()) h.urls.push_back(jstr(u, ""));
    if (o["thr"].is<float>()) { h.hasThr = true; h.thr = o["thr"]; }
    h.armed = (h.event != "switch.power_below");
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
  return u;
}

void fireHook(const Hook& h, const String& ev) {
  h_cid_tmp = h.cid;
  for (auto& u : h.urls)
    if (httpQueue.size() < 12) httpQueue.push_back(expandUrl(u, ev));
}

void fireEvent(const String& ev, int cid = 0) {
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

// ======================= Relais =======================
void updateLed();

bool setRelay(bool on, const String& src, float toggleAfter = -1) {
  if (on && overtemp) return false;
  bool was = relayOn;
  relayOn = on;
  relaySrc = src;
  digitalWrite(PIN_RELAY, on ? HIGH : LOW);
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
  if (was != on) {
    if (cfg.initialState == "restore_last") saveRelayState();
    fireEvent(on ? "switch.on" : "switch.off");
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
  digitalWrite(PIN_LED, (cfg.led && relayOn) ? LOW : HIGH);
#endif
}

void handleLed() {
#if HAS_LED
  if (apActive) digitalWrite(PIN_LED, (millis() / 500) & 1 ? LOW : HIGH);
#endif
}

// ======================= Eingang (AC-tauglich) =======================
IRAM_ATTR void inIsr() {
  if (((GPI >> PIN_INPUT) & 1) == inActiveLevel) inPulse = true;
}

void resetWifiAndReboot() {
  cfg.ssid = ""; cfg.pass = ""; cfg.staticIp = false;
  saveCfg();
  rebootAt = millis() + 500;
}

struct BtnState { uint32_t pressMs = 0, relMs = 0; uint8_t pushes = 0; };
BtnState bs0, bs100;

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

void onInputEdge(bool st) {
  uint32_t now = millis();
  if (now < 60000 && ++bootEdges >= 10) { resetWifiAndReboot(); return; }
  btnEdge(0, cfg.inType, st, bs0);
  inputAction(cfg.inMode, st, cfg.inMode == "momentary" ? "button" : "switch");
}

bool sampleInput(uint32_t now) {
  if (inPulse || (((GPI >> PIN_INPUT) & 1) == inActiveLevel)) { inPulse = false; inLastActiveMs = now; }
  return (now - inLastActiveMs) < 35;   // haelt bei 50-Hz-Halbwellen durch
}

void handleInput() {
  uint32_t now = millis();
  bool raw = sampleInput(now);
  if (raw != inCand) { inCand = raw; inCandMs = now; }
  if (inCand != inState && now - inCandMs >= 40) { inState = inCand; onInputEdge(inState); }
  btnTick(0, inState, bs0);
}

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
#if HAS_PM
IRAM_ATTR void cfIsr() {
  uint32_t now = micros();
  if (cfLastUs) { cfSumUs += now - cfLastUs; cfCnt++; }
  cfLastUs = now;
}
#endif

void handlePm() {
#if HAS_PM
  uint32_t now = millis();
  if (now - lastPmMs < 1000) return;
  float dt = (now - lastPmMs) / 1000.0f;
  lastPmMs = now;
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
    fireEvent("switch.overpower");
  }
  for (auto& h : cfg.hooks) {
    if (!h.enable || !h.hasThr) continue;
    if (h.event == "switch.power_above") {
      if (h.armed && apower > h.thr) { h.armed = false; fireHook(h, h.event); }
      else if (apower < h.thr * 0.95f) h.armed = true;
    } else if (h.event == "switch.power_below") {
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
    fireEvent("switch.overtemperature");
  } else if (overtemp && tempC < cfg.tempLimit - 10) overtemp = false;
#endif
}

// ======================= WLAN =======================
void startAP() {
  if (apActive) return;
  WiFi.mode(cfg.ssid.length() ? WIFI_AP_STA : WIFI_AP);
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

void startSTA() {
  if (!cfg.ssid.length()) return;
  WiFi.hostname(devId);
  if (cfg.staticIp) {
    IPAddress ip, gw, m, d;
    ip.fromString(cfg.ip); gw.fromString(cfg.gw); m.fromString(cfg.mask);
    if (!d.fromString(cfg.dns)) d = gw;
    WiFi.config(ip, gw, m, d);
  } else {
    WiFi.config(0U, 0U, 0U);
  }
  WiFi.begin(cfg.ssid, cfg.pass);
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
  if (up) {
    if (!wifiWasUp) { wifiWasUp = true; wifiUpMs = now; onWifiUp(); }
    if (apActive && now - wifiUpMs > 120000UL) stopAP();
  } else {
    if (wifiWasUp) { wifiWasUp = false; wifiLostMs = now; }
    if (!apActive && (!cfg.ssid.length() || now - wifiLostMs > 60000UL)) startAP();
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
String updInstall(const String& url, const String& sha) {
  if (relayOn) return "Relais ist an – Update nur bei ausgeschaltetem Relais";
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
      if (relayOn) { updLastErr = "Update wartet: Relais ist an"; return; }
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
  if (timerActive) {
    o["timer_started_at"] = (long)timerStartedAt;
    o["timer_duration"] = timerDurMs / 1000.0;
  }
#if HAS_PM
  o["apower"] = r1(apower);
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
  o["auto_on"] = cfg.autoOn; o["auto_on_delay"] = cfg.autoOnDelay;
  o["auto_off"] = cfg.autoOff; o["auto_off_delay"] = cfg.autoOffDelay;
#if HAS_PM
  o["power_limit"] = cfg.powerLimit;
  JsonObject pc = o["pm_calibration"].to<JsonObject>();
  pc["factor"] = cfg.pwrCal; pc["source"] = cfg.calSrc; pc["default"] = PWR_CAL_DEFAULT;
#endif
#if HAS_TEMP
  o["temp_limit"] = cfg.tempLimit;
#endif
}

void inputStatus(JsonObject o) { o["id"] = 0; o["state"] = inState; }
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
  o["type"] = cfg.inType; o["invert"] = cfg.inInvert;
}

void sysStatus(JsonObject o) {
  o["mac"] = macStr; o["restart_required"] = false;
  if (validTime()) {
    time_t t = time(nullptr); struct tm lt; localtime_r(&t, &lt);
    char b[6]; snprintf(b, sizeof b, "%02d:%02d", lt.tm_hour, lt.tm_min);
    o["time"] = b; o["unixtime"] = (long)t;
  } else { o["time"] = nullptr; o["unixtime"] = nullptr; }
  o["uptime"] = millis() / 1000;
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
  if (up) { o["sta_ip"] = WiFi.localIP().toString(); o["status"] = "got ip"; o["ssid"] = WiFi.SSID(); o["rssi"] = WiFi.RSSI(); }
  else { o["sta_ip"] = nullptr; o["status"] = cfg.ssid.length() ? "connecting" : "disconnected"; o["ssid"] = nullptr; o["rssi"] = nullptr; }
  o["ap_active"] = apActive;
}

void wifiConfig(JsonObject o) {
  JsonObject s = o["sta"].to<JsonObject>();
  s["ssid"] = cfg.ssid; s["is_open"] = cfg.pass.length() == 0; s["enable"] = cfg.ssid.length() > 0;
  s["ipv4mode"] = cfg.staticIp ? "static" : "dhcp";
  s["ip"] = cfg.ip; s["netmask"] = cfg.mask; s["gw"] = cfg.gw; s["nameserver"] = cfg.dns;
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
    if (h.cid != 0 && !(HAS_ADDON && h.cid == 100)) return HAS_ADDON ? "cid: 0 (SW) oder 100 (Add-on)" : "cid muss 0 sein";
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
  if ((h.event == "switch.power_above" || h.event == "switch.power_below") && !h.hasThr) return "threshold (Watt) fehlt";
  h.armed = (h.event != "switch.power_below");
  return "";
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
    switchStatus(o["switch:0"].to<JsonObject>()); inputStatus(o["input:0"].to<JsonObject>());
#if HAS_ADDON
    addonStatus(o["input:100"].to<JsonObject>());
#endif
    return 0;
  }
  if (m == "shelly.getconfig") {
    JsonObject o = out.to<JsonObject>();
    sysConfig(o["sys"].to<JsonObject>()); wifiConfig(o["wifi"].to<JsonObject>());
    switchConfig(o["switch:0"].to<JsonObject>()); inputConfig(o["input:0"].to<JsonObject>());
#if HAS_ADDON
    addonConfig(o["input:100"].to<JsonObject>());
#endif
    return 0;
  }
  if (m == "shelly.listmethods") {
    JsonArray a = out["methods"].to<JsonArray>();
    for (auto s : METHODS) a.add(s);
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
    if (relayOn) ERR(-114, "Relais ist an – Update nur bei ausgeschaltetem Relais");
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
  if (m.startsWith("input.") && pid != 0 && !(HAS_ADDON && pid == 100))
    ERR(-103, HAS_ADDON ? "Ungültige id – Eingänge: 0 (SW), 100 (Add-on)" : "Ungültige id – Eingang hat nur id 0");
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
    //   {"tasmota_powercal": 12530} Wert von Tasmota "PowerCal" (BL0937) uebernehmen
    //   {"factor": 1.7066}          Faktor in W/Hz direkt setzen
    //   {"reset": true}             zurueck auf Standard
    if (p["reset"].is<bool>() && p["reset"].as<bool>()) {
      cfg.pwrCal = PWR_CAL_DEFAULT; cfg.calSrc = "default";
    } else if (p["tasmota_powercal"].is<float>()) {
      float pc = p["tasmota_powercal"];
      if (pc < 1000 || pc > 100000) ERR(-103, "tasmota_powercal unplausibel (typ. 8000–20000)");
      cfg.pwrCal = 1362.0f * pc / 1e7f; cfg.calSrc = "tasmota";
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
  if (m == "input.getstatus") { inputStatus(out.to<JsonObject>()); return 0; }
  if (m == "input.getconfig") { inputConfig(out.to<JsonObject>()); return 0; }
  if (m == "input.setconfig") {
    JsonVariantConst c = p["config"];
    if (!c.is<JsonObjectConst>()) ERR(-103, "config fehlt");
    if (c["name"].is<const char*>()) cfg.inName = c["name"].as<const char*>();
    if (c["type"].is<const char*>()) {
      String v = c["type"].as<const char*>();
      if (v != "switch" && v != "button") ERR(-103, "type: switch|button");
      cfg.inType = v;
    }
    if (c["invert"].is<bool>()) { cfg.inInvert = c["invert"]; inActiveLevel = cfg.inInvert ? LOW : HIGH; }
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
    setRelay(on, "http", timer);
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
  JsonObject r = d["relays"].to<JsonArray>().add<JsonObject>();
  r["ison"] = relayOn; r["has_timer"] = timerActive; r["overpower"] = overpower; r["source"] = relaySrc;
#if HAS_PM
  JsonObject mt = d["meters"].to<JsonArray>().add<JsonObject>();
  mt["power"] = r1(apower); mt["is_valid"] = true; mt["total"] = (long)(energyWh * 60);  // Watt-Minuten wie Gen1
#endif
#if HAS_TEMP
  if (!isnan(tempC)) { d["temperature"] = r1(tempC); d["overtemperature"] = overtemp; }
#endif
  JsonArray ins = d["inputs"].to<JsonArray>();
  ins.add<JsonObject>()["input"] = inState ? 1 : 0;
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

// ======================= Setup / Loop =======================
void setup() {
  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, LOW);
#if HAS_LED
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);
#endif
#if HAS_ADDON
  Serial.begin(115200, SERIAL_8N1, SERIAL_TX_ONLY);   // RX (GPIO3) gehoert dem Add-on
#else
  Serial.begin(115200);
#endif
  Serial.println(F("\nHermit OS " FW_VERSION));
  Serial.printf("Flash: Header %u KB, Chip %u KB, OTA frei %u KB\n",
                ESP.getFlashChipSize() / 1024, ESP.getFlashChipRealSize() / 1024, ESP.getFreeSketchSpace() / 1024);
  if (ESP.getFlashChipRealSize() < ESP.getFlashChipSize())
    Serial.println(F("WARNUNG: Chip kleiner als Firmware-Layout!"));

  if (!LittleFS.begin()) { LittleFS.format(); LittleFS.begin(); }

  uint8_t mac[6];
  WiFi.macAddress(mac);
  char b[13];
  snprintf(b, sizeof b, "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  macStr = b;
  String lower = macStr; lower.toLowerCase();
  devId = String(DEV_PREFIX) + "-" + lower;

  loadCal();
  loadCfg();
  loadEnergy();

#if HAS_ADDON
  pinMode(PIN_ADDON_OUT, OUTPUT);
  addonRelease();
  pinMode(PIN_ADDON_IN, INPUT_PULLUP);
  delay(20);
  exState = exCand = readAddon();
#endif

  // Eingang
  inActiveLevel = cfg.inInvert ? LOW : HIGH;
  pinMode(PIN_INPUT, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_INPUT), inIsr, CHANGE);
  inLastActiveMs = millis() - 1000;
  uint32_t t0 = millis();
  while (millis() - t0 < 80) { inState = inCand = sampleInput(millis()); delay(1); }
  inCandMs = millis();

  // Startzustand Relais
  bool start = false;
  if (cfg.initialState == "on") start = true;
  else if (cfg.initialState == "restore_last") start = loadRelayState();
  else if (cfg.initialState == "match_input") start = inState;
  setRelay(start, "init");

#if HAS_PM
  pinMode(PIN_CF, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_CF), cfIsr, RISING);
  lastPmMs = minuteStartMs = lastEnergySaveMs = millis();
#endif

  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.mode(WIFI_STA);
  wifiLostMs = millis();
  if (cfg.ssid.length()) startSTA(); else startAP();

  setupServer();
  Serial.println("ID: " + devId);
}

void loop() {
  server.handleClient();
  if (apActive) dnsServer.processNextRequest();
  if (mdnsUp) MDNS.update();
  handleWifi();
  handleInput();
  handleAddon();
  handleTimer();
  handlePm();
  handleTemp();
  handleLed();
  processQueue();
  handleUpdate();
  if (rebootAt && (int32_t)(millis() - rebootAt) >= 0) {
    saveEnergy();
    delay(100);
    ESP.restart();
  }
}
