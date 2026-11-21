// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 D.Koch – Hermit OS
// Hermit Loader – kleine Zwischen-Firmware fuer den Wechsel Tasmota -> Hermit OS.
// Tasmota akzeptiert sie wegen ihrer Groesse direkt (aehnlich tasmota-minimal). Danach:
//   - WLAN: im ESP gespeicherte Daten, dann die aus den Tasmota-Einstellungen, sonst Access-Point
//     hermit-loader-xxxxxx (192.168.33.1)
//   - Hermit-OS-.bin im Browser hochladen
//   - WLAN-Daten bleiben im WLAN-Speicher des ESP; Hermit OS uebernimmt sie beim ersten Start
// Die Relais-Pins werden nicht angesteuert (bleiben aus).
#ifdef HERMIT_LOADER

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <Updater.h>
#include "tasmota_import.h"

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
    "<b>Hermit OS hochladen</b><br><small>hermit-1.bin, hermit-1pm.bin oder die -addon-Variante (nicht .gz)</small>"
    "<input type=file name=f accept=.bin><button>Hochladen &amp; flashen</button></form>");
  h += F("<form class=c method=post action=/wifi><b>WLAN einrichten</b><br><small>Hermit OS &uuml;bernimmt diese Daten beim ersten Start.</small><br>SSID<input name=s value='");
  h += esc(wSsid) + F("'>Passwort<input name=p type=password><button>Verbinden</button></form></body></html>");
  server.send(200, "text/html; charset=utf-8", h);
}

void redirectHome() { server.sendHeader("Location", "/"); server.send(303); }

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
    if (Update.hasError()) msg = "Upload fehlgeschlagen (Fehler " + String(Update.getError()) + ")";
    else { msg = "OK: Hermit OS installiert - Neustart ..."; rebootAt = millis() + 1500; }
    redirectHome();
  }, []() {
    HTTPUpload& u = server.upload();
    if (u.status == UPLOAD_FILE_START) Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000);
    else if (u.status == UPLOAD_FILE_WRITE) Update.write(u.buf, u.currentSize);
    else if (u.status == UPLOAD_FILE_END) Update.end(true);
    else if (u.status == UPLOAD_FILE_ABORTED) Update.end(false);
    yield();
  });
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
}

void loop() {
  server.handleClient();
  if (apOn) dns.processNextRequest();
  if (rebootAt && (int32_t)(millis() - rebootAt) >= 0) ESP.restart();
}

#endif
