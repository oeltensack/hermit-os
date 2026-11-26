# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 D.Koch – Pagurus OS
"""Fenster/Tuer offen: Klimaanlage pausieren, solange geheizt (oder gekuehlt) wird und ein Kontakt offen steht.

Zwei Wege, die sich kombinieren lassen:
- Kontakte (BLU Door/Window usw.): offen laenger als "delay" Sekunden -> Anlage aus; alle wieder zu laenger als
  "close_delay" Sekunden -> Anlage mit dem vorigen Modus wieder an.
- Temperatursturz ohne Kontakt: faellt die Raumtemperatur beim Heizen innerhalb von "drop_min" Minuten um
  "drop_k" Kelvin (beim Kuehlen: steigt sie), wird fuer "drop_pause" Minuten pausiert.
Schaltet jemand die Anlage waehrend der Pause selbst ein, gilt das als Absicht und die Pause endet.
"""
import json
import threading
import time
from collections import deque

from core import alert, dev_name, q

BUS = None
DEFAULT = {"sensors": [], "delay": 30, "close_delay": 15, "modes": ["heat"],
           "drop": False, "drop_k": 1.0, "drop_min": 3, "drop_pause": 15}
MODE_TXT = {"heat": "Heizen", "cool": "Kühlen", "dry": "Entfeuchten", "fan": "Lüften", "auto": "Automatik"}
_open_since = {}            # dev -> Zeitpunkt, seit dem ein Kontakt offen ist
_closed_since = {}          # dev -> Zeitpunkt, seit dem alle Kontakte zu sind (waehrend der Pause)
_hist = {}                  # dev -> deque[(ts, temp)]
_quiet_until = {}           # dev -> nach dem Wiedereinschalten keine Sturz-Erkennung
_lock = threading.Lock()


def init_db():
    q("""CREATE TABLE IF NOT EXISTS winopen(dev TEXT PRIMARY KEY, cfg TEXT, paused REAL, until REAL,
         saved TEXT, reason TEXT)""")


def get(dev):
    r = q("SELECT * FROM winopen WHERE dev=?", (dev,), one=True)
    if not r:
        return None
    return {"cfg": {**DEFAULT, **json.loads(r["cfg"] or "{}")}, "paused": r["paused"], "until": r["until"],
            "reason": r["reason"] or ""}


def save(dev, c):
    cfg = dict(DEFAULT)
    cfg["sensors"] = [str(s) for s in (c.get("sensors") or []) if str(s) and str(s) != dev][:20]
    cfg["delay"] = max(5, min(3600, int(c.get("delay", DEFAULT["delay"]))))
    cfg["close_delay"] = max(0, min(3600, int(c.get("close_delay", DEFAULT["close_delay"]))))
    cfg["modes"] = [m for m in (c.get("modes") or ["heat"]) if m in MODE_TXT] or ["heat"]
    cfg["drop"] = bool(c.get("drop"))
    cfg["drop_k"] = max(0.3, min(5.0, float(c.get("drop_k", DEFAULT["drop_k"]))))
    cfg["drop_min"] = max(1, min(30, int(c.get("drop_min", DEFAULT["drop_min"]))))
    cfg["drop_pause"] = max(1, min(240, int(c.get("drop_pause", DEFAULT["drop_pause"]))))
    if not cfg["sensors"] and not cfg["drop"]:
        raise ValueError("Kontakt auswählen oder Temperatursturz einschalten")
    with _lock:
        if q("SELECT dev FROM winopen WHERE dev=?", (dev,), one=True):
            q("UPDATE winopen SET cfg=? WHERE dev=?", (json.dumps(cfg), dev))
        else:
            q("INSERT INTO winopen(dev, cfg) VALUES(?,?)", (dev, json.dumps(cfg)))
    return cfg


def delete(dev):
    with _lock:
        r = get(dev)
        if r and r["paused"]:
            try:
                _resume(dev, r, "Fenster-offen-Erkennung entfernt")
            except Exception:
                pass
        q("DELETE FROM winopen WHERE dev=?", (dev,))


def _status(dev):
    r = q("SELECT status, online, name FROM devices WHERE id=?", (dev,), one=True)
    return (json.loads(r["status"] or "{}"), bool(r["online"])) if r else ({}, False)


def _room_temp(st):
    for k in ("ext_temperature_C", "current_C"):
        if isinstance(st.get(k), (int, float)):
            return float(st[k])
    return None


def _pause(dev, st, reason, until=None):
    saved = {k: st.get(k) for k in ("mode", "target_C", "fan") if st.get(k) is not None}
    BUS.rpc(dev, "Climate.Set", {"id": 0, "on": False})
    q("UPDATE winopen SET paused=?, until=?, saved=?, reason=? WHERE dev=?",
      (time.time(), until, json.dumps(saved), reason, dev))
    alert(dev, "window", f"{dev_name(dev)}: {MODE_TXT.get(saved.get('mode'), 'Klima')} pausiert – {reason}")


def _resume(dev, r, why):
    row = q("SELECT saved FROM winopen WHERE dev=?", (dev,), one=True)
    saved = json.loads(row["saved"] or "{}") if row else {}
    p = {"id": 0, "on": True}
    for k in ("mode", "target_C", "fan"):           # vorheriger Zustand bzw. Wunsch eines Zeitplans aus der Pause
        if saved.get(k) is not None:
            p[k] = saved[k]
    BUS.rpc(dev, "Climate.Set", p)                  # schlaegt es fehl, bleibt die Pause und es wird erneut versucht
    q("UPDATE winopen SET paused=NULL, until=NULL, saved=NULL, reason=NULL WHERE dev=?", (dev,))
    _hist.pop(dev, None)
    _quiet_until[dev] = time.time() + 600
    alert(dev, "window", f"{dev_name(dev)}: wieder an ({why})", push=False)


def _drop(dev, st, cfg, now):
    """Temperatursturz (Heizen) bzw. -anstieg (Kuehlen) im Zeitfenster erkennen."""
    t = _room_temp(st)
    h = _hist.setdefault(dev, deque())
    if t is not None and (not h or now - h[-1][0] >= 20 or h[-1][1] != t):
        h.append((now, t))
    while h and now - h[0][0] > cfg["drop_min"] * 60:
        h.popleft()
    if t is None or len(h) < 3 or now < _quiet_until.get(dev, 0) or now - h[0][0] < cfg["drop_min"] * 60 * 0.6:
        return False
    if st.get("mode") == "cool":
        return t - min(x[1] for x in h) >= cfg["drop_k"]
    return max(x[1] for x in h) - t >= cfg["drop_k"]


def step():
    now = time.time()
    for row in q("SELECT dev FROM winopen"):
        dev = row["dev"]
        with _lock:
            r = get(dev)
            if not r:
                continue
            cfg = r["cfg"]
            st, online = _status(dev)
            if not online or st.get("link") is False:
                continue
            opened = [s for s in cfg["sensors"] if _status(s)[0].get("contact") is True]
            if opened:
                _open_since.setdefault(dev, now)
                _closed_since.pop(dev, None)
            else:
                _open_since.pop(dev, None)
                _closed_since.setdefault(dev, now)
            if r["paused"]:
                if st.get("output") and now - r["paused"] > 30:            # selbst wieder eingeschaltet
                    q("UPDATE winopen SET paused=NULL, until=NULL, saved=NULL, reason=NULL WHERE dev=?", (dev,))
                    _quiet_until[dev] = now + 600
                elif r["until"]:                                          # Pause wegen Temperatursturz
                    if now >= r["until"] and not opened:
                        _resume(dev, r, "Pause abgelaufen")
                elif not opened and now - _closed_since.get(dev, now) >= cfg["close_delay"]:
                    _resume(dev, r, "geschlossen")
                continue
            if not st.get("output") or st.get("mode") not in cfg["modes"]:
                _hist.pop(dev, None)
                continue
            if opened and now - _open_since[dev] >= cfg["delay"]:
                _pause(dev, st, "offen: " + ", ".join(dev_name(s) for s in opened))
            elif cfg["drop"] and _drop(dev, st, cfg, now):
                _pause(dev, st, f"Temperatursturz, Pause {cfg['drop_pause']} min", now + cfg["drop_pause"] * 60)


def view(dev):
    r = get(dev)
    if not r:
        return None
    return {"paused": bool(r["paused"]), "reason": r["reason"], "until": r["until"]}


def loop():
    while True:
        try:
            step()
        except Exception as e:
            print("Fenster offen:", e, flush=True)
        time.sleep(5)


def start(bus):
    global BUS
    BUS = bus
    init_db()
    threading.Thread(target=loop, daemon=True).start()
