# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 D.Koch – Pagurus OS
"""Kuehlgeraete: Kompressor-Zyklen aus der Leistung erkennen, auswerten und bei Auffaelligkeiten warnen.

Ohne Temperaturfuehler bleibt das Laufverhalten: Ein gesundes Kuehlgeraet laeuft in Zyklen (z. B. 15 min an,
30 min aus). Laeuft der Kompressor ununterbrochen, steht oft die Tuer offen oder die Dichtung/Kuehlung ist
defekt; laeuft er gar nicht mehr an, fehlt Strom oder der Kompressor ist kaputt. Steigt der Tagesverbrauch
deutlich, vereist das Geraet oder es wird zu oft geoeffnet.
"""
import json
import time
from datetime import timedelta

from core import alert, dev_name, fmt_dur, now_local, price_eur_kwh, q, qid

TYPES = {
    "fridge":  {"label": "Kühlschrank", "on_w": 25, "max_run_min": 120, "max_off_min": 180, "dev_pct": 35},
    "freezer": {"label": "Gefrierschrank", "on_w": 25, "max_run_min": 180, "max_off_min": 240, "dev_pct": 35},
    "combo":   {"label": "Kühl-Gefrier-Kombi", "on_w": 25, "max_run_min": 150, "max_off_min": 180, "dev_pct": 35},
}


def get(dev):
    return q("SELECT * FROM fridges WHERE dev=?", (dev,), one=True)


def save(dev, cfg):
    t = cfg.get("type") or "fridge"
    if t not in TYPES:
        raise ValueError("Unbekannte Art")
    d = {**TYPES[t], **{k: v for k, v in cfg.items() if v not in (None, "")}}
    vals = (t, d.get("label") or TYPES[t]["label"], float(d["on_w"]), int(d["max_run_min"]), int(d["max_off_min"]),
            int(d["dev_pct"]))
    if get(dev):
        q("UPDATE fridges SET type=?, label=?, on_w=?, max_run_min=?, max_off_min=?, dev_pct=? WHERE dev=?", vals + (dev,))
    else:
        q("""INSERT INTO fridges(dev, type, label, on_w, max_run_min, max_off_min, dev_pct, since)
             VALUES(?,?,?,?,?,?,?,?)""", (dev,) + vals + (time.time(),))


def delete(dev):
    q("DELETE FROM fridges WHERE dev=?", (dev,))
    q("DELETE FROM fridge_cycles WHERE dev=?", (dev,))


def step(dev, st):
    """Bei jedem Status mit Leistung: Kompressor an/aus mit Hysterese erkennen."""
    f = get(dev)
    if not f or "apower" not in st:
        return
    p = float(st.get("apower") or 0)
    now = time.time()
    on = p >= f["on_w"] * (0.6 if f["state"] == "on" else 1.0)
    new = "on" if on else "off"
    if f["state"] == "on":
        q("UPDATE fridge_cycles SET peak_w=MAX(COALESCE(peak_w,0), ?) WHERE dev=? AND end IS NULL", (p, dev))
    if new == f["state"]:
        return
    if new == "on":
        q("UPDATE fridge_cycles SET end=? WHERE dev=? AND end IS NULL", (now, dev))   # falls offen geblieben
        qid("INSERT INTO fridge_cycles(dev, start, peak_w) VALUES(?,?,?)", (dev, now, p))
        if f["alarm_off"]:
            alert(dev, "online", f"{f['label']} ({dev_name(dev)}): Kompressor läuft wieder", push=False)
    else:
        q("UPDATE fridge_cycles SET end=? WHERE dev=? AND end IS NULL", (now, dev))
        if f["alarm_run"]:
            alert(dev, "online", f"{f['label']} ({dev_name(dev)}): Kompressor macht wieder Pause", push=False)
    q("UPDATE fridges SET state=?, since=?, alarm_run=0, alarm_off=0 WHERE dev=?", (new, now, dev))


def checks():
    """Regelmaessig (Hintergrund): Dauerlauf, Stillstand, auffaelliger Tagesverbrauch."""
    now = time.time()
    for f in q("""SELECT f.*, d.online FROM fridges f JOIN devices d ON d.id=f.dev"""):
        if not f["online"] or not f["since"]:
            continue                                   # offline meldet bereits die Geraeteueberwachung
        dur = now - f["since"]
        name = f"{f['label']} ({dev_name(f['dev'])})"
        if f["state"] == "on" and not f["alarm_run"] and dur > f["max_run_min"] * 60:
            q("UPDATE fridges SET alarm_run=1 WHERE dev=?", (f["dev"],))
            alert(f["dev"], "fridge", f"{name}: Kompressor läuft seit {fmt_dur(dur)} ohne Pause – Tür offen?",
                  title=f"{f['label']}: Dauerlauf")
        if f["state"] != "on" and not f["alarm_off"] and dur > f["max_off_min"] * 60:
            q("UPDATE fridges SET alarm_off=1 WHERE dev=?", (f["dev"],))
            alert(f["dev"], "fridge", f"{name}: Kompressor seit {fmt_dur(dur)} nicht angelaufen – kühlt es noch?",
                  title=f"{f['label']}: kein Kühlen")
    _daily_deviation()


def _daily_deviation():
    now = now_local()
    if now.hour < 7:
        return
    yday = (now - timedelta(days=1)).strftime("%Y-%m-%d")
    for f in q("SELECT * FROM fridges"):
        if f["last_dev_check"] == yday:
            continue
        q("UPDATE fridges SET last_dev_check=? WHERE dev=?", (yday, f["dev"]))
        y = q("SELECT wh FROM energy WHERE dev=? AND day=?", (f["dev"], yday), one=True)
        start = (now - timedelta(days=15)).strftime("%Y-%m-%d")
        hist = [r["wh"] for r in q("SELECT wh FROM energy WHERE dev=? AND day>=? AND day<? AND wh>0",
                                   (f["dev"], start, yday))]
        if not y or len(hist) < 5:
            continue
        avg = sum(hist) / len(hist)
        if avg > 0 and y["wh"] > avg * (1 + f["dev_pct"] / 100):
            pct = round((y["wh"] / avg - 1) * 100)
            k = lambda wh: f"{wh / 1000:.2f}".replace(".", ",")   # noqa: E731
            alert(f["dev"], "fridge", f"{f['label']} ({dev_name(f['dev'])}): gestern {pct} % mehr Energie als üblich "
                  f"({k(y['wh'])} statt {k(avg)} kWh) – vereist, Tür oft offen oder Dichtung?",
                  title=f"{f['label']}: Verbrauch auffällig")


def view(f):
    """Kennzahlen fuer die Oberflaeche (letzte 24 h, letzte 14 Tage)."""
    dev = f["dev"]
    now = time.time()
    d = q("SELECT name, online, status FROM devices WHERE id=?", (dev,), one=True)
    st = json.loads(d["status"] or "{}") if d else {}
    t0 = now - 86400
    cyc = q("SELECT start, COALESCE(end, ?) AS end, end IS NULL AS open, peak_w FROM fridge_cycles "
            "WHERE dev=? AND COALESCE(end, ?) > ? ORDER BY start", (now, dev, now, t0))
    on_s = sum(min(c["end"], now) - max(c["start"], t0) for c in cyc)
    done = [c for c in cyc if not c["open"] and c["start"] >= t0]
    starts = [c["start"] for c in cyc if c["start"] >= t0]
    avg_on = sum(c["end"] - c["start"] for c in done) / len(done) if done else None
    gaps = [cyc[i + 1]["start"] - cyc[i]["end"] for i in range(len(cyc) - 1)]
    avg_off = sum(gaps) / len(gaps) if gaps else None
    today = now_local().strftime("%Y-%m-%d")
    e = q("SELECT wh FROM energy WHERE dev=? AND day=?", (dev, today), one=True)
    start14 = (now_local() - timedelta(days=14)).strftime("%Y-%m-%d")
    hist = [r["wh"] for r in q("SELECT wh FROM energy WHERE dev=? AND day>=? AND day<? AND wh>0", (dev, start14, today))]
    avg_wh = sum(hist) / len(hist) if hist else None
    p = price_eur_kwh()
    tracked = now - (q("SELECT MIN(start) AS s FROM fridge_cycles WHERE dev=?", (dev,), one=True)["s"] or now)
    return {
        "dev": dev, "name": (d["name"] if d and d["name"] else dev), "online": bool(d and d["online"]),
        "type": f["type"], "label": f["label"], "state": f["state"] or "off", "since": f["since"],
        "apower": st.get("apower"), "alarm": bool(f["alarm_run"] or f["alarm_off"]),
        "alarm_text": "Dauerlauf" if f["alarm_run"] else ("kein Kühlen" if f["alarm_off"] else None),
        "duty": on_s / 86400 if tracked > 3600 else None, "duty_hours": min(24, tracked / 3600),
        "cycles": len(starts), "avg_on": avg_on, "avg_off": avg_off,
        "today_wh": e["wh"] if e else 0, "avg_wh": avg_wh, "days": len(hist),
        "year_kwh": avg_wh * 365 / 1000 if avg_wh else None, "year_cost": avg_wh * 365 / 1000 * p if avg_wh else None,
        "timeline": [[c["start"], c["end"]] for c in cyc],
        "cfg": {k: f[k] for k in ("on_w", "max_run_min", "max_off_min", "dev_pct")},
    }


def cleanup():
    q("DELETE FROM fridge_cycles WHERE end IS NOT NULL AND end<?", (time.time() - 60 * 86400,))
