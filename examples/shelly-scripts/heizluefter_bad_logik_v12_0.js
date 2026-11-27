// --- HEIZLÜFTER BAD LOGIK v12.0 (gemeinsam für Bad-Heizlüfter und Badheizer) ---
// v12.0: eine Fassung für beide Bad-Heizlüfter (vorher Heizlüfter v11.2 und Badheizer v11.5).
//        Taster an SW1 (input:0) UND/ODER am Add-on-Digitaleingang (input:100) – es zählt, was vorhanden ist.
//        Shelly-App-Komponenten (boolean:200/201, number:200) werden nur benutzt, wenn sie angelegt sind.
// v11.5: eigener App-Schalter boolean:201 = Boost (an/aus, zeigt Boost-Zustand)
// v11.4: Fensterkontakt Shelly BLU Door/Window (wird automatisch gefunden)
// O1 = Lüfter, O2 = Heizregister 1 kW
// Add-on: Fühler = temperature:100
// SW1/SW2 schalten keine Relais (entkoppelt)
//
// Betrieb EIN: hält im Zeitfenster von-bis die Temperatur t (von = bis -> ganztägig)
// Betrieb AUS: nur Frostschutz
// Taster:      kurz  = Boost an/aus: heizt boost Minuten auf bt (auch bei Betrieb AUS)
//              2x    = Boost neu starten (volle Zeit)
//              lang  = Boost aus + Störung quittieren
// Fenster offen: Heizung pausiert (Lüfter läuft nach), Boost-Zeit steht still, Frostschutz bleibt
// Frostschutz: immer aktiv, heizt unter frostOn bis frostOff
//
// KVS:
//   hl_cfg   : {"on":true,"t":21,"bt":24,"boost":60,"von":"06:00","bis":"22:00"}
//              Altes v10-Format {temp,mode,tStart,tEnd} wird beim ersten Lesen übernommen.
//   hl_cmd   : "on" | "off" | "boost_on" | "boost_off" | "boost_toggle" | "ack"
//   hl_state : Status für Cloud und Display
//   hl_stats : Tagesverbrauch d0/d1/d2 (kWh)

// ===== Konfiguration =====
let CFG = {
  fanID: 0,            // O1 Lüfter
  heatID: 1,           // O2 Heizregister
  btnIDs: [0, 100],    // Taster: SW1 (input:0) und Add-on-Digitaleingang (input:100), was vorhanden ist
  vcOn: 200,           // boolean:200 in der App = Betrieb EIN/AUS (null = nicht benutzen)
  vcTemp: 200,         // number:200 in der App = Halte-Temperatur (null = nicht benutzen)
  vcBoost: 201,        // boolean:201 in der App = Boost an/aus (null = nicht benutzen)
  sensor: "temperature:100",
  hyst: 0.5,           // Heizen ab Soll - hyst, aus bei Soll
  fanLeadS: 3,         // Lüfter-Vorlauf bevor Heizung zuschaltet
  runAfterS: 90,       // Lüfter-Nachlauf nach Heizung aus
  fanMinW: 15,         // Mindestleistung Lüfter (0 = Überwachung aus)
  heatMinW: 500,       // Mindestleistung Heizregister 1 kW (0 = Überwachung aus)
  heatChkS: 30,        // Lastprüfung erst so lange nach Heizbeginn (Register läuft langsam an)
  heatLowS: 20,        // Störung erst, wenn die Leistung so lange am Stück zu niedrig ist
  maxTemp: 35,         // Notabschaltung Raumtemperatur
  frost: true,         // Frostschutz aktiv
  frostOn: 10,         // Frostschutz heizt unter 10 °C ...
  frostOff: 12,        // ... bis 12 °C
  winSensor: "auto",   // BLU Fensterkontakt: "auto" = suchen, z. B. "bthomesensor:201" = fest, null = aus
  winDelayS: 0,        // Fenster muss so lange offen sein, bis die Heizung pausiert
  winResumeS: 30,      // nach dem Schließen so lange warten, bis wieder geheizt wird
  loopMs: 5000,
  stateEveryS: 30      // hl_state max. alle 30 s schreiben (außer bei Statuswechsel)
};

// ===== Zustand =====
let PH_AUS = 0, PH_VOR = 1, PH_HEIZ = 2, PH_NACH = 3;
let phase = PH_AUS, phaseT = 0;
let boost = false, boostEnd = 0;
// Einstellungen (Startwerte, werden aus hl_cfg geladen)
let cfg = { on: true, t: 21, bt: 24, boost: 60, von: "06:00", bis: "22:00" };
let cfgDirty = false;   // cfg wurde lokal geändert (Befehl/App) -> beim nächsten Durchlauf in hl_cfg schreiben
let err = "";                         // gespeicherte Störung (Lüfter/Heizregister), Quittieren per Taster lang
let softErr = "", lastSoft = "";      // Störung, die von selbst zurückgeht, wenn die Ursache weg ist
let frost = false;
let heatLow = -1;       // seit wann die Heizleistung zu niedrig ist (-1 = nicht)
let busy = false, again = false;
let stats = { d0: 0, d1: 0, d2: 0, lt: 0, ld: -1 };
let lastSave = 0, lastStateW = -9999, lastKey = "", lastJson = "";
let winComp = null, winScanT = -9999;   // gefundene Sensor-Komponente
let winOpen = false, winRaw = false, winChgT = 0, lastU = -1;
let btnUsed = [];       // Taster-Eingänge, die es auf diesem Gerät gibt

// ===== Hilfsfunktionen =====
// Zahl oder 0 (Messwerte fehlen z. B. kurz nach dem Start)
function nz(v) { return typeof v === "number" ? v : 0; }

function uptime() {
  let s = Shelly.getComponentStatus("sys");
  return s ? s.uptime : 0;
}

// Calls nacheinander ausführen (max. gleichzeitige Calls nie überschritten)
function callSeq(list, done, i) {
  if (!i) i = 0;
  if (i >= list.length) { done(); return; }
  let c = list[i];
  Shelly.call(c[0], c[1], function () { callSeq(list, done, i + 1); });
}

// Liegt die lokale Uhrzeit im Zeitfenster? (ohne Zeit vom Netz: ja)
function inWin(sys) {
  if (cfg.von === cfg.bis || !sys.time) return true;
  let n = sys.time;
  if (cfg.von < cfg.bis) return n >= cfg.von && n < cfg.bis;
  return n >= cfg.von || n < cfg.bis;   // über Mitternacht, z. B. 22:00-06:00
}

// Lokaler Tag aus unixtime + lokaler Uhrzeit (sys.time "HH:MM")
function dayIdx(sys) {
  if (!sys.unixtime || !sys.time) return -1;
  let p = sys.time.split(":");
  let loc = (p[0] * 1) * 60 + (p[1] * 1);
  let utc = Math.floor((sys.unixtime % 86400) / 60);
  let off = loc - utc;
  if (off > 720) off -= 1440;
  if (off < -720) off += 1440;
  return Math.floor((sys.unixtime + off * 60) / 86400);
}

function boostOn() {
  boost = true;
  boostEnd = uptime() + cfg.boost * 60;
  err = "";
  print("Boost an (" + cfg.bt + " °C, " + cfg.boost + " min)");
}

function boostOff(why) {
  if (boost) print("Boost aus" + (why ? ": " + why : ""));
  boost = false;
}

function fault(msg) {
  if (err !== msg) print("STÖRUNG: " + msg);
  err = msg;
  boost = false;
}

// ===== Hauptkette =====
function run() {
  if (busy) { again = true; return; }
  busy = true;
  Shelly.call("KVS.Get", { key: "hl_cmd" }, function (r) {
    let cmd = (r && r.value) ? r.value : "";
    if (cmd === "") { readCfg(); return; }
    Shelly.call("KVS.Set", { key: "hl_cmd", value: "" }, function () {
      if (cmd === "boost_on") boostOn();
      else if (cmd === "boost_off") boostOff("Befehl");
      else if (cmd === "boost_toggle") { if (boost) boostOff("Befehl"); else boostOn(); }
      else if (cmd === "ack") { err = ""; print("Störung quittiert (Web)"); }
      if (cmd === "on" || cmd === "off") {
        cfg.on = cmd === "on";
        cfgDirty = true;
        print("Betrieb " + (cfg.on ? "EIN" : "AUS"));
      }
      readCfg();
    });
  });
}

function tOk(s) {
  return typeof s === "string" && s.length === 5 && s.charAt(2) === ":" && s >= "00:00" && s <= "23:59";
}

function readCfg() {
  if (cfgDirty) {
    cfgDirty = false;
    let q = [["KVS.Set", { key: "hl_cfg", value: JSON.stringify(cfg) }]];
    logic(q);
    callSeq(q, finish);
    return;
  }
  Shelly.call("KVS.Get", { key: "hl_cfg" }, function (r) {
    // während der Abfrage in der App geändert -> lokale Werte gelten
    if (cfgDirty) { readCfg(); return; }
    let miss = true;
    if (r && r.value) {
      try {
        let c = JSON.parse(r.value);
        if (typeof c === "number") c = { t: c };
        if (c && typeof c === "object") {
          // Übernahme aus v10: temp -> t, tStart/tEnd -> von/bis, mode "man" -> ganztägig
          if (typeof c.t !== "number" && typeof c.temp === "number") c.t = c.temp;
          if (!tOk(c.von) && tOk(c.tStart)) c.von = c.tStart;
          if (!tOk(c.bis) && tOk(c.tEnd)) c.bis = c.tEnd;
          if (c.mode === "man" && c.tStart !== undefined) { c.von = "00:00"; c.bis = "00:00"; }

          miss = false;
          if (typeof c.t === "number") cfg.t = c.t; else miss = true;
          if (typeof c.bt === "number") cfg.bt = c.bt; else miss = true;
          if (typeof c.boost === "number" && c.boost > 0) cfg.boost = c.boost; else miss = true;
          if (typeof c.on === "boolean") cfg.on = c.on; else miss = true;
          if (tOk(c.von)) cfg.von = c.von; else miss = true;
          if (tOk(c.bis)) cfg.bis = c.bis; else miss = true;
        }
      } catch (e) {}
    }
    let q = [];
    if (miss) q.push(["KVS.Set", { key: "hl_cfg", value: JSON.stringify(cfg) }]);
    logic(q);
    callSeq(q, finish);
  });
}

function finish() {
  busy = false;
  if (again) { again = false; run(); }
}

// ===== Regelung =====
function logic(q) {
  let sys = Shelly.getComponentStatus("sys");
  let sf = Shelly.getComponentStatus("switch:" + CFG.fanID);
  let sh = Shelly.getComponentStatus("switch:" + CFG.heatID);
  let ts = Shelly.getComponentStatus(CFG.sensor);
  if (!sys || !sf || !sh) return;

  let u = sys.uptime;
  let pF = nz(sf.apower), pH = nz(sh.apower);
  let pFk = typeof sf.apower === "number", pHk = typeof sh.apower === "number";  // Messwert vorhanden?
  let t = (ts && typeof ts.tC === "number") ? ts.tC : null;

  // Fensterkontakt, Boost-Zeit
  winUpdate(u);
  if (boost && winOpen && lastU >= 0) boostEnd += u - lastU;   // Boost-Zeit läuft bei offenem Fenster nicht ab
  lastU = u;

  if (boost && u >= boostEnd) boostOff("Zeit abgelaufen");

  // Sicherheit (setzt sich selbst zurück, sobald die Ursache weg ist)
  softErr = "";
  if (t === null) softErr = "Temperaturfühler";
  else if (t >= CFG.maxTemp) softErr = "Übertemperatur " + t + " °C";
  if (sh.output && !sf.output) softErr = "Heizung ohne Lüfter";
  if (softErr !== lastSoft) {
    if (softErr !== "") print("STÖRUNG: " + softErr);
    lastSoft = softErr;
  }
  if (sh.output && phase !== PH_HEIZ) { phase = PH_NACH; phaseT = u; } // extern eingeschaltet

  // Frostschutz
  if (CFG.frost && t !== null) {
    if (t < CFG.frostOn) {
      if (!frost) print("Frostschutz an (" + t + " °C)");
      frost = true;
    } else if (t >= CFG.frostOff) {
      if (frost) print("Frostschutz aus (" + t + " °C)");
      frost = false;
    }
  } else frost = false;

  // App-Komponenten an die Einstellungen angleichen (nur wenn vorhanden)
  if (CFG.vcOn !== null) {
    let b = Shelly.getComponentStatus("boolean:" + CFG.vcOn);
    if (b && b.value !== cfg.on) q.push(["Boolean.Set", { id: CFG.vcOn, value: cfg.on }]);
  }
  if (CFG.vcTemp !== null) {
    let n = Shelly.getComponentStatus("number:" + CFG.vcTemp);
    if (n && n.value !== cfg.t) q.push(["Number.Set", { id: CFG.vcTemp, value: cfg.t }]);
  }
  if (CFG.vcBoost !== null) {
    let bb = Shelly.getComponentStatus("boolean:" + CFG.vcBoost);
    if (bb && bb.value !== boost) q.push(["Boolean.Set", { id: CFG.vcBoost, value: boost }]);
  }

  // Soll: Boost > Halten (im Zeitfenster) > nur Frostschutz
  let win = inWin(sys);
  let hold = cfg.on && win;
  let tgt = null;
  if (hold) tgt = cfg.t;
  if (boost && (tgt === null || cfg.bt > tgt)) tgt = cfg.bt;

  let ok = err === "" && softErr === "" && t !== null;
  let wantT = tgt !== null && t !== null && (phase === PH_HEIZ ? t < tgt : t < tgt - CFG.hyst);
  if (winOpen) wantT = false;   // Fenster offen: nur noch Frostschutz
  let want = ok && (wantT || frost);

  // Ablauf: AUS -> VORLAUF -> HEIZT -> NACHLAUF -> AUS
  if (phase !== PH_HEIZ) heatLow = -1;
  if (phase === PH_AUS || phase === PH_NACH) {
    if (want) { phase = PH_VOR; phaseT = u; }
    else if (phase === PH_NACH && u - phaseT >= CFG.runAfterS) { phase = PH_AUS; phaseT = u; }
  } else if (phase === PH_VOR) {
    if (!want) { phase = PH_AUS; phaseT = u; }
    else if (sf.output && u - phaseT >= CFG.fanLeadS) {
      if (CFG.fanMinW > 0 && (!pFk || pF < CFG.fanMinW)) {
        if (u - phaseT >= CFG.fanLeadS + 10) {
          fault(pFk ? "Lüfter läuft nicht (" + pF.toFixed(0) + " W)" : "Lüfter: keine Leistungsmessung");
          phase = PH_NACH; phaseT = u;
        }
      } else { phase = PH_HEIZ; phaseT = u; }
    }
  } else if (phase === PH_HEIZ) {
    if (!want) { phase = PH_NACH; phaseT = u; }
    else if (CFG.fanMinW > 0 && pFk && pF < CFG.fanMinW) {
      fault("Lüfter ausgefallen (" + pF.toFixed(0) + " W)");
      phase = PH_NACH; phaseT = u;
    }
    else if (CFG.heatMinW > 0 && sh.output && u - phaseT >= CFG.heatChkS && pHk && pH < CFG.heatMinW) {
      if (heatLow < 0) heatLow = u;
      if (u - heatLow >= CFG.heatLowS) {
        fault("Heizregister ohne Last (" + pH.toFixed(0) + " W)");
        phase = PH_NACH; phaseT = u;
      }
    }
    else heatLow = -1;
  }

  // Ausgänge (Heizung immer zuerst, Heizung nur bei bestätigtem Lüfter)
  let wantHeat = phase === PH_HEIZ;
  let wantFan = phase !== PH_AUS;
  if (sh.output !== wantHeat) q.push(["Switch.Set", { id: CFG.heatID, on: wantHeat }]);
  if (sf.output !== wantFan) q.push(["Switch.Set", { id: CFG.fanID, on: wantFan }]);

  // Energie / Tagesstatistik
  let tot = (nz(sf.aenergy ? sf.aenergy.total : 0) + nz(sh.aenergy ? sh.aenergy.total : 0)) / 1000;
  if (stats.lt === 0) stats.lt = tot;
  if (tot < stats.lt) stats.lt = tot - stats.d0; // Zähler nach Neustart zurückgesetzt
  stats.d0 = tot - stats.lt;

  let d = dayIdx(sys);
  let save = false;
  if (d >= 0 && stats.ld !== d) {
    if (typeof stats.ld === "number" && stats.ld >= 0) {
      let gap = d - stats.ld;
      stats.d2 = gap === 1 ? stats.d1 : (gap === 2 ? stats.d0 : 0);
      stats.d1 = gap === 1 ? stats.d0 : 0;
    }
    stats.d0 = 0; stats.lt = tot; stats.ld = d;
    save = true;
  }
  if (save || u - lastSave >= 900) {
    lastSave = u;
    q.push(["KVS.Set", { key: "hl_stats", value: JSON.stringify(stats) }]);
  }

  // Status
  let st;
  let e = err !== "" ? err : softErr;
  if (e !== "") st = "⚠️ STÖRUNG: " + e;
  else if (phase === PH_HEIZ && !wantT) st = "❄️ FROSTSCHUTZ";
  else if (phase === PH_HEIZ) st = "🔥 HEIZT auf " + tgt + " °C" + (boost ? " (Boost)" : "");
  else if (winOpen) st = "🪟 FENSTER OFFEN, Heizung pausiert" + (phase === PH_NACH ? " (Nachlauf)" : "") + (boost ? " · Boost wartet" : "");
  else if (phase === PH_VOR) st = "🌀 VORLAUF";
  else if (phase === PH_NACH) st = "🌀 NACHLAUF";
  else if (tgt !== null) st = "✅ BEREIT, hält " + tgt + " °C" + (boost ? " (Boost)" : "");
  else if (cfg.on) st = "🌙 PAUSE bis " + cfg.von;
  else st = "💤 AUS";

  let rest = boost ? Math.ceil((boostEnd - u) / 60) : 0;
  let state = {
    temp: t === null ? null : Math.round(t * 10) / 10,
    pwr: (pF + pH).toFixed(0),
    status: st,
    isB: boost,
    isH: phase === PH_HEIZ,
    isAuto: hold,
    on: cfg.on,
    win: win,
    tgt: tgt,
    isFrost: frost,
    winOpen: winComp === null ? null : winOpen,   // null = kein Fensterkontakt
    rest: rest,
    err: e
  };

  let key = st + "|" + boost + "|" + rest;
  let js = JSON.stringify(state);
  if (key !== lastKey || (js !== lastJson && u - lastStateW >= CFG.stateEveryS)) {
    lastKey = key; lastJson = js; lastStateW = u;
    q.push(["KVS.Set", { key: "hl_state", value: js }]);
  }
}

// ===== Taster (SW1 = input:0 und/oder Add-on input:100) =====
// Funktioniert mit Eingangstyp "Taster" (push-Events) und "Schalter" (toggle mit state).
// Bei "Schalter" misst das Script die Druckdauer selbst: < 1,5 s = kurz, sonst lang.
let btnDown = -1;

function btnShort() { if (boost) boostOff("Taster"); else boostOn(); }
function btnLong() { boostOff("Taster lang"); err = ""; print("Störung quittiert (Taster)"); }

function isBtn(comp) {
  for (let i = 0; i < btnUsed.length; i++) if (comp === "input:" + btnUsed[i]) return true;
  return false;
}

Shelly.addEventHandler(function (ev) {
  if (!ev || !ev.info || !isBtn(ev.component)) return;
  let e = ev.info.event;
  if (e === "single_push") btnShort();
  else if (e === "double_push") boostOn();
  else if (e === "long_push") btnLong();
  else if (e === "toggle" && ev.info.state === true) { btnDown = Shelly.getUptimeMs(); return; }
  else if (e === "toggle" && ev.info.state === false) {
    if (btnDown < 0) return;
    let d = Shelly.getUptimeMs() - btnDown;
    btnDown = -1;
    if (d < 1500) btnShort(); else btnLong();
  }
  else return;
  run();
});

// ===== Shelly-App: Schalter und Schieber =====
Shelly.addStatusHandler(function (st) {
  if (!st || !st.delta) return;
  if (winComp !== null && st.component === winComp) { run(); return; }   // Fenster sofort auswerten
  if (CFG.vcBoost !== null && st.component === "boolean:" + CFG.vcBoost && typeof st.delta.value === "boolean") {
    if (st.delta.value === boost) return;
    if (st.delta.value) boostOn(); else boostOff("App");
    print("App: Boost " + (boost ? "an" : "aus"));
    run();
    return;
  }
  if (CFG.vcOn !== null && st.component === "boolean:" + CFG.vcOn && typeof st.delta.value === "boolean") {
    if (st.delta.value === cfg.on) return;
    cfg.on = st.delta.value; cfgDirty = true;
    print("App: Betrieb " + (cfg.on ? "EIN" : "AUS"));
    run();
  }
  if (CFG.vcTemp !== null && st.component === "number:" + CFG.vcTemp && typeof st.delta.value === "number") {
    if (st.delta.value === cfg.t) return;
    cfg.t = st.delta.value; cfgDirty = true;
    print("App: Halten " + cfg.t + " °C");
    run();
  }
});

// ===== Fensterkontakt (Shelly BLU Door/Window) =====
// Sucht unter bthomesensor:200..239 den Sensor mit BTHome-Objekt "window" (obj_id 45)
function winFind(u) {
  if (CFG.winSensor === null) return;
  if (CFG.winSensor !== "auto") { winComp = CFG.winSensor; return; }
  if (u - winScanT < 60) return;   // höchstens 1x pro Minute suchen
  winScanT = u;
  for (let i = 200; i < 240; i++) {
    let c = Shelly.getComponentConfig("bthomesensor:" + i);
    if (c && c.obj_id === 45) {
      winComp = "bthomesensor:" + i;
      print("Fensterkontakt gefunden: " + winComp + (c.name ? " (" + c.name + ")" : ""));
      return;
    }
  }
}

function winUpdate(u) {
  if (winComp === null) winFind(u);
  if (winComp === null) { winOpen = false; return; }
  let s = Shelly.getComponentStatus(winComp);
  if (!s) {
    winOpen = false;
    if (CFG.winSensor === "auto") { print("Fensterkontakt " + winComp + " weg, suche neu"); winComp = null; }
    return;
  }
  let raw = s.value === true;
  if (raw !== winRaw) { winRaw = raw; winChgT = u; }
  let need = winRaw ? CFG.winDelayS : CFG.winResumeS;
  if (winOpen !== winRaw && u - winChgT >= need) {
    winOpen = winRaw;
    print(winOpen ? "Fenster offen: Heizung pausiert" : "Fenster zu: weiter");
  }
}

// ===== Start =====
function init() {
  Shelly.call("KVS.Get", { key: "hl_stats" }, function (r) {
    if (r && r.value) {
      try { let s = JSON.parse(r.value); if (s && typeof s === "object") stats = s; } catch (e) {}
    }

    // Geräte-Config prüfen, nur bei Abweichung schreiben
    let q = [];
    for (let i = 0; i < CFG.btnIDs.length; i++) {
      let ic = Shelly.getComponentConfig("input:" + CFG.btnIDs[i]);
      if (!ic) continue;
      btnUsed.push(CFG.btnIDs[i]);
      if (ic.type !== "button")
        q.push(["Input.SetConfig", { id: CFG.btnIDs[i], config: { type: "button" } }]);
    }
    let ids = [CFG.fanID, CFG.heatID];
    for (let i = 0; i < ids.length; i++) {
      let c = Shelly.getComponentConfig("switch:" + ids[i]);
      if (c && (c.initial_state !== "off" || c.in_mode !== "detached"))
        q.push(["Switch.SetConfig", { id: ids[i], config: { initial_state: "off", in_mode: "detached" } }]);
    }

    callSeq(q, function () {
      Timer.set(CFG.loopMs, true, run);
      run();
      let txt = "";
      for (let i = 0; i < btnUsed.length; i++) txt += (i ? ", " : "") + "input:" + btnUsed[i];
      print("Heizlüfter Bad v12.0 gestartet. Taster: " + (txt === "" ? "KEINER GEFUNDEN" : txt));
    });
  });
}

init();
