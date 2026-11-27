// Shelly Heizung LOGIK V13.2 - speichersparend (ohne Abgasteil von V12.7)
// V13.2: Sync mit der externen Cloud entfernt (keine Daten mehr nach draussen, keine Vorgaben von dort). Einstellen nur
//        noch lokal ueber cfg (Pagurus Cloud -> Heizung). Schluessel cl entfaellt.
// V13.1: cfg und cfg_set zu einem Endpunkt cfg zusammengelegt (Shelly erlaubt max. 5 Endpunkte, V13.0 startete nicht).
// V13.0: alle Einstellungen ueber Endpunkt cfg (lesen) und cfg?key=wert (aendern), z. B. von anlage.html.
//        Werden dauerhaft im KVS gespeichert (heat_cfg1, heat_cfg2) und beim Start geladen.
//        cfg?ov=x setzt das gelernte Nachheizen. Alle Antworten mit CORS-Header (Aufruf aus dem Browser).
//        status liefert zusaetzlich kessel_max, demand, sf, nacht, ver.
// V12.9: nachts (aE bis aS) startet der Brenner nur noch als Frostschutz: Speicher oder Kessel unter fr,
//        heizt bis beide fr+panH erreicht haben. Legionellenschutz am 1. erst ab aS (vorher ab 04:00).
//        tN wird nicht mehr verwendet. Boost-Knopf und Schornsteinfeger funktionieren auch nachts.
// V12.8: Morgen-Sollwert tM von aS bis mE (Modus bleibt "tag"). Automatischer Zapf-Boost heizt morgens nur bis tM,
//        der Boost-Knopf (Display/Dashboard) weiterhin bis tS.
// V12.6: Pumpe laeuft nach pMin weiter, solange Ruecklauf >= Speicher + rAus (statt Kessel - Ruecklauf >= rAus).
// V12.5: lernt das Nachheizen (Anstieg Speicher nach Brenner aus) und schaltet den Brenner entsprechend frueher ab.
// V12.4: Pumpe richtet sich beim Laufen nach dem Ruecklauf, Mindestlaufzeit pMin.
// V12.3: Pumpe nutzt Restwaerme in jedem Modus (bis Speicher rest), Notfall ignoriert Kesselsperre nur bis Kessel kPan.
// V12.2: Kesselsperre gibt schon bei 2 K unter kOff frei, im Notfall (Speicher < pan) gilt sie nicht.
// api_data liefert exakt die Felder von V11.9 (Touchdisplay), status ist fuer Dashboard V2 und anlage.html.
//
// EINSTELLUNGEN (CFG, Startwerte - gespeicherte Werte aus dem KVS haben Vorrang):
// aS/aE  Tagbetrieb von/bis          tS/tL  Soll Tag/Legionellen (Grad C)
// mE/tM  morgens (aS bis mE) gilt Soll tM statt tS
// ein    Brenner ein unter Soll-ein  aus       Brenner aus mind. aus K unter Soll
// ovMax  max. gelerntes Nachheizen (K). Gelernter Wert: ST.ov (status: nachheiz), wird mit der Statistik gespeichert
// kOff   Brenner aus, wenn Kessel >= Speicher + kOff, wieder frei unter kOff-kOH
// kHy    Hysterese Kessel-Obergrenze   kPan  Notfall heizt trotz Kesselsperre nur bis Kessel kPan
// rest   Pumpe laedt Restwaerme aus dem Kessel bis Speicher rest (auch nachts)
// kMax   Kessel-Obergrenze           pan       tagsueber gilt die Kesselsperre nicht, wenn Speicher < pan
// fr     Frostschutz nachts: Brenner ein, wenn Speicher oder Kessel < fr, aus ab fr+panH
// sperr  Taktsperre (s)              fs        Fail-safe Relais (s)
// minW   Flamme brennt ab Watt       lph       Liter pro Stunde
// pEin   Pumpe ein ab Differenz Kessel-Speicher
// rAus   Pumpe laeuft, solange Ruecklauf >= Speicher + rAus  pMin  Mindestlaufzeit Pumpe (s)
// pAus   nur ohne Ruecklauffuehler: Pumpe aus unter Differenz Kessel-Speicher    sMax  max. Speicher beim Waerme abfuehren
// drop   Zapf-Boost: Abfall pro Zyklus   bMax  Boost max. (s)   err  Fehlmessungen bis Stoerung

let VER = "13.2";
let CFG = {aS:"04:30",aE:"21:00",tS:55,tN:40,tL:65,mE:"12:00",tM:45,ein:3,aus:1,kOff:15,kOH:2,kHy:5,kMax:85,kPan:65,rest:60,pan:38,panH:3,fr:8,
sperr:600,fs:120,minW:120,lph:2.01,pEin:10,pAus:8,rAus:3,pMin:180,sMax:70,drop:0.2,ovMax:8,bMax:7200,err:3};

// aenderbare Einstellungen, zwei KVS-Schluessel (je max. 255 Byte) und erlaubte Bereiche
let K1 = ["aS","aE","mE","tS","tM","tL","fr","panH","ein","aus","bMax","drop","ovMax"];
let K2 = ["kOff","kOH","kHy","kMax","kPan","rest","pan","pEin","pAus","rAus","pMin","sMax","sperr","minW","lph"];
let R = {tS:[40,70],tM:[35,65],tL:[55,75],fr:[3,20],panH:[1,10],ein:[1,10],aus:[0,8],bMax:[600,14400],drop:[0.05,2],ovMax:[0,15],
kOff:[5,40],kOH:[1,10],kHy:[2,15],kMax:[70,95],kPan:[50,85],rest:[40,70],pan:[30,50],pEin:[3,25],pAus:[1,20],rAus:[0,15],pMin:[0,900],
sMax:[50,80],sperr:[0,3600],minW:[0,500],lph:[0.5,5]};

let S = {m:"init",soll:55,s:null,k:null,r:null,sOk:false,kOk:false,eS:0,eK:0,f:"",sf:false,boost:false,bA:false,bUp:0,nt:false,
leg:false,dem:false,kSp:false,kMx:false,bOn:false,pOn:false,offUp:-1,pUp:-1,last:null,lastUp:-1,burnS:0,ovOn:false,ovS:0,ovM:0,ovT:0,ovB:0,aux:null,auxUp:-1,w:0};
let ST = {total:0,daily:[0,0,0,0,0,0,0],lastDaySaved:-1,lastUnix:0,ov:1};
let loaded = false;

function n(v){return typeof v==="number"?v:0}
function up(){let s=Shelly.getComponentStatus("sys");return s?s.uptime:0}
function tmp(key){let c=Shelly.getComponentStatus(key);return(c&&typeof c.tC==="number"&&c.tC>-30&&c.tC<120)?c.tC:null}
function lock(u){if(S.offUp<0)return 0;let r=CFG.sperr-(u-S.offUp);return r>0?r:0}
function num(v){if(typeof v==="number")return v;if(typeof v!=="string"||v.length<1||v.length>12)return null;
try{let x=JSON.parse(v);return typeof x==="number"?x:null}catch(e){return null}}
function qv(q,k){if(typeof q!=="string")return null;let p=q.split("&");
for(let i=0;i<p.length;i++){let e=p[i].indexOf("=");if(e>0&&p[i].slice(0,e)===k)return p[i].slice(e+1)}return null}
function dec(v){let s="",i=0;while(i<v.length){let t=v.slice(i,i+3);
if(t==="%3A"||t==="%3a"){s+=":";i+=3}else if(t==="%2C"||t==="%2c"||t==="%2E"||t==="%2e"){s+=".";i+=3}
else{let c=v.slice(i,i+1);s+=c===","?".":c;i++}}return s}
function tOk(s){return typeof s==="string"&&s.length===5&&s.charAt(2)===":"&&s>="00:00"&&s<="23:59"}
function isT(k){return k==="aS"||k==="aE"||k==="mE"}
function save(){if(!loaded)return;Shelly.call("KVS.Set",{key:"heat_stats",value:JSON.stringify(ST)});S.burnS=0}
function wk(){let w=0;for(let i=0;i<7;i++)w+=n(ST.daily[i]);return w}
function td(){return n(ST.daily[(new Date()).getDay()])}
function auxOk(u){return S.aux!==null&&S.auxUp>=0&&u-S.auxUp<900}

// Einstellungen pruefen und uebernehmen. Ungueltige Schluessel landen in bad. Rueckgabe: Anzahl Aenderungen
function apply(o,bad){let a=CFG.aS,e=CFG.aE,m=CFG.mE,c=0,L=[K1,K2];
for(let j=0;j<2;j++){let K=L[j];for(let i=0;i<K.length;i++){let k=K[i],v=o[k];if(v===undefined||v===null)continue;
if(isT(k)){if(tOk(v)){if(CFG[k]!==v)c++;CFG[k]=v}else bad.push(k);continue}
let x=num(v),r=R[k];if(x!==null&&r&&x>=r[0]&&x<=r[1]){if(CFG[k]!==x)c++;CFG[k]=x}else bad.push(k)}}
if(CFG.aS>=CFG.aE){if(CFG.aS!==a||CFG.aE!==e)c--;CFG.aS=a;CFG.aE=e;bad.push("aS");bad.push("aE")}
if(CFG.mE<CFG.aS||CFG.mE>CFG.aE){CFG.mE=m;if(CFG.mE<CFG.aS||CFG.mE>CFG.aE)CFG.mE=CFG.aS;bad.push("mE")}
if(CFG.tM>CFG.tS)CFG.tM=CFG.tS;
if(CFG.kOH>=CFG.kOff)CFG.kOH=CFG.kOff-1;
return c}

function pick(K){let o={};for(let i=0;i<K.length;i++)o[K[i]]=CFG[K[i]];return JSON.stringify(o)}
function cfgSave(){Shelly.call("KVS.Set",{key:"heat_cfg1",value:pick(K1)},function(){Shelly.call("KVS.Set",{key:"heat_cfg2",value:pick(K2)})})}
function cfgLoad(k,next){Shelly.call("KVS.Get",{key:k},function(r){
if(r&&typeof r.value==="string"){try{let b=[];apply(JSON.parse(r.value),b)}catch(e){}}if(next)next()})}

Shelly.call("KVS.Get",{key:"heat_stats"},function(r){
if(r&&r.value){try{let d=JSON.parse(r.value);if(typeof d.total==="number"&&d.daily&&d.daily.length===7){
ST.total=d.total;for(let i=0;i<7;i++)ST.daily[i]=n(d.daily[i]);
ST.lastDaySaved=typeof d.lastDaySaved==="number"?d.lastDaySaved:-1;ST.lastUnix=n(d.lastUnix);if(typeof d.ov==="number")ST.ov=d.ov}}catch(e){}}
loaded=true;
cfgLoad("heat_cfg1",function(){cfgLoad("heat_cfg2",function(){print("Heizung Logik V"+VER+" - Tag "+CFG.aS+"-"+CFG.aE+", Soll "+CFG.tS+"/"+CFG.tM+" C")})})});

function ctl(){
let sy=Shelly.getComponentStatus("sys");if(!sy||!sy.time)return;
let u=sy.uptime,now=new Date(),day=now.getDay(),h=now.getHours(),mi=now.getMinutes();
let t=(h<10?"0":"")+h+":"+(mi<10?"0":"")+mi;
let dt=S.lastUp>=0?u-S.lastUp:0;if(dt<0||dt>120)dt=0;S.lastUp=u;
if(loaded){let g=ST.lastUnix>0?sy.unixtime-ST.lastUnix:0;ST.lastUnix=sy.unixtime;
if(ST.lastDaySaved!==day||g>518400){let d=ST.lastDaySaved;
if(g>518400){for(let i=0;i<7;i++)ST.daily[i]=0}else if(d>=0&&d<=6){while(d!==day){d=(d+1)%7;ST.daily[d]=0}}else ST.daily[day]=0;
ST.lastDaySaved=day;save()}}
let v=tmp("temperature:100");if(v===null){if(S.eS<99)S.eS++}else{S.eS=0;S.s=v}
v=tmp("temperature:101");if(v===null){if(S.eK<99)S.eK++}else{S.eK=0;S.k=v}
S.r=tmp("temperature:102");
S.sOk=S.s!==null&&S.eS<CFG.err;S.kOk=S.k!==null&&S.eK<CFG.err;
let f=S.sOk?"":"speicher";if(!S.kOk)f=f===""?"kessel":f+",kessel";
if(f!==S.f)print(f===""?"Sensoren OK":"STOERUNG "+f);S.f=f;
let ip=Shelly.getComponentStatus("input:0");S.sf=!!(ip&&ip.state===true);
let night=t>=CFG.aE||t<CFG.aS,leg=now.getDate()===1&&!night&&h<6;S.nt=night;
if(S.sOk){if(S.last!==null&&dt>=15&&dt<=60&&!night&&!S.boost&&S.last-S.s>=CFG.drop){S.boost=true;S.bA=true;S.bUp=u}S.last=S.s}
let od=ST.ov>CFG.aus?ST.ov:CFG.aus,mor=!night&&t<CFG.mE,bt=S.bA&&mor?CFG.tM:CFG.tS;
if(S.boost&&((S.sOk&&S.s>=bt-od)||u-S.bUp>=CFG.bMax))S.boost=false;
let m=S.sf?"schornsteinfeger":(leg?"legionellen":(S.boost?"boost":(night?"nacht":"tag")));
S.m=m;S.leg=(m==="legionellen");
let so=CFG.tS;if(m==="nacht")so=CFG.fr;if(m==="tag"&&mor)so=CFG.tM;if(m==="boost")so=bt;if(S.leg)so=CFG.tL;S.soll=so;
let off=so-od,on=so-CFG.ein;if(on>off-2)on=off-2;if(m==="nacht"){on=CFG.fr;off=CFG.fr+CFG.panH}if(S.leg)off=so;
let x=m==="nacht"&&S.k<S.s?S.k:S.s;if(f!=="")S.dem=false;else if(S.dem)S.dem=x<off;else S.dem=x<on;
if(f===""){let d=S.k-S.s;if(S.kSp){if(d<CFG.kOff-CFG.kOH)S.kSp=false}else if(d>=CFG.kOff)S.kSp=true}
if(S.kOk){if(S.kMx){if(S.k<CFG.kMax-CFG.kHy)S.kMx=false}else if(S.k>=CFG.kMax)S.kMx=true}else S.kMx=false;
let b=Shelly.getComponentStatus("switch:0"),bo=!!(b&&b.output);S.w=(b&&typeof b.apower==="number")?b.apower:0;
let want=f===""&&S.dem&&(!S.kSp||(S.s<CFG.pan&&S.k<CFG.kPan))&&!S.kMx;
if(want&&!bo&&lock(u)>0)want=false;
if(S.sf)want=!S.kMx;
if(bo&&!want){S.offUp=u;if(!S.sf&&f===""&&!S.dem&&m!=="nacht"){S.ovOn=true;S.ovS=S.s;S.ovM=S.s;S.ovT=u;S.ovB=u}}
if(loaded&&bo&&dt>0&&S.w>CFG.minW){let l=CFG.lph*dt/3600;ST.total+=l;ST.daily[day]=n(ST.daily[day])+l;S.burnS+=dt;if(S.burnS>=1800)save()}
if(bo&&!want&&S.burnS>0)save();
if(want)Shelly.call("Switch.Set",{id:0,on:true,toggle_after:CFG.fs});else if(bo)Shelly.call("Switch.Set",{id:0,on:false});
S.bOn=want;
let p=Shelly.getComponentStatus("switch:1"),po=!!(p&&p.output),pu=false;
if(S.sf)pu=true;else if(f===""){let d=S.k-S.s;
let lim=so>CFG.rest?so:CFG.rest;
if(S.s<lim){if(S.leg)pu=d>0;else if(m==="boost")pu=d>2;else if(!po)pu=d>=CFG.pEin;else if(S.r===null)pu=d>CFG.pAus;else pu=u-S.pUp<CFG.pMin||S.r-S.s>=CFG.rAus}
if(S.k>=CFG.kMax-5&&d>5&&S.s<CFG.sMax)pu=true}
if(pu&&!po)S.pUp=u;
if(pu!==po)Shelly.call("Switch.Set",{id:1,on:pu});S.pOn=pu;
if(S.ovOn){if(want||f!=="")S.ovOn=false;else{if(S.s>S.ovM){S.ovM=S.s;S.ovT=u}
if((!pu&&u-S.ovT>=600)||u-S.ovB>=5400){let r=S.ovM-S.ovS;S.ovOn=false;
if(r>=0&&r<=CFG.ovMax){ST.ov=Math.round((ST.ov+r)*5)/10;print("Nachheizen: +"+r.toFixed(1)+" K, neu "+ST.ov+" K");save()}}}}}

function js(res,o){res.code=200;res.headers=[["Content-Type","application/json"],["Access-Control-Allow-Origin","*"],["Cache-Control","no-store"]];
res.body=JSON.stringify(o);res.send()}

HTTPServer.registerEndpoint("aux_data",function(q,res){let t=num(qv(q.query,"temp"));
if(t!==null&&t>-50&&t<150){S.aux=t;S.auxUp=up()}res.code=200;res.send()});

HTTPServer.registerEndpoint("api_data",function(q,res){let w=wk(),d=td(),u=up();
js(res,{temp_s:S.sOk?S.s:null,temp_k:S.kOk?S.k:null,temp_aux:auxOk(u)?S.aux:0,pump:S.pOn?1:0,burn:S.bOn?1:0,
boost:(S.boost||S.leg||S.sf)?1:0,pump_b:S.pOn,burn_b:S.bOn,cons_total:ST.total,cons_week:w,cons_today:d,
total:ST.total,week:w,today:d})});

HTTPServer.registerEndpoint("status",function(q,res){let u=up();
js(res,{ver:VER,temp_s:S.sOk?S.s:null,temp_k:S.kOk?S.k:null,temp_r:S.r,temp_aux:auxOk(u)?S.aux:null,mode:S.m,soll:S.soll,
fault:S.f,lockout_s:S.bOn?0:lock(u),power:S.w,pump:S.pOn?1:0,burn:S.bOn?1:0,boost_manual:S.boost,kessel_sperre:S.kSp,kessel_max:S.kMx,
demand:S.dem,sf:S.sf,nacht:S.nt,nachheiz:ST.ov,cons_total:ST.total,cons_week:wk(),cons_today:td()})});

HTTPServer.registerEndpoint("boost_trigger",function(q,res){let v=qv(q.query,"on"),o=!S.boost;
if(v==="1")o=true;if(v==="0")o=false;S.boost=o;S.bA=false;if(o)S.bUp=up();ctl();js(res,{boost:S.boost})});

// cfg ohne Parameter = lesen, cfg?tS=55&aS=05%3A00 = aendern (max. 5 Endpunkte je Script, daher nur einer)
HTTPServer.registerEndpoint("cfg",function(q,res){let o={},bad=[],L=[K1,K2];
for(let j=0;j<2;j++){let K=L[j];for(let i=0;i<K.length;i++){let v=qv(q.query,K[i]);if(v!==null)o[K[i]]=dec(v)}}
let c=apply(o,bad),ov=num(qv(q.query,"ov"));
if(ov!==null&&ov>=0&&ov<=CFG.ovMax){ST.ov=ov;save();print("Nachheizen gesetzt: "+ov+" K")}
if(c>0){cfgSave();print("Einstellungen geaendert ("+c+")")}
ctl();js(res,{ok:bad.length===0,bad:bad,changed:c,ver:VER,cfg:CFG,ov:ST.ov})});

Timer.set(3000,false,ctl);
Timer.set(20000,true,ctl);
