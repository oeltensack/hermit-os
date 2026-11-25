# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 D.Koch – Pagurus OS
"""Shelly BLU (Bluetooth, BTHome) ueber Gen2+/Gen3-Shellys als Gateway – lokal, ohne Shelly Cloud.

Die BLU-Geraete sind auf dem Shelly als Komponenten angelegt (bthomedevice:2xx / bthomesensor:2xx).
Der Shelly schickt deren Werte per MQTT (status/bthomesensor:2xx, NotifyStatus, NotifyEvent).
Welche Komponente welcher Wert ist (Adresse, BTHome-Objekt), holt die Cloud einmal per
Shelly.GetComponents vom Gateway. Jedes BLU-Geraet wird eine eigene Zeile "blu-<adresse>" –
hoeren es mehrere Shellys, bleibt es ein Geraet.
"""
import json
import threading
import time

import shade
from core import q

GEN1_SENSOR = "sensor:0"
_refresh_at = {}                 # Gateway -> Zeit der letzten Abfrage
_last_ev = {}
_lock = threading.Lock()
BUS = None

# BTHome-Objekt-ID -> (Feld, Art)
OBJ = {
    0x01: ("battery", "num"),
    0x02: ("temperature", "temp"), 0x45: ("temperature", "temp"), 0x57: ("temperature", "temp"),
    0x03: ("humidity", "hum"), 0x2E: ("humidity", "hum"),
    0x05: ("lux", "lux"),
    0x0C: ("voltage", "num"),
    0x2D: ("contact", "open"), 0x1A: ("contact", "open"), 0x1D: ("contact", "open"),
    0x21: ("motion", "motion"), 0x23: ("motion", "motion"), 0x22: ("motion", "motion"),
    0x3F: ("rotation", "num"),
    0x20: ("moisture", "bool"), 0x29: ("moisture", "bool"),
    0x2C: ("vibration", "bool"),
}
BUTTON = 0x3A


def init_db():
    q("""CREATE TABLE IF NOT EXISTS blu_map(gw TEXT, key TEXT, addr TEXT, obj INTEGER, idx INTEGER, name TEXT,
         PRIMARY KEY(gw, key))""")


def dev_id(addr):
    return "blu-" + str(addr).replace(":", "").lower()


def _row(addr, name=None):
    from bus import ensure
    d = dev_id(addr)
    ensure(d)
    q("""UPDATE devices SET comp=?, model=COALESCE(model, 'BLU'), mac=COALESCE(mac, ?),
         name=COALESCE(NULLIF(name,''), ?) WHERE id=?""", (GEN1_SENSOR, str(addr).upper(), name or None, d))
    return d


def _map(gw, key):
    return q("SELECT * FROM blu_map WHERE gw=? AND key=?", (gw, key), one=True)


def refresh(gw, force=False):
    """Komponenten des Gateways einlesen (im Hintergrund, hoechstens alle 5 min)."""
    now = time.time()
    with _lock:
        if not force and now - _refresh_at.get(gw, 0) < 300:
            return
        _refresh_at[gw] = now
    threading.Thread(target=_refresh, args=(gw,), daemon=True).start()


def _refresh(gw):
    try:
        comps, off = [], 0
        while True:
            try:
                r = BUS.rpc(gw, "Shelly.GetComponents", {"dynamic_only": True, "include": ["config", "status"],
                                                         "offset": off})
            except Exception:
                r = BUS.rpc(gw, "Shelly.GetComponents", {"offset": off})
            part = r.get("components") or []
            comps += part
            off += len(part)
            if not part or off >= int(r.get("total") or 0):
                break
        names = {}
        for c in comps:
            key, cfg = c.get("key", ""), c.get("config") or {}
            if key.startswith("bthomedevice:") and cfg.get("addr"):
                names[cfg["addr"].lower()] = cfg.get("name")
                q("INSERT OR REPLACE INTO blu_map(gw, key, addr, obj, idx, name) VALUES(?,?,?,?,?,?)",
                  (gw, key, cfg["addr"].lower(), -1, 0, cfg.get("name")))
        for c in comps:
            key, cfg = c.get("key", ""), c.get("config") or {}
            if key.startswith("bthomesensor:") and cfg.get("addr"):
                q("INSERT OR REPLACE INTO blu_map(gw, key, addr, obj, idx, name) VALUES(?,?,?,?,?,?)",
                  (gw, key, cfg["addr"].lower(), int(cfg.get("obj_id") or 0), int(cfg.get("idx") or 0),
                   cfg.get("name")))
        for c in comps:                          # aktuelle Werte gleich uebernehmen
            if c.get("key", "").startswith("bthome") and c.get("status"):
                status(gw, c["key"], c["status"], live=False, names=names)
    except Exception as e:
        print(f"BLU: Komponenten von {gw} nicht lesbar: {e}", flush=True)


def status(gw, key, st, live=True, names=None):
    """Status einer bthomedevice-/bthomesensor-Komponente vom Gateway."""
    from bus import seen, set_status
    import engine
    m = _map(gw, key)
    if not m:
        refresh(gw)
        return
    if names is None:
        n = q("SELECT name FROM blu_map WHERE gw=? AND addr=? AND obj=-1", (gw, m["addr"]), one=True)
        dname = n["name"] if n else None
    else:
        dname = names.get(m["addr"])
    dev = _row(m["addr"], dname)
    feed = live or names is not None             # frisch vom Gateway gelesen zaehlt fuer die Fuehler
    new = {"via": gw}
    if m["obj"] == -1:                           # bthomedevice: Batterie, Empfang
        if st.get("battery") is not None:
            new["battery"] = st["battery"]
        if st.get("rssi") is not None:
            new["rssi"] = st["rssi"]
    else:
        v = st.get("value")
        spec = OBJ.get(m["obj"])
        if v is None or not spec or m["obj"] == BUTTON:
            return
        field, kind = spec
        if m["idx"]:
            field = f"{field}_{m['idx'] + 1}"
        if kind == "temp":
            new[field] = {"tC": v}
            if feed and not m["idx"]:
                shade.sensor(dev, "temperature:0", {"tC": v})
        elif kind == "hum":
            new[field] = {"rh": v}
            if feed and not m["idx"]:
                shade.sensor(dev, "humidity:0", {"rh": v})
        elif kind == "lux":
            new[field] = v
            if feed and not m["idx"]:
                shade.sensor(dev, "illuminance:0", {"lux": v})
        elif kind in ("open", "motion", "bool"):
            b = bool(v)
            old = q("SELECT status FROM devices WHERE id=?", (dev,), one=True)
            prev = json.loads(old["status"] or "{}").get(field) if old else None
            new[field] = b
            if live and prev is not None and prev != b:
                ev = {"open": ("open", "closed"), "motion": ("motion", "no_motion")}.get(kind, (field + "_on", field + "_off"))
                engine.on_event(dev, f"{field}:0", ev[0] if b else ev[1])
        else:
            new[field] = v
    set_status(dev, GEN1_SENSOR, new, merge=True, live=live)
    if live:
        seen(dev)


def event(gw, comp, ev):
    """Tastendruck eines BLU-Tasters (NotifyEvent der bthomesensor-Komponente). True = erledigt."""
    from bus import seen, set_status
    import engine
    m = _map(gw, comp)
    if not m:
        refresh(gw)
        return True
    k = (m["addr"], m["idx"], ev)                # hoeren mehrere Shellys den Taster: nur einmal ausloesen
    now = time.time()
    if now - _last_ev.get(k, 0) < 2:
        return True
    _last_ev[k] = now
    n = q("SELECT name FROM blu_map WHERE gw=? AND addr=? AND obj=-1", (gw, m["addr"]), one=True)
    dev = _row(m["addr"], n["name"] if n else None)
    seen(dev)
    set_status(dev, GEN1_SENSOR, {"button": f"{m['idx'] + 1}: {ev}", "button_ts": time.time(), "via": gw},
               merge=True)
    engine.on_event(dev, f"button:{m['idx'] + 1}", ev)
    return True
