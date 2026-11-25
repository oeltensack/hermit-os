// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 D.Koch – Pagurus OS
// Weboberflaeche Pagurus OS Klima (ESP32)
#pragma once
#include <Arduino.h>

const char INDEX_HTML[] PROGMEM = R"HTML(<!DOCTYPE html><html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Pagurus Klima</title>
<style>
:root{--bg:#0f1419;--c:#1a2129;--fg:#e6e9ec;--m:#8a96a3;--a:#2f9be0;--on:#3fb950;--e:#f85149;--b:#2a333c;--cool:#3fa7ff;--heat:#ff7b3f}
*{box-sizing:border-box}body{margin:0;font:15px/1.4 system-ui,sans-serif;background:var(--bg);color:var(--fg)}
header{padding:12px 16px;background:var(--c);display:flex;justify-content:space-between;align-items:center;gap:10px;flex-wrap:wrap}
h1{font-size:18px;margin:0}main{max-width:900px;margin:auto;padding:12px}
nav{display:flex;gap:6px;flex-wrap:wrap}nav a{color:var(--fg);padding:7px 12px;border-radius:6px;background:var(--bg);cursor:pointer}nav a.a{background:var(--a)}
.tab{display:none}.tab.a{display:block}.card{background:var(--c);border-radius:10px;padding:16px;margin:12px 0}
h2{font-size:13px;text-transform:uppercase;letter-spacing:.05em;color:var(--m);margin:0 0 12px}
.big{display:flex;gap:24px;align-items:center;flex-wrap:wrap}
.pw{width:96px;height:96px;border-radius:50%;border:0;font-weight:700;font-size:18px;color:#fff;background:#3a444f;cursor:pointer}.pw.on{background:var(--on);box-shadow:0 0 18px #3fb95066}
.kv{display:grid;grid-template-columns:repeat(auto-fill,minmax(130px,1fr));gap:10px 18px;flex:1}.v{font-size:20px;font-weight:600}.l{font-size:12px;color:var(--m)}
.set{display:flex;align-items:center;gap:14px}.set .t{font-size:40px;font-weight:700;min-width:120px;text-align:center}
.bt{padding:8px 14px;border:0;border-radius:6px;background:#3a444f;color:#fff;font:inherit;cursor:pointer}.bt.p{background:var(--a)}.bt.sel{background:var(--a)}
.row{display:flex;gap:8px;flex-wrap:wrap;margin:8px 0}.rnd{width:52px;height:52px;border-radius:50%;font-size:24px;padding:0}
label{display:block;font-size:12px;color:var(--m);margin:10px 0 4px}input,select{width:100%;padding:9px;border-radius:6px;border:1px solid var(--b);background:var(--bg);color:var(--fg);font:inherit}
.chk{display:flex;gap:8px;align-items:center;margin:10px 0}.chk input{width:auto}
#toast{position:fixed;bottom:18px;left:50%;transform:translateX(-50%);padding:9px 16px;border-radius:6px;display:none;color:#fff;background:var(--e)}#toast.ok{background:var(--on)}
.warn{color:var(--e)}
</style></head><body>
<header><h1 id="ti">Pagurus Klima</h1><nav><a data-t="k" class="a">Klima</a><a data-t="w">WLAN</a><a data-t="q">MQTT</a><a data-t="s">System</a></nav></header>
<main>
<section class="tab a" id="k">
<div class="card"><div class="big"><button class="pw" id="pw" onclick="tog()">–</button>
<div class="kv"><div><div class="v" id="rt">–</div><div class="l">Raum (Klimagerät)</div></div>
<div><div class="v" id="dt">–</div><div class="l">Raum (DHT22)</div></div>
<div><div class="v" id="dh">–</div><div class="l">Feuchte</div></div>
<div><div class="v" id="ot">–</div><div class="l">Ausblasluft</div></div>
<div><div class="v" id="cp">–</div><div class="l">Kompressor</div></div></div></div>
<div id="lnk" class="warn" style="margin-top:10px;display:none">Keine Antwort von der Klimaanlage – UART-Verbindung (GPIO16/17) prüfen</div></div>
<div class="card"><h2>Solltemperatur</h2><div class="set"><button class="bt rnd" onclick="tgt(-1)">−</button><div class="t" id="tg">–</div><button class="bt rnd" onclick="tgt(1)">+</button></div></div>
<div class="card"><h2>Betriebsart</h2><div class="row" id="md"></div>
<h2 style="margin-top:16px">Lüfter</h2><div class="row" id="fn"></div>
<h2 style="margin-top:16px">Extras</h2><div class="row" id="ex"></div></div>
</section>
<section class="tab" id="w"><div class="card"><h2>WLAN</h2>
<div id="ws" class="l"></div><label>Netz</label><select id="w_s"></select><button class="bt" style="margin-top:6px" onclick="scan()">Netze suchen</button>
<label>oder Name von Hand</label><input id="w_m"><label>Passwort</label><input id="w_p" type="password">
<div class="row"><button class="bt p" onclick="saveW()">Speichern und neu starten</button></div></div></section>
<section class="tab" id="q"><div class="card"><h2>MQTT (Pagurus Cloud)</h2>
<div class="chk"><input type="checkbox" id="q_e"><span>MQTT einschalten</span></div>
<label>Server (host:port)</label><input id="q_s" placeholder="192.168.4.102:1883"><label>Benutzer</label><input id="q_u"><label>Passwort</label><input id="q_p" type="password" placeholder="leer = unverändert">
<label>Topic-Präfix</label><input id="q_t"><div id="qs" class="l" style="margin-top:8px"></div>
<div class="row"><button class="bt p" onclick="saveQ()">Speichern</button></div></div></section>
<section class="tab" id="s"><div class="card"><h2>Gerät</h2><label>Name</label><input id="s_n">
<div class="chk"><input type="checkbox" id="s_b"><span>Piepton der Klimaanlage bei Befehlen</span></div>
<label>Korrektur DHT22 (°C)</label><input id="s_o" type="number" step="0.1">
<div class="row"><button class="bt p" onclick="saveS()">Speichern</button></div></div>
<div class="card"><h2>Firmware</h2><div id="fv" class="l"></div>
<label>Update-Modus</label><select id="u_m"><option value="check">Nur prüfen und anzeigen</option><option value="auto">Automatisch installieren</option><option value="off">Aus</option></select>
<label>Update-Quelle (update.json)</label><input id="u_u">
<div class="row"><button class="bt" onclick="saveU()">Speichern</button><button class="bt" onclick="chk()">Jetzt prüfen</button><button class="bt p" id="ui" style="display:none" onclick="inst()">Installieren</button><button class="bt" onclick="if(confirm('Neu starten?'))rpc('Shelly.Reboot')">Neu starten</button></div></div></section>
</main><div id="toast"></div>
<script>
const $=i=>document.getElementById(i);
const MODES=[['cool','Kühlen'],['heat','Heizen'],['dry','Entfeuchten'],['fan','Lüften'],['auto','Automatik']];
const FANS=[['auto','Auto'],['low','1'],['low_mid','2'],['mid','3'],['mid_high','4'],['high','5']];
const dez=(v,d)=>(+v).toLocaleString('de-DE',{minimumFractionDigits:d,maximumFractionDigits:d});
const f1=(v,u)=>v==null?'–':dez(v,1)+' '+u;
let S=null,C=null,busy=false,pendT=null,tTimer=0;
function toast(m,ok){const t=$('toast');t.textContent=m;t.className=ok?'ok':'';t.style.display='block';clearTimeout(t._h);t._h=setTimeout(()=>t.style.display='none',3000)}
async function rpc(m,p){const r=await fetch('/rpc',{method:'POST',body:JSON.stringify({id:1,method:m,params:p||{}})});const j=await r.json();if(j.error)throw new Error(j.error.message);return j.result}
async function run(f,ok){try{await f();if(ok)toast(ok,1)}catch(e){toast(e.message)}}
function render(){const c=S['climate:0']||{},on=!!c.output;
 $('pw').className='pw'+(on?' on':'');$('pw').textContent=on?'AN':'AUS';
 $('rt').textContent=f1(c.current_C,'°C');$('ot').textContent=f1(c.outlet_C,'°C');$('cp').textContent=c.compressor==null?'–':c.compressor?'läuft':'aus';
 const t=S['temperature:0']||{},h=S['humidity:0']||{};$('dt').textContent=f1(t.tC,'°C');$('dh').textContent=h.rh==null?'–':dez(h.rh,0)+' %';
 $('tg').textContent=pendT!=null?dez(pendT,0)+' °C':(c.target_C==null?'–':dez(c.target_C,c.target_C%1?1:0)+' °C');
 $('lnk').style.display=c.link?'none':'block';
 $('md').innerHTML=MODES.map(([k,l])=>`<button class="bt ${c.mode==k?'sel':''}" onclick="set({mode:'${k}'})">${l}</button>`).join('');
 $('fn').innerHTML=FANS.map(([k,l])=>`<button class="bt ${c.fan==k?'sel':''}" onclick="set({fan:'${k}'})">${l}</button>`).join('');
 $('ex').innerHTML=[['swing_v','Swing ↕'],['swing_h','Swing ↔'],['eco','Eco'],['turbo','Turbo'],['mute','Leise']].map(([k,l])=>`<button class="bt ${c[k]?'sel':''}" onclick="set({${k}:${!c[k]}})">${l}</button>`).join('');
 const w=S.wifi;$('ws').textContent=w.sta_ip?`Verbunden mit ${w.ssid} · ${w.sta_ip} · ${w.rssi} dBm`:'Nicht verbunden'+(w.ap?' – Zugangspunkt aktiv':'');
 $('qs').textContent=S.mqtt.connected?'MQTT verbunden':'MQTT nicht verbunden';
 const u=S.sys;$('fv').innerHTML=`Version ${C?C._ver:''} · Laufzeit ${Math.floor(u.uptime/3600)} h ${Math.floor(u.uptime%3600/60)} min · RAM frei ${Math.round(u.ram_free/1024)} KB`+(u.available_updates&&u.available_updates.stable?` · <b>neu: ${u.available_updates.stable.version}</b>`:'')+(u.upd_error?` · <span class="warn">${u.upd_error}</span>`:'');
 $('ui').style.display=u.available_updates&&u.available_updates.stable?'':'none'}
async function load(){if(busy)return;try{S=await rpc('Shelly.GetStatus');render()}catch(e){}}
async function cfg(){C=await rpc('Shelly.GetConfig');const i=await rpc('Shelly.GetDeviceInfo');C._ver=i.ver;$('ti').textContent=i.name||'Pagurus Klima';
 $('s_n').value=C.sys.device.name||'';$('s_b').checked=C['climate:0'].beep;$('s_o').value=C['temperature:0'].offset_C||0;
 $('q_e').checked=C.mqtt.enable;$('q_s').value=C.mqtt.server||'';$('q_u').value=C.mqtt.user||'';$('q_t').value=C.mqtt.topic_prefix||'';
 $('u_m').value=C.update.mode;$('u_u').value=C.update.url;$('w_m').value=C.wifi.sta.ssid||''}
function set(p){busy=true;run(async()=>{await rpc('Climate.Set',p);busy=false;setTimeout(load,300)}).finally(()=>busy=false)}
function tog(){const c=S['climate:0']||{};set({on:!c.output})}
function tgt(d){const c=S['climate:0']||{};pendT=Math.max(16,Math.min(31,Math.round((pendT!=null?pendT:c.target_C||24)+d)));render();clearTimeout(tTimer);tTimer=setTimeout(()=>{const t=pendT;pendT=null;set({target_C:t})},700)}
function scan(){run(async()=>{const r=await rpc('WiFi.Scan');$('w_s').innerHTML=r.results.sort((a,b)=>b.rssi-a.rssi).map(x=>`<option>${x.ssid}</option>`).join('');$('w_m').value=''})}
function saveW(){const s=$('w_m').value||$('w_s').value;if(!s)return toast('Netz wählen');run(()=>rpc('WiFi.SetConfig',{config:{sta:{ssid:s,pass:$('w_p').value}}}),'Gespeichert – Neustart')}
function saveQ(){const c={enable:$('q_e').checked,server:$('q_s').value,user:$('q_u').value,topic_prefix:$('q_t').value};if($('q_p').value)c.pass=$('q_p').value;run(()=>rpc('MQTT.SetConfig',{config:c}),'Gespeichert')}
function saveS(){run(async()=>{await rpc('Sys.SetConfig',{config:{device:{name:$('s_n').value}}});await rpc('Climate.SetConfig',{config:{beep:$('s_b').checked}});await rpc('Temperature.SetConfig',{config:{offset_C:+$('s_o').value||0}});await cfg()},'Gespeichert')}
function saveU(){run(()=>rpc('Sys.SetConfig',{config:{update:{mode:$('u_m').value,url:$('u_u').value}}}),'Gespeichert')}
function chk(){run(async()=>{const r=await rpc('Shelly.CheckForUpdate');await load();toast(r.stable?'Neu: '+r.stable.version:'Firmware ist aktuell',1)})}
function inst(){if(confirm('Update installieren? Das Gerät startet danach neu.'))run(()=>rpc('Shelly.Update'),'Update läuft …')}
document.querySelectorAll('nav a').forEach(a=>a.onclick=()=>{document.querySelectorAll('nav a').forEach(x=>x.classList.toggle('a',x===a));document.querySelectorAll('.tab').forEach(t=>t.classList.toggle('a',t.id==a.dataset.t));if(a.dataset.t=='w'&&!$('w_s').options.length)scan()});
cfg().then(load);setInterval(load,3000);
</script></body></html>)HTML";
