// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 D.Koch – Hermit OS
// Hermit Loader – kleine Zwischen-Firmware fuer den Wechsel Tasmota -> Hermit OS.
// Tasmota akzeptiert sie wegen ihrer Groesse direkt (aehnlich tasmota-minimal). Danach:
//   - WLAN: im ESP gespeicherte Daten, dann die aus den Tasmota-Einstellungen, sonst Access-Point
//     hermit-loader-xxxxxx (192.168.33.1)
//   - Hermit-OS-.bin im Browser hochladen
//   - WLAN-Daten bleiben im WLAN-Speicher des ESP; Hermit OS uebernimmt sie beim ersten Start
// Die Relais-Pins werden nicht angesteuert (bleiben aus).
//
// LOADER_AUTO (1-MB-Geraete, z. B. Sonoff RF Bridge): Hermit OS passt dort nicht zweimal in den Flash.
//   - Kommt der Loader von Hermit OS (Update), steht im RTC-Speicher, welche Datei er laden soll.
//   - Kommt er von Tasmota, holt er LOADER_VARIANT aus der update.json im Netz.
//   Beides automatisch, sobald WLAN steht; schlaegt es fehl, bleibt die Seite zum Hochladen.
#ifdef HERMIT_LOADER

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <Updater.h>
#include "tasmota_import.h"
#ifdef LOADER_AUTO
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include "upd_handoff.h"
#ifndef LOADER_VARIANT
#define LOADER_VARIANT "hermit-rfbridge"
#endif
#define LOADER_UPD_URL "https://raw.githubusercontent.com/oeltensack/hermit-os/main/update.json"
uint32_t autoAt = 0;
bool autoFromRtc = false, busy = false;
UpdHandoff ho;
#endif

ESP8266WebServer server(80);
DNSServer dns;
String devId, msg, wSsid;
bool apOn = false;
uint32_t rebootAt = 0;

static String esc(const String& s) {
  String o; for (char c : s) { if (c == '<') o += "&lt;"; else if (c == '&') o += "&amp;"; else if (c == '"' || c == '\'') o += "&quot;"; else o += c; }
  return o;
}

void startAP() {
  if (apOn) return;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(IPAddress(192, 168, 33, 1), IPAddress(192, 168, 33, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP(devId.c_str());
  dns.start(53, "*", IPAddress(192, 168, 33, 1));
  apOn = true;
}

bool waitSta(uint32_t ms) {
  uint32_t t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < ms) { delay(100); server.handleClient(); if (apOn) dns.processNextRequest(); }
  return WiFi.status() == WL_CONNECTED;
}

void page() {
  bool sta = WiFi.status() == WL_CONNECTED;
  String h = F("<!DOCTYPE html><html lang=de><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>Hermit Loader</title><style>body{font:15px system-ui;background:#0f1419;color:#e6e9ec;max-width:560px;margin:auto;padding:12px}"
    ".c{background:#1a2129;border-radius:10px;padding:14px;margin:12px 0}input{width:100%;padding:8px;margin:4px 0 10px;box-sizing:border-box;"
    "background:#0f1419;color:#e6e9ec;border:1px solid #2a333c;border-radius:6px}button{padding:9px 14px;border:0;border-radius:6px;background:#2f9be0;"
    "color:#fff;font:inherit}.m{padding:10px;border-radius:6px;background:#2a333c}small{color:#8a96a3}</style></head><body><h2>&#x1F41A; Hermit Loader</h2>");
  if (msg.length()) h += "<div class=m>" + esc(msg) + "</div>";
  h += "<div class=c><b>WLAN:</b> " + (sta ? esc(WiFi.SSID()) + " &middot; " + WiFi.localIP().toString() : String("nicht verbunden")) + "</div>";
  h += F("<form class=c method=post action=/upload enctype=multipart/form-data onsubmit=\"this.querySelector('button').textContent='L&auml;dt &hellip;'\">"
#ifdef LOADER_AUTO
    "<b>Hermit OS hochladen</b><br><small>" LOADER_VARIANT ".bin.gz</small>"
    "<input type=file name=f accept=.bin,.gz><button>Hochladen &amp; flashen</button></form>");
  if (sta) h += F("<form class=c method=post action=/auto><b>Aus dem Internet</b><br><small>Neueste Version laut update.json</small><br><button>Jetzt installieren</button></form>");
#else
    "<b>Hermit OS hochladen</b><br><small>hermit-1.bin, hermit-1pm.bin oder die -addon-Variante (nicht .gz)</small>"
    "<input type=file name=f accept=.bin><button>Hochladen &amp; flashen</button></form>");
#endif
  h += F("<form class=c method=post action=/wifi><b>WLAN einrichten</b><br><small>Hermit OS &uuml;bernimmt diese Daten beim ersten Start.</small><br>SSID<input name=s value='");
  h += esc(wSsid) + F("'>Passwort<input name=p type=password><button>Verbinden</button></form></body></html>");
  server.send(200, "text/html; charset=utf-8", h);
}

void redirectHome() { server.sendHeader("Location", "/"); server.send(303); }

#ifdef LOADER_AUTO
bool openUrl(HTTPClient& http, std::unique_ptr<WiFiClient>& cl, const String& url) {
  if (url.startsWith("https://")) {
    auto* sc = new BearSSL::WiFiClientSecure();
    sc->setInsecure();                       // Integritaet ueber SHA-256
    sc->setBufferSizes(16384, 512);
    cl.reset(sc);
  } else cl.reset(new WiFiClient());
  http.setTimeout(15000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setUserAgent("HermitLoader");
  return http.begin(*cl, url);
}

// Wert eines Schluessels im Block hinter `from` (ohne JSON-Bibliothek; update.json ist flach genug)
String jval(const String& j, int from, const char* key) {
  int k = j.indexOf(String("\"") + key + "\"", from);
  if (k < 0) return "";
  int a = j.indexOf('"', j.indexOf(':', k) + 1);
  int b = a < 0 ? -1 : j.indexOf('"', a + 1);
  return b > a ? j.substring(a + 1, b) : "";
}

// Datei laden, SHA-256 pruefen, flashen. Leerer String = Erfolg.
String fetchFlash(const String& url, const uint8_t* sha) {
  HTTPClient http; std::unique_ptr<WiFiClient> cl;
  if (!openUrl(http, cl, url)) return "URL ungültig";
  int code = http.GET();
  if (code != 200) { http.end(); return "HTTP " + String(code); }
  int len = http.getSize();
  if (len <= 0) { http.end(); return "Unbekannte Dateigröße"; }
  if (!Update.begin(len)) { http.end(); return "Zu wenig Platz (" + String(len / 1024) + " KB)"; }
  br_sha256_context ctx; br_sha256_init(&ctx);
  WiFiClient* st = http.getStreamPtr();
  uint8_t buf[1024];
  int left = len; uint32_t last = millis();
  while (left > 0 && millis() - last < 20000) {
    size_t av = st->available();
    if (!av) { if (!http.connected()) break; delay(1); continue; }
    int n = st->readBytes(buf, av > sizeof(buf) ? sizeof(buf) : av);
    if (n <= 0) continue;
    br_sha256_update(&ctx, buf, n);
    if (Update.write(buf, n) != (size_t)n) break;
    left -= n; last = millis();
    server.handleClient();
  }
  http.end();
  if (left > 0) { Update.end(false); return "Download abgebrochen"; }
  uint8_t h[32]; br_sha256_out(&ctx, h);
  bool chk = false; for (int i = 0; i < 32; i++) chk |= sha[i] != 0;
  if (chk && memcmp(h, sha, 32)) { Update.end(false); return "SHA-256 stimmt nicht"; }
  if (!Update.end(true)) return "Flash-Fehler " + String(Update.getError());
  return "";
}

void autoInstall() {
  String url; uint8_t sha[32];
  if (autoFromRtc) { url = ho.url; memcpy(sha, ho.sha, 32); }
  else {
    msg = "Lade update.json …";
    HTTPClient http; std::unique_ptr<WiFiClient> cl;
    if (!openUrl(http, cl, LOADER_UPD_URL)) { msg = "update.json: URL ungültig"; return; }
    int code = http.GET();
    String j = code == 200 ? http.getString() : "";
    http.end();
    int at = j.indexOf("\"" LOADER_VARIANT "\"");
    if (at < 0) { msg = "update.json: kein Eintrag für " LOADER_VARIANT " (HTTP " + String(code) + ")"; return; }
    url = jval(j, at, "url");
    String base = LOADER_UPD_URL; base = base.substring(0, base.lastIndexOf('/') + 1);
    if (!url.startsWith("http")) url = base + url;
    updHexToSha(jval(j, at, "sha256"), sha);
  }
  Serial.println("Lade " + url);
  msg = "Lade " + url + " …";
  busy = true;
  String e = fetchFlash(url, sha);
  busy = false;
  if (e.length()) { msg = "Automatische Installation fehlgeschlagen: " + e + " – Datei unten hochladen."; Serial.println(msg); return; }
  updHandoffClear();
  msg = "OK: Hermit OS installiert - Neustart ...";
  rebootAt = millis() + 1000;
}
#endif

void setup() {
  Serial.begin(115200);
  char b[7]; snprintf(b, sizeof b, "%06x", ESP.getChipId());
  devId = String("hermit-loader-") + b;
  WiFi.persistent(true);              // Zugangsdaten im WLAN-Speicher des ESP ablegen
  WiFi.mode(WIFI_STA);
  WiFi.hostname(devId);

  server.on("/", HTTP_GET, page);
  server.on("/wifi", HTTP_POST, []() {
    wSsid = server.arg("s");
    WiFi.begin(wSsid, server.arg("p"));
    msg = waitSta(20000) ? "Verbunden mit " + wSsid + " - IP " + WiFi.localIP().toString() : "Verbindung mit " + wSsid + " fehlgeschlagen";
    redirectHome();
  });
  server.on("/upload", HTTP_POST, []() {
#ifdef LOADER_AUTO
    if (busy) { msg = "Automatische Installation läuft bereits"; redirectHome(); return; }
#endif
    if (Update.hasError()) msg = "Upload fehlgeschlagen (Fehler " + String(Update.getError()) + ")";
    else { msg = "OK: Hermit OS installiert - Neustart ..."; rebootAt = millis() + 1500; }
    redirectHome();
  }, []() {
    HTTPUpload& u = server.upload();
#ifdef LOADER_AUTO
    if (busy) return;                          // automatischer Download laeuft
    if (u.status == UPLOAD_FILE_START) autoAt = 0;
#endif
    if (u.status == UPLOAD_FILE_START) Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000);
    else if (u.status == UPLOAD_FILE_WRITE) Update.write(u.buf, u.currentSize);
    else if (u.status == UPLOAD_FILE_END) Update.end(true);
    else if (u.status == UPLOAD_FILE_ABORTED) Update.end(false);
    yield();
  });
#ifdef LOADER_AUTO
  server.on("/auto", HTTP_POST, []() { if (busy) { redirectHome(); return; } autoFromRtc = false; autoAt = millis() + 300; msg = "Installation läuft …"; redirectHome(); });
#endif
  server.onNotFound([]() {
    if (apOn) { server.sendHeader("Location", "http://192.168.33.1/"); server.send(302); }
    else server.send(404, "text/plain", "Nicht gefunden");
  });
  server.begin();

  // 1. im ESP gespeicherte WLAN-Daten, 2. Tasmota-Einstellungen, sonst Access-Point
  String src;
  struct station_config sc;
  wifi_station_get_config_default(&sc);
  wSsid = String((char*)sc.ssid);
  if (wSsid.length()) { WiFi.begin(); if (waitSta(20000)) src = "gespeichertem WLAN"; }
  if (!src.length()) {
    String ts, tp;
    if (tasimport::readWifi(ts, tp)) {
      wSsid = ts;
      WiFi.begin(ts, tp);                       // persistent: Hermit OS uebernimmt die Daten spaeter
      if (waitSta(20000)) src = "WLAN aus Tasmota (" + ts + ")";
    }
  }
  if (!src.length()) startAP();
  msg = src.length() ? "Verbunden mit " + src :
        "Kein WLAN - mit " + devId + " verbinden und http://192.168.33.1 oeffnen";
  Serial.println(msg);
#ifdef LOADER_AUTO
  // Vom Hermit-Update: Datei aus dem RTC-Speicher (max. 3 Versuche); von Tasmota: einmal update.json
  autoFromRtc = updHandoffRead(ho);
  if (autoFromRtc && ho.tries >= 3) { updHandoffClear(); autoFromRtc = false; msg += " - automatisches Update dreimal fehlgeschlagen"; }
  else if (src.length()) {
    if (autoFromRtc) { ho.tries++; updHandoffWrite(ho); }
    // vom Update sofort; von Tasmota erst nach 60 s – solange kann das Flash-Tool die eigene Datei hochladen
    autoAt = millis() + (autoFromRtc ? 2000 : 60000);
    msg += autoFromRtc ? " - installiere Hermit OS automatisch …" : " - installiert Hermit OS in 60 s aus dem Internet (oder Datei hochladen)";
  }
#endif
}

void loop() {
  server.handleClient();
#ifdef LOADER_AUTO
  if (autoAt && (int32_t)(millis() - autoAt) >= 0) { autoAt = 0; autoInstall(); }
#endif
  if (apOn) dns.processNextRequest();
  if (rebootAt && (int32_t)(millis() - rebootAt) >= 0) ESP.restart();
}

#endif
