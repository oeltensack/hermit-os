# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 D.Koch – Hermit OS
"""Hermit Cloud – kleine Heim-Cloud fuer Hermit-OS- und Shelly-Gen2+-Geraete.

Die Geraete verbinden sich per MQTT (Gen2-Format) mit dem Broker. Diese App liest mit,
speichert Status, Leistung und Energie in SQLite und bietet eine Weboberflaeche:
Geraeteuebersicht, Schalten, Energieverlauf und Meldungen.
"""
import asyncio
import hashlib
import hmac
import itertools
import json
import os
import sqlite3
import threading
import time
import urllib.request
from datetime import datetime, timedelta
from pathlib import Path
from zoneinfo import ZoneInfo

import paho.mqtt.client as mqtt
from fastapi import FastAPI, HTTPException, Request, Response
from fastapi.responses import FileResponse, JSONResponse

VERSION = json.loads((Path(__file__).parent / "version.json").read_text())["version"]
UPDATE_URL = os.environ.get("UPDATE_URL",
    "https://raw.githubusercontent.com/oeltensack/hermit-os/main/cloud/app/version.json")
DATA = Path(os.environ.get("DATA_DIR", "/data"))
DB_PATH = DATA / "cloud.db"
MQTT_HOST = os.environ.get("MQTT_HOST", "mosquitto")
MQTT_PORT = int(os.environ.get("MQTT_PORT", "1883"))
MQTT_USER = os.environ.get("MQTT_USER", "")
MQTT_PASS = os.environ.get("MQTT_PASS", "")
PASSWORD = os.environ.get("CLOUD_PASSWORD", "")
TZ = ZoneInfo(os.environ.get("TZ", "Europe/Berlin"))
KEEP_POWER_DAYS = int(os.environ.get("KEEP_POWER_DAYS", "31"))
OFFLINE_AFTER = int(os.environ.get("OFFLINE_AFTER", "180"))     # s ohne Lebenszeichen
SRC = "hermit-cloud"
STATIC = Path(__file__).parent / "static"

# ======================= Datenbank =======================
_db_lock = threading.Lock()


def db():
    c = sqlite3.connect(DB_PATH, check_same_thread=False, timeout=10)
    c.row_factory = sqlite3.Row
    return c


DB = None


def q(sql, args=(), one=False):
    with _db_lock:
        cur = DB.execute(sql, args)
        rows = cur.fetchall()
        DB.commit()
    return (rows[0] if rows else None) if one else rows


def init_db():
    global DB
    DATA.mkdir(parents=True, exist_ok=True)
    DB = db()
    DB.executescript("""
    PRAGMA journal_mode=WAL;
    CREATE TABLE IF NOT EXISTS devices(
      id TEXT PRIMARY KEY, name TEXT, model TEXT, app TEXT, ver TEXT, mac TEXT, ip TEXT, gen INTEGER,
      online INTEGER DEFAULT 0, last_seen REAL DEFAULT 0, first_seen REAL,
      comp TEXT, status TEXT DEFAULT '{}', info TEXT DEFAULT '{}',
      last_total REAL, today_wh REAL DEFAULT 0, hidden INTEGER DEFAULT 0);
    CREATE TABLE IF NOT EXISTS power(dev TEXT, ts INTEGER, w REAL, PRIMARY KEY(dev, ts));
    CREATE TABLE IF NOT EXISTS energy(dev TEXT, day TEXT, wh REAL, PRIMARY KEY(dev, day));
    CREATE TABLE IF NOT EXISTS alerts(id INTEGER PRIMARY KEY AUTOINCREMENT, ts REAL, dev TEXT,
      kind TEXT, text TEXT, ack INTEGER DEFAULT 0);
    CREATE INDEX IF NOT EXISTS alerts_ts ON alerts(ts);
    CREATE TABLE IF NOT EXISTS settings(k TEXT PRIMARY KEY, v TEXT);
    """)
    DB.commit()


def today():
    return datetime.now(TZ).strftime("%Y-%m-%d")


def dev_name(dev):
    r = q("SELECT name FROM devices WHERE id=?", (dev,), one=True)
    return (r["name"] if r and r["name"] else dev)


def alert(dev, kind, text):
    q("INSERT INTO alerts(ts, dev, kind, text) VALUES(?,?,?,?)", (time.time(), dev, kind, text))


# ======================= MQTT =======================
class Bus:
    def __init__(self):
        self.c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"{SRC}-{os.getpid()}")
        if MQTT_USER:
            self.c.username_pw_set(MQTT_USER, MQTT_PASS)
        self.c.on_connect = self.on_connect
        self.c.on_message = self.on_message
        self.c.on_disconnect = lambda *a: None
        self.connected = False
        self.ids = itertools.count(1)
        self.wait = {}          # rpc-id -> (Event, [Antwort])
        self.loop = None

    def start(self):
        self.c.connect_async(MQTT_HOST, MQTT_PORT, keepalive=60)
        self.c.loop_start()

    def on_connect(self, c, userdata, flags, rc, props=None):
        self.connected = not rc.is_failure
        if self.connected:
            c.subscribe([("+/online", 0), ("+/info", 0), ("+/status/+", 0), ("+/events/rpc", 0),
                         (f"{SRC}/rpc", 0)])

    # ---- eingehend ----
    def on_message(self, c, userdata, msg):
        try:
            self.handle(msg.topic, msg.payload)
        except Exception as e:                       # nie den MQTT-Thread sterben lassen
            print("Fehler bei", msg.topic, e, flush=True)

    def handle(self, topic, payload):
        if topic == f"{SRC}/rpc":
            d = json.loads(payload)
            w = self.wait.get(d.get("id"))
            if w:
                w[1].append(d)
                w[0].set()
            return
        dev, _, rest = topic.partition("/")
        if not dev or dev == SRC:
            return
        if rest == "online":
            on = payload.decode().strip() == "true"
            seen(dev, online=on)
            if on and not q("SELECT info FROM devices WHERE id=? AND info!='{}'", (dev,), one=True):
                # Shelly Gen2+ ohne /info: Geraeteinfo per RPC holen
                threading.Thread(target=self.fetch_info, args=(dev,), daemon=True).start()
            return
        if not payload:
            return
        d = json.loads(payload)
        if rest == "info":
            set_info(dev, d)
        elif rest.startswith("status/"):
            comp = rest[7:]
            if comp.startswith(("switch:", "cct:", "light:")):
                set_status(dev, comp, d)
            else:
                seen(dev)
        elif rest == "events/rpc":
            m = d.get("method")
            p = d.get("params") or {}
            if m == "NotifyEvent":
                for e in p.get("events", []):
                    on_event(dev, e)
            elif m in ("NotifyStatus", "NotifyFullStatus"):
                for k, v in p.items():
                    if isinstance(v, dict) and k.startswith(("switch:", "cct:", "light:")):
                        set_status(dev, k, v, merge=True)
                seen(dev)

    def fetch_info(self, dev):
        try:
            set_info(dev, self.rpc(dev, "Shelly.GetDeviceInfo"))
        except Exception:
            pass

    # ---- ausgehend ----
    def rpc(self, dev, method, params=None, timeout=6):
        if not self.connected:
            raise RuntimeError("Keine Verbindung zum MQTT-Broker")
        rid = next(self.ids)
        ev, box = threading.Event(), []
        self.wait[rid] = (ev, box)
        try:
            req = {"id": rid, "src": SRC, "method": method}
            if params:
                req["params"] = params
            self.c.publish(f"{dev}/rpc", json.dumps(req))
            if not ev.wait(timeout):
                raise TimeoutError("Gerät antwortet nicht")
            r = box[0]
            if "error" in r:
                raise RuntimeError(r["error"].get("message") or str(r["error"]))
            return r.get("result")
        finally:
            self.wait.pop(rid, None)


BUS = Bus()


# ======================= Geraetezustand =======================
def ensure(dev):
    q("INSERT OR IGNORE INTO devices(id, first_seen) VALUES(?,?)", (dev, time.time()))


def seen(dev, online=None):
    ensure(dev)
    r = q("SELECT online, hidden FROM devices WHERE id=?", (dev,), one=True)
    now = time.time()
    if online is False:
        if r["online"]:
            q("UPDATE devices SET online=0 WHERE id=?", (dev,))
            if not r["hidden"]:
                alert(dev, "offline", f"{dev_name(dev)} ist offline")
        return
    if not r["online"] and r["hidden"] == 0 and q("SELECT 1 FROM alerts WHERE dev=? AND kind='offline'", (dev,), one=True):
        alert(dev, "online", f"{dev_name(dev)} ist wieder online")
    q("UPDATE devices SET online=1, last_seen=? WHERE id=?", (now, dev))


def set_info(dev, d):
    ensure(dev)
    q("""UPDATE devices SET info=?, model=?, app=?, ver=?, mac=?, gen=?, ip=COALESCE(?, ip),
         name=COALESCE(?, name) WHERE id=?""",
      (json.dumps(d), d.get("model"), d.get("app"), d.get("ver"), d.get("mac"), d.get("gen"),
       d.get("ip"), d.get("name"), dev))
    seen(dev)


def set_status(dev, comp, st, merge=False):
    ensure(dev)
    r = q("SELECT status, comp, last_total, today_wh FROM devices WHERE id=?", (dev,), one=True)
    old = json.loads(r["status"] or "{}") if r["comp"] in (None, comp) else {}
    new = {**old, **st} if merge else st
    if merge and "aenergy" in st:
        new["aenergy"] = st["aenergy"]
    q("UPDATE devices SET status=?, comp=? WHERE id=?", (json.dumps(new), comp, dev))
    seen(dev)
    now = int(time.time())
    if "apower" in st:
        q("INSERT OR REPLACE INTO power(dev, ts, w) VALUES(?,?,?)", (dev, now // 60 * 60, float(st["apower"])))
    tot = (st.get("aenergy") or {}).get("total")
    if tot is not None:
        tot = float(tot)
        last = r["last_total"]
        if last is not None:
            delta = tot - last
            if delta < 0:                 # Zaehler zurueckgesetzt (Reset, Werkseinstellung)
                delta = 0
            if delta > 0:
                q("""INSERT INTO energy(dev, day, wh) VALUES(?,?,?)
                     ON CONFLICT(dev, day) DO UPDATE SET wh = wh + excluded.wh""", (dev, today(), delta))
        q("UPDATE devices SET last_total=? WHERE id=?", (tot, dev))


EVENT_TEXT = {
    "overpower": ("overpower", "Überlast – Relais abgeschaltet"),
    "overtemperature": ("overtemp", "Übertemperatur – Relais abgeschaltet"),
    "overtemp": ("overtemp", "Übertemperatur – Relais abgeschaltet"),
    "reset": ("reset", "Schutzabschaltung am Gerät zurückgesetzt"),
    "overvoltage": ("overvoltage", "Überspannung"),
}


def on_event(dev, e):
    seen(dev)
    ev = e.get("event", "")
    if ev in EVENT_TEXT:
        kind, text = EVENT_TEXT[ev]
        alert(dev, kind, f"{dev_name(dev)}: {text}")


def watchdog():
    while True:
        time.sleep(30)
        try:
            lim = time.time() - OFFLINE_AFTER
            for r in q("SELECT id FROM devices WHERE online=1 AND last_seen<?", (lim,)):
                seen(r["id"], online=False)
            q("DELETE FROM power WHERE ts<?", (int(time.time()) - KEEP_POWER_DAYS * 86400,))
            q("DELETE FROM alerts WHERE ts<? AND ack=1", (time.time() - 180 * 86400,))
        except Exception as e:
            print("Watchdog:", e, flush=True)


# ======================= Web =======================
app = FastAPI(title="Hermit Cloud", version=VERSION)


def token():
    return hmac.new(PASSWORD.encode(), b"hermit-cloud", hashlib.sha256).hexdigest()


@app.middleware("http")
async def auth(request: Request, call_next):
    p = request.url.path
    if PASSWORD and p.startswith("/api/") and p != "/api/login":
        if not hmac.compare_digest(request.cookies.get("hc", ""), token()):
            return JSONResponse({"detail": "Anmeldung erforderlich"}, status_code=401)
    return await call_next(request)


@app.on_event("startup")
def startup():
    init_db()
    BUS.start()
    threading.Thread(target=watchdog, daemon=True).start()
    threading.Thread(target=updater_loop, daemon=True).start()


@app.get("/")
def index():
    return FileResponse(STATIC / "index.html")


@app.post("/api/login")
async def login(request: Request, response: Response):
    d = await request.json()
    if not PASSWORD:
        return {"ok": True}
    if not hmac.compare_digest(str(d.get("password", "")), PASSWORD):
        raise HTTPException(401, "Passwort falsch")
    response.set_cookie("hc", token(), max_age=180 * 86400, httponly=True, samesite="lax")
    return {"ok": True}


@app.post("/api/logout")
def logout(response: Response):
    response.delete_cookie("hc")
    return {"ok": True}


# ======================= Update der Cloud =======================
# Die App prueft version.json auf GitHub. Installiert wird vom Container "updater": die App legt
# data/update.request ab, der Updater holt den neuen Stand (git pull bzw. Archiv) und baut neu.
UPD = {"available": None, "last_check": 0, "error": ""}


def setting(k, default=None):
    r = q("SELECT v FROM settings WHERE k=?", (k,), one=True)
    return r["v"] if r else default


def set_setting(k, v):
    q("INSERT OR REPLACE INTO settings(k, v) VALUES(?,?)", (k, str(v)))


def ver_tuple(v):
    return tuple(int(x) for x in str(v).split(".") if x.isdigit())


def upd_check():
    try:
        with urllib.request.urlopen(UPDATE_URL + f"?t={int(time.time())}", timeout=15) as r:
            v = json.loads(r.read())["version"]
        UPD["available"] = v if ver_tuple(v) > ver_tuple(VERSION) else None
        UPD["error"] = ""
    except Exception as e:
        UPD["error"] = f"Prüfung fehlgeschlagen: {e}"
    UPD["last_check"] = time.time()
    return UPD["available"]


def updater_present():
    try:
        return time.time() - float((DATA / "updater.alive").read_text()) < 120
    except Exception:
        return False


def upd_request():
    if not updater_present():
        raise RuntimeError("Updater-Container läuft nicht (siehe cloud/README.md)")
    (DATA / "update.request").write_text(UPD["available"] or "manual")
    alert("cloud", "update", f"Cloud-Update auf {UPD['available'] or 'aktuellen Stand'} gestartet")


def updater_loop():
    time.sleep(60)
    last_day = None
    while True:
        try:
            mode, hour = setting("upd_mode", "install"), int(setting("upd_hour", "4"))
            now = datetime.now(TZ)
            if mode != "off" and (time.time() - UPD["last_check"] > 6 * 3600 or
                                  (now.hour == hour and last_day != now.date())):
                if upd_check() and mode == "install" and now.hour == hour and last_day != now.date() \
                        and updater_present() and not (DATA / "update.request").exists():
                    upd_request()
                if now.hour == hour:
                    last_day = now.date()
        except Exception as e:
            print("Update:", e, flush=True)
        time.sleep(300)


@app.get("/api/update")
def update_info():
    def rd(n):
        try:
            return (DATA / n).read_text()[-3000:]
        except Exception:
            return ""
    return {"current": VERSION, "available": UPD["available"], "last_check": UPD["last_check"],
            "error": UPD["error"], "mode": setting("upd_mode", "install"), "hour": int(setting("upd_hour", "4")),
            "updater": updater_present(), "running": (DATA / "update.request").exists(), "log": rd("update.log")}


@app.post("/api/update/check")
def update_check():
    upd_check()
    return update_info()


@app.post("/api/update/install")
def update_install():
    try:
        upd_request()
    except Exception as e:
        raise HTTPException(409, str(e))
    return update_info()


@app.post("/api/update/config")
async def update_config(request: Request):
    d = await request.json()
    if d.get("mode") in ("off", "check", "install"):
        set_setting("upd_mode", d["mode"])
    if "hour" in d and 0 <= int(d["hour"]) <= 23:
        set_setting("upd_hour", int(d["hour"]))
    return update_info()


@app.get("/api/info")
def info():
    return {"version": VERSION, "mqtt": BUS.connected, "auth": bool(PASSWORD), "update": UPD["available"],
            "broker": f"{os.environ.get('PUBLIC_BROKER', '')}"}


def dev_row(r):
    st = json.loads(r["status"] or "{}")
    info = json.loads(r["info"] or "{}")
    e = q("SELECT wh FROM energy WHERE dev=? AND day=?", (r["id"], today()), one=True)
    return {
        "id": r["id"], "name": r["name"] or "", "model": r["model"], "app": r["app"], "ver": r["ver"],
        "mac": r["mac"], "ip": r["ip"], "gen": r["gen"], "online": bool(r["online"]),
        "last_seen": r["last_seen"], "comp": r["comp"], "status": st,
        "hermit": str(info.get("fw_id", "")).startswith("hermit") or str(r["model"] or "").startswith("HERMIT"),
        "today_wh": round(e["wh"], 1) if e else 0.0,
    }


@app.get("/api/devices")
def devices():
    rows = q("SELECT * FROM devices WHERE hidden=0 ORDER BY COALESCE(NULLIF(name,''), id)")
    return [dev_row(r) for r in rows]


def get_dev(dev):
    r = q("SELECT * FROM devices WHERE id=?", (dev,), one=True)
    if not r:
        raise HTTPException(404, "Unbekanntes Gerät")
    return r


async def rpc_async(dev, method, params=None):
    try:
        return await asyncio.to_thread(BUS.rpc, dev, method, params)
    except Exception as e:
        raise HTTPException(502, str(e))


@app.post("/api/devices/{dev}/rpc")
async def dev_rpc(dev: str, request: Request):
    get_dev(dev)
    d = await request.json()
    return {"result": await rpc_async(dev, d["method"], d.get("params"))}


@app.post("/api/devices/{dev}/switch")
async def dev_switch(dev: str, request: Request):
    r = get_dev(dev)
    d = await request.json()
    comp = r["comp"] or "switch:0"
    kind, cid = comp.split(":")
    method = {"switch": "Switch", "cct": "CCT", "light": "Light"}.get(kind, "Switch")
    if "on" in d:
        res = await rpc_async(dev, f"{method}.Set", {"id": int(cid), "on": bool(d["on"])})
    else:
        res = await rpc_async(dev, f"{method}.Toggle", {"id": int(cid)})
    st = await rpc_async(dev, f"{method}.GetStatus", {"id": int(cid)})
    set_status(dev, comp, st, merge=True)
    return {"result": res, "status": st}


@app.post("/api/devices/{dev}/name")
async def dev_rename(dev: str, request: Request):
    get_dev(dev)
    name = (await request.json()).get("name", "").strip()
    q("UPDATE devices SET name=? WHERE id=?", (name, dev))
    try:                                     # auch am Geraet setzen, wenn erreichbar
        await asyncio.to_thread(BUS.rpc, dev, "Sys.SetConfig", {"config": {"device": {"name": name}}}, 4)
    except Exception:
        pass
    return {"ok": True}


@app.delete("/api/devices/{dev}")
def dev_delete(dev: str):
    get_dev(dev)
    q("DELETE FROM devices WHERE id=?", (dev,))
    q("DELETE FROM power WHERE dev=?", (dev,))
    q("DELETE FROM energy WHERE dev=?", (dev,))
    return {"ok": True}


@app.get("/api/devices/{dev}/power")
def dev_power(dev: str, hours: int = 24):
    since = int(time.time()) - min(hours, KEEP_POWER_DAYS * 24) * 3600
    return [[r["ts"], r["w"]] for r in q("SELECT ts, w FROM power WHERE dev=? AND ts>=? ORDER BY ts", (dev, since))]


@app.get("/api/energy")
def energy(dev: str = "", days: int = 31):
    start = (datetime.now(TZ) - timedelta(days=days - 1)).strftime("%Y-%m-%d")
    if dev:
        rows = q("SELECT day, wh FROM energy WHERE dev=? AND day>=? ORDER BY day", (dev, start))
    else:
        rows = q("SELECT day, SUM(wh) AS wh FROM energy WHERE day>=? GROUP BY day ORDER BY day", (start,))
    have = {r["day"]: r["wh"] for r in rows}
    out = []
    for i in range(days):
        d = (datetime.now(TZ) - timedelta(days=days - 1 - i)).strftime("%Y-%m-%d")
        out.append([d, round(have.get(d, 0.0), 1)])
    return out


@app.get("/api/energy/months")
def energy_months(dev: str = ""):
    if dev:
        rows = q("SELECT substr(day,1,7) AS m, SUM(wh) AS wh FROM energy WHERE dev=? GROUP BY m ORDER BY m DESC LIMIT 24", (dev,))
    else:
        rows = q("SELECT substr(day,1,7) AS m, SUM(wh) AS wh FROM energy GROUP BY m ORDER BY m DESC LIMIT 24")
    return [[r["m"], round(r["wh"], 1)] for r in rows]


@app.get("/api/energy/by_device")
def energy_by_device(days: int = 30):
    start = (datetime.now(TZ) - timedelta(days=days - 1)).strftime("%Y-%m-%d")
    rows = q("""SELECT e.dev, COALESCE(NULLIF(d.name,''), e.dev) AS name, SUM(e.wh) AS wh FROM energy e
                LEFT JOIN devices d ON d.id=e.dev WHERE e.day>=? GROUP BY e.dev ORDER BY wh DESC""", (start,))
    return [{"dev": r["dev"], "name": r["name"], "wh": round(r["wh"], 1)} for r in rows]


@app.get("/api/alerts")
def alerts(limit: int = 200):
    rows = q("""SELECT a.*, COALESCE(NULLIF(d.name,''), a.dev) AS name FROM alerts a
                LEFT JOIN devices d ON d.id=a.dev ORDER BY a.id DESC LIMIT ?""", (limit,))
    return [dict(r) for r in rows]


@app.post("/api/alerts/ack")
async def alerts_ack(request: Request):
    d = await request.json() if (await request.body()) else {}
    if d.get("id"):
        q("UPDATE alerts SET ack=1 WHERE id=?", (int(d["id"]),))
    else:
        q("UPDATE alerts SET ack=1 WHERE ack=0")
    return {"ok": True}
