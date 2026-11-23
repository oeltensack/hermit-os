// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 D.Koch – Pagurus OS
// =====================================================================
// Heizlüfter – Stufe 2 über Shelly 1PM (Pagurus OS)
// Läuft als ZUSÄTZLICHES Script auf dem Shelly Plus 2PM, neben dem
// bestehenden Heizlüfter-Script. Das Logik-Script bleibt unverändert.
//
//   Plus 2PM  O1 = Lüfter, O2 = Heizregister Stufe 1, SW1 = Taster
//   1PM       O  = Heizregister Stufe 2
//
// Stufe 2 läuft nur, wenn Lüfter UND Stufe 1 an sind und der Lüfter
// tatsächlich Strom zieht. Der 1PM bekommt jeden Befehl mit Ablaufzeit
// (Watchdog): fällt WLAN oder Script aus, schaltet er selbst ab.
// =====================================================================

let CFG = {
  pm1: "http://192.168.0.51",   // IP des 1PM (bei Passwort: "http://admin:PASS@192.168.0.51")
  luefter_id: 0,                // switch:0 = Lüfter
  stufe1_id: 1,                 // switch:1 = Stufe 1
  luefter_min_w: 5,             // Lüfter muss mindestens so viel ziehen, sonst keine Stufe 2

  // Wann soll Stufe 2 laufen?
  //   "mit_stufe1" – immer zusammen mit Stufe 1
  //   "aufheizen"  – nur die ersten aufheiz_min Minuten nach Einschalten von Stufe 1
  //   "manuell"    – nur per Boost (Doppelklick SW1) oder Event vom Logik-Script
  modus: "aufheizen",
  aufheiz_min: 15,
  boost_min: 15,
  boost_doppelklick: true,      // Doppelklick auf SW1 (input:0) startet/beendet Boost

  watchdog_s: 45,               // 1PM schaltet selbst ab, wenn so lange kein Befehl kommt
  refresh_s: 15,                // Befehl wird so oft erneuert (deutlich < watchdog_s)

  soll_min_w: 600,              // Stufe 2 an, aber weniger Leistung -> Fehler (Register/Thermosicherung?)
  aus_max_w: 20,                // Stufe 2 aus, aber mehr Leistung -> Relais klebt
  power_limit_w: 1300,          // Überlastabschaltung im 1PM
  temp_limit_c: 85              // Übertemperaturabschaltung im 1PM
};

// --------------------------------------------------------------------
let st = {
  stufe1_seit: 0,      // ms, wann Stufe 1 eingeschaltet wurde
  boost_bis: 0,        // ms
  extern: false,       // Wunsch vom Logik-Script per Shelly.emitEvent("stufe2", {on:true})
  soll: false,         // zuletzt gesendeter Sollzustand
  an_seit: 0,
  busy: false,
  fehler: "",
  offline: 0,
  konfig: false,
  aus_seit: 0,
  nochmal: false
};

function now() { return Date.now(); }

function rpc1pm(method, params, cb) {
  Shelly.call("HTTP.POST", {
    url: CFG.pm1 + "/rpc",
    content_type: "application/json",
    body: JSON.stringify({ id: 1, src: "plus2pm", method: method, params: params }),
    timeout: 5
  }, function (res, err) {
    if (err !== 0 || !res || res.code !== 200) { if (cb) cb(null); return; }
    let j = JSON.parse(res.body);
    if (cb) cb(j.error ? null : j.result);
  });
}

function sw(id) { return Shelly.getComponentStatus("switch", id); }

function verriegelung() {
  let l = sw(CFG.luefter_id), s1 = sw(CFG.stufe1_id);
  if (!l || !s1) return false;
  if (!l.output || !s1.output) return false;
  if (typeof l.apower === "number" && l.apower < CFG.luefter_min_w) return false;
  return true;
}

function wunsch() {
  let t = now();
  if (t < st.boost_bis || st.extern) return true;
  if (CFG.modus === "mit_stufe1") return true;
  if (CFG.modus === "aufheizen") return st.stufe1_seit > 0 && (t - st.stufe1_seit) < CFG.aufheiz_min * 60000;
  return false;
}

function pruefen(s) {
  if (!s) {
    st.offline++;
    if (st.offline === 3) print("Stufe 2: 1PM nicht erreichbar – Watchdog schaltet dort ab");
    return;
  }
  st.offline = 0;
  let p = typeof s.apower === "number" ? s.apower : null;
  let f = "";
  if (s.errors && s.errors.length) f = "1PM meldet: " + s.errors.join(",");
  else if (p !== null && st.soll && s.output && now() - st.an_seit > 5000 && p < CFG.soll_min_w) f = "Stufe 2 an, nur " + p + " W";
  else if (p !== null && !st.soll && now() - st.aus_seit > 5000 && p > CFG.aus_max_w) f = "Stufe 2 aus, aber " + p + " W – Relais klebt?";

  if (f !== st.fehler) { st.fehler = f; if (f) print("FEHLER " + f); else print("Stufe 2 wieder ok"); }

  // Relais klebt: Lüfter zwangsweise an lassen, damit das Register nicht ohne Luft heizt
  if (f.indexOf("klebt") >= 0) Shelly.call("Switch.Set", { id: CFG.luefter_id, on: true });
}

function zyklus() {
  if (st.busy) { st.nochmal = true; return; }
  st.busy = true;
  st.nochmal = false;
  if (!st.konfig) konfigurieren();
  let soll = verriegelung() && wunsch();
  if (soll && !st.soll) st.an_seit = now();
  if (!soll && st.soll) st.aus_seit = now();
  st.soll = soll;
  let p = soll ? { id: 0, on: true, toggle_after: CFG.watchdog_s } : { id: 0, on: false };
  rpc1pm("Switch.Set", p, function () {
    rpc1pm("Switch.GetStatus", { id: 0 }, function (s) {
      pruefen(s);
      st.busy = false;
      if (st.nochmal) zyklus();
    });
  });
}

// Sofort reagieren, wenn Lüfter oder Stufe 1 schalten
Shelly.addStatusHandler(function (ev) {
  if (ev.component === "switch:" + CFG.stufe1_id && typeof ev.delta.output !== "undefined") {
    st.stufe1_seit = ev.delta.output ? now() : 0;
    if (!ev.delta.output) st.boost_bis = 0;
    zyklus();
  }
  if (ev.component === "switch:" + CFG.luefter_id && typeof ev.delta.output !== "undefined") zyklus();
});

// Boost per Doppelklick und Wunsch vom Logik-Script
Shelly.addEventHandler(function (ev) {
  if (!ev || !ev.info) return;
  if (CFG.boost_doppelklick && ev.component === "input:0" && ev.info.event === "double_push") {
    st.boost_bis = now() < st.boost_bis ? 0 : now() + CFG.boost_min * 60000;
    print(st.boost_bis ? "Boost " + CFG.boost_min + " min" : "Boost aus");
    zyklus();
  }
  if (ev.info.event === "stufe2" && ev.info.data) {
    st.extern = !!ev.info.data.on;
    zyklus();
  }
});

// 1PM einmalig passend konfigurieren: Eingang ohne Funktion, nach Stromausfall aus,
// Auto-Aus = Watchdog (greift auch bei Schalten über die 1PM-Weboberfläche)
function konfigurieren() {
 rpc1pm("Switch.SetConfig", { id: 0, config: {
  name: "Heizregister Stufe 2",
  in_mode: "detached",
  initial_state: "off",
  auto_on: false,
  auto_off: true,
  auto_off_delay: CFG.watchdog_s,
  power_limit: CFG.power_limit_w,
  temp_limit: CFG.temp_limit_c
}}, function (r) { st.konfig = !!r; if (r) print("1PM konfiguriert"); });
}

// Stufe 1 läuft evtl. schon beim Scriptstart
let s1 = sw(CFG.stufe1_id);
if (s1 && s1.output) st.stufe1_seit = now();

Timer.set(CFG.refresh_s * 1000, true, zyklus);
zyklus();
