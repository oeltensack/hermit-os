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
#btn{width:104px;height:104px;border-radius:50%;border:0;font-size:22px;font-weight:700;color:#fff;background:var(--off);cursor:pointer;transition:.2s}
#btn.on{background:var(--on);box-shadow:0 0 24px #3fb95066}
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
nav a.a{background:var(--a)}.tab{display:none}.ao{display:none}.lt{display:none}
input[type=range]{padding:0;height:28px;accent-color:var(--a)}
#ctr{background:linear-gradient(90deg,#ffb15a,#fff,#cfe3ff);border-radius:6px}.vi{display:none}.tab.a{display:block}
#toast{position:fixed;bottom:18px;left:50%;transform:translateX(-50%);padding:9px 16px;border-radius:6px;display:none;color:#fff}
code{background:var(--bg);padding:1px 5px;border-radius:4px;font-size:12px}
</style></head><body>
<header><h1 id="t">Hermit OS</h1><span class="l" id="ver"></span></header>
<main>
<nav><a data-t="st" class="a">Status</a><a data-t="sw">Schalter</a><a data-t="wh">Aktionen</a><a data-t="wf">WLAN</a><a data-t="sy">System</a><a data-t="rp">RPC</a></nav>

<section class="tab a" id="st"><div class="card"><div class="big">
<button id="btn">–</button>
<div class="grid">
<div class="pm"><div class="v" id="pw">–</div><div class="l">Leistung</div></div>
<div class="pm"><div class="v" id="en">–</div><div class="l">Energie</div></div>
<div class="pm"><div class="v" id="tc">–</div><div class="l">Temperatur</div></div>
<div class="vi"><div class="v" id="vo">–</div><div class="l">Spannung</div></div>
<div class="vi"><div class="v" id="cu">–</div><div class="l">Strom · cos φ</div></div>
<div id="inw"><div class="v" id="in">–</div><div class="l">Eingang SW</div></div>
<div class="ao"><div class="v" id="ex">–</div><div class="l" id="exl">Add-on-Taster</div></div>
<div><div class="v" id="rs">–</div><div class="l">WLAN</div></div>
<div><div class="v" id="ip">–</div><div class="l">IP</div></div>
<div><div class="v" id="up">–</div><div class="l">Laufzeit</div></div>
<div><div class="v" id="fl">–</div><div class="l">Flash</div></div>
</div></div><div class="l" id="src" style="margin-top:12px"></div></div>
<div class="card lt"><h2>Licht</h2>
<label>Helligkeit: <b id="brv">–</b></label><input type="range" id="bri" min="1" max="100" onchange="setL({brightness:+this.value})" oninput="$('brv').textContent=this.value+' %'">
<label>Farbtemperatur: <b id="ctv">–</b></label><input type="range" id="ctr" min="2700" max="6500" step="50" onchange="setL({ct:+this.value})" oninput="$('ctv').textContent=this.value+' K'">
<div class="row"><button class="bt g" onclick="setL({ct:2700})">Warmweiß</button><button class="bt g" onclick="setL({ct:4000})">Neutral</button><button class="bt g" onclick="setL({ct:6500})">Tageslicht</button></div></div>
<div class="card"><h2>Zeitschaltung</h2><div class="row">
<div><label>Einschalten für (s)</label><input id="ta" type="number" min="1" value="60"></div></div>
<button class="bt" onclick="pulse()">Für Dauer einschalten</button></div>
</section>

<section class="tab" id="sw"><div class="card"><h2 id="swh">Schalter</h2>
<label>Name</label><input id="s_name">
<div class="row"><div class="so"><label>Eingangsmodus</label><select id="s_in_mode">
<option value="follow">Folgen (Schalter = Relais)</option><option value="flip">Umschalten bei jeder Flanke</option>
<option value="momentary">Taster (toggle bei Druck)</option><option value="detached">Getrennt (nur Ereignis)</option></select></div>
<div><label>Zustand nach Stromausfall</label><select id="s_initial_state">
<option value="off">Aus</option><option value="on">An</option><option value="restore_last">Letzter Zustand</option><option value="match_input">Wie Eingang</option></select></div></div>
<div class="row"><div><label class="ck"><input type="checkbox" id="s_auto_off">Auto-Aus nach (s)</label><input id="s_auto_off_delay" type="number" step="0.1"></div>
<div><label class="ck"><input type="checkbox" id="s_auto_on">Auto-An nach (s)</label><input id="s_auto_on_delay" type="number" step="0.1"></div></div>
<div class="row"><div class="pm"><label>Abschaltleistung (W, 0 = aus)</label><input id="s_power_limit" type="number"></div>
<div class="pm"><label>Abschalttemperatur (°C)</label><input id="s_temp_limit" type="number"></div></div>
<div class="lt"><div class="row"><div><label>Überblendzeit (s)</label><input id="l_tr" type="number" step="0.1" min="0"></div>
<div><label class="ck" style="margin-top:34px"><input type="checkbox" id="l_ctinv">Warm-/Kaltkanal tauschen</label></div></div>
<div class="l">WLAN-Reset: Lampe 5× kurz hintereinander aus- und einschalten (jeweils unter 6 s an).</div></div>
<div class="io"><h2 style="margin-top:20px">Eingang SW</h2>
<div class="row"><div><label>Typ</label><select id="i_type"><option value="switch">Schalter</option><option value="button">Taster</option></select></div>
<div><label class="ck" style="margin-top:34px"><input type="checkbox" id="i_invert">Eingang invertieren</label></div></div>
<label>Eingangsfilter</label><select id="i_filter"><option value="ac">Wechselspannung (50-Hz-Halbwellen überbrücken)</option><option value="dc">Nur Pegel (Gleichspannung / feste Pegel)</option></select>
<button class="bt g" onclick="diag()">Eingang messen (roh)</button><pre id="i_diag" style="display:none"></pre></div>
<button class="bt" onclick="saveSw()">Speichern</button></div>
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
<div id="h_cidw" style="display:none"><label>Eingang</label><select id="h_cid"><option value="0">SW (input:0)</option><option value="100">Add-on (input:100)</option></select></div>
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
<div class="v" id="y_cf" style="font-size:16px">–</div>
<div class="l">Der Faktor bleibt auch bei Werkseinstellungen und Firmware-Updates erhalten.</div>
<label>a) Referenzlast: Relais an, gemessene Wirkleistung (W)</label><input id="y_cal" type="number" step="0.1">
<button class="bt" onclick="calib({apower:+$('y_cal').value})">Mit Referenz kalibrieren</button>
<div class="vi"><label>Spannung mit Messgerät (V)</label><input id="y_vr" type="number" step="0.1">
<label>Strom mit Messgerät (A) – Last an, Relais an</label><input id="y_ir" type="number" step="0.001">
<button class="bt" onclick="calVI()">Spannung/Strom kalibrieren</button></div>
<label>b) Wert aus Tasmota übernehmen (Konsole: <code>PowerCal</code>)</label><input id="y_tpc" type="number" placeholder="12530">
<button class="bt" onclick="calib({tasmota_powercal:+$('y_tpc').value})">Tasmota-Wert übernehmen</button>
<label>c) Faktor direkt (W/Hz)</label><input id="y_fac" type="number" step="0.0001">
<div class="row"><button class="bt" onclick="calib({factor:+$('y_fac').value})">Faktor setzen</button>
<button class="bt g" onclick="calib({reset:true})">Standard wiederherstellen</button>
<button class="bt g" onclick="rstc()">Energiezähler nullen</button></div></div>
<div class="card"><h2>Automatische Updates</h2>
<div class="v" id="u_st" style="font-size:16px">–</div><div class="l" id="u_info"></div>
<div class="row"><div><label>Modus</label><select id="u_mode"><option value="off">Aus</option><option value="check">Nur prüfen und anzeigen</option><option value="install">Automatisch installieren</option></select></div>
<div><label>Uhrzeit (Stunde)</label><input id="u_hour" type="number" min="0" max="23"></div></div>
<label>Update-Quelle (update.json)</label><input id="u_url">
<label>GitHub-Token (nur bei privatem Repo)</label><input id="u_tok" type="password">
<div class="l">Installiert wird nur bei ausgeschaltetem Relais. Die Firmware wird vor dem Umschalten per SHA-256 geprüft.</div>
<div class="row"><button class="bt" onclick="saveUpd()">Speichern</button><button class="bt g" onclick="chkUpd()">Jetzt prüfen</button><button class="bt g" id="u_inst" onclick="instUpd()">Jetzt installieren</button></div></div>
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
let LT=false,LS=null;
function showLT(){document.querySelectorAll('.lt').forEach(e=>e.style.display=LT?'block':'none');document.querySelectorAll('.so,.io').forEach(e=>e.style.display=LT?'none':'');$('swh').textContent=LT?'Licht':'Schalter'}
function setL(p){run(async()=>{p.id=0;if(LS&&!LS.output&&p.on==null)p.on=true;await rpc('CCT.Set',p);poll()})}
async function poll(){try{const s=await rpc('Shelly.GetStatus'),sw=s['switch:0']||s['cct:0'];LT=!!s['cct:0'];LS=s['cct:0']||null;showLT();
if(LT){if(document.activeElement!==$('bri')){$('bri').value=sw.brightness;$('brv').textContent=sw.brightness+' %'}if(document.activeElement!==$('ctr')){$('ctr').value=sw.ct;$('ctv').textContent=sw.ct+' K'}}
$('btn').className=sw.output?'on':'';$('btn').textContent=sw.output?'AN':'AUS';PM='apower' in sw;
$('pw').textContent=fmt(sw.apower,1,'W');$('en').textContent=sw.aenergy?fmt(sw.aenergy.total/1000,3,'kWh'):'–';
$('tc').textContent=sw.temperature?fmt(sw.temperature.tC,1,'°C'):'–';$('in').textContent=s['input:0']?(s['input:0'].state?'AN':'AUS'):'–';$('inw').style.display=s['input:0']?'':'none';AO=!!s['input:100'];if(AO)$('ex').textContent=s['input:100'].state?'AN':'AUS';document.querySelectorAll('.ao').forEach(e=>e.style.display=AO?'block':'none');
$('rs').textContent=s.wifi.rssi!=null?s.wifi.rssi+' dBm':(s.wifi.ap_active?'AP':'–');$('ip').textContent=s.wifi.sta_ip||'–';$('up').textContent=upt(s.sys.uptime);
const fr=s.sys.flash_real,fh=s.sys.flash_size;$('fl').textContent=(fr/1048576)+' MB'+(fr<fh?' ⚠':'');$('fl').title='Header '+(fh/1048576)+' MB, Chip-ID '+s.sys.flash_id+', OTA frei '+Math.round(s.sys.ota_free/1024)+' KB';
$('src').textContent='Quelle: '+sw.source+(sw.timer_duration?' · Timer '+sw.timer_duration+' s':'')+(sw.errors?' · Fehler: '+sw.errors.join(', '):'')+(s.sys.time?' · '+s.sys.time:'');
document.querySelectorAll('.pm').forEach(e=>e.style.display=PM?'':'none');const VI='voltage' in sw;if(VI){$('vo').textContent=fmt(sw.voltage,1,'V');$('cu').textContent=sw.current==null?'–':fmt(sw.current,3,'A')+(sw.pf!=null?' · '+sw.pf.toFixed(2):'')}document.querySelectorAll('.vi').forEach(e=>e.style.display=VI?'block':'none')}catch(e){}}
$('btn').onclick=()=>run(async()=>{await rpc(LT?'CCT.Toggle':'Switch.Toggle',{id:0});poll()});
function pulse(){run(async()=>{await rpc(LT?'CCT.Set':'Switch.Set',{id:0,on:true,toggle_after:+$('ta').value});poll()})}
function stat(){$('w_static').style.display=$('w_mode').value=='static'?'':'none'}
async function loadCfg(){const c=await rpc('Shelly.GetConfig'),sw=c['switch:0']||c['cct:0'],inp=c['input:0']||{};LT=!!c['cct:0'];showLT();if(LT){$('l_tr').value=sw.transition_duration;$('l_ctinv').checked=sw.ct_invert}
['in_mode','initial_state','auto_on_delay','auto_off_delay','power_limit','temp_limit'].forEach(k=>$('s_'+k).value=sw[k]??'');
showCal(sw.pm_calibration);$('s_name').value=sw.name||'';$('s_auto_on').checked=sw.auto_on;$('s_auto_off').checked=sw.auto_off;$('i_type').value=inp.type;$('i_invert').checked=inp.invert;$('i_filter').value=inp.filter||'ac';
$('y_name').value=c.sys.device.name||'';$('y_tz').value=c.sys.location.tz;$('y_sntp').value=c.sys.sntp.server;$('y_led').checked=c.sys.ui.led;
const x=c['input:100'];if(x){AO=true;$('x_name').value=x.name||'';$('x_type').value=x.type;$('x_rel').value=x.relay_action;$('x_inv').checked=x.invert;$('x_out').checked=x.addon_out_invert}document.querySelectorAll('.ao').forEach(e=>e.style.display=AO?'block':'none');
const w=c.wifi.sta;$('w_ssid').value=w.ssid||'';$('w_mode').value=w.ipv4mode;$('w_ip').value=w.ip||'';$('w_nm').value=w.netmask||'';$('w_gw').value=w.gw||'';$('w_dns').value=w.nameserver||'';stat();
const d=await rpc('Shelly.GetDeviceInfo');$('t').textContent=d.name||d.id;$('ver').textContent='Hermit OS '+d.ver+' · '+d.model+' · '+d.id;document.title=d.name||d.id;
showUpd(c.sys.updates,d.ver);$('y_auth').placeholder=d.auth_en?'Passwort ist gesetzt':'kein Passwort gesetzt';
const lm=await rpc('Shelly.ListMethods');$('r_list').innerHTML=lm.methods.map(m=>`<option>${m}</option>`).join('')}
function saveEx(){run(()=>rpc('Input.SetConfig',{id:100,config:{name:$('x_name').value,type:$('x_type').value,relay_action:$('x_rel').value,invert:$('x_inv').checked,addon_out_invert:$('x_out').checked}}),'Gespeichert')}
function diag(){run(async()=>{const r=await rpc('Input.Diag',{id:0});$('i_diag').style.display='block';$('i_diag').textContent='HIGH-Anteil: '+r.high_pct+' %\nFlanken in 200 ms: '+r.edges+'  (≈ '+r.approx_hz+' Hz)\nErkannter Zustand: '+(r.state?'AN':'AUS')+'  ·  Filter '+r.filter+(r.invert?' · invertiert':'')})}
function saveSw(){if(LT)return run(()=>rpc('CCT.SetConfig',{id:0,config:{name:$('s_name').value,initial_state:$('s_initial_state').value,auto_on:$('s_auto_on').checked,auto_on_delay:+$('s_auto_on_delay').value,auto_off:$('s_auto_off').checked,auto_off_delay:+$('s_auto_off_delay').value,transition_duration:+$('l_tr').value,ct_invert:$('l_ctinv').checked}}),'Gespeichert');
run(async()=>{const n=i=>+$(i).value;await rpc('Switch.SetConfig',{id:0,config:{name:$('s_name').value,in_mode:$('s_in_mode').value,initial_state:$('s_initial_state').value,auto_on:$('s_auto_on').checked,auto_on_delay:n('s_auto_on_delay'),auto_off:$('s_auto_off').checked,auto_off_delay:n('s_auto_off_delay'),power_limit:n('s_power_limit'),temp_limit:n('s_temp_limit')}});
await rpc('Input.SetConfig',{id:0,config:{type:$('i_type').value,invert:$('i_invert').checked,filter:$('i_filter').value}})},'Gespeichert')}
async function loadHooks(){await run(async()=>{if(!SUP.length){const s=await rpc('Webhook.ListSupported');SUP=Object.keys(s.types);$('h_ev').innerHTML=SUP.map(e=>`<option>${e}</option>`).join('')}
const l=await rpc('Webhook.List');$('hooks').innerHTML=l.hooks.length?l.hooks.map(h=>`<tr><td><input type="checkbox" style="width:auto" ${h.enable?'checked':''} onchange="tgl(${h.id},this.checked)"></td><td><b>${esc(h.event)}</b>${h.event.startsWith('input.')&&AO?(h.cid==100?' · Add-on':' · SW'):''}${h.threshold!=null?' ('+h.threshold+' W)':''}<div class="l">${esc(h.name||'')}</div></td><td style="word-break:break-all">${h.urls.map(esc).join('<br>')}</td><td><button class="bt r" style="margin:0" onclick="delh(${h.id})">✕</button></td></tr>`).join(''):'<tr><td class="l">Noch keine Aktionen</td></tr>'})}
$('h_ev').onchange=()=>{const v=$('h_ev').value;$('h_thrw').style.display=v.includes('power_')?'':'none';$('h_cidw').style.display=AO&&v.startsWith('input.')?'':'none'};
function addh(){run(async()=>{const p={event:$('h_ev').value,cid:AO&&$('h_ev').value.startsWith('input.')?+$('h_cid').value:0,name:$('h_name').value,enable:true,urls:$('h_urls').value.split('\n').map(s=>s.trim()).filter(Boolean)};const t=$('h_thr').value;if(t!=='')p.threshold=+t;await rpc('Webhook.Create',p);$('h_urls').value='';$('h_name').value='';loadHooks()},'Aktion angelegt')}
function delh(id){run(async()=>{await rpc('Webhook.Delete',{id});loadHooks()})}
function tgl(id,en){run(()=>rpc('Webhook.Update',{id,enable:en}))}
function scan(){run(async()=>{$('scan').textContent='Suche …';try{const r=await rpc('WiFi.Scan');$('w_list').innerHTML=r.results.sort((a,b)=>b.rssi-a.rssi).map(n=>`<option value="${esc(n.ssid)}">${n.rssi} dBm</option>`).join('');toast(r.results.length+' Netze gefunden')}finally{$('scan').textContent='Netze suchen'}})}
function saveWifi(){run(async()=>{const s={ssid:$('w_ssid').value,ipv4mode:$('w_mode').value,ip:$('w_ip').value,netmask:$('w_nm').value,gw:$('w_gw').value,nameserver:$('w_dns').value};if($('w_pass').value)s.pass=$('w_pass').value;const c={sta:s};if($('w_ap').value)c.ap={pass:$('w_ap').value};await rpc('WiFi.SetConfig',{config:c})},'Gespeichert – verbinde neu')}
function saveSys(){run(async()=>{await rpc('Sys.SetConfig',{config:{device:{name:$('y_name').value},location:{tz:$('y_tz').value},sntp:{server:$('y_sntp').value},ui:{led:$('y_led').checked}}});loadCfg()},'Gespeichert')}
function setAuth(clear){run(async()=>{if(!clear&&!$('y_auth').value)throw new Error('Kein Passwort eingegeben');await rpc('Shelly.SetAuth',{pass:clear?null:$('y_auth').value});$('y_auth').value='';loadCfg()},clear?'Passwort entfernt':'Passwort gesetzt')}
const CSRC={default:'Standard (wie Tasmota BL0937)',reference:'mit Referenzlast kalibriert',tasmota:'aus Tasmota übernommen',manual:'manuell gesetzt'};
function showCal(c){if(!c)return;$('y_cf').textContent=c.factor.toFixed(4)+' W/Hz · '+(CSRC[c.source]||c.source)+(c.source!='default'?' (Standard '+c.default.toFixed(4)+')':'');$('y_fac').value=c.factor.toFixed(4)}
function calVI(){const p={};if($('y_vr').value)p.voltage=+$('y_vr').value;if($('y_ir').value)p.current=+$('y_ir').value;calib(p)}
function calib(p){run(async()=>{p.id=0;const r=await rpc('Switch.Calibrate',p);if(r.factor!=null){showCal(r);toast('Faktor: '+r.factor.toFixed(4)+' W/Hz')}else toast('Spannung/Strom kalibriert')})}
function showUpd(u,ver){if(!u)return;$('u_mode').value=u.mode;$('u_hour').value=u.hour;$('u_url').value=u.url;$('u_tok').placeholder=u.token_set?'gesetzt – leer lassen = unverändert':'kein Token';
$('u_st').textContent=u.available?'Neue Version '+u.available+' verfügbar':'Aktuell ('+ver+')';$('u_inst').style.display=u.available?'':'none';
$('u_info').textContent='Variante '+u.variant+(u.last_check?' · zuletzt geprüft '+new Date(u.last_check*1000).toLocaleString('de-DE'):'')+(u.last_error?' · '+u.last_error:'')+(u.state!='idle'?' · '+u.state:'')}
async function loadUpd(){const c=await rpc('Sys.GetConfig');const d=await rpc('Shelly.GetDeviceInfo');showUpd(c.updates,d.ver)}
function saveUpd(){run(async()=>{const u={mode:$('u_mode').value,hour:+$('u_hour').value,url:$('u_url').value};if($('u_tok').value)u.token=$('u_tok').value;await rpc('Sys.SetConfig',{config:{updates:u}});$('u_tok').value='';await loadUpd()},'Gespeichert')}
function chkUpd(){run(async()=>{const r=await rpc('Shelly.CheckForUpdate');await loadUpd();toast(r.stable?'Neue Version '+r.stable.version:'Firmware ist aktuell')})}
function instUpd(){if(confirm('Update jetzt installieren? Das Gerät startet danach neu.'))run(async()=>{await rpc('Shelly.Update',{stage:'stable'});toast('Update läuft – Gerät startet gleich neu')})}
function rstc(){if(confirm('Energiezähler nullen?'))run(()=>rpc('Switch.ResetCounters',{id:0}),'Zähler genullt')}
function sendRpc(){run(async()=>{const t=$('r_p').value.trim();const p=t?JSON.parse(t):{};try{$('r_o').textContent=JSON.stringify(await rpc($('r_m').value,p),null,2)}catch(e){$('r_o').textContent='Fehler: '+e.message}})}
function init(){loadCfg().catch(()=>setTimeout(init,3000))}
let pb=false;async function tick(){if(pb)return;pb=true;await poll();pb=false}
init();tick();setInterval(tick,2000);
</script></body></html>)HTML";
