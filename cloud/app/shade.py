# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 D.Koch – Pagurus OS
"""Beschattung: Rollläden nach Sonnenstand, Temperatur und Wetter.

Eine Beschattungsregel gilt fuer eine Fassade (Himmelsrichtung). Sie wird aktiv, wenn
  - die Sonne auf der Fassade steht (Azimut im Bereich, Hoehe zwischen min und max),
  - die gewaehlte Temperatur die Einschaltschwelle erreicht (Hysterese bis zur Ausschaltschwelle),
  - optional: Innentemperatur, Hoechstwert-Vorhersage, Sonneneinstrahlung,
  - im Zeitfenster und an den gewaehlten Tagen,
und das jeweils fuer die Verzoegerungszeit. Dann fahren die Rollläden auf die Beschattungsposition
(nur nach unten – ein Rollladen, der schon weiter zu ist, bleibt). Endet die Beschattung, fahren sie
wieder auf (oder auf die vorige Position), aber nur, wenn sie noch dort stehen, wo die Regel sie hingefahren hat.
Wer einen Rollladen waehrend der Beschattung von Hand bewegt, hat Vorrang bis zum Ende dieser Beschattung.
"""
import json
import math
import threading
import time
import urllib.request
from datetime import datetime, timezone

from core import TZ, alert, dev_name, now_local, q, qid, setting

BUS = None
WX = {"ok": False, "ts": 0, "err": ""}          # Wetter (Open-Meteo)
AUTO = {}                                        # dev -> Zeitpunkt des letzten Automatik-Befehls
MANUAL = {}                                      # dev -> Zeitpunkt der letzten Handbedienung
_lock = threading.Lock()


def init_db():
    q("""CREATE TABLE IF NOT EXISTS sensors(dev TEXT, comp TEXT, value REAL, unit TEXT, ts REAL,
         PRIMARY KEY(dev, comp))""")
    q("""CREATE TABLE IF NOT EXISTS shades(id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT, enabled INTEGER DEFAULT 1,
         cfg TEXT, state TEXT DEFAULT '{}')""")


# ======================= Sonnenstand =======================
def sun_pos(lat, lon, when=None):
    """Azimut (0 = Nord, 90 = Ost, 180 = Sued) und Hoehe in Grad, NOAA-Naeherung."""
    t = (when or datetime.now(timezone.utc)).astimezone(timezone.utc)
    n = t.timetuple().tm_yday
    hour = t.hour + t.minute / 60 + t.second / 3600
    g = 2 * math.pi / 365 * (n - 1 + (hour - 12) / 24)
    eqt = 229.18 * (0.000075 + 0.001868 * math.cos(g) - 0.032077 * math.sin(g)
                    - 0.014615 * math.cos(2 * g) - 0.040849 * math.sin(2 * g))
    decl = (0.006918 - 0.399912 * math.cos(g) + 0.070257 * math.sin(g) - 0.006758 * math.cos(2 * g)
            + 0.000907 * math.sin(2 * g) - 0.002697 * math.cos(3 * g) + 0.00148 * math.sin(3 * g))
    tst = hour * 60 + eqt + 4 * lon                          # wahre Sonnenzeit in Minuten
    ha = math.radians(tst / 4 - 180)
    lr = math.radians(lat)
    cz = math.sin(lr) * math.sin(decl) + math.cos(lr) * math.cos(decl) * math.cos(ha)
    cz = max(-1.0, min(1.0, cz))
    zen = math.acos(cz)
    elev = 90 - math.degrees(zen)
    az = math.degrees(math.atan2(math.sin(ha), math.cos(ha) * math.sin(lr) - math.tan(decl) * math.cos(lr))) + 180
    return round(az % 360, 1), round(elev, 1)


def location():
    loc = setting("location", {}) or {}
    if loc.get("lat") in (None, "") or loc.get("lon") in (None, ""):
        return None
    return float(loc["lat"]), float(loc["lon"])


def sun_now():
    loc = location()
    if not loc:
        return None
    az, el = sun_pos(*loc)
    return {"azimuth": az, "elevation": el}


# ======================= Wetter (Open-Meteo, ohne Schluessel) =======================
def fetch_weather():
    loc = location()
    if not loc or not setting("weather_on", True):
        WX.update(ok=False, err="Standort fehlt" if not loc else "aus")
        return
    url = ("https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,cloud_cover,shortwave_radiation,wind_gusts_10m,is_day"
           "&daily=temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=1" % loc)
    try:
        with urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": "PagurusCloud"}), timeout=15) as r:
            d = json.loads(r.read())
        c, dl = d.get("current") or {}, d.get("daily") or {}
        WX.update(ok=True, ts=time.time(), err="", temp=c.get("temperature_2m"), clouds=c.get("cloud_cover"),
                  radiation=c.get("shortwave_radiation"), gusts=c.get("wind_gusts_10m"),
                  tmax=(dl.get("temperature_2m_max") or [None])[0], tmin=(dl.get("temperature_2m_min") or [None])[0])
    except Exception as e:
        WX.update(ok=WX.get("ok") and time.time() - WX.get("ts", 0) < 3600, err=str(e)[:120])


# ======================= Fuehler (aus MQTT) =======================
UNITS = {"temperature": "°C", "humidity": "%", "illuminance": "lx"}


def sensor(dev, comp, st):
    kind = comp.split(":")[0]
    v = None
    if kind == "temperature":
        v = st.get("tC")
    elif kind == "humidity":
        v = st.get("rh")
    elif kind == "illuminance":
        v = st.get("lux")
    if v is None:
        return
    q("INSERT OR REPLACE INTO sensors(dev, comp, value, unit, ts) VALUES(?,?,?,?,?)",
      (dev, comp, float(v), UNITS.get(kind, ""), time.time()))


def sensors():
    out = []
    for r in q("SELECT * FROM sensors ORDER BY dev, comp"):
        out.append({"src": f"dev:{r['dev']}|{r['comp']}", "label": f"{dev_name(r['dev'])} · {r['comp']}",
                    "value": r["value"], "unit": r["unit"], "age": round(time.time() - r["ts"])})
    return out


def source_value(src, max_age=6 * 3600):
    """'wx:temp', 'wx:tmax', 'wx:radiation' … oder 'dev:<id>|<comp>' -> (Wert, Text)."""
    if not src:
        return None, ""
    if src.startswith("wx:"):
        k = src[3:]
        if not WX.get("ok"):
            return None, "Wetter nicht verfügbar"
        return WX.get(k), ""
    if src.startswith("dev:"):
        dev, _, comp = src[4:].partition("|")
        r = q("SELECT value, ts FROM sensors WHERE dev=? AND comp=?", (dev, comp), one=True)
        if not r:
            return None, "Fühler meldet nichts"
        if time.time() - r["ts"] > max_age:                 # H&T meldet selten, 6 h gelten noch
            return None, "Fühler-Wert zu alt"
        return r["value"], ""
    return None, "Quelle unbekannt"


def source_label(src):
    if not src:
        return "–"
    lab = {"wx:temp": "Außentemperatur (Wetter)", "wx:tmax": "Höchstwert heute (Wetter)",
           "wx:radiation": "Sonneneinstrahlung (Wetter)", "wx:clouds": "Bewölkung (Wetter)"}
    if src in lab:
        return lab[src]
    dev, _, comp = src[4:].partition("|")
    return f"{dev_name(dev)} · {comp}"


# ======================= Rollläden =======================
def covers():
    out = []
    for r in q("SELECT id, name, comp, status, online FROM devices WHERE comp LIKE 'cover:%' AND hidden=0"):
        st = json.loads(r["status"] or "{}")
        out.append({"id": r["id"], "name": dev_name(r["id"]), "online": bool(r["online"]), "state": st.get("state"),
                    "pos": st.get("current_pos"), "pos_control": bool(st.get("pos_control"))})
    return out


def _cover(dev):
    r = q("SELECT status, online FROM devices WHERE id=?", (dev,), one=True)
    return (json.loads(r["status"] or "{}"), bool(r["online"])) if r else ({}, False)


def move(dev, pos):
    """Automatik-Fahrt: Position 0..100 (ohne Positionssteuerung: 0..99 = zu, 100 = auf)."""
    st, online = _cover(dev)
    if not online:
        raise RuntimeError("offline")
    AUTO[dev] = time.time()
    if st.get("pos_control"):
        return BUS.switch(dev, pos=pos)
    return BUS.switch(dev, cover="open" if pos >= 100 else "close")


def on_status(dev, comp, new, old):
    """Handbedienung erkennen: Fahrt beginnt, ohne dass die Automatik sie kurz vorher ausgeloest hat."""
    if not comp.startswith("cover:"):
        return
    moving = ("opening", "closing")
    if new.get("state") in moving and old.get("state") not in moving and time.time() - AUTO.get(dev, 0) > 20:
        MANUAL[dev] = time.time()


def frost_block():
    f = setting("frost_c", None)
    if f in (None, ""):
        return False
    t = WX.get("temp") if WX.get("ok") else None
    return t is not None and t < float(f)


# ======================= Regeln =======================
DEFAULT = {"covers": [], "azimuth": 180, "width": 70, "elev_min": 10, "elev_max": 90, "pos": 20,
           "temp_src": "wx:temp", "temp_on": 24, "temp_off": 21, "in_src": "", "in_on": 23,
           "fc_on": None, "rad_min": 0, "lux_src": "", "lux_on": 0, "delay_on": 5, "delay_off": 15, "from": "", "to": "", "days": 127,
           "end": "open"}


def rule_cfg(r):
    return {**DEFAULT, **json.loads(r["cfg"] or "{}")}


def _angle_diff(a, b):
    return abs((a - b + 180) % 360 - 180)


def evaluate(c, active):
    """-> (Bedingung erfuellt, [Gruende])"""
    why, ok = [], True
    sun = sun_now()
    if not sun:
        return False, ["Standort fehlt (Einstellungen)"]
    on_face = _angle_diff(sun["azimuth"], float(c["azimuth"])) <= float(c["width"])
    high = float(c["elev_min"]) <= sun["elevation"] <= float(c["elev_max"])
    if not (on_face and high):
        ok = False
        why.append("Sonne nicht auf der Fassade" if sun["elevation"] > 0 else "Sonne unter dem Horizont")
    t, err = source_value(c.get("temp_src"))
    if c.get("temp_src"):
        thr = float(c["temp_off"] if active else c["temp_on"])
        if t is None:
            ok = False; why.append(err or "keine Temperatur")
        elif (t <= thr) if active else (t < thr):
            ok = False; why.append(f"{t:.1f} °C {'≤' if active else '<'} {thr:g} °C")
    if c.get("in_src"):
        ti, err = source_value(c["in_src"])
        thr = float(c["in_on"]) - (1 if active else 0)
        if ti is None:
            ok = False; why.append(err or "keine Innentemperatur")
        elif ti < thr:
            ok = False; why.append(f"innen {ti:.1f} °C < {thr:g} °C")
    if c.get("fc_on") not in (None, ""):
        tm = WX.get("tmax") if WX.get("ok") else None
        if tm is None:
            ok = False; why.append("keine Vorhersage")
        elif tm < float(c["fc_on"]):
            ok = False; why.append(f"Höchstwert heute {tm:.0f} °C < {float(c['fc_on']):g} °C")
    # Nur bei Sonne: Lichtsensor vor Ort; faellt er aus (oder Wert aelter als 30 min), gilt die Einstrahlung aus dem Wetter
    lux_used = False
    if c.get("lux_src") and float(c.get("lux_on") or 0) > 0:
        lx, err = source_value(c["lux_src"], max_age=1800)
        if lx is not None:
            lux_used = True
            lim = float(c["lux_on"]) * (0.6 if active else 1)    # bei kurzen Wolken nicht gleich hochfahren
            if lx < lim:
                ok = False; why.append(f"Helligkeit {lx:.0f} lx < {lim:.0f}")
        elif float(c.get("rad_min") or 0) <= 0:
            why.append(f"Lichtsensor: {err or 'kein Wert'} – ohne Einstrahlungsgrenze nicht geprüft")
        else:
            why.append(f"Lichtsensor: {err or 'kein Wert'} – Wetter-Einstrahlung gilt")
    if not lux_used and float(c.get("rad_min") or 0) > 0:
        rad = WX.get("radiation") if WX.get("ok") else None
        lim = float(c["rad_min"]) * (0.6 if active else 1)       # bei kurzen Wolken nicht gleich hochfahren
        if rad is None:
            ok = False; why.append("keine Einstrahlung")
        elif rad < lim:
            ok = False; why.append(f"Einstrahlung {rad:.0f} W/m² < {lim:.0f}")
    now = now_local()
    if not (int(c.get("days", 127)) >> now.weekday()) & 1:
        ok = False; why.append("heute nicht")
    f, to = c.get("from"), c.get("to")
    if f and to:
        cur = now.strftime("%H:%M")
        if not (f <= cur < to if f <= to else (cur >= f or cur < to)):
            ok = False; why.append(f"außerhalb {f}–{to}")
    return ok, why


def _activate(r, c, s):
    s["moved"] = {}
    for dev in c["covers"]:
        if MANUAL.get(dev, 0) > time.time() - 600:      # gerade von Hand bewegt: in Ruhe lassen
            continue
        st, online = _cover(dev)
        if not online:
            continue
        cur = st.get("current_pos")
        target = int(c["pos"])
        if st.get("pos_control") and cur is not None and cur <= target + 2:
            continue                                     # steht schon tiefer
        if not st.get("pos_control") and st.get("state") == "closed":
            continue
        try:
            move(dev, target)
            s["moved"][dev] = {"prev": cur if cur is not None else (100 if st.get("state") == "open" else None),
                               "target": target}
        except Exception as e:
            alert(dev, "rule", f"Beschattung „{r['name']}“: {dev_name(dev)}: {e}")


def _deactivate(r, c, s):
    for dev, m in (s.get("moved") or {}).items():
        if MANUAL.get(dev, 0) > s.get("since", 0):       # zwischendurch von Hand bewegt
            continue
        st, online = _cover(dev)
        if not online:
            continue
        cur = st.get("current_pos")
        if st.get("pos_control") and cur is not None and abs(cur - m["target"]) > 3:
            continue                                     # steht woanders (z. B. abends schon zu)
        if c["end"] == "none":
            continue
        back = 100 if c["end"] == "open" or m.get("prev") is None else int(m["prev"])
        try:
            move(dev, back)
        except Exception as e:
            alert(dev, "rule", f"Beschattung „{r['name']}“: {dev_name(dev)}: {e}")
    s["moved"] = {}


def step():
    """Alle Regeln pruefen (jede Minute)."""
    with _lock:
        for r in q("SELECT * FROM shades"):
            c = rule_cfg(r)
            s = json.loads(r["state"] or "{}")
            active = bool(s.get("active"))
            want, why = evaluate(c, active)
            if not r["enabled"] or s.get("paused_day") == now_local().strftime("%Y-%m-%d"):
                want = False
                why = ["ausgeschaltet" if not r["enabled"] else "für heute pausiert"]
            frost = frost_block()
            now = time.time()
            s["why"] = why
            if want != active:
                s.setdefault("cand", now)
                delay = float(c["delay_on" if want else "delay_off"]) * 60
                if now - s["cand"] >= delay and not frost:
                    s.pop("cand", None)
                    s["active"] = want
                    s["since"] = now
                    (_activate if want else _deactivate)(r, c, s)
                    s["last"] = {"ts": now, "what": "Beschattung an" if want else "Beschattung aus"}
                elif frost:
                    s["why"] = why + ["Frostschutz: keine Fahrt"]
            else:
                s.pop("cand", None)
            q("UPDATE shades SET state=? WHERE id=?", (json.dumps(s), r["id"]))


def rules_view():
    out = []
    for r in q("SELECT * FROM shades ORDER BY name"):
        c = rule_cfg(r)
        s = json.loads(r["state"] or "{}")
        out.append({"id": r["id"], "name": r["name"], "enabled": bool(r["enabled"]), "cfg": c,
                    "active": bool(s.get("active")), "why": s.get("why") or [], "since": s.get("since"),
                    "pending": s.get("cand"), "paused": s.get("paused_day") == now_local().strftime("%Y-%m-%d"),
                    "last": s.get("last"), "temp_label": source_label(c.get("temp_src")),
                    "covers": [{"id": d, "name": dev_name(d)} for d in c["covers"]]})
    return out


def save_rule(d):
    c = {k: d.get(k, v) for k, v in DEFAULT.items()}
    c["covers"] = [str(x) for x in (d.get("covers") or [])]
    for k in ("azimuth", "width", "elev_min", "elev_max", "pos", "temp_on", "temp_off", "in_on", "rad_min", "lux_on",
              "delay_on", "delay_off", "days"):
        c[k] = float(str(c[k]).replace(",", ".")) if c[k] not in (None, "") else DEFAULT[k]
    c["lux_src"] = str(c.get("lux_src") or "")
    c["fc_on"] = float(str(d["fc_on"]).replace(",", ".")) if d.get("fc_on") not in (None, "") else None
    c["pos"] = max(0, min(100, int(c["pos"])))
    c["days"] = int(c["days"])
    if c["temp_off"] > c["temp_on"]:
        raise ValueError("Ausschaltschwelle muss unter der Einschaltschwelle liegen")
    if c["end"] not in ("open", "restore", "none"):
        c["end"] = "open"
    name = (d.get("name") or "Beschattung").strip()
    en = 1 if d.get("enabled", True) else 0
    if d.get("id"):
        q("UPDATE shades SET name=?, enabled=?, cfg=? WHERE id=?", (name, en, json.dumps(c), int(d["id"])))
        return int(d["id"])
    return qid("INSERT INTO shades(name, enabled, cfg) VALUES(?,?,?)", (name, en, json.dumps(c)))


def pause_today(rid, on=True):
    r = q("SELECT state FROM shades WHERE id=?", (rid,), one=True)
    if not r:
        return
    s = json.loads(r["state"] or "{}")
    if on:
        s["paused_day"] = now_local().strftime("%Y-%m-%d")
    else:
        s.pop("paused_day", None)
    q("UPDATE shades SET state=? WHERE id=?", (json.dumps(s), rid))


def overview():
    sun = sun_now()
    return {"sun": sun, "weather": {k: v for k, v in WX.items()}, "sensors": sensors(), "covers": covers(),
            "frost_c": setting("frost_c", None), "weather_on": setting("weather_on", True), "rules": rules_view()}


# ======================= Hintergrund =======================
def loop():
    last_wx = 0
    while True:
        try:
            if time.time() - last_wx > 600:
                last_wx = time.time()
                fetch_weather()
            step()
        except Exception as e:
            print("Beschattung:", e, flush=True)
        time.sleep(60)


def start(bus):
    global BUS
    BUS = bus
    init_db()
    threading.Thread(target=loop, daemon=True).start()
