# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 D.Koch – Hermit OS
"""MQTT-Anbindung (Gen2-Format) und Geraetezustand."""
import itertools
import json
import os
import threading
import time

import paho.mqtt.client as mqtt

import engine
from core import OFFLINE_ALERT_AFTER, alert, dev_name, now_local, q, today

MQTT_HOST = os.environ.get("MQTT_HOST", "mosquitto")
MQTT_PORT = int(os.environ.get("MQTT_PORT", "1883"))
MQTT_USER = os.environ.get("MQTT_USER", "")
MQTT_PASS = os.environ.get("MQTT_PASS", "")
SRC = "hermit-cloud"
MAIN_COMPS = ("switch:", "cct:", "light:")


class Bus:
    def __init__(self):
        self.c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"{SRC}-{os.getpid()}")
        if MQTT_USER:
            self.c.username_pw_set(MQTT_USER, MQTT_PASS)
        self.c.on_connect = self.on_connect
        self.c.on_message = self.on_message
        self.connected = False
        self.ids = itertools.count(1)
        self.wait = {}

    def start(self):
        self.c.connect_async(MQTT_HOST, MQTT_PORT, keepalive=60)
        self.c.loop_start()

    def on_connect(self, c, userdata, flags, rc, props=None):
        self.connected = not rc.is_failure
        if self.connected:
            c.subscribe([("+/online", 0), ("+/info", 0), ("+/status/+", 0), ("+/events/rpc", 0),
                         (f"{SRC}/rpc", 0)])

    def on_message(self, c, userdata, msg):
        try:
            self.handle(msg.topic, msg.payload, msg.retain)
        except Exception as e:
            print("Fehler bei", msg.topic, e, flush=True)

    def handle(self, topic, payload, retained=False):
        # Gespeicherte (retained) Nachrichten liefert der Broker beim Verbinden der Cloud erneut aus –
        # sie aktualisieren die Daten, zaehlen aber nicht als Lebenszeichen. Massgeblich ist dann /online.
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
                threading.Thread(target=self.fetch_info, args=(dev,), daemon=True).start()
            return
        if not payload:
            return
        d = json.loads(payload)
        live = not retained
        if rest == "info":
            set_info(dev, d, live=live)
        elif rest.startswith("status/"):
            comp = rest[7:]
            if comp.startswith(MAIN_COMPS):
                set_status(dev, comp, d, live=live)
            elif live:
                seen(dev)
        elif rest == "events/rpc" and live:
            m = d.get("method")
            p = d.get("params") or {}
            if m == "NotifyEvent":
                for e in p.get("events", []):
                    on_event(dev, e)
            elif m in ("NotifyStatus", "NotifyFullStatus"):
                for k, v in p.items():
                    if isinstance(v, dict) and k.startswith(MAIN_COMPS):
                        set_status(dev, k, v, merge=True)
                seen(dev)

    def fetch_info(self, dev):
        try:
            set_info(dev, self.rpc(dev, "Shelly.GetDeviceInfo"))
        except Exception:
            pass

    def rpc(self, dev, method, params=None, timeout=6):
        if not self.connected:
            raise RuntimeError("Keine Verbindung zum MQTT-Broker")
        rid = next(self.ids)
        ev, box = threading.Event(), []
        self.wait[rid] = (ev, box)
        try:
            req = {"id": rid, "src": SRC, "method": method}
            if params is not None:
                req["params"] = params
            self.c.publish(f"{dev}/rpc", json.dumps(req))
            if not ev.wait(timeout):
                raise TimeoutError(f"{dev_name(dev)} antwortet nicht")
            r = box[0]
            if "error" in r:
                raise RuntimeError(r["error"].get("message") or str(r["error"]))
            return r.get("result")
        finally:
            self.wait.pop(rid, None)

    # ---- Schalten (fuer Oberflaeche, Zeitplaene, Regeln) ----
    def switch(self, dev, on=None):
        r = q("SELECT comp, status FROM devices WHERE id=?", (dev,), one=True)
        if not r:
            raise RuntimeError(f"Unbekanntes Gerät {dev}")
        if json.loads(r["status"] or "{}").get("meter_only"):
            raise RuntimeError(f"{dev_name(dev)}: Messfirmware – nicht schaltbar")
        comp = r["comp"] or "switch:0"
        kind, cid = comp.split(":")
        m = {"switch": "Switch", "cct": "CCT", "light": "Light"}.get(kind, "Switch")
        if on is None:
            res = self.rpc(dev, f"{m}.Toggle", {"id": int(cid)})
        else:
            res = self.rpc(dev, f"{m}.Set", {"id": int(cid), "on": bool(on)})
        try:
            st = self.rpc(dev, f"{m}.GetStatus", {"id": int(cid)})
            set_status(dev, comp, st, merge=True)
        except Exception:
            pass
        return res


BUS = Bus()


# ======================= Geraetezustand =======================
def ensure(dev):
    q("INSERT OR IGNORE INTO devices(id, first_seen) VALUES(?,?)", (dev, time.time()))


def seen(dev, online=None):
    """Lebenszeichen bzw. Online-Meldung. Offline wird sofort angezeigt, aber erst nach
    OFFLINE_ALERT_AFTER gemeldet (siehe check_offline) – kurze WLAN-Aussetzer erzeugen keine Meldung."""
    ensure(dev)
    r = q("SELECT online, off_alerted FROM devices WHERE id=?", (dev,), one=True)
    if online is False:
        if r["online"]:
            q("UPDATE devices SET online=0, off_since=?, off_alerted=0 WHERE id=?", (time.time(), dev))
        return
    if not r["online"]:
        q("UPDATE devices SET online=1, last_seen=?, off_since=NULL, off_alerted=0 WHERE id=?", (time.time(), dev))
        if r["off_alerted"]:                       # nur wenn vorher wirklich „offline“ gemeldet wurde
            alert(dev, "online", f"{dev_name(dev)} ist wieder online")
            engine.on_online(dev, True)
        return
    q("UPDATE devices SET last_seen=? WHERE id=?", (time.time(), dev))


def check_offline():
    """Vom Watchdog aufgerufen: Geraete, die laenger als OFFLINE_ALERT_AFTER weg sind, melden."""
    lim = time.time() - OFFLINE_ALERT_AFTER
    for r in q("SELECT id, hidden FROM devices WHERE online=0 AND off_alerted=0 AND off_since IS NOT NULL AND off_since<?",
               (lim,)):
        q("UPDATE devices SET off_alerted=1 WHERE id=?", (r["id"],))
        if not r["hidden"]:
            alert(r["id"], "offline", f"{dev_name(r['id'])} ist offline")
        engine.on_online(r["id"], False)


def set_info(dev, d, live=True):
    ensure(dev)
    old = q("SELECT info FROM devices WHERE id=?", (dev,), one=True)
    merged = {**json.loads(old["info"] or "{}"), **{k: v for k, v in d.items() if v is not None}}
    q("""UPDATE devices SET info=?, model=COALESCE(?, model), app=COALESCE(?, app), ver=COALESCE(?, ver),
         mac=COALESCE(?, mac), gen=COALESCE(?, gen), ip=COALESCE(?, ip),
         name=COALESCE(NULLIF(name,''), ?, name) WHERE id=?""",
      (json.dumps(merged), d.get("model"), d.get("app"), d.get("ver"), d.get("mac"), d.get("gen"),
       d.get("ip"), d.get("name"), dev))
    if live:
        seen(dev)


def set_status(dev, comp, st, merge=False, live=True):
    ensure(dev)
    r = q("SELECT status, comp, last_total FROM devices WHERE id=?", (dev,), one=True)
    old = json.loads(r["status"] or "{}") if r["comp"] in (None, comp) else {}
    new = {**old, **st} if merge else st
    q("UPDATE devices SET status=?, comp=? WHERE id=?", (json.dumps(new), comp, dev))
    if not live:
        return                                    # alter Stand vom Broker: nichts verbuchen
    seen(dev)
    now = int(time.time())
    if "apower" in st:
        w = float(st["apower"])
        q("""INSERT INTO power(dev, ts, w, wmax) VALUES(?,?,?,?)
             ON CONFLICT(dev, ts) DO UPDATE SET w=excluded.w, wmax=MAX(COALESCE(wmax, 0), excluded.wmax)""",
          (dev, now // 60 * 60, w, w))
    tot = (st.get("aenergy") or {}).get("total")
    if tot is not None:
        tot = float(tot)
        last = r["last_total"]
        if last is not None and tot > last:
            q("""INSERT INTO energy(dev, day, wh) VALUES(?,?,?)
                 ON CONFLICT(dev, day) DO UPDATE SET wh = wh + excluded.wh""", (dev, today(), tot - last))
            q("""INSERT INTO energy_h(dev, hour, wh) VALUES(?,?,?)
                 ON CONFLICT(dev, hour) DO UPDATE SET wh = wh + excluded.wh""",
              (dev, now_local().strftime("%Y-%m-%d %H"), tot - last))
        q("UPDATE devices SET last_total=? WHERE id=?", (tot, dev))   # kleiner = Zaehler zurueckgesetzt
    engine.on_status(dev, comp, new, old)


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
    engine.on_event(dev, e.get("component", ""), ev)
