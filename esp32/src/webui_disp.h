// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 D.Koch – Pagurus OS
// Weboberflaeche Pagurus OS Heizungsanzeige (AZ-Touch, ESP32)
#pragma once
#include <Arduino.h>

const char INDEX_HTML[] PROGMEM = R"HTML(<!DOCTYPE html><html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Pagurus Heizungsanzeige</title>
<style>
:root{--bg:#0f1419;--c:#1a2129;--fg:#e6e9ec;--m:#8a96a3;--a:#2f9be0;--on:#3fb950;--e:#f85149;--b:#2a333c;--o:#f0883e}
*{box-sizing:border-box}body{margin:0;font:15px/1.4 system-ui,sans-serif;background:var(--bg);color:var(--fg)}
header{padding:12px 16px;background:var(--c);display:flex;justify-content:space-between;align-items:center;gap:10px;flex-wrap:wrap}
h1{font-size:18px;margin:0}main{max-width:900px;margin:auto;padding:12px}
nav{display:flex;gap:6px;flex-wrap:wrap}nav a{color:var(--fg);padding:7px 12px;border-radius:6px;background:var(--bg);cursor:pointer}nav a.a{background:var(--a)}
.tab{display:none}.tab.a{display:block}.card{background:var(--c);border-radius:10px;padding:16px;margin:12px 0}
h2{font-size:13px;text-transform:uppercase;letter-spacing:.05em;color:var(--m);margin:0 0 12px}
.kv{display:grid;grid-template-columns:repeat(auto-fill,minmax(130px,1fr));gap:10px 18px}.v{font-size:20px;font-weight:600}.l{font-size:12px;color:var(--m)}
.bt{padding:8px 14px;border:0;border-radius:6px;background:#3a444f;color:#fff;font:inherit;cursor:pointer}.bt.p{background:var(--a)}.bt.on{background:var(--on)}.bt.hot{background:var(--e)}
.row{display:flex;gap:8px;flex-wrap:wrap;margin:8px 0}.g2{display:grid;grid-template-columns:2fr 1fr;gap:0 10px}
label{display:block;font-size:12px;color:var(--m);margin:10px 0 4px}input,select{width:100%;padding:9px;border-radius:6px;border:1px solid var(--b);background:var(--bg);color:var(--fg);font:inherit}
#toast{position:fixed;bottom:18px;left:50%;transform:translateX(-50%);padding:9px 16px;border-radius:6px;display:none;color:#fff;background:var(--e)}#toast.ok{background:var(--on)}
.warn{color:var(--e)}.ok{color:var(--on)}.dot{display:inline-block;width:9px;height:9px;border-radius:50%;margin-right:6px;background:var(--m)}.dot.y{background:var(--on)}.dot.n{background:var(--e)}
</style></head><body>
<header><h1 id="ti">Pagurus Heizungsanzeige</h1><nav><a data-t="h" class="a">Heizung</a><a data-t="d">Anzeige</a><a data-t="w">WLAN</a><a data-t="q">MQTT</a><a data-t="s">System</a></nav></header>
<main>
<section class="tab a" id="h">
<div class="card"><h2>Warmwasser</h2><div id="ww_st" class="l"></div><div class="kv" id="ww" style="margin-top:10px"></div>
<div class="row"><button class="bt" id="b_bo" onclick="run(()=>rpc('Display.Boost').then(()=>setTimeout(load,1800)))">Boost</button></div></div>
<div class="card"><h2>Zirkulation</h2><div id="z_st" class="l"></div><div class="kv" id="zi" style="margin-top:10px"></div>
<div class="row"><button class="bt" id="b_zi" onclick="run(()=>rpc('Display.Zirk').then(()=>setTimeout(load,1800)))">Ein/Aus</button></div></div>
<div class="card" id="bad_c" style="display:none"><h2 id="bad_h">Bad</h2><div id="b_st" class="l"></div><div class="kv" id="ba" style="margin-top:10px"></div>
<div class="l" style="margin-top:8px">Werte kommen, solange am Display die Seite BAD offen ist – <a href="#" onclick="run(()=>rpc('Display.Page',{page:'bad'}).then(()=>setTimeout(load,3000)));return false" style="color:var(--a)">jetzt dorthin wechseln</a>.</div></div>
</section>
<section class="tab" id="d"><div class="card"><h2>Quellen</h2>
<div class="g2"><div><label>Warmwasser-Logik: IP-Adresse des Shelly</label><input id="d_wi" placeholder="192.168.3.77"></div>
<div><label>Nummer des Logik-Scripts</label><input id="d_ws" type="number" min="1" max="20"></div></div>
<div class="l">Gelesen wird <code>/script/&lt;Nr&gt;/api_data</code>, Boost über <code>/script/&lt;Nr&gt;/boost_trigger</code> (Heizung Logik ab V12).</div>
<div class="g2"><div><label>Zirkulationspumpe: IP-Adresse des Shelly</label><input id="d_zi" placeholder="192.168.3.34"></div>
<div><label>Temperaturfühler (Add-on, -1 = keiner)</label><input id="d_zt" type="number" min="-1" max="199"></div></div>
<div class="l">Schaltet <code>switch:0</code>, Temperatur aus <code>temperature:&lt;Nr&gt;</code> (meist 100 oder 101).</div>
<div class="g2"><div><label>Heizlüfter für die Seite BAD: IP-Adresse (leer = keine Seite BAD)</label><input id="d_hi" placeholder="192.168.3.181"></div>
<div><label>Name in der Kopfzeile</label><input id="d_hn" maxlength="9" placeholder="BAD"></div></div>
<div class="l">Heizlüfter-Script ab v11: liest <code>hl_state</code>, <code>hl_cfg</code>, <code>hl_stats</code>, schreibt Soll/Betrieb in <code>hl_cfg</code> und Boost/Quittieren in <code>hl_cmd</code>.</div>
<label>Bild</label><select id="d_r"><option value="3">normal</option><option value="1">auf dem Kopf</option></select>
<div class="row"><button class="bt p" onclick="saveD()">Speichern</button></div></div></section>
<section class="tab" id="w"><div class="card"><h2>WLAN</h2>
<div id="ws" class="l"></div><label>Netz</label><select id="w_s"></select><button class="bt" style="margin-top:6px" onclick="scan()">Netze suchen</button>
<label>oder Name von Hand</label><input id="w_m"><label>Passwort</label><input id="w_p" type="password">
<div class="row"><button class="bt p" onclick="saveW()">Speichern und neu starten</button></div></div></section>
<section class="tab" id="q"><div class="card"><h2>MQTT (Pagurus Cloud)</h2>
<div class="row" style="align-items:center"><input type="checkbox" id="q_e" style="width:auto"><span>MQTT einschalten</span></div>
<label>Server (host:port)</label><input id="q_s" placeholder="192.168.4.102:1883"><label>Benutzer</label><input id="q_u"><label>Passwort</label><input id="q_p" type="password" placeholder="leer = unverändert">
<label>Topic-Präfix</label><input id="q_t"><div id="qs" class="l" style="margin-top:8px"></div>
<div class="row"><button class="bt p" onclick="saveQ()">Speichern</button></div></div></section>
<section class="tab" id="s"><div class="card"><h2>Gerät</h2><label>Name</label><input id="s_n">
<div class="row"><button class="bt p" onclick="saveS()">Speichern</button></div></div>
<div class="card"><h2>Firmware</h2><div id="fv" class="l"></div>
<label>Update-Modus</label><select id="u_m"><option value="check">Nur prüfen und anzeigen</option><option value="auto">Automatisch installieren</option><option value="off">Aus</option></select>
<label>Update-Quelle (update.json)</label><input id="u_u">
<div class="row"><button class="bt" onclick="saveU()">Speichern</button><button class="bt" onclick="chk()">Jetzt prüfen</button><button class="bt p" id="ui" style="display:none" onclick="inst()">Installieren</button><button class="bt" onclick="if(confirm('Neu starten?'))rpc('Shelly.Reboot')">Neu starten</button></div>
<label id="u_fl">Firmware-Datei hochladen</label><input type="file" id="u_f" accept=".bin"><div class="row"><button class="bt" onclick="upl()">Hochladen</button></div></div></section>
</main><div id="toast"></div>
<script>
const $=i=>document.getElementById(i);
const dez=(v,d)=>(+v).toLocaleString('de-DE',{minimumFractionDigits:d,maximumFractionDigits:d});
const f1=(v,u)=>v==null?'–':dez(v,1)+' '+u;
let S=null,C=null;
function toast(m,ok){const t=$('toast');t.textContent=m;t.className=ok?'ok':'';t.style.display='block';clearTimeout(t._h);t._h=setTimeout(()=>t.style.display='none',3000)}
async function rpc(m,p){const r=await fetch('/rpc',{method:'POST',body:JSON.stringify({id:1,method:m,params:p||{}})});const j=await r.json();if(j.error)throw new Error(j.error.message);return j.result}
async function run(f,ok){try{await f();if(ok)toast(ok,1)}catch(e){toast(e.message)}}
const kv=a=>a.map(([l,v])=>`<div><div class="v">${v}</div><div class="l">${l}</div></div>`).join('');
function src(x,nm){if(!x.configured)return `<span class="dot"></span>${nm}: keine Adresse – Reiter <b>Anzeige</b>`;
 return `<span class="dot ${x.ok?'y':'n'}"></span>`+(x.ok?`verbunden, vor ${x.age_s} s`:`<span class="warn">keine Daten${x.error?' ('+x.error+')':''}</span>`)}
function render(){const d=S['display:0']||{},w=d.ww||{},z=d.zirk||{},b=d.bad||{};
 $('bad_c').style.display=b.configured?'':'none';if(b.configured){$('bad_h').textContent=(C&&C['display:0'].hl_name)||'Bad';$('b_st').innerHTML=src(b,'Bad');
 $('ba').innerHTML='status' in b?kv([['Raum',f1(b.temp,'°C')],['Soll',f1(b.soll,'°C')],['Status',b.status],['Leistung',b.pwr+' W'],['Betrieb',b.on?'ein':'aus'],['Boost',b.boost?'an':'aus']]):''}
 $('ww_st').innerHTML=src(w,'Warmwasser');$('z_st').innerHTML=src(z,'Zirkulation');
 $('ww').innerHTML='temp_s' in w?kv([['Speicher',f1(w.temp_s,'°C')],['Kessel',f1(w.temp_k,'°C')],['Brenner',w.burn?'an':'aus'],['Pumpe',w.pump?'an':'aus'],['Öl heute',f1(w.cons_today,'L')]]):'';
 $('zi').innerHTML='output' in z?kv([['Pumpe',z.output?'an':'aus'],['Temperatur',f1(z.tC,'°C')]]):'';
 $('b_bo').className='bt'+(w.boost?' hot':'');$('b_bo').textContent=w.boost?'Boost läuft – beenden':'Boost';
 $('b_zi').className='bt'+(z.output?' on':'');$('b_zi').textContent=z.output?'Zirkulation aus':'Zirkulation ein';
 const wi=S.wifi;$('ws').textContent=wi.sta_ip?`Verbunden mit ${wi.ssid} · ${wi.sta_ip} · ${wi.rssi} dBm`:'Nicht verbunden'+(wi.ap?' – Zugangspunkt aktiv':'');
 $('qs').textContent=S.mqtt.connected?'MQTT verbunden':'MQTT nicht verbunden';
 const u=S.sys;$('fv').innerHTML=`Version ${C?C._ver:''} · Laufzeit ${Math.floor(u.uptime/3600)} h ${Math.floor(u.uptime%3600/60)} min · RAM frei ${Math.round(u.ram_free/1024)} KB`+(u.available_updates&&u.available_updates.stable?` · <b>neu: ${u.available_updates.stable.version}</b>`:'')+(u.upd_error?` · <span class="warn">${u.upd_error}</span>`:'');
 $('ui').style.display=u.available_updates&&u.available_updates.stable?'':'none'}
async function load(){try{S=await rpc('Shelly.GetStatus');render()}catch(e){}}
async function cfg(){C=await rpc('Shelly.GetConfig');const i=await rpc('Shelly.GetDeviceInfo');C._ver=i.ver;$('ti').textContent=i.name||'Pagurus Heizungsanzeige';
 $('u_fl').textContent=`Firmware-Datei hochladen (${i.variant}.bin)`;const d=C['display:0'];
 $('d_wi').value=d.ww_ip||'';$('d_ws').value=d.ww_sid;$('d_zi').value=d.zirk_ip||'';$('d_zt').value=d.zirk_tid;$('d_r').value=d.rotation;$('d_hi').value=d.hl_ip||'';$('d_hn').value=d.hl_name||'BAD';
 $('s_n').value=C.sys.device.name||'';
 $('q_e').checked=C.mqtt.enable;$('q_s').value=C.mqtt.server||'';$('q_u').value=C.mqtt.user||'';$('q_t').value=C.mqtt.topic_prefix||'';
 $('u_m').value=C.update.mode;$('u_u').value=C.update.url;$('w_m').value=C.wifi.sta.ssid||''}
function saveD(){run(async()=>{await rpc('Display.SetConfig',{config:{ww_ip:$('d_wi').value,ww_sid:+$('d_ws').value||1,zirk_ip:$('d_zi').value,zirk_tid:$('d_zt').value===''?-1:+$('d_zt').value,rotation:+$('d_r').value,hl_ip:$('d_hi').value,hl_name:$('d_hn').value}});await cfg();setTimeout(load,3000)},'Gespeichert')}
function scan(){run(async()=>{const r=await rpc('WiFi.Scan');$('w_s').innerHTML=r.results.sort((a,b)=>b.rssi-a.rssi).map(x=>`<option>${x.ssid}</option>`).join('');$('w_m').value=''})}
function saveW(){const s=$('w_m').value||$('w_s').value;if(!s)return toast('Netz wählen');run(()=>rpc('WiFi.SetConfig',{config:{sta:{ssid:s,pass:$('w_p').value}}}),'Gespeichert – Neustart')}
function saveQ(){const c={enable:$('q_e').checked,server:$('q_s').value,user:$('q_u').value,topic_prefix:$('q_t').value};if($('q_p').value)c.pass=$('q_p').value;run(()=>rpc('MQTT.SetConfig',{config:c}),'Gespeichert')}
function saveS(){run(async()=>{await rpc('Sys.SetConfig',{config:{device:{name:$('s_n').value}}});await cfg()},'Gespeichert')}
function saveU(){run(()=>rpc('Sys.SetConfig',{config:{update:{mode:$('u_m').value,url:$('u_u').value}}}),'Gespeichert')}
function chk(){run(async()=>{const r=await rpc('Shelly.CheckForUpdate');await load();toast(r.stable?'Neu: '+r.stable.version:'Firmware ist aktuell',1)})}
function upl(){const f=$('u_f').files[0];if(!f)return toast('Datei wählen');const fd=new FormData();fd.append('f',f);toast('Lade hoch …',1);
 fetch('/update',{method:'POST',body:fd}).then(r=>r.text().then(t=>toast(t,r.ok))).catch(e=>toast(e.message))}
function inst(){if(confirm('Update installieren? Das Gerät startet danach neu.'))run(()=>rpc('Shelly.Update'),'Update läuft …')}
document.querySelectorAll('nav a').forEach(a=>a.onclick=()=>{document.querySelectorAll('nav a').forEach(x=>x.classList.toggle('a',x===a));document.querySelectorAll('.tab').forEach(t=>t.classList.toggle('a',t.id==a.dataset.t));if(a.dataset.t=='w'&&!$('w_s').options.length)scan()});
cfg().then(()=>{load();if(!C['display:0'].ww_ip&&!C['display:0'].zirk_ip)document.querySelector('nav a[data-t=d]').click()});setInterval(load,3000);
</script></body></html>)HTML";
