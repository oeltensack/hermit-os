// --- KONVEKTOR LOGIK v1.7 (gleiches Bedienkonzept wie Heizlüfter 3 v11.4) ---
// v1.7: Stufen nach Anstieg statt nach Abstand: Start mit 750 W, steigt die Temperatur zu langsam
//       (weniger als riseK in stepS), eine Stufe höher (1250 W, dann 2000 W). Kurz vor dem Soll
//       (nearK) eine Stufe zurück, damit nichts überschwingt. "max" wird beim Start nicht mehr auf 1 gesetzt.
// v1.6: Stufen nach Abstand zum Soll: ab 1 K 750 W, ab 4 K 1250 W, ab 8 K 2000 W
// v1.5: hl_state gekürzt (war über 255 Byte -> wurde nicht gespeichert, Web-UI ohne Status)
// v1.4: kein Taster; Start immer mit 750 W; ohne Fühler bleibt die Stufe, bis sie im Web-UI geändert wird
// v1.3: O1 = 1250 W, O2 = 750 W; nur Gerätethermostat an SW1, SW2 unbenutzt, kein Add-on
// v1.2: Gerätethermostat an SW1 (begrenzt / regelt bei Fühlerausfall), Gerätewahlschalter an SW2
// v1.1: Raumfühler frei wählbar (Shelly BLU H&T oder Add-on), Add-on-Fühler als Ersatz
// Konvektionsheizer 750 / 1250 / 2000 W an Shelly Plus 2PM
// O1 = Heizelement 1250 W (stark), O2 = Heizelement 750 W (schwach)
//   Stufe 1 = O2        ->  750 W
//   Stufe 2 = O1        -> 1250 W
//   Stufe 3 = O1 + O2   -> 2000 W
// Gerätethermostat schaltet nur noch L auf SW1 (Signal),
// die Heizstäbe hängen direkt an O1/O2. Überhitzungsschutz/Kippschalter bleiben im Lastkreis.
// SW1 Thermostat: Kontakt zu = Wärmebedarf. Shelly heizt nur, wenn Fühler UND Thermostat Wärme wollen
//                 (Drehknopf wirkt als Obergrenze). Fällt der Fühler aus, regelt der Thermostat allein.
// SW2:            unbenutzt (optional Gerätewahlschalter, siehe swIn/swMode)
// Raumfühler: Shelly BLU H&T (wird automatisch gefunden) oder Add-on (sensor = "temperature:100").
//   Ohne Fühler regelt der Gerätethermostat allein (Stufe = Höchststufe aus dem Web-UI).
// Shelly-App (optional): boolean:200 = Betrieb EIN/AUS, number:200 = Halte-Temperatur
//
// Stufenwahl nach Anstieg:
//   Heizbeginn immer mit 750 W. Alle stepS wird geprüft, wie weit die Temperatur gestiegen ist.
//   Weniger als riseK -> eine Stufe höher (bis "max", im Web-UI einstellbar).
//   Kommt die Temperatur bis nearK an das Soll heran, eine Stufe zurück. Dort höher nur,
//   wenn die Temperatur fällt, tiefer, wenn sie noch um riseK steigt.
// Betrieb EIN: hält im Zeitfenster von-bis die Temperatur t (von = bis -> ganztägig)
// Betrieb AUS: nur Frostschutz (startet ebenfalls mit 750 W)
// Boost (Web-UI): heizt boost Minuten auf bt
// Fenster offen (Shelly BLU Door/Window): Heizung pausiert, Boost-Zeit steht still
//
// KVS:
//   hl_cfg   : {"on":true,"t":21,"bt":24,"boost":60,"von":"06:00","bis":"22:00","max":3}
//   hl_cmd   : "on" | "off" | "boost_on" | "boost_off" | "boost_toggle" | "ack"
//   hl_state : Status für Cloud und Display
//   hl_stats : Tagesverbrauch d0/d1/d2 (kWh)

// ===== Konfiguration =====
let CFG = {
  ch: [1, 0],          // Switch-IDs: [Element 750 W = O2, Element 1250 W = O1]
  watt: [750, 1250],
  minW: [500, 850],    // Lastüberwachung je Element (0 = aus)
  vcOn: 200,           // boolean:200 in der App (null = nicht benutzen)
  vcTemp: 200,         // number:200 in der App (null = nicht benutzen)
  sensor: "blu",       // Raumfühler: "blu" = Shelly BLU H&T automatisch suchen,
                       //   "bthomesensor:202" = bestimmter BLU-Fühler, "temperature:100" = Add-on-Fühler
  sensorFb: null,      // Ersatzfühler, z. B. "temperature:100" mit Add-on (null = keiner -> Störung)
  sensorAgeS: 900,     // BLU-Wert gilt als ausgefallen, wenn er älter ist (s)
  thermoIn: 0,         // Gerätethermostat an SW1 (input:0), null = nicht angeschlossen
  thermoInv: false,    // true, wenn der Kontakt bei Wärmebedarf öffnet
  swIn: null,          // Wahlschalter am Gerät an SW2: 1 = input:1, null = nicht angeschlossen
  swMode: "on",        // "on" = Betrieb EIN/AUS, "eco" = aus: nur 750 W
  hyst: 1.0,           // Heizen ab Soll - hyst, aus bei Soll
  stepS: 600,          // so lange je Stufe, dann Anstieg prüfen (s)
  riseK: 0.3,          // weniger Anstieg als das in stepS = zu langsam -> nächste Stufe
  nearK: 0.5,          // höchstens so weit unter Soll: eine Stufe zurück
  heatChkS: 20,        // Lastprüfung erst so lange nach dem Einschalten eines Elements
  heatLowS: 20,        // Störung erst, wenn die Leistung so lange am Stück zu niedrig ist
  maxTemp: 35,         // Notabschaltung Raumtemperatur
  frost: true,
  frostOn: 10,
  frostOff: 12,
  winSensor: "auto",   // "auto" = BLU-Fensterkontakt suchen, "bthomesensor:201" = fest, null = aus
  winDelayS: 0,
  winResumeS: 30,
  loopMs: 5000,
  stateEveryS: 30
};

let STW = [0, 750, 1250, 2000];

// ===== Zustand =====
let stage = 0, stageT = -9999;
let refT = null, refU = 0;   // Temperatur und Zeit zu Beginn der laufenden Prüfzeit
let nearIn = false;          // Temperatur ist kurz vor dem Soll
let boost = false, boostEnd = 0;
let cfg = { on: true, t: 21, bt: 24, boost: 60, von: "06:00", bis: "22:00", max: 3 };
let cfgDirty = false;
let err = "", softErr = "", lastSoft = "";
let frost = false;
let onT = [-1, -1], lowT = [-1, -1];
let busy = false, again = false;
let stats = { d0: 0, d1: 0, d2: 0, lt: 0, ld: -1 };
let lastSave = 0, lastStateW = -9999, lastKey = "", lastJson = "";
let winComp = null, winScanT = -9999;
let winOpen = false, winRaw = false, winChgT = 0, lastU = -1;

// ===== Hilfsfunktionen =====
function nz(v) { return typeof v === "number" ? v : 0; }

function uptime() {
  let s = Shelly.getComponentStatus("sys");
  return s ? s.uptime : 0;
}

function callSeq(list, done, i) {
  if (!i) i = 0;
  if (i >= list.length) { done(); return; }
  let c = list[i];
  Shelly.call(c[0], c[1], function () { callSeq(list, done, i + 1); });
}

function inWin(sys) {
  if (cfg.von === cfg.bis || !sys.time) return true;
  let n = sys.time;
  if (cfg.von < cfg.bis) return n >= cfg.von && n < cfg.bis;
  return n >= cfg.von || n < cfg.bis;
}

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

function tOk(s) {
  return typeof s === "string" && s.length === 5 && s.charAt(2) === ":" && s >= "00:00" && s <= "23:59";
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

function readCfg() {
  if (cfgDirty) {
    cfgDirty = false;
    let q = [["KVS.Set", { key: "hl_cfg", value: JSON.stringify(cfg) }]];
    logic(q);
    callSeq(q, finish);
    return;
  }
  Shelly.call("KVS.Get", { key: "hl_cfg" }, function (r) {
    if (cfgDirty) { readCfg(); return; }
    let miss = true;
    if (r && r.value) {
      try {
        let c = JSON.parse(r.value);
        if (c && typeof c === "object") {
          miss = false;
          if (typeof c.t === "number") cfg.t = c.t; else miss = true;
          if (typeof c.bt === "number") cfg.bt = c.bt; else miss = true;
          if (typeof c.boost === "number" && c.boost > 0) cfg.boost = c.boost; else miss = true;
          if (typeof c.on === "boolean") cfg.on = c.on; else miss = true;
          if (tOk(c.von)) cfg.von = c.von; else miss = true;
          if (tOk(c.bis)) cfg.bis = c.bis; else miss = true;
          if (typeof c.max === "number" && c.max >= 1 && c.max <= 3) cfg.max = Math.round(c.max); else miss = true;
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

// ===== Stufe nach Anstieg =====
// goal = Zieltemperatur, t = Raumtemperatur, lim = Höchststufe, u = Uptime
function stageByRise(goal, t, lim, u) {
  if (stage === 0) { nearIn = false; return 1; }               // Heizbeginn immer mit 750 W
  let ns = stage;
  let near = goal - t <= CFG.nearK;
  if (near && !nearIn && stage > 1) {
    ns = stage - 1;                                            // kurz vor dem Ziel: eine Stufe zurück
    print("Kurz vor Soll: eine Stufe zurück");
  } else if (refT !== null && u - refU >= CFG.stepS) {
    let rise = t - refT;
    if (!near && rise < CFG.riseK && stage < lim) {
      ns = stage + 1;
      print("Anstieg " + rise.toFixed(1) + " K in " + Math.round((u - refU) / 60) + " min zu langsam");
    } else if (near && rise < 0 && stage < lim) {
      ns = stage + 1;                                          // am Soll: Stufe hält nicht -> höher
      print("Temperatur fällt am Soll (" + rise.toFixed(1) + " K)");
    } else if (near && rise >= CFG.riseK && stage > 1) {
      ns = stage - 1;                                          // am Soll: steigt noch schnell -> tiefer
      print("Am Soll noch schneller Anstieg");
    }
    refT = t; refU = u;                                        // nächste Prüfzeit
  }
  nearIn = near;
  if (ns > lim) ns = lim;
  return ns;
}

// ===== Regelung =====
function logic(q) {
  let sys = Shelly.getComponentStatus("sys");
  let s0 = Shelly.getComponentStatus("switch:" + CFG.ch[0]);
  let s1 = Shelly.getComponentStatus("switch:" + CFG.ch[1]);
  if (!sys || !s0 || !s1) return;
  let sw = [s0, s1];

  let u = sys.uptime;
  let t = readTemp(sys, u);

  // Fensterkontakt + Boost-Zeit
  winUpdate(u);
  if (boost && winOpen && lastU >= 0) boostEnd += u - lastU;
  lastU = u;
  if (boost && u >= boostEnd) boostOff("Zeit abgelaufen");

  // Sicherheit (setzt sich selbst zurück)
  let thS = inState(CFG.thermoIn);
  if (thS !== null && CFG.thermoInv) thS = !thS;
  let thOnly = t === null && thS !== null;     // Fühler weg -> Gerätethermostat regelt
  let swS = inState(CFG.swIn);
  softErr = "";
  if (t === null && !thOnly) softErr = "Temperaturfühler";
  else if (t >= CFG.maxTemp) softErr = "Übertemperatur " + t + " °C";
  if (softErr !== lastSoft) {
    if (softErr !== "") print("STÖRUNG: " + softErr);
    lastSoft = softErr;
  }

  // Lastüberwachung je Element (z. B. Überhitzungsschutz im Gerät ausgelöst, Element defekt)
  for (let i = 0; i < 2; i++) {
    let s = sw[i];
    if (!s.output) { onT[i] = -1; lowT[i] = -1; continue; }
    if (onT[i] < 0) onT[i] = u;
    if (CFG.minW[i] > 0 && u - onT[i] >= CFG.heatChkS && typeof s.apower === "number" && s.apower < CFG.minW[i]) {
      if (lowT[i] < 0) lowT[i] = u;
      if (u - lowT[i] >= CFG.heatLowS) fault("Element " + CFG.watt[i] + " W ohne Last (" + s.apower.toFixed(0) + " W)");
    } else lowT[i] = -1;
  }

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

  // App-Komponenten angleichen
  if (CFG.vcOn !== null) {
    let b = Shelly.getComponentStatus("boolean:" + CFG.vcOn);
    if (b && b.value !== cfg.on) q.push(["Boolean.Set", { id: CFG.vcOn, value: cfg.on }]);
  }
  if (CFG.vcTemp !== null) {
    let n = Shelly.getComponentStatus("number:" + CFG.vcTemp);
    if (n && n.value !== cfg.t) q.push(["Number.Set", { id: CFG.vcTemp, value: cfg.t }]);
  }

  // Soll: Boost > Halten (im Zeitfenster) > nur Frostschutz
  let win = inWin(sys);
  let hold = cfg.on && win;
  let tgt = null;
  if (hold) tgt = cfg.t;
  if (boost && (tgt === null || cfg.bt > tgt)) tgt = cfg.bt;

  let ok = err === "" && softErr === "" && (t !== null || thOnly);
  let wantS = tgt !== null && t !== null && (stage > 0 ? t < tgt : t < tgt - CFG.hyst);
  let thBlk = thS === false;                   // Thermostat am Gerät sagt: warm genug
  let wantT = thOnly ? (tgt !== null && thS === true) : (wantS && !thBlk);
  if (winOpen) wantT = false;
  let lim = cfg.max;
  if (CFG.swMode === "eco" && swS === false) lim = 1;
  let want = ok && (wantT || frost);

  // Stufe bestimmen
  let ns = 0;
  if (want) {
    if (wantT && thOnly) ns = lim;                                   // ohne Fühler: Stufe aus dem Web-UI
    else ns = stageByRise(wantT ? tgt : CFG.frostOff, t, lim, u);
  }
  if (ns !== stage) {
    print("Stufe " + stage + " -> " + ns + " (" + STW[ns] + " W)");
    stage = ns; stageT = u;
    refT = t; refU = u;                                              // Prüfzeit neu beginnen
  }

  // Ausgänge: erst aus, dann ein
  let w0 = stage === 1 || stage === 3;
  let w1 = stage >= 2;
  if (s0.output && !w0) q.push(["Switch.Set", { id: CFG.ch[0], on: false }]);
  if (s1.output && !w1) q.push(["Switch.Set", { id: CFG.ch[1], on: false }]);
  if (!s0.output && w0) q.push(["Switch.Set", { id: CFG.ch[0], on: true }]);
  if (!s1.output && w1) q.push(["Switch.Set", { id: CFG.ch[1], on: true }]);

  // Energie / Tagesstatistik
  let tot = (nz(s0.aenergy ? s0.aenergy.total : 0) + nz(s1.aenergy ? s1.aenergy.total : 0)) / 1000;
  if (stats.lt === 0) stats.lt = tot;
  if (tot < stats.lt) stats.lt = tot - stats.d0;
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
  let sl = "Stufe " + stage + " · " + STW[stage] + " W";
  if (e !== "") st = "⚠️ STÖRUNG: " + e;
  else if (stage > 0 && !wantT) st = "❄️ FROSTSCHUTZ · " + sl;
  else if (stage > 0 && thOnly) st = "🔥 HEIZT (Thermostat) · " + sl;
  else if (stage > 0) st = "🔥 HEIZT auf " + tgt + " °C · " + sl + (boost ? " (Boost)" : "");
  else if (winOpen) st = "🪟 FENSTER OFFEN, Heizung pausiert" + (boost ? " · Boost wartet" : "");
  else if (tgt !== null && wantS && thBlk) st = "✅ BEREIT · Gerätethermostat begrenzt";
  else if (tgt !== null) st = "✅ BEREIT, hält " + tgt + " °C" + (boost ? " (Boost)" : "");
  else if (cfg.on) st = "🌙 PAUSE bis " + cfg.von;
  else st = "💤 AUS";

  let rest = boost ? Math.ceil((boostEnd - u) / 60) : 0;
  // KVS-Werte dürfen max. 255 Byte lang sein -> nur was gebraucht wird
  let state = {
    temp: t === null ? null : Math.round(t * 10) / 10,
    status: st,
    stufe: stage,
    isB: boost,
    rest: rest,
    err: e,
    tsrc: tSrc,
    th: thS
  };
  let key = st + "|" + boost + "|" + rest;
  let js = JSON.stringify(state);
  if (js.length > 250) { state.tsrc = tSrc === "" ? "" : "BLU"; js = JSON.stringify(state); }
  if (js.length > 250) { state.err = "zu lang"; js = JSON.stringify(state); }
  if (key !== lastKey || (js !== lastJson && u - lastStateW >= CFG.stateEveryS)) {
    lastKey = key; lastJson = js; lastStateW = u;
    q.push(["KVS.Set", { key: "hl_state", value: js }]);
  }
}

// ===== Hilfsfunktion Eingänge SW1/SW2 =====
function inState(id) {
  if (id === null) return null;
  let s = Shelly.getComponentStatus("input:" + id);
  return (s && typeof s.state === "boolean") ? s.state : null;
}

Shelly.addEventHandler(function (ev) {
  if (!ev || !ev.info) return;
  if (typeof ev.component !== "string") return;
  // SW2 Wahlschalter am Gerät
  if (CFG.swIn !== null && ev.component === "input:" + CFG.swIn) {
    if (CFG.swMode === "on" && typeof ev.info.state === "boolean" && ev.info.state !== cfg.on) {
      cfg.on = ev.info.state; cfgDirty = true;
      print("Schalter am Gerät: Betrieb " + (cfg.on ? "EIN" : "AUS"));
    }
    run(); return;
  }
  // SW1 Gerätethermostat
  if (CFG.thermoIn !== null && ev.component === "input:" + CFG.thermoIn) { run(); return; }
});

// ===== Shelly-App =====
Shelly.addStatusHandler(function (st) {
  if (!st || !st.delta) return;
  if (winComp !== null && st.component === winComp) { run(); return; }
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

// ===== Raumfühler =====
// Add-on-Fühler (temperature:x) oder BLU-Fühler (bthomesensor:x, BTHome-Objekt "temperature" obj_id 69)
let tComp = null, tScanT = -9999, tSrc = "";

function tFind(u) {
  if (CFG.sensor !== "blu") { tComp = CFG.sensor; return; }
  if (u - tScanT < 60) return;
  tScanT = u;
  for (let i = 200; i < 240; i++) {
    let c = Shelly.getComponentConfig("bthomesensor:" + i);
    if (c && c.obj_id === 69) {
      tComp = "bthomesensor:" + i;
      print("Raumfühler gefunden: " + tComp + (c.name ? " (" + c.name + ")" : ""));
      return;
    }
  }
}

function tRead(comp, sys) {
  if (comp === null) return null;
  let s = Shelly.getComponentStatus(comp);
  if (!s) return null;
  if (typeof s.tC === "number") return s.tC;                         // Add-on
  if (typeof s.value !== "number") return null;                      // BLU
  if (sys.unixtime && typeof s.last_updated_ts === "number" &&
      sys.unixtime - s.last_updated_ts > CFG.sensorAgeS) return null; // zu alt
  return s.value;
}

function readTemp(sys, u) {
  if (tComp === null) tFind(u);
  let t = tRead(tComp, sys), src = tComp;
  if (t === null && CFG.sensorFb !== null) { t = tRead(CFG.sensorFb, sys); src = CFG.sensorFb + " (Ersatz)"; }
  if (t === null) src = "";
  if (src !== tSrc) { print("Temperatur von: " + (src === "" ? "KEINEM Fühler" : src)); tSrc = src; }
  return t;
}

// ===== Fensterkontakt (Shelly BLU Door/Window) =====
function winFind(u) {
  if (CFG.winSensor === null) return;
  if (CFG.winSensor !== "auto") { winComp = CFG.winSensor; return; }
  if (u - winScanT < 60) return;
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
    let q = [];
    let ins = [CFG.thermoIn, CFG.swIn];
    for (let i = 0; i < 2; i++) {
      if (ins[i] === null) continue;
      let c = Shelly.getComponentConfig("input:" + ins[i]);
      if (c && c.type !== "switch")
        q.push(["Input.SetConfig", { id: ins[i], config: { type: "switch" } }]);
    }
    for (let i = 0; i < 2; i++) {
      let c = Shelly.getComponentConfig("switch:" + CFG.ch[i]);
      if (c && (c.initial_state !== "off" || c.in_mode !== "detached"))
        q.push(["Switch.SetConfig", { id: CFG.ch[i], config: { initial_state: "off", in_mode: "detached" } }]);
    }
    callSeq(q, function () {
      Timer.set(CFG.loopMs, true, run);
      run();
      print("Konvektor v1.7 gestartet. 1250 W = O" + (CFG.ch[1] + 1) + ", 750 W = O" + (CFG.ch[0] + 1) +
            ", Thermostat = SW" + (CFG.thermoIn === null ? "-" : CFG.thermoIn + 1) +
            ", Stufen nach Anstieg (" + CFG.riseK + " K / " + (CFG.stepS / 60) + " min)");
    });
  });
}

init();
