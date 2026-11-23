// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 D.Koch – Hermit OS
#pragma once
#include <Arduino.h>

const char INDEX_HTML[] PROGMEM = R"HTML(<!DOCTYPE html><html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Hermit OS</title>
<style>
:root{--bg:#0f1419;--c:#1a2129;--fg:#e6e9ec;--m:#8a96a3;--a:#2f9be0;--on:#3fb950;--off:#56606b;--e:#f85149;--b:#2a333c}
*{box-sizing:border-box}body{margin:0;font:15px/1.4 system-ui,sans-serif;background:var(--bg);color:var(--fg)}
header{padding:14px 18px;background:var(--c);display:flex;justify-content:space-between;align-items:center;gap:10px}
h1{font-size:18px;margin:0}main{max-width:760px;margin:auto;padding:12px}
.card{background:var(--c);border-radius:10px;padding:16px;margin:12px 0}
h2{font-size:13px;margin:0 0 12px;color:var(--m);text-transform:uppercase;letter-spacing:.06em}
.big{display:flex;align-items:center;gap:22px;flex-wrap:wrap}
#btn,#btn2{width:104px;height:104px;border-radius:50%;border:0;font-size:22px;font-weight:700;color:#fff;background:var(--off);cursor:pointer;transition:.2s}
#btn.on,#btn2.on{background:var(--on);box-shadow:0 0 24px #3fb95066}.c2,.cv,.cvp,.chs,.cvx,.rf{display:none}.rft td{padding:6px 4px}.rft .bt{margin:0 4px 0 0;padding:5px 10px}
.cvb{display:flex;gap:10px;flex-wrap:wrap}.cvb .bt{flex:1;min-width:90px;font-size:16px;padding:12px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(120px,1fr));gap:12px;flex:1}
.v{font-size:19px;font-weight:600}.l{font-size:12px;color:var(--m)}
label{display:block;margin:10px 0 3px;font-size:13px;color:var(--m)}
input,select,textarea{width:100%;padding:8px;border-radius:6px;border:1px solid var(--b);background:var(--bg);color:var(--fg);font:inherit}
textarea{min-height:70px;font-family:ui-monospace,monospace;font-size:13px}
.row{display:flex;gap:10px;flex-wrap:wrap}.row>*{flex:1;min-width:140px}
.bt{margin-top:12px;padding:8px 14px;border:0;border-radius:6px;background:var(--a);color:#fff;font:inherit;cursor:pointer}
.bt.r{background:var(--e)}.bt.g{background:#3a444f}
table{width:100%;border-collapse:collapse;font-size:13px}td{padding:7px 5px;border-bottom:1px solid var(--b);vertical-align:top}
.ck{display:flex;align-items:center;gap:8px;margin-top:12px;color:var(--fg);font-size:14px}.ck input{width:auto}
pre{background:var(--bg);padding:10px;border-radius:6px;overflow:auto;font-size:12px;max-height:340px;white-space:pre-wrap}
nav{display:flex;gap:6px;flex-wrap:wrap}nav a{color:var(--fg);text-decoration:none;padding:7px 11px;border-radius:6px;background:var(--c);font-size:13px;cursor:pointer}
nav a.a{background:var(--a)}.tab{display:none}.ao{display:none}.lt{display:none}.dm{display:none}
input[type=range]{padding:0;height:28px;accent-color:var(--a)}
#ctr{background:linear-gradient(90deg,#ffb15a,#fff,#cfe3ff);border-radius:6px}.vi{display:none}.tab.a{display:block}
#toast{position:fixed;bottom:18px;left:50%;transform:translateX(-50%);padding:9px 16px;border-radius:6px;display:none;color:#fff}
code{background:var(--bg);padding:1px 5px;border-radius:4px;font-size:12px}
</style></head><body>
<header><h1 id="t">Hermit OS</h1><span class="l" id="ver"></span></header>
<main>
<nav><a data-t="st" class="a">Status</a><a data-t="sw" id="swtab">Schalter</a><a data-t="wh">Aktionen</a><a data-t="wf">WLAN</a><a data-t="sy">System</a><a data-t="rp">RPC</a></nav>

<section class="tab a" id="st"><div class="card"><div class="big">
<button id="btn">–</button>
<div class="grid">
<div class="pm"><div class="v" id="pw">–</div><div class="l">Leistung</div></div>
<div class="pm"><div class="v" id="en">–</div><div class="l">Energie</div></div>
<div class="pm"><div class="v" id="tc">–</div><div class="l">Temperatur</div></div>
<div class="vi"><div class="v" id="vo">–</div><div class="l">Spannung</div></div>
<div class="vi"><div class="v" id="cu">–</div><div class="l">Strom · cos φ</div></div>
<div id="inw"><div class="v" id="in">–</div><div class="l" id="inl">Eingang SW</div></div>
<div class="dm"><div class="v" id="in2">–</div><div class="l">Eingang SW2</div></div>
<div class="ao"><div class="v" id="ex">–</div><div class="l" id="exl">Add-on-Taster</div></div>
<div><div class="v" id="rs">–</div><div class="l">WLAN</div></div>
<div><div class="v" id="ip">–</div><div class="l">IP</div></div>
<div><div class="v" id="up">–</div><div class="l">Laufzeit</div></div>
<div><div class="v" id="fl">–</div><div class="l">Flash</div></div>
</div></div><div class="l" id="src" style="margin-top:12px"></div></div>
<div class="card lt"><h2>Licht</h2>
<label>Helligkeit: <b id="brv">–</b></label><input type="range" id="bri" min="1" max="100" onchange="setL({brightness:+this.value})" oninput="$('brv').textContent=this.value+' %'">
<div class="cto"><label>Farbtemperatur: <b id="ctv">–</b></label><input type="range" id="ctr" min="2700" max="6500" step="50" onchange="setL({ct:+this.value})" oninput="$('ctv').textContent=this.value+' K'">
<div class="row"><button class="bt g" onclick="setL({ct:2700})">Warmweiß</button><button class="bt g" onclick="setL({ct:4000})">Neutral</button><button class="bt g" onclick="setL({ct:6500})">Tageslicht</button></div></div>
<div class="dm"><div class="row"><button class="bt g" onclick="setL({brightness:10,on:true})">10 %</button><button class="bt g" onclick="setL({brightness:50,on:true})">50 %</button><button class="bt g" onclick="setL({brightness:100,on:true})">100 %</button></div>
<div class="l" id="stm" style="margin-top:10px"></div></div></div>
<div class="card c2"><h2>Kanal 2</h2><div class="big"><button id="btn2">–</button><div class="grid">
<div class="pm"><div class="v" id="pw2">–</div><div class="l">Leistung</div></div>
<div class="pm"><div class="v" id="en2">–</div><div class="l">Energie</div></div>
<div class="vi"><div class="v" id="cu2">–</div><div class="l">Strom · cos φ</div></div>
<div><div class="v" id="in2b">–</div><div class="l">Eingang SW2</div></div></div></div><div class="l" id="src2" style="margin-top:12px"></div></div>
<div class="card cv"><h2>Rollladen</h2><div class="v" id="cvst" style="margin-bottom:10px">–</div>
<div class="cvb"><button class="bt" onclick="cv('Open')">▲ Auf</button><button class="bt g" onclick="cv('Stop')">■ Stopp</button><button class="bt" onclick="cv('Close')">▼ Zu</button></div>
<div class="cvp"><label>Position: <b id="cvv">–</b></label><input type="range" id="cvpos" min="0" max="100" onchange="cvGo(+this.value)" oninput="$('cvv').textContent=this.value+' %'"></div>
<div class="l" id="cvh" style="margin-top:10px"></div></div>
<div class="card rf"><h2>Funk 433 MHz</h2><table class="rft" id="rfe"></table>
<h2 style="margin-top:18px">Zuletzt empfangen</h2><table class="rft" id="rfr"></table><div class="l" id="rfi" style="margin-top:8px"></div></div>
<div class="card nm"><h2>Zeitschaltung</h2><div class="row">
<div><label>Einschalten für (s)</label><input id="ta" type="number" min="1" value="60"></div></div>
<button class="bt" onclick="pulse()">Für Dauer einschalten</button></div>
</section>

<section class="tab" id="sw"><div class="card rf"><h2 id="r_h">Code speichern</h2><input type="hidden" id="r_id">
<div class="row"><div><label>Name</label><input id="r_name" placeholder="z. B. Fenster Bad, Steckdose Terrasse"></div><div><label>Art</label><select id="r_type" onchange="rfLbl()"><option value="button">Taste (Fernbedienung, Klingel, Bewegungsmelder)</option><option value="contact">Kontakt (Tür/Fenster: offen/zu)</option><option value="switch">Schalter (Funksteckdose: an/aus)</option></select></div></div>
<label id="r_l1">Code</label><input id="r_c1" placeholder="aus „Zuletzt empfangen“ übernehmen">
<div id="r_w2"><label id="r_l2">Code 2</label><input id="r_c2"></div>
<div class="row"><button class="bt" onclick="rfSaveE()">Speichern</button><button class="bt g" onclick="rfNew()">Leeren</button></div></div>
<div class="card rf"><h2>Gespeicherte Codes</h2><table class="rft" id="r_list"></table></div>
<div class="card rf"><h2>Empfang &amp; Senden</h2>
<div class="row"><div><label>Empfangsmodus</label><select id="r_mode"><option value="standard">Standard (EV1527/PT2262 – Fernbedienungen, Melder)</option><option value="protocols">Alle Protokolle (Portisch-Firmware)</option><option value="raw">Rohdaten (Portisch-Firmware)</option></select></div><div><label>Wiederholungen beim Senden</label><input id="r_rep" type="number" min="1" max="40"></div></div>
<button class="bt" onclick="rfCfg()">Übernehmen</button>
<label>Code senden</label><div class="row"><input id="r_tx" placeholder="S:…, P:… oder R:…"><button class="bt" style="flex:0;margin-top:0" onclick="rfTx()">Senden</button></div>
<button class="bt g" onclick="run(()=>rpc('RF.Beep',{ms:200}),'Piep')">Piepton</button></div>
<div class="card" id="swc"><h2 id="swh">Schalter</h2>
<div class="chs"><label>Kanal</label><select id="s_ch" onchange="fillSw()"><option value="0">Kanal 1 (Relais 1, Eingang SW1)</option><option value="1">Kanal 2 (Relais 2, Eingang SW2)</option></select></div>
<p class="mt" style="display:none">Messfirmware: Das Relais bleibt eingeschaltet und wird nicht ferngeschaltet. Nach einer Abschaltung wegen Überlast oder Übertemperatur schaltet es nur der Taster bzw. der SW-Eingang am Gerät wieder ein.</p>
<label>Name</label><input id="s_name">
<div class="row"><div class="so nm"><label>Eingangsmodus</label><select id="s_in_mode">
<option value="follow">Folgen (Schalter = Relais)</option><option value="flip">Umschalten bei jeder Flanke</option>
<option value="momentary">Taster (toggle bei Druck)</option><option value="detached">Getrennt (nur Ereignis)</option></select></div>
<div class="nm"><label id="s_isl">Zustand nach Stromausfall</label><select id="s_initial_state">
<option value="off">Aus</option><option value="on">An</option><option value="restore_last">Letzter Zustand</option><option value="match_input" class="so">Wie Eingang</option></select></div></div>
<div class="row nm"><div><label class="ck"><input type="checkbox" id="s_auto_off">Auto-Aus nach (s)</label><input id="s_auto_off_delay" type="number" step="0.1"></div>
<div><label class="ck"><input type="checkbox" id="s_auto_on">Auto-An nach (s)</label><input id="s_auto_on_delay" type="number" step="0.1"></div></div>
<div class="row"><div class="pm"><label>Abschaltleistung (W, 0 = aus)</label><input id="s_power_limit" type="number"></div>
<div class="pm"><label>Abschalttemperatur (°C)</label><input id="s_temp_limit" type="number"></div></div>
<div class="lt"><div class="row"><div><label>Überblendzeit (s)</label><input id="l_tr" type="number" step="0.1" min="0"></div>
<div class="cto"><label class="ck" style="margin-top:34px"><input type="checkbox" id="l_ctinv">Warm-/Kaltkanal tauschen</label></div></div>
<div class="l cto">WLAN-Reset: Lampe 5× kurz hintereinander aus- und einschalten (jeweils unter 6 s an).</div></div>
<div class="dm"><h2 style="margin-top:20px">Dimmen</h2>
<div class="row"><div><label>Minimale Helligkeit (%, gegen Flackern)</label><input id="d_min" type="number" min="0" max="99"></div>
<div><label>Maximale Helligkeit (%)</label><input id="d_max" type="number" min="1" max="100"></div></div>
<div class="row"><div><label>Dimmart</label><select id="d_edge"><option value="0">Phasenabschnitt (LED, Standard)</option><option value="1">Phasenanschnitt (Halogen, Trafo)</option></select></div>
<div><label>Glättung im Dimmer (1–5)</label><input id="d_fade" type="number" min="1" max="5"></div>
<div><label>Dimmgeschwindigkeit Taster (1–5)</label><input id="d_bfade" type="number" min="1" max="5"></div></div>
<div class="l">Taster: kurz drücken = an/aus, gedrückt halten = dimmen (Richtung wechselt jedes Mal). Zwei Taster: SW1 an/heller, SW2 aus/dunkler.</div></div>
<div class="io"><h2 style="margin-top:20px" id="ioh">Eingang SW</h2>
<div class="row"><div><label>Typ</label><select id="i_type"><option value="switch">Schalter</option><option value="button">Taster</option></select></div>
<div><label class="ck" style="margin-top:34px"><input type="checkbox" id="i_invert">Eingang invertieren</label></div></div>
<label>Eingangsfilter</label><select id="i_filter"><option value="ac">Wechselspannung (50-Hz-Halbwellen überbrücken)</option><option value="dc">Nur Pegel (Gleichspannung / feste Pegel)</option></select>
<button class="bt g" onclick="diag(0)">Eingang messen (roh)</button><pre id="i_diag" style="display:none"></pre>
<div class="dm"><h2 style="margin-top:20px">Eingang SW2</h2>
<div class="row"><div><label>Name</label><input id="i2_name"></div><div><label>Typ</label><select id="i2_type"><option value="button">Taster</option><option value="switch">Schalter</option></select></div>
<div><label class="ck" style="margin-top:34px"><input type="checkbox" id="i2_invert">Eingang invertieren</label></div></div>
<div class="l">Der Eingangsfilter gilt für SW1 und SW2.</div>
<button class="bt g" onclick="diag(1)">SW2 messen (roh)</button></div></div>
<button class="bt" onclick="saveSw()">Speichern</button></div>
<div class="card cv"><h2>Rollladen</h2>
<label>Name</label><input id="c_name" placeholder="z. B. Rollladen Wohnzimmer">
<div class="row"><div><label>Eingänge</label><select id="c_in_mode"><option value="dual">Zwei Eingänge: SW1 auf, SW2 zu</option><option value="single">Ein Eingang: SW1 auf / stopp / zu</option><option value="detached">Getrennt (nur Ereignisse)</option></select></div>
<div><label>Art der Eingänge</label><select id="c_itype"><option value="switch">Schalter (an = fahren, aus = stopp)</option><option value="button">Taster (drücken = fahren / stopp)</option></select></div></div>
<div class="row"><div><label>Nach Stromausfall</label><select id="c_initial"><option value="stopped">Stehen bleiben</option><option value="open">Öffnen</option><option value="closed">Schließen</option></select></div>
<div><label>Eingangsfilter</label><select id="c_filter"><option value="ac">Wechselspannung</option><option value="dc">Nur Pegel</option></select></div></div>
<div class="row"><div><label>Max. Fahrzeit auf (s)</label><input id="c_mo" type="number" step="0.1"></div><div><label>Max. Fahrzeit zu (s)</label><input id="c_mc" type="number" step="0.1"></div></div>
<div class="row"><div><label>Fahrzeit auf, kalibriert (s, 0 = ohne Position)</label><input id="c_to" type="number" step="0.01"></div><div><label>Fahrzeit zu, kalibriert (s)</label><input id="c_tc" type="number" step="0.01"></div></div>
<div class="row"><div><label>Endlage erkannt unter (W)</label><input id="c_it" type="number" step="0.5"></div><div><label>… für (s)</label><input id="c_ic" type="number" step="0.1"></div></div>
<div class="row"><div><label>Abschaltleistung (W, 0 = aus)</label><input id="c_pl" type="number"></div><div><label>Abschalttemperatur (°C)</label><input id="c_tl" type="number"></div></div>
<label class="ck"><input type="checkbox" id="c_swap">Eingänge tauschen</label>
<label class="ck"><input type="checkbox" id="c_inv">Richtungen tauschen (Relais 1 = zu)</label>
<label class="ck"><input type="checkbox" id="c_ob">Hinderniserkennung</label>
<div class="row"><div><label>Hindernis über (W)</label><input id="c_ot" type="number"></div><div><label>nach Anlauf (s)</label><input id="c_oh" type="number" step="0.1"></div>
<div><label>Dann</label><select id="c_oa"><option value="stop">Anhalten</option><option value="reverse">Kurz zurückfahren</option></select></div></div>
<div class="l" style="margin-top:8px">Kalibrieren: Der Rollladen fährt ganz zu, ganz auf und wieder zu und misst dabei die Fahrzeiten. Danach kann er Positionen anfahren. Die Endlagen erkennt Hermit OS am Abschalten des Motors.</div>
<div class="row"><button class="bt" onclick="saveCv()">Speichern</button><button class="bt g" onclick="if(confirm('Kalibrierfahrt starten? Der Rollladen fährt ganz zu, auf und wieder zu.'))cv('Calibrate')">Kalibrieren</button></div></div>
<div class="card ao"><h2>Add-on-Taster (input:100)</h2>
<div class="l">Taster zwischen DATA und GND des Add-ons.</div>
<label>Name</label><input id="x_name">
<div class="row"><div><label>Typ</label><select id="x_type"><option value="button">Taster</option><option value="switch">Schalter</option></select></div>
<div><label>Wirkung aufs Relais</label><select id="x_rel"><option value="none">Keine (nur Ereignis/Aktion)</option><option value="momentary">Umschalten bei Druck</option><option value="flip">Umschalten bei jeder Flanke</option><option value="follow">Relais folgt</option></select></div></div>
<label class="ck"><input type="checkbox" id="x_inv">Eingang invertieren</label>
<label class="ck"><input type="checkbox" id="x_out">Add-on-Ausgang invertieren (wenn der Eingang dauerhaft „gedrückt“ zeigt)</label>
<button class="bt" onclick="saveEx()">Speichern</button></div></section>

<section class="tab" id="wh"><div class="card"><h2>Aktionen (Webhooks)</h2>
<table id="hooks"></table></div>
<div class="card"><h2>Neue Aktion</h2>
<div class="row"><div><label>Ereignis</label><select id="h_ev"></select></div><div><label>Name</label><input id="h_name"></div>
<div id="h_cidw" style="display:none"><label id="h_cidl">Eingang</label><select id="h_cid"></select></div>
<div id="h_thrw" style="display:none"><label>Schwelle (W)</label><input id="h_thr" type="number"></div></div>
<label>URLs (eine pro Zeile, max. 5)</label><textarea id="h_urls" placeholder="http://192.168.0.60/rpc/Switch.Set?id=0&on=true"></textarea>
<div class="l" style="margin-top:6px">Platzhalter: <code>${event}</code> <code>${output}</code> <code>${input}</code> <code>${apower}</code> <code>${aenergy}</code> <code>${tC}</code> <code>${id}</code> <code>${name}</code> <code>${ip}</code></div>
<button class="bt" onclick="addh()">Anlegen</button></div></section>

<section class="tab" id="wf"><div class="card"><h2>WLAN</h2>
<label>SSID</label><input id="w_ssid" list="w_list"><datalist id="w_list"></datalist>
<button class="bt g" id="scan" onclick="scan()">Netze suchen</button>
<label>Passwort</label><input id="w_pass" type="password" placeholder="leer lassen = unverändert">
<label>IPv4</label><select id="w_mode" onchange="stat()"><option value="dhcp">DHCP</option><option value="static">Statisch</option></select>
<div id="w_static"><div class="row"><div><label>IP</label><input id="w_ip"></div><div><label>Netzmaske</label><input id="w_nm" placeholder="255.255.255.0"></div></div>
<div class="row"><div><label>Gateway</label><input id="w_gw"></div><div><label>DNS</label><input id="w_dns"></div></div></div>
<label class="ck"><input type="checkbox" id="w_roam">Stärksten Access Point wählen (mehrere APs mit gleichem Namen)</label>
<div class="row"><div><label>Bei schwachem Empfang unter … dBm nach besserem AP suchen</label><input id="w_rthr" type="number" min="-90" max="-50"></div><div><label>Verbunden mit</label><div class="v" id="w_bss" style="font-size:14px">–</div></div></div>
<label>Passwort Access-Point (mind. 8 Zeichen, leer = offen)</label><input id="w_ap" type="password" placeholder="leer lassen = unverändert">
<button class="bt" onclick="saveWifi()">Speichern &amp; verbinden</button></div></section>

<section class="tab" id="sy"><div class="card"><h2>Gerät</h2>
<label>Gerätename</label><input id="y_name">
<div class="row"><div><label>Zeitzone (POSIX)</label><input id="y_tz"></div><div><label>NTP-Server</label><input id="y_sntp"></div></div>
<label class="ck pm"><input type="checkbox" id="y_led">Status-LED zeigt Relais</label>
<button class="bt" onclick="saveSys()">Speichern</button></div>
<div class="card"><h2>Zugangsschutz</h2>
<label>Passwort für Benutzer <code>admin</code></label><input id="y_auth" type="password">
<div class="row"><button class="bt" onclick="setAuth(0)">Passwort setzen</button><button class="bt g" onclick="setAuth(1)">Passwort entfernen</button></div></div>
<div class="card pm"><h2>Leistung kalibrieren</h2>
<div class="cvx"><label>Kanal</label><select id="y_ch" onchange="showCal()"><option value="0">Kanal 1 (Relais 1)</option><option value="1">Kanal 2 (Relais 2)</option></select></div>
<div class="v" id="y_cf" style="font-size:16px">–</div><div class="l dm">Der Dimmer misst über seinen STM32. Der Faktor korrigiert diesen Messwert.</div>
<div class="l">Der Faktor bleibt auch bei Werkseinstellungen und Firmware-Updates erhalten.</div>
<label>a) Referenzlast: Relais an, gemessene Wirkleistung (W)</label><input id="y_cal" type="number" step="0.1">
<button class="bt" onclick="calib({apower:+$('y_cal').value})">Mit Referenz kalibrieren</button>
<div class="vi"><label>Spannung mit Messgerät (V)</label><input id="y_vr" type="number" step="0.1">
<label>Strom mit Messgerät (A) – Last an, Relais an</label><input id="y_ir" type="number" step="0.001">
<button class="bt" onclick="calVI()">Spannung/Strom kalibrieren</button></div>
<div class="tpc"><label>b) Wert aus Tasmota übernehmen (Konsole: <code>PowerCal</code>)</label><input id="y_tpc" type="number" placeholder="12530">
<button class="bt" onclick="calib({tasmota_powercal:+$('y_tpc').value})">Tasmota-Wert übernehmen</button></div>
<label>c) Faktor direkt</label><input id="y_fac" type="number" step="0.0001">
<div class="row"><button class="bt" onclick="calib({factor:+$('y_fac').value})">Faktor setzen</button>
<button class="bt g" onclick="calib({reset:true})">Standard wiederherstellen</button>
<button class="bt g" onclick="rstc()">Energiezähler nullen</button></div></div>
<div class="card"><h2>Automatische Updates</h2>
<div class="v" id="u_st" style="font-size:16px">–</div><div class="l" id="u_info"></div>
<div class="row"><div><label>Modus</label><select id="u_mode"><option value="off">Aus</option><option value="check">Nur prüfen und anzeigen</option><option value="install">Automatisch installieren</option></select></div>
<div><label>Uhrzeit (Stunde)</label><input id="u_hour" type="number" min="0" max="23"></div></div>
<label>Update-Quelle (update.json)</label><input id="u_url">
<label>GitHub-Token (nur bei privatem Fork)</label><input id="u_tok" type="password">
<div class="l">Das Relais bleibt während Update und Neustart in seinem Zustand. Die Firmware wird vor dem Umschalten per SHA-256 geprüft.</div>
<div class="row"><button class="bt" onclick="saveUpd()">Speichern</button><button class="bt g" onclick="chkUpd()">Jetzt prüfen</button><button class="bt g" id="u_inst" onclick="instUpd()">Jetzt installieren</button></div></div>
<div class="card"><h2>Cloud / MQTT</h2>
<div class="v" id="q_st" style="font-size:16px">–</div>
<label class="ck"><input type="checkbox" id="q_en">MQTT einschalten (Hermit Cloud, Home Assistant, Node-RED)</label>
<div class="row"><div><label>Server (host:port)</label><input id="q_srv" placeholder="192.168.0.50:1883"></div><div><label>Topic-Präfix</label><input id="q_pre"></div></div>
<div class="row"><div><label>Benutzer</label><input id="q_user"></div><div><label>Passwort</label><input id="q_pass" type="password"></div></div>
<div class="l">Topics wie Shelly Gen2: &lt;präfix&gt;/rpc, /events/rpc, /status/…, /online. Aus = keine Verbindung, das Gerät arbeitet wie bisher.</div>
<button class="bt" onclick="saveMq()">Speichern</button></div>
<div class="card"><h2>Firmware manuell</h2>
<form method="POST" action="/update" enctype="multipart/form-data"><input type="file" name="firmware" accept=".bin">
<button class="bt">Hochladen &amp; flashen</button></form></div>
<div class="card"><h2>Neustart</h2><div class="row">
<button class="bt g" onclick="run(()=>rpc('Shelly.Reboot'),'Startet neu …')">Neustart</button>
<button class="bt r" onclick="if(confirm('Alle Einstellungen löschen?'))run(()=>rpc('Shelly.FactoryReset'),'Werkseinstellungen – AP startet')">Werkseinstellungen</button></div></div>
</section>

<section class="tab" id="rp"><div class="card"><h2>RPC-Konsole</h2>
<label>Methode</label><input id="r_m" value="Shelly.GetStatus" list="r_list"><datalist id="r_list"></datalist>
<label>Parameter (JSON)</label><textarea id="r_p">{}</textarea>
<button class="bt" onclick="sendRpc()">Senden</button><pre id="r_o"></pre></div></section>
</main><div id="toast"></div>
<script>
const $=i=>document.getElementById(i);let PM=false,AO=false,SUP=[];
let Q=Promise.resolve();function rpc(m,p){const r=Q.then(()=>rpc0(m,p));Q=r.catch(()=>{});return r}
async function rpc0(m,p){const r=await fetch('/rpc',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({id:Date.now()%1e6,method:m,params:p||{}})});const j=await r.json();if(j.error)throw new Error(j.error.message||('Fehler '+j.error.code));return j.result}
function toast(t,bad){const e=$('toast');e.textContent=t;e.style.background=bad?'#b33':'#2a7a3a';e.style.display='block';clearTimeout(e._t);e._t=setTimeout(()=>e.style.display='none',2800)}
async function run(f,ok){try{await f();if(ok)toast(ok)}catch(e){toast(e.message,1)}}
function esc(s){return String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]))}
function fmt(v,d,u){return v==null||isNaN(v)?'–':(+v).toFixed(d)+' '+u}
function upt(s){const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return(d?d+'d ':'')+h+'h '+m+'m'}
document.querySelectorAll('nav a').forEach(a=>a.onclick=()=>{document.querySelectorAll('nav a,.tab').forEach(x=>x.classList.remove('a'));a.classList.add('a');$(a.dataset.t).classList.add('a');if(a.dataset.t=='wh')loadHooks()});
let LT=false,LS=null,MT=false,DM=false,CH2=false,CV=false,CFG=null,RF=false,RFE=[];
const RFT={button:'Taste',contact:'Kontakt',switch:'Schalter'};
function ago(t){return t==null?'':t<60?t+' s':t<3600?Math.floor(t/60)+' min':Math.floor(t/3600)+' h'}
function rfShow(r){RFE=r.entries;$('rfe').innerHTML=RFE.length?RFE.map(e=>`<tr><td><b>${esc(e.name||RFT[e.type]+' '+e.id)}</b><div class="l">${RFT[e.type]} · id ${e.id}${e.last_ago!=null?' · vor '+ago(e.last_ago):''}</div></td><td class="v" style="font-size:15px">${e.type=='button'?'':e.state==null?'–':e.type=='contact'?(e.state?'offen':'zu'):(e.state?'AN':'AUS')}</td><td style="text-align:right">${e.type=='switch'?`<button class="bt" onclick="rfSw(${e.id},true)">An</button><button class="bt g" onclick="rfSw(${e.id},false)">Aus</button>`:e.type=='button'?`<button class="bt g" onclick="rfSend(${e.id})">Senden</button>`:''}</td></tr>`).join(''):'<tr><td class="l">Noch nichts gespeichert – Fernbedienung drücken, dann unten übernehmen.</td></tr>';
$('rfr').innerHTML=r.recent.length?r.recent.map(x=>{const e=x.id!=null&&RFE.find(k=>k.id==x.id);return`<tr><td><code title="${esc(x.code)}">${esc(x.short)}</code><div class="l">vor ${ago(x.ago)}${x.count>1?' · '+x.count+'×':''}</div></td><td>${e?'<b>'+esc(e.name||RFT[e.type]+' '+e.id)+'</b>':''}</td><td style="text-align:right">${e?'':`<button class="bt" onclick="rfTake('${x.code}')">Übernehmen</button>`}<button class="bt g" onclick="rfTxc('${x.code}')">Senden</button></td></tr>`}).join(''):'<tr><td class="l">Noch kein Funkcode empfangen</td></tr>';
$('rfi').textContent='Modus: '+({standard:'Standard',protocols:'alle Protokolle',raw:'Rohdaten'}[r.mode]||r.mode)+' · Rahmen vom Funkchip: '+r.frames+(r.errors?' · Fehler: '+r.errors:'')+(r.last_rx_ago!=null?' · zuletzt vor '+ago(r.last_rx_ago):'')}
function rfSw(id,on){run(async()=>{await rpc('Switch.Set',{id,on});poll()})}
function rfSend(id){run(()=>rpc('RF.Send',{id}),'Gesendet')}
function rfTxc(c){run(()=>rpc('RF.Send',{code:c}),'Gesendet')}
function rfTx(){rfTxc($('r_tx').value.trim())}
function rfLbl(){const t=$('r_type').value;$('r_l1').textContent=t=='contact'?'Code „offen“':t=='switch'?'Code „an“':'Code';$('r_l2').textContent=t=='contact'?'Code „geschlossen“ (leer, wenn der Melder nur „offen“ sendet)':'Code „aus“';$('r_w2').style.display=t=='button'?'none':''}
function rfNew(){$('r_id').value='';['r_name','r_c1','r_c2'].forEach(k=>$(k).value='');$('r_h').textContent='Code speichern';rfLbl()}
function rfTake(c){if($('r_c1').value&&$('r_type').value!='button')$('r_c2').value=c;else $('r_c1').value=c;document.querySelector('nav a[data-t=sw]').click();toast('Code übernommen')}
function rfEdit(id){const e=CFG.rf.entries.find(x=>x.id==id);if(!e)return;$('r_id').value=e.id;$('r_name').value=e.name||'';$('r_type').value=e.type;$('r_c1').value=e.code1;$('r_c2').value=e.code2||'';$('r_h').textContent='Bearbeiten: id '+e.id;rfLbl();scrollTo(0,0)}
async function rfLoadCfg(){CFG.rf=await rpc('RF.GetConfig');const c=CFG.rf;$('r_mode').value=c.mode;$('r_rep').value=c.repeats;
$('r_list').innerHTML=c.entries.length?c.entries.map(e=>`<tr><td><b>${esc(e.name||'–')}</b><div class="l">${RFT[e.type]} · id ${e.id}</div></td><td style="word-break:break-all"><code>${esc(e.code1)}</code>${e.code2?'<br><code>'+esc(e.code2)+'</code>':''}</td><td style="text-align:right;white-space:nowrap"><button class="bt g" onclick="rfEdit(${e.id})">✎</button><button class="bt r" onclick="rfDel(${e.id})">✕</button></td></tr>`).join(''):'<tr><td class="l">Keine</td></tr>'}
function rfSaveE(){run(async()=>{const id=$('r_id').value,p={name:$('r_name').value,type:$('r_type').value,code1:$('r_c1').value.trim(),code2:$('r_type').value=='button'?'':$('r_c2').value.trim()};if(id!=='')await rpc('RF.Update',Object.assign({id:+id},p));else await rpc('RF.Add',p);rfNew();await rfLoadCfg();poll()},'Gespeichert')}
function rfDel(id){if(confirm('Eintrag löschen?'))run(async()=>{await rpc('RF.Delete',{id});await rfLoadCfg();poll()},'Gelöscht')}
function rfCfg(){run(async()=>{await rpc('RF.SetConfig',{config:{mode:$('r_mode').value,repeats:+$('r_rep').value}});await rfLoadCfg()},'Übernommen')}
const IM_SW='<option value="follow">Folgen (Schalter = Relais)</option><option value="flip">Umschalten bei jeder Flanke</option><option value="momentary">Taster (toggle bei Druck)</option><option value="detached">Getrennt (nur Ereignis)</option>';
const IM_DM='<option value="dim">Ein Taster: kurz an/aus, halten = dimmen</option><option value="dual_dim">Zwei Taster: SW1 an/heller, SW2 aus/dunkler</option><option value="follow">Schalter: Licht folgt SW1</option><option value="flip">Schalter: jede Flanke an SW1 schaltet um</option><option value="detached">Getrennt (nur Ereignisse)</option>';
function cmp(){return LT?'CCT':DM?'Light':CV?'Cover':'Switch'}
const SH=(c,v,d)=>document.querySelectorAll(c).forEach(e=>e.style.display=v?(d||'block'):'none');
const CVN={open:'Offen',closed:'Geschlossen',opening:'Öffnet …',closing:'Schließt …',stopped:'Angehalten',calibrating:'Kalibriert …'};
function cv(m){run(async()=>{await rpc('Cover.'+m,{id:0});poll()})}
function cvGo(p){run(async()=>{await rpc('Cover.GoToPosition',{id:0,pos:p});poll()})}
function showLT(){const L=LT||DM;document.querySelectorAll('.lt').forEach(e=>e.style.display=L?'block':'none');document.querySelectorAll('.dm').forEach(e=>e.style.display=DM?'block':'none');document.querySelectorAll('.cto').forEach(e=>e.style.display=DM?'none':'');document.querySelectorAll('.tpc').forEach(e=>e.style.display=(DM||CH2||CV)?'none':'');document.querySelectorAll('.so,.io').forEach(e=>e.style.display=LT?'none':'');document.querySelectorAll('option.so').forEach(e=>e.style.display=(LT||DM)?'none':'');$('inl').textContent=$('ioh').textContent=(DM||CH2||CV)?'Eingang SW1':'Eingang SW';SH('.c2',CH2);SH('.cv',CV);SH('.chs',CH2);SH('.cvx',CH2||CV);$('swc').style.display=CV?'none':'';$('btn').style.display=CV?'none':'';if(CV)SH('.nm',false);if(CH2)$('ioh').textContent='Eingang (zum gewählten Kanal)';SH('.rf',RF);if(RF){$('swc').style.display='none';$('btn').style.display='none';SH('.nm',false)}
if(DM&&!$('s_in_mode').dataset.dm){$('s_in_mode').innerHTML=IM_DM;$('s_in_mode').dataset.dm=1}$('swh').textContent=L?'Licht':'Schalter';$('swtab').textContent=RF?'Funk':CV?'Rollladen':L?'Licht':'Schalter';$('s_isl').textContent=LT?'Nach Einschalten am Wandschalter':'Zustand nach Stromausfall';$('s_name').placeholder=L?'z. B. Deckenlampe Bad':'';if(MT)document.querySelectorAll('.nm').forEach(e=>e.style.display='none')}
function setL(p){run(async()=>{p.id=0;await rpc(cmp()+'.Set',p);poll()})}
async function poll(){try{const s=await rpc('Shelly.GetStatus');let sw=s['switch:0']||s['cct:0']||s['light:0']||s['cover:0'];RF=!!s.rf;LT=!!s['cct:0'];DM=!!s['light:0'];CH2=!RF&&!!s['switch:1'];CV=!!s['cover:0'];LS=s['cct:0']||s['light:0']||null;showLT();if(RF){rfShow(s.rf);sw={source:s.rf.mode}}
if(DM){$('in2').textContent=s['input:1']?(s['input:1'].state?'AN':'AUS'):'–';if(document.activeElement!==$('bri')){$('bri').value=sw.brightness;$('brv').textContent=sw.brightness+' %'}}
if(LT){if(document.activeElement!==$('bri')){$('bri').value=sw.brightness;$('brv').textContent=sw.brightness+' %'}if(document.activeElement!==$('ctr')){$('ctr').value=sw.ct;$('ctv').textContent=sw.ct+' K'}}
if(CH2){const k=s['switch:1'];$('btn2').className=k.output?'on':'';$('btn2').textContent=k.output?'AN':'AUS';$('pw2').textContent=fmt(k.apower,1,'W');$('en2').textContent=k.aenergy?fmt(k.aenergy.total/1000,3,'kWh'):'–';$('cu2').textContent=k.current==null?'–':fmt(k.current,3,'A')+(k.pf!=null?' · '+k.pf.toFixed(2):'');$('in2b').textContent=s['input:1']?(s['input:1'].state?'AN':'AUS'):'–';$('src2').textContent='Quelle: '+k.source+(k.timer_duration?' · Timer '+k.timer_duration+' s':'')+(k.errors?' · Fehler: '+k.errors.join(', '):'')}
if(CV){$('cvst').textContent=(CVN[sw.state]||sw.state)+(sw.current_pos!=null?' · '+sw.current_pos+' %':'');SH('.cvp',sw.pos_control);if(sw.pos_control&&sw.current_pos!=null&&document.activeElement!==$('cvpos')){$('cvpos').value=sw.current_pos;$('cvv').textContent=sw.current_pos+' %'}
$('cvh').textContent=sw.pos_control?'':'Ohne Kalibrierung nur Auf/Stopp/Zu – unter „Rollladen“ kalibrieren.';$('in').textContent=s['input:0']?(s['input:0'].state?'AN':'AUS'):'–'}
MT=!!sw.meter_only;$('btn').className=sw.output?'on':'';$('btn').textContent=sw.output?'AN':(MT?'AUSGELÖST':'AUS');$('btn').title=MT?'Reset nur am Taster/SW des Geräts':'';document.querySelectorAll('.nm').forEach(e=>e.style.display=(MT||CV||RF)?'none':'');document.querySelectorAll('.mt').forEach(e=>e.style.display=MT?'':'none');PM='apower' in sw;
$('pw').textContent=fmt(sw.apower,1,'W');$('en').textContent=sw.aenergy?fmt(sw.aenergy.total/1000,3,'kWh'):'–';
$('tc').textContent=sw.temperature?fmt(sw.temperature.tC,1,'°C'):'–';$('in').textContent=s['input:0']?(s['input:0'].state?'AN':'AUS'):'–';$('inw').style.display=s['input:0']?'':'none';AO=!!s['input:100'];if(AO)$('ex').textContent=s['input:100'].state?'AN':'AUS';document.querySelectorAll('.ao').forEach(e=>e.style.display=AO?'block':'none');
if(s.mqtt)$('q_st').textContent=$('q_en').checked?(s.mqtt.connected?'verbunden':'nicht verbunden'):'aus';$('rs').textContent=s.wifi.rssi!=null?s.wifi.rssi+' dBm':(s.wifi.ap_active?'AP':'–');$('w_bss').textContent=s.wifi.bssid?s.wifi.bssid+' · Kanal '+s.wifi.channel+' · '+s.wifi.rssi+' dBm'+(s.wifi.roams?' · '+s.wifi.roams+'× gewechselt':''):'–';$('ip').textContent=s.wifi.sta_ip||'–';$('up').textContent=upt(s.sys.uptime);
const fr=s.sys.flash_real,fh=s.sys.flash_size;$('fl').textContent=(fr/1048576)+' MB'+(fr<fh?' ⚠':'');$('fl').title='Header '+(fh/1048576)+' MB, Chip-ID '+s.sys.flash_id+', OTA frei '+Math.round(s.sys.ota_free/1024)+' KB';
$('src').textContent=RF?(s.sys.time||''):(CH2?'Kanal 1 · ':'')+'Quelle: '+sw.source+(sw.timer_duration?' · Timer '+sw.timer_duration+' s':'')+(sw.errors?' · Fehler: '+sw.errors.join(', '):'')+(s.sys.time?' · '+s.sys.time:'');
document.querySelectorAll('.pm').forEach(e=>e.style.display=PM?'':'none');const VI='voltage' in sw;if(VI){$('vo').textContent=fmt(sw.voltage,1,'V');$('cu').textContent=sw.current==null?'–':fmt(sw.current,3,'A')+(sw.pf!=null?' · '+sw.pf.toFixed(2):'')}document.querySelectorAll('.vi').forEach(e=>e.style.display=VI?'block':'none')}catch(e){}}
$('btn').onclick=()=>MT||run(async()=>{await rpc(cmp()+'.Toggle',{id:0});poll()});
$('btn2').onclick=()=>run(async()=>{await rpc('Switch.Toggle',{id:1});poll()});
function pulse(){run(async()=>{await rpc(cmp()+'.Set',{id:0,on:true,toggle_after:+$('ta').value});poll()})}
function stat(){$('w_static').style.display=$('w_mode').value=='static'?'':'none'}
function fillSw(){const ch=CH2?+$('s_ch').value:0,sw=CFG['switch:'+ch],inp=CFG['input:'+ch]||{};
['in_mode','initial_state','auto_on_delay','auto_off_delay','power_limit','temp_limit'].forEach(k=>$('s_'+k).value=sw[k]??'');
$('s_name').value=sw.name||'';$('s_auto_on').checked=sw.auto_on;$('s_auto_off').checked=sw.auto_off;$('i_type').value=inp.type;$('i_invert').checked=inp.invert;$('i_filter').value=inp.filter||'ac'}
function fillCv(){const v=CFG['cover:0'],i0=CFG['input:0']||{};$('c_name').value=v.name||'';$('c_in_mode').value=v.in_mode;$('c_initial').value=v.initial_state;$('c_itype').value=i0.type||'switch';$('c_filter').value=i0.filter||'ac';
$('c_mo').value=v.maxtime_open;$('c_mc').value=v.maxtime_close;$('c_to').value=v.pos_calibration.open_time;$('c_tc').value=v.pos_calibration.close_time;$('c_it').value=v.motor.idle_power_thr;$('c_ic').value=v.motor.idle_confirm_period;
$('c_pl').value=v.power_limit;$('c_tl').value=v.temp_limit;$('c_swap').checked=v.swap_inputs;$('c_inv').checked=v.invert_directions;const o=v.obstruction_detection;$('c_ob').checked=o.enable;$('c_ot').value=o.power_thr;$('c_oh').value=o.holdoff;$('c_oa').value=o.action}
function saveCv(){run(async()=>{const n=i=>+$(i).value;await rpc('Cover.SetConfig',{id:0,config:{name:$('c_name').value,in_mode:$('c_in_mode').value,initial_state:$('c_initial').value,maxtime_open:n('c_mo'),maxtime_close:n('c_mc'),power_limit:n('c_pl'),temp_limit:n('c_tl'),swap_inputs:$('c_swap').checked,invert_directions:$('c_inv').checked,
motor:{idle_power_thr:n('c_it'),idle_confirm_period:n('c_ic')},obstruction_detection:{enable:$('c_ob').checked,power_thr:n('c_ot'),holdoff:n('c_oh'),action:$('c_oa').value},pos_calibration:{open_time:n('c_to'),close_time:n('c_tc')}}});
for(const id of[0,1])await rpc('Input.SetConfig',{id,config:{type:$('c_itype').value,filter:$('c_filter').value}});CFG=await rpc('Shelly.GetConfig');fillCv()},'Gespeichert')}
async function loadCfg(){const c=await rpc('Shelly.GetConfig'),sw=c['switch:0']||c['cct:0']||c['light:0']||c['cover:0']||{},inp=c['input:0']||{};CFG=c;RF=!!c.rf;LT=!!c['cct:0'];DM=!!c['light:0'];CH2=!RF&&!!c['switch:1'];CV=!!c['cover:0'];if(RF){rfLoadCfg();rfLbl()}MT=!!sw.meter_only;showLT();if(CV)fillCv();if(LT||DM){$('l_tr').value=sw.transition_duration;$('l_ctinv').checked=!!sw.ct_invert}
if(DM){$('d_min').value=sw.range_map[0];$('d_max').value=sw.range_map[1];$('d_edge').value=sw.leading_edge?1:0;$('d_fade').value=sw.fade_rate;$('d_bfade').value=sw.button_fade_rate;const i2=c['input:1']||{};$('i2_name').value=i2.name||'';$('i2_type').value=i2.type||'button';$('i2_invert').checked=!!i2.invert;
stmLine()}
if(!CV&&!RF){['in_mode','initial_state','auto_on_delay','auto_off_delay','power_limit','temp_limit'].forEach(k=>$('s_'+k).value=sw[k]??'');
$('s_name').value=sw.name||'';$('s_auto_on').checked=sw.auto_on;$('s_auto_off').checked=sw.auto_off;$('i_type').value=inp.type;$('i_invert').checked=inp.invert;$('i_filter').value=inp.filter||'ac';if(CH2)fillSw()}
showCal();
$('y_name').value=c.sys.device.name||'';$('y_tz').value=c.sys.location.tz;$('y_sntp').value=c.sys.sntp.server;$('y_led').checked=c.sys.ui.led;
const x=c['input:100'];if(x){AO=true;$('x_name').value=x.name||'';$('x_type').value=x.type;$('x_rel').value=x.relay_action;$('x_inv').checked=x.invert;$('x_out').checked=x.addon_out_invert}document.querySelectorAll('.ao').forEach(e=>e.style.display=AO?'block':'none');
const w=c.wifi.sta;$('w_ssid').value=w.ssid||'';$('w_mode').value=w.ipv4mode;$('w_ip').value=w.ip||'';$('w_nm').value=w.netmask||'';$('w_gw').value=w.gw||'';$('w_dns').value=w.nameserver||'';stat();if(c.wifi.roam){$('w_roam').checked=c.wifi.roam.enable;$('w_rthr').value=c.wifi.roam.rssi_thr}
const d=await rpc('Shelly.GetDeviceInfo');$('t').textContent=d.name||d.id;$('ver').textContent='Hermit OS '+d.ver+' · '+d.model+' · '+d.id;document.title=d.name||d.id;
showUpd(c.sys.updates,d.ver);showMq(c.mqtt);$('y_auth').placeholder=d.auth_en?'Passwort ist gesetzt':'kein Passwort gesetzt';
const lm=await rpc('Shelly.ListMethods');$('r_list').innerHTML=lm.methods.map(m=>`<option>${m}</option>`).join('')}
function saveEx(){run(()=>rpc('Input.SetConfig',{id:100,config:{name:$('x_name').value,type:$('x_type').value,relay_action:$('x_rel').value,invert:$('x_inv').checked,addon_out_invert:$('x_out').checked}}),'Gespeichert')}
async function stmLine(){try{const t=await rpc('Hermit.Stm');$('stm').textContent='STM32: '+(t.online?(t.proto=='stock'?'Original-Firmware ':'quelloffene Firmware ')+'v'+t.version:'keine Antwort')+' · Rohwert Helligkeit '+t.brightness_raw+'/1000'}catch(e){}}
function diag(id){run(async()=>{const r=await rpc('Input.Diag',{id:id});$('i_diag').style.display='block';$('i_diag').textContent='HIGH-Anteil: '+r.high_pct+' %\nFlanken in 200 ms: '+r.edges+'  (≈ '+r.approx_hz+' Hz)\nErkannter Zustand: '+(r.state?'AN':'AUS')+'  ·  Filter '+r.filter+(r.invert?' · invertiert':'')})}
function saveSw(){if(DM)return run(async()=>{const n=i=>+$(i).value;await rpc('Light.SetConfig',{id:0,config:{name:$('s_name').value,in_mode:$('s_in_mode').value,initial_state:$('s_initial_state').value,auto_on:$('s_auto_on').checked,auto_on_delay:n('s_auto_on_delay'),auto_off:$('s_auto_off').checked,auto_off_delay:n('s_auto_off_delay'),transition_duration:n('l_tr'),range_map:[n('d_min'),n('d_max')],leading_edge:$('d_edge').value=='1',fade_rate:n('d_fade'),button_fade_rate:n('d_bfade'),power_limit:n('s_power_limit'),temp_limit:n('s_temp_limit')}});
await rpc('Input.SetConfig',{id:0,config:{type:$('i_type').value,invert:$('i_invert').checked,filter:$('i_filter').value}});await rpc('Input.SetConfig',{id:1,config:{name:$('i2_name').value,type:$('i2_type').value,invert:$('i2_invert').checked}});stmLine()},'Gespeichert');
if(LT)return run(()=>rpc('CCT.SetConfig',{id:0,config:{name:$('s_name').value,initial_state:$('s_initial_state').value,auto_on:$('s_auto_on').checked,auto_on_delay:+$('s_auto_on_delay').value,auto_off:$('s_auto_off').checked,auto_off_delay:+$('s_auto_off_delay').value,transition_duration:+$('l_tr').value,ct_invert:$('l_ctinv').checked}}),'Gespeichert');
run(async()=>{const n=i=>+$(i).value,ch=CH2?+$('s_ch').value:0;await rpc('Switch.SetConfig',{id:ch,config:{name:$('s_name').value,in_mode:$('s_in_mode').value,initial_state:$('s_initial_state').value,auto_on:$('s_auto_on').checked,auto_on_delay:n('s_auto_on_delay'),auto_off:$('s_auto_off').checked,auto_off_delay:n('s_auto_off_delay'),power_limit:n('s_power_limit'),temp_limit:n('s_temp_limit')}});
await rpc('Input.SetConfig',{id:ch,config:{type:$('i_type').value,invert:$('i_invert').checked,filter:$('i_filter').value}});CFG=await rpc('Shelly.GetConfig')},'Gespeichert')}
async function loadHooks(){await run(async()=>{if(!SUP.length){const s=await rpc('Webhook.ListSupported');SUP=Object.keys(s.types);$('h_ev').innerHTML=SUP.map(e=>`<option>${e}</option>`).join('');$('h_ev').onchange()}
const l=await rpc('Webhook.List');$('hooks').innerHTML=l.hooks.length?l.hooks.map(h=>`<tr><td><input type="checkbox" style="width:auto" ${h.enable?'checked':''} onchange="tgl(${h.id},this.checked)"></td><td><b>${esc(h.event)}</b>${h.event.startsWith('input.')&&AO?(h.cid==100?' · Add-on':' · SW'):''}${h.event.startsWith('input.')&&(DM||CH2||CV)?(h.cid==1?' · SW2':' · SW1'):''}${CH2&&h.event.startsWith('switch.')?(h.cid==1?' · Kanal 2':' · Kanal 1'):''}${RF&&(h.event.startsWith('rf.')||h.event.startsWith('switch.'))&&h.event!='rf.unknown'?' · '+esc(((CFG&&CFG.rf&&CFG.rf.entries.find(e=>e.id==h.cid))||{name:'id '+h.cid}).name||'id '+h.cid):''}${h.threshold!=null?' ('+h.threshold+' W)':''}<div class="l">${esc(h.name||'')}</div></td><td style="word-break:break-all">${h.urls.map(esc).join('<br>')}</td><td><button class="bt r" style="margin:0" onclick="delh(${h.id})">✕</button></td></tr>`).join(''):'<tr><td class="l">Noch keine Aktionen</td></tr>'})}
$('h_ev').onchange=()=>{const v=$('h_ev').value;$('h_cidl').textContent='Eingang';if(RF&&(v.startsWith('rf.')||v.startsWith('switch.'))){const t=v=='rf.button'?'button':v.startsWith('switch.')?'switch':'contact',L=((CFG&&CFG.rf&&CFG.rf.entries)||[]).filter(e=>e.type==t);$('h_cid').innerHTML=L.map(e=>`<option value="${e.id}">${esc(e.name||RFT[t]+' '+e.id)}</option>`).join('')||'<option value="0">(erst einen Code speichern)</option>';$('h_cidl').textContent='Code';$('h_cidw').style.display=v=='rf.unknown'?'none':'';$('h_thrw').style.display='none';return}$('h_thrw').style.display=v.includes('power_')?'':'none';const isIn=v.startsWith('input.'),isSw=CH2&&v.startsWith('switch.');$('h_cid').innerHTML=isSw?'<option value="0">Kanal 1 (switch:0)</option><option value="1">Kanal 2 (switch:1)</option>':(DM||CH2||CV)?'<option value="0">SW1 (input:0)</option><option value="1">SW2 (input:1)</option>':'<option value="0">SW (input:0)</option><option value="100">Add-on (input:100)</option>';$('h_cidw').style.display=((AO||DM||CH2||CV)&&isIn)||isSw?'':'none'};
function addh(){run(async()=>{const p={event:$('h_ev').value,cid:$('h_cidw').style.display==''?+$('h_cid').value:0,name:$('h_name').value,enable:true,urls:$('h_urls').value.split('\n').map(s=>s.trim()).filter(Boolean)};const t=$('h_thr').value;if(t!=='')p.threshold=+t;await rpc('Webhook.Create',p);$('h_urls').value='';$('h_name').value='';loadHooks()},'Aktion angelegt')}
function delh(id){run(async()=>{await rpc('Webhook.Delete',{id});loadHooks()})}
function tgl(id,en){run(()=>rpc('Webhook.Update',{id,enable:en}))}
function scan(){run(async()=>{$('scan').textContent='Suche …';try{const r=await rpc('WiFi.Scan');$('w_list').innerHTML=r.results.sort((a,b)=>b.rssi-a.rssi).map(n=>`<option value="${esc(n.ssid)}">${n.rssi} dBm</option>`).join('');toast(r.results.length+' Netze gefunden')}finally{$('scan').textContent='Netze suchen'}})}
function saveWifi(){run(async()=>{const s={ssid:$('w_ssid').value,ipv4mode:$('w_mode').value,ip:$('w_ip').value,netmask:$('w_nm').value,gw:$('w_gw').value,nameserver:$('w_dns').value};if($('w_pass').value)s.pass=$('w_pass').value;const c={sta:s,roam:{enable:$('w_roam').checked,rssi_thr:+$('w_rthr').value||-70}};if($('w_ap').value)c.ap={pass:$('w_ap').value};await rpc('WiFi.SetConfig',{config:c})},'Gespeichert – verbinde neu')}
function saveSys(){run(async()=>{await rpc('Sys.SetConfig',{config:{device:{name:$('y_name').value},location:{tz:$('y_tz').value},sntp:{server:$('y_sntp').value},ui:{led:$('y_led').checked}}});loadCfg()},'Gespeichert')}
function setAuth(clear){run(async()=>{if(!clear&&!$('y_auth').value)throw new Error('Kein Passwort eingegeben');await rpc('Shelly.SetAuth',{pass:clear?null:$('y_auth').value});$('y_auth').value='';loadCfg()},clear?'Passwort entfernt':'Passwort gesetzt')}
const CSRC={default:'Standard (wie Tasmota)',reference:'mit Referenzlast kalibriert',tasmota:'aus Tasmota übernommen',manual:'manuell gesetzt'};
function calCh(){return(CH2||CV)?+$('y_ch').value:0}
function showCal(c){if(!c&&CFG){const ch=calCh();if(CV){const k=CFG['cover:0'].pm_calibration[ch];c={factor:k.factor,source:k.factor==1?'default':'manual',default:1}}else c=(CFG['switch:'+ch]||CFG['switch:0']||CFG['light:0']||{}).pm_calibration}if(!c)return;const FX=DM||CH2||CV;$('y_cf').textContent=(FX?'Faktor ':'')+c.factor.toFixed(4)+(FX?' · ':' W/Hz · ')+(FX&&c.source=='default'?'Standard':(CSRC[c.source]||c.source))+(c.source!='default'?' (Standard '+c.default.toFixed(4)+')':'');$('y_fac').value=c.factor.toFixed(4)}
function calVI(){const p={};if($('y_vr').value)p.voltage=+$('y_vr').value;if($('y_ir').value)p.current=+$('y_ir').value;calib(p)}
function calib(p){run(async()=>{p.id=calCh();const r=await rpc(CV?'Switch.Calibrate':cmp()+'.Calibrate',p);CFG=await rpc('Shelly.GetConfig');if(r.factor!=null){showCal();toast('Faktor: '+r.factor.toFixed(4)+(DM||CH2||CV?'':' W/Hz'))}else toast('Spannung/Strom kalibriert')})}
function showUpd(u,ver){if(!u)return;$('u_mode').value=u.mode;$('u_hour').value=u.hour;$('u_url').value=u.url;$('u_tok').placeholder=u.token_set?'gesetzt – leer lassen = unverändert':'kein Token';
$('u_st').textContent=u.available?'Neue Version '+u.available+' verfügbar':'Aktuell ('+ver+')';$('u_inst').style.display=u.available?'':'none';
$('u_info').textContent='Variante '+u.variant+(u.last_check?' · zuletzt geprüft '+new Date(u.last_check*1000).toLocaleString('de-DE'):'')+(u.last_error?' · '+u.last_error:'')+(u.state!='idle'?' · '+u.state:'')}
function showMq(m){if(!m)return;$('q_en').checked=m.enable;$('q_srv').value=m.server||'';$('q_pre').value=m.topic_prefix||'';$('q_user').value=m.user||'';$('q_pass').value='';$('q_pass').placeholder=m.pass_set?'gesetzt – leer lassen = unverändert':'kein Passwort'}
function saveMq(){run(async()=>{const c={enable:$('q_en').checked,server:$('q_srv').value,topic_prefix:$('q_pre').value,user:$('q_user').value||null};if($('q_pass').value)c.pass=$('q_pass').value;if(!c.user)c.pass=null;await rpc('MQTT.SetConfig',{config:c});showMq(await rpc('MQTT.GetConfig'))},'Gespeichert')}
async function loadUpd(){const c=await rpc('Sys.GetConfig');const d=await rpc('Shelly.GetDeviceInfo');showUpd(c.updates,d.ver)}
function saveUpd(){run(async()=>{const u={mode:$('u_mode').value,hour:+$('u_hour').value,url:$('u_url').value};if($('u_tok').value)u.token=$('u_tok').value;await rpc('Sys.SetConfig',{config:{updates:u}});$('u_tok').value='';await loadUpd()},'Gespeichert')}
function chkUpd(){run(async()=>{const r=await rpc('Shelly.CheckForUpdate');await loadUpd();toast(r.stable?'Neue Version '+r.stable.version:'Firmware ist aktuell')})}
function instUpd(){if(confirm('Update jetzt installieren? Das Gerät startet danach neu.'))run(async()=>{await rpc('Shelly.Update',{stage:'stable'});toast('Update läuft – Gerät startet gleich neu')})}
function rstc(){if(confirm('Energiezähler nullen?'))run(()=>rpc(cmp()+'.ResetCounters',{id:CV?0:calCh()}),'Zähler genullt')}
function sendRpc(){run(async()=>{const t=$('r_p').value.trim();const p=t?JSON.parse(t):{};try{$('r_o').textContent=JSON.stringify(await rpc($('r_m').value,p),null,2)}catch(e){$('r_o').textContent='Fehler: '+e.message}})}
function init(){loadCfg().catch(()=>setTimeout(init,3000))}
let pb=false;async function tick(){if(pb)return;pb=true;await poll();pb=false}
init();tick();setInterval(tick,2000);
</script></body></html>)HTML";
