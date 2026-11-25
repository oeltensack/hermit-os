# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 D.Koch – Pagurus OS
"""Gemeinsame Grundlagen: Konfiguration, Datenbank, Einstellungen, Meldungen, Push."""
import json
import os
import queue
import sqlite3
import threading
import time
import urllib.parse
import urllib.request
from datetime import datetime
from pathlib import Path
from zoneinfo import ZoneInfo

DATA = Path(os.environ.get("DATA_DIR", "/data"))
DB_PATH = DATA / "cloud.db"
TZ = ZoneInfo(os.environ.get("TZ", "Europe/Berlin"))
KEEP_POWER_DAYS = int(os.environ.get("KEEP_POWER_DAYS", "31"))
OFFLINE_AFTER = int(os.environ.get("OFFLINE_AFTER", "180"))
OFFLINE_ALERT_AFTER = int(os.environ.get("OFFLINE_ALERT_AFTER", "120"))   # kurze Aussetzer nicht melden

_lock = threading.RLock()
DB = None


def q(sql, args=(), one=False):
    with _lock:
        cur = DB.execute(sql, args)
        rows = cur.fetchall()
        DB.commit()
        last = cur.lastrowid
    if one is None:
        return last
    return (rows[0] if rows else None) if one else rows


def qid(sql, args=()):
    """INSERT ausfuehren und neue Zeilen-ID liefern."""
    return q(sql, args, one=None)


def _cols(table):
    return {r[1] for r in DB.execute(f"PRAGMA table_info({table})").fetchall()}


def _add_col(table, col, decl):
    if col not in _cols(table):
        DB.execute(f"ALTER TABLE {table} ADD COLUMN {col} {decl}")


def init_db():
    global DB
    DATA.mkdir(parents=True, exist_ok=True)
    DB = sqlite3.connect(DB_PATH, check_same_thread=False, timeout=15)
    DB.row_factory = sqlite3.Row
    DB.executescript("""
    PRAGMA journal_mode=WAL;
    CREATE TABLE IF NOT EXISTS devices(
      id TEXT PRIMARY KEY, name TEXT, model TEXT, app TEXT, ver TEXT, mac TEXT, ip TEXT, gen INTEGER,
      online INTEGER DEFAULT 0, last_seen REAL DEFAULT 0, first_seen REAL,
      comp TEXT, status TEXT DEFAULT '{}', info TEXT DEFAULT '{}',
      last_total REAL, today_wh REAL DEFAULT 0, hidden INTEGER DEFAULT 0);
    CREATE TABLE IF NOT EXISTS power(dev TEXT, ts INTEGER, w REAL, PRIMARY KEY(dev, ts));
    CREATE TABLE IF NOT EXISTS energy(dev TEXT, day TEXT, wh REAL, PRIMARY KEY(dev, day));
    CREATE TABLE IF NOT EXISTS energy_h(dev TEXT, hour TEXT, wh REAL, PRIMARY KEY(dev, hour));
    CREATE TABLE IF NOT EXISTS alerts(id INTEGER PRIMARY KEY AUTOINCREMENT, ts REAL, dev TEXT,
      kind TEXT, text TEXT, ack INTEGER DEFAULT 0);
    CREATE INDEX IF NOT EXISTS alerts_ts ON alerts(ts);
    CREATE TABLE IF NOT EXISTS settings(k TEXT PRIMARY KEY, v TEXT);
    CREATE TABLE IF NOT EXISTS users(name TEXT PRIMARY KEY, pw TEXT, role TEXT, created REAL);
    CREATE TABLE IF NOT EXISTS sessions(token TEXT PRIMARY KEY, name TEXT, created REAL, seen REAL);
    CREATE TABLE IF NOT EXISTS appliances(dev TEXT PRIMARY KEY, type TEXT, label TEXT,
      start_w REAL, start_s INTEGER, end_w REAL, end_s INTEGER, remind_min INTEGER,
      state TEXT DEFAULT 'idle', since REAL, run_id INTEGER, cand REAL, low REAL);
    CREATE TABLE IF NOT EXISTS runs(id INTEGER PRIMARY KEY AUTOINCREMENT, dev TEXT, type TEXT,
      start REAL, end REAL, wh REAL, start_total REAL, peak_w REAL, cost REAL, ack INTEGER DEFAULT 0,
      reminded INTEGER DEFAULT 0);
    CREATE INDEX IF NOT EXISTS runs_dev ON runs(dev, start);
    CREATE TABLE IF NOT EXISTS schedules(id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT, enabled INTEGER DEFAULT 1,
      days INTEGER DEFAULT 127, time TEXT, target TEXT, action TEXT, last_run TEXT);
    CREATE TABLE IF NOT EXISTS rules(id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT, enabled INTEGER DEFAULT 1,
      trig TEXT, action TEXT, last_fire REAL, active INTEGER DEFAULT 0, token TEXT);
    CREATE TABLE IF NOT EXISTS scenes(id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT, sort INTEGER DEFAULT 0,
      actions TEXT, last_run REAL);
    CREATE TABLE IF NOT EXISTS fridges(dev TEXT PRIMARY KEY, type TEXT, label TEXT, on_w REAL, max_run_min INTEGER,
      max_off_min INTEGER, dev_pct INTEGER, state TEXT, since REAL, alarm_run INTEGER DEFAULT 0,
      alarm_off INTEGER DEFAULT 0, last_dev_check TEXT);
    CREATE TABLE IF NOT EXISTS fridge_cycles(id INTEGER PRIMARY KEY AUTOINCREMENT, dev TEXT, start REAL, end REAL,
      peak_w REAL);
    CREATE INDEX IF NOT EXISTS fridge_cycles_dev ON fridge_cycles(dev, start);
    CREATE TABLE IF NOT EXISTS backups(id INTEGER PRIMARY KEY AUTOINCREMENT, dev TEXT, ts REAL, model TEXT,
      ver TEXT, data TEXT);
    CREATE INDEX IF NOT EXISTS backups_dev ON backups(dev, ts);
    """)
    _add_col("devices", "room", "TEXT DEFAULT ''")
    _add_col("power", "wmax", "REAL")
    _add_col("devices", "off_since", "REAL")
    _add_col("devices", "off_alerted", "INTEGER DEFAULT 0")
    DB.commit()


def setting(k, default=None):
    r = q("SELECT v FROM settings WHERE k=?", (k,), one=True)
    if r is None:
        return default
    try:
        return json.loads(r["v"])
    except Exception:
        return r["v"]


def set_setting(k, v):
    q("INSERT OR REPLACE INTO settings(k, v) VALUES(?,?)", (k, json.dumps(v)))


def now_local():
    return datetime.now(TZ)


def today():
    return now_local().strftime("%Y-%m-%d")


def price_eur_kwh():
    try:
        return float(setting("price_ct", 30.0)) / 100.0
    except Exception:
        return 0.30


def dev_name(dev):
    r = q("SELECT name FROM devices WHERE id=?", (dev,), one=True)
    return (r["name"] if r and r["name"] else dev)


def fmt_kwh(wh):
    return f"{wh / 1000:.2f} kWh".replace(".", ",") if wh >= 1000 else f"{wh:.0f} Wh"


def fmt_eur(e):
    return f"{e:.2f} €".replace(".", ",")


def fmt_dur(s):
    s = int(s)
    h, m = s // 3600, (s % 3600) // 60
    return f"{h}:{m:02d} h" if h else f"{m} min"


# ======================= Meldungen + Push =======================
PUSH_KINDS = {
    "overpower": "Überlast", "overtemp": "Übertemperatur", "offline": "Gerät offline",
    "online": "Gerät wieder online", "reset": "Reset am Gerät", "finished": "Gerät fertig",
    "reminder": "Erinnerung", "fridge": "Kühlgerät-Warnung", "rule": "Regel", "update": "Updates",
}
DEFAULT_PUSH = ["overpower", "overtemp", "offline", "finished", "reminder", "fridge", "rule"]
_pushq = queue.Queue()


def alert(dev, kind, text, push=True, title=None, prio=None):
    q("INSERT INTO alerts(ts, dev, kind, text) VALUES(?,?,?,?)", (time.time(), dev, kind, text))
    if push and kind in (setting("push_kinds", DEFAULT_PUSH) or []):
        notify(title or PUSH_KINDS.get(kind, "Pagurus Cloud"), text, prio or
               (5 if kind in ("overpower", "overtemp", "fridge") else 4 if kind in ("finished", "offline") else 3))


def notify(title, text, prio=3):
    _pushq.put((title, text, prio))


def _send(cfg, title, text, prio):
    errs = []
    if cfg.get("ntfy_url"):
        try:
            # JSON-Veroeffentlichung: Umlaute in Titel/Text ohne Header-Probleme
            base, _, topic = cfg["ntfy_url"].rstrip("/").rpartition("/")
            body = json.dumps({"topic": topic, "title": title, "message": text, "priority": prio,
                               "tags": ["house"]}).encode()
            h = {"Content-Type": "application/json"}
            if cfg.get("ntfy_token"):
                h["Authorization"] = "Bearer " + cfg["ntfy_token"]
            req = urllib.request.Request(base, data=body, headers=h, method="POST")
            urllib.request.urlopen(req, timeout=10).read()
        except Exception as e:
            errs.append(f"ntfy: {e}")
    if cfg.get("tg_token") and cfg.get("tg_chat"):
        try:
            data = urllib.parse.urlencode({"chat_id": cfg["tg_chat"], "text": f"{title}\n{text}"}).encode()
            urllib.request.urlopen(f"https://api.telegram.org/bot{cfg['tg_token']}/sendMessage", data=data,
                                   timeout=10).read()
        except Exception as e:
            errs.append(f"Telegram: {e}")
    return errs


def push_cfg():
    return setting("push", {}) or {}


def push_test():
    cfg = push_cfg()
    if not cfg.get("ntfy_url") and not (cfg.get("tg_token") and cfg.get("tg_chat")):
        return ["Kein Push-Dienst eingetragen"]
    return _send(cfg, "Pagurus Cloud", "Testnachricht – Push funktioniert.", 3)


def _push_worker():
    while True:
        title, text, prio = _pushq.get()
        try:
            errs = _send(push_cfg(), title, text, prio)
            if errs:
                print("Push:", "; ".join(errs), flush=True)
        except Exception as e:
            print("Push:", e, flush=True)


def start_push():
    threading.Thread(target=_push_worker, daemon=True).start()
