<?php
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 D.Koch – Pagurus OS
// Pagurus Portal – Oberfläche (Fernzugang zur Pagurus Cloud zu Hause)
header('X-Frame-Options: DENY');
header("Content-Security-Policy: default-src 'self'; style-src 'self' 'unsafe-inline'; script-src 'self' 'unsafe-inline'; img-src 'self' data:");
header('Referrer-Policy: no-referrer');
?><!DOCTYPE html><html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover"><title>Pagurus Portal</title>
<meta name="theme-color" content="#1a2129"><meta name="robots" content="noindex,nofollow">
<link rel="manifest" href="manifest.webmanifest"><link rel="icon" href="icon-192.png"><link rel="apple-touch-icon" href="apple-touch-icon.png">
<meta name="apple-mobile-web-app-title" content="Pagurus">
<meta name="apple-mobile-web-app-capable" content="yes"><meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
<style>
:root{--bg:#0f1419;--c:#1a2129;--fg:#e6e9ec;--m:#8a96a3;--a:#2f9be0;--on:#3fb950;--off:#56606b;--e:#f85149;--w:#d29922;--b:#2a333c}
*{box-sizing:border-box}body{margin:0;font:15px/1.4 system-ui,sans-serif;background:var(--bg);color:var(--fg);padding-bottom:env(safe-area-inset-bottom)}
header{padding:10px 16px;padding-top:max(10px,env(safe-area-inset-top));background:var(--c);display:flex;justify-content:space-between;align-items:center;gap:10px;flex-wrap:wrap;position:sticky;top:0;z-index:5}
h1{font-size:18px;margin:0}main{max-width:1150px;margin:auto;padding:10px}
nav{display:flex;gap:6px}nav a{color:var(--fg);text-decoration:none;padding:7px 11px;border-radius:6px;background:var(--bg);font-size:13px;cursor:pointer}
nav a.a{background:var(--a)}.badge{background:var(--e);border-radius:9px;padding:0 6px;font-size:11px;margin-left:5px}
.tab{display:none}.tab.a{display:block}
.card{background:var(--c);border-radius:10px;padding:16px;margin:12px 0}
h3{font-size:15px;margin:18px 0 6px}
.devs{display:grid;grid-template-columns:repeat(auto-fill,minmax(250px,1fr));gap:12px}
.dev{background:var(--c);border-radius:10px;padding:14px;display:flex;flex-direction:column;gap:10px}
.dev.off{opacity:.55}.dn{font-weight:600;font-size:16px}.dm{font-size:12px;color:var(--m)}
.dot{width:9px;height:9px;border-radius:50%;display:inline-block;margin-right:6px;background:var(--off)}.dot.on{background:var(--on)}
.sw{width:64px;height:64px;border-radius:50%;border:0;font-weight:700;color:#fff;background:var(--off);cursor:pointer;flex:none;font-size:14px}
.sw.on{background:var(--on);box-shadow:0 0 16px #3fb95055}.sw:disabled{cursor:default}.sw.wait{opacity:.6}
.dv{display:flex;gap:14px;align-items:center}.kv{display:grid;grid-template-columns:repeat(2,1fr);gap:6px 12px;flex:1}
.v{font-size:17px;font-weight:600}.l{font-size:11px;color:var(--m)}
.bt{padding:7px 13px;border:0;border-radius:6px;background:var(--a);color:#fff;font:inherit;cursor:pointer}.bt.g{background:#3a444f}
.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center}
.tag{font-size:11px;padding:1px 7px;border-radius:9px;background:#3a444f}.tag.r{background:#5a1d1d}.tag.g{background:#1d4a2a}
input{padding:9px;border-radius:6px;border:1px solid var(--b);background:var(--bg);color:var(--fg);font:inherit;width:100%}
input[type=range]{padding:0;accent-color:var(--a)}
table{width:100%;border-collapse:collapse;font-size:13px}td{padding:7px 6px;border-bottom:1px solid var(--b);vertical-align:top}
tr.ack{color:var(--m)}.kind{font-size:11px;padding:1px 6px;border-radius:4px;background:#3a444f;white-space:nowrap}
.kind.overpower,.kind.overtemp,.kind.offline,.kind.fridge{background:#5a1d1d}.kind.online,.kind.reset,.kind.finished{background:#1d4a2a}
#stat{font-size:12px;color:var(--m)}#stat.bad{color:var(--e)}
#login{position:fixed;inset:0;background:var(--bg);display:none;align-items:center;justify-content:center;z-index:20}
#login .card{width:320px}#login .bt{width:100%;margin-top:10px}
#toast{position:fixed;bottom:18px;left:50%;transform:translateX(-50%);padding:9px 16px;border-radius:6px;display:none;color:#fff;background:var(--e);z-index:30;max-width:90vw}
#toast.ok{background:var(--on)}.empty{color:var(--m);padding:24px;text-align:center}
</style></head><body>
<div id="login"><div class="card"><h1 style="margin-bottom:14px">Pagurus Portal</h1>
<form id="lf"><input type="password" id="pw" placeholder="Passwort" autocomplete="current-password" autofocus>
<button class="bt" type="submit">Anmelden</button></form></div></div>
<header><h1>Pagurus</h1><nav><a data-t="dv" class="a">Geräte</a><a data-t="al">Meldungen<span class="badge" id="nb" style="display:none"></span></a><a id="lo">Abmelden</a></nav>
<div id="stat"></div></header>
<main><section class="tab a" id="dv"><div id="sum" class="kv card" style="grid-template-columns:repeat(2,max-content);gap:6px 28px;padding:12px 16px"></div><div id="scbar" class="row" style="margin:6px 0 2px"></div><div id="devs"><div class="empty">Lade …</div></div></section>
<section class="tab" id="al"><div class="card"><table id="atab"></table></div></section></main>
<div id="toast"></div>
<script>
const $=id=>document.getElementById(id);
const esc=s=>String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const fmt=(v,d,u)=>v==null||isNaN(v)?'–':(+v).toLocaleString('de-DE',{minimumFractionDigits:d,maximumFractionDigits:d})+' '+u;
function ago(t){if(!t)return'nie';const s=Math.max(0,Date.now()/1000-t);return s<60?Math.round(s)+' s':s<3600?Math.round(s/60)+' min':s<86400?Math.round(s/3600)+' h':Math.round(s/86400)+' Tg'}
function toast(m,ok){const t=$('toast');t.textContent=m;t.className=ok?'ok':'';t.style.display='block';clearTimeout(t._h);t._h=setTimeout(()=>t.style.display='none',3500)}
async function api(a,body){const o=body?{method:'POST',headers:{'Content-Type':'application/json','X-Pagurus':'1'},body:JSON.stringify(body)}:{cache:'no-store'};
 const r=await fetch('api.php?a='+a,o);const j=await r.json().catch(()=>({}));
 if(r.status==401){$('login').style.display='flex';throw new Error('login')}
 if(!r.ok)throw new Error(j.error||('HTTP '+r.status));return j}
let S=null,PEND={};
const KIND={overpower:'Überlast',overtemp:'Übertemperatur',offline:'Offline',online:'Online',reset:'Reset',overvoltage:'Überspannung',update:'Update',finished:'Fertig',reminder:'Erinnerung',rule:'Regel',fridge:'Kühlgerät'};
const QUIET=['online','reset','update','rule','finished'];
function card(d){const ro=S.read_only,w=PEND[d.id],cov=(d.comp||'').startsWith('cover:'),sen=d.comp=='sensor:0';
 let kv='';
 if(d.apower!=null)kv+=`<div><div class="v">${fmt(d.apower,1,'W')}</div><div class="l">Leistung</div></div>`;
 if(d.wh!=null&&!sen)kv+=`<div><div class="v">${fmt(d.wh,0,'Wh')}</div><div class="l">heute</div></div>`;
 if(d.tC!=null)kv+=`<div><div class="v">${fmt(d.tC,1,'°C')}</div><div class="l">Temperatur</div></div>`;
 if(d.rh!=null)kv+=`<div><div class="v">${fmt(d.rh,0,'%')}</div><div class="l">Feuchte</div></div>`;
 if(d.lux!=null)kv+=`<div><div class="v">${fmt(d.lux,0,'lx')}</div><div class="l">Helligkeit</div></div>`;
 if(d.battery!=null)kv+=`<div><div class="v">${fmt(d.battery,0,'%')}</div><div class="l">Batterie</div></div>`;
 const can=!ro&&d.online&&!d.meter_only&&!sen;
 let ctl='';
 if(cov){const st={open:'offen',closed:'geschlossen',opening:'öffnet',closing:'schließt',stopped:'gestoppt'}[d.state]||d.state||'';
  ctl=`<div class="v">${esc(st)}${d.pos!=null?' · '+d.pos+' %':''}</div>`+(can?`<div class="row"><button class="bt g" onclick="cmd('${esc(d.id)}','open')">▲ Auf</button><button class="bt g" onclick="cmd('${esc(d.id)}','stop')">■ Stopp</button><button class="bt g" onclick="cmd('${esc(d.id)}','close')">▼ Zu</button></div>
  ${d.pos!=null?`<input type="range" min="0" max="100" value="${d.pos}" onchange="cmd('${esc(d.id)}','pos',+this.value)">`:''}`:'');}
 const btn=cov||sen||d.output==null?'':`<button class="sw ${d.output?'on':''} ${w?'wait':''}" ${can?'':'disabled'} onclick="cmd('${esc(d.id)}','toggle')">${w?'…':d.meter_only?'MESS':d.output?'AN':'AUS'}</button>`;
 const tags=[d.appliance?`<span class="tag ${d.appliance.state=='finished'?'g':''}">${esc(d.appliance.label)}</span>`:'',d.fridge_alarm?'<span class="tag r">Kühlgerät-Alarm</span>':''].join('');
 return `<div class="dev ${d.online||sen?'':'off'}"><div><div class="dn">${esc(d.name||d.id)}</div>
 <div class="dm"><span class="dot ${d.online?'on':''}"></span>${sen?'Fühler · zuletzt vor '+ago(d.last_seen):d.online?'online':'offline seit '+ago(d.last_seen)}${d.model?' · '+esc(d.model):''}</div></div>
 ${tags?`<div class="row">${tags}</div>`:''}<div class="dv">${btn}<div class="kv">${kv}</div></div>${ctl}</div>`}
function render(){const ok=S.now-S.last_sync<30;$('stat').className=ok?'':'bad';
 $('stat').textContent=S.last_sync?(ok?'Cloud verbunden':'Cloud nicht erreichbar seit '+ago(S.last_sync))+(S.read_only?' · nur Anzeige':''):'Cloud hat sich noch nicht gemeldet';
 $('scbar').innerHTML=S.read_only?'':(S.scenes||[]).map(x=>`<button class="bt g" onclick="cmd('scene:${x.id}','scene')">${esc(x.name)}</button>`).join('');
 const D=S.devices||[];{const pw=D.reduce((t,d)=>t+(d.apower||0),0),wh=D.reduce((t,d)=>t+(d.wh||0),0);$('sum').innerHTML=D.length?`<div><div class="v">${fmt(pw,0,'W')}</div><div class="l">Leistung jetzt</div></div><div><div class="v">${fmt(wh/1000,2,'kWh')}</div><div class="l">heute</div></div>`:''}
 if(!D.length){$('devs').innerHTML='<div class="empty">Noch keine Geräte – die Pagurus Cloud hat noch nichts übertragen.</div>';}
 else{const g={};D.forEach(d=>(g[d.room||'']=g[d.room||'']||[]).push(d));
  const ks=Object.keys(g).sort((a,b)=>a===''?1:b===''?-1:a.localeCompare(b,'de'));
  $('devs').innerHTML=ks.map(k=>`<h3>${k?esc(k):(ks.length>1?'Ohne Raum':'Alle Geräte')} <span class="l">${g[k].length}</span></h3><div class="devs">${g[k].map(card).join('')}</div>`).join('');}
 const A=S.alerts||[],n=A.filter(x=>!x.ack&&!QUIET.includes(x.kind)).length;$('nb').textContent=n;$('nb').style.display=n?'':'none';
 $('atab').innerHTML=A.length?A.map(x=>`<tr class="${x.ack?'ack':''}"><td style="white-space:nowrap">${new Date(x.ts*1000).toLocaleString('de-DE',{day:'2-digit',month:'2-digit',hour:'2-digit',minute:'2-digit'})}</td><td><span class="kind ${esc(x.kind)}">${esc(KIND[x.kind]||x.kind)}</span></td><td><b>${esc(x.name||x.dev)}</b><br>${esc(x.text)}</td></tr>`).join(''):'<tr><td class="empty">Keine Meldungen</td></tr>';
 for(const c of S.cmds||[]){if(PEND[c.dev]===c.id&&c.state!='new'&&c.state!='sent'){delete PEND[c.dev];if(c.state=='error')toast(c.result||'Fehler');else if(c.state=='expired')toast('Cloud hat den Befehl nicht abgeholt');else if(c.dev.startsWith('scene:'))toast('Szene ausgeführt',1)}}}
let busy=false;
async function load(){if(busy||document.hidden)return;busy=true;try{S=await api('state');$('login').style.display='none';render()}catch(e){if(e.message!='login')$('stat').textContent='Portal nicht erreichbar'}finally{busy=false}}
async function cmd(dev,act,pos){try{const r=await api('cmd',{dev,act,pos});PEND[dev]=r.id;render();setTimeout(load,800)}catch(e){if(e.message!='login')toast(e.message)}}
$('lf').onsubmit=async e=>{e.preventDefault();try{await api('login',{password:$('pw').value});$('pw').value='';$('login').style.display='none';load()}catch(x){toast(x.message)}};
$('lo').onclick=async()=>{await api('logout',{}).catch(()=>{});location.reload()};
document.querySelectorAll('nav a[data-t]').forEach(a=>a.onclick=()=>{document.querySelectorAll('nav a[data-t]').forEach(x=>x.classList.toggle('a',x===a));document.querySelectorAll('.tab').forEach(t=>t.classList.toggle('a',t.id==a.dataset.t))});
document.addEventListener('visibilitychange',load);
load();setInterval(load,3000);
</script></body></html>
