# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 D.Koch – Hermit OS
"""Automatik: Fertig-Erkennung (Waesche & Co.), Zeitplaene, Regeln, Konfig-Sicherung, Firmware."""
import json
import math
import secrets
import threading
import time
import urllib.request

import fridge
from datetime import datetime, timedelta

from core import (TZ, alert, dev_name, fmt_dur, fmt_eur, fmt_kwh, now_local, price_eur_kwh, q,
                  qid, setting)

BUS = None            # wird von der App gesetzt (bus.BUS)
_lock = threading.RLock()

# ======================= Fertig-Erkennung =======================
TYPES = {
    "washer":     {"label": "Waschmaschine", "start_w": 15, "start_s": 120, "end_w": 5, "end_s": 360, "remind_min": 30},
    "dryer":      {"label": "Trockner", "start_w": 50, "start_s": 60, "end_w": 10, "end_s": 300, "remind_min": 20},
    "dishwasher": {"label": "Spülmaschine", "start_w": 15, "start_s": 120, "end_w": 5, "end_s": 900, "remind_min": 0},
    "other":      {"label": "Gerät", "start_w": 10, "start_s": 60, "end_w": 3, "end_s": 300, "remind_min": 0},
}
LAUNDRY = ("washer", "dryer")
_ap_mem = {}          # dev -> {"cand_total":…, "low_total":…, "last_ts":…, "acc_wh":…}


def appliance(dev):
    return q("SELECT * FROM appliances WHERE dev=?", (dev,), one=True)


def set_appliance(dev, cfg):
    t = cfg.get("type") or "other"
    if t not in TYPES:
        raise ValueError("Unbekannter Typ")
    d = {**TYPES[t], **{k: v for k, v in cfg.items() if v not in (None, "")}}
    old = appliance(dev)
    if old:
        q("""UPDATE appliances SET type=?, label=?, start_w=?, start_s=?, end_w=?, end_s=?, remind_min=? WHERE dev=?""",
          (t, d.get("label") or TYPES[t]["label"], float(d["start_w"]), int(d["start_s"]), float(d["end_w"]),
           int(d["end_s"]), int(d["remind_min"]), dev))
    else:
        q("""INSERT INTO appliances(dev, type, label, start_w, start_s, end_w, end_s, remind_min, state)
             VALUES(?,?,?,?,?,?,?,?, 'idle')""",
          (dev, t, d.get("label") or TYPES[t]["label"], float(d["start_w"]), int(d["start_s"]), float(d["end_w"]),
           int(d["end_s"]), int(d["remind_min"])))


def _total(st, dev, now, p):
    """Zaehlerstand in Wh; ohne aenergy wird die Leistung aufintegriert."""
    tot = (st.get("aenergy") or {}).get("total")
    m = _ap_mem.setdefault(dev, {"acc_wh": 0.0, "last_ts": now})
    m["acc_wh"] += p * max(0.0, now - m["last_ts"]) / 3600.0
    m["last_ts"] = now
    return float(tot) if tot is not None else m["acc_wh"]


def _appliance_step(dev, st):
    a = appliance(dev)
    if not a or "apower" not in st:
        return
    now = time.time()
    p = float(st.get("apower") or 0)
    tot = _total(st, dev, now, p)
    mem = _ap_mem[dev]
    state, cand, low = a["state"], a["cand"], a["low"]
    if state in ("idle", "finished"):
        if p >= a["start_w"]:
            if not cand:
                q("UPDATE appliances SET cand=? WHERE dev=?", (now, dev))
                mem["cand_total"] = tot
            elif now - cand >= a["start_s"]:
                rid = qid("INSERT INTO runs(dev, type, start, start_total, peak_w) VALUES(?,?,?,?,?)",
                          (dev, a["type"], cand, mem.get("cand_total", tot), p))
                if state == "finished":                # neuer Lauf quittiert den alten
                    q("UPDATE runs SET ack=1 WHERE dev=? AND id<?", (dev, rid))
                q("UPDATE appliances SET state='running', since=?, run_id=?, cand=NULL, low=NULL WHERE dev=?",
                  (cand, rid, dev))
                on_appliance(dev, "started", a["label"])
        elif cand:
            q("UPDATE appliances SET cand=NULL WHERE dev=?", (dev,))
    elif state == "running":
        q("UPDATE runs SET peak_w=MAX(COALESCE(peak_w,0), ?) WHERE id=?", (p, a["run_id"]))
        if p < a["end_w"]:
            if not low:
                q("UPDATE appliances SET low=? WHERE dev=?", (now, dev))
                mem["low_total"] = tot
            elif now - low >= a["end_s"]:
                r = q("SELECT * FROM runs WHERE id=?", (a["run_id"],), one=True)
                end_total = mem.get("low_total", tot)
                wh = max(0.0, end_total - (r["start_total"] or end_total)) if r else 0.0
                cost = wh / 1000.0 * price_eur_kwh()
                q("UPDATE runs SET end=?, wh=?, cost=? WHERE id=?", (low, wh, cost, a["run_id"]))
                q("UPDATE appliances SET state='finished', since=?, low=NULL WHERE dev=?", (low, dev))
                dur = low - (r["start"] if r else low)
                alert(dev, "finished", f"{a['label']} ({dev_name(dev)}) fertig – {fmt_dur(dur)}, "
                      f"{fmt_kwh(wh)}, {fmt_eur(cost)}", title=f"{a['label']} fertig")
                on_appliance(dev, "finished", a["label"])
        elif low:
            q("UPDATE appliances SET low=NULL WHERE dev=?", (dev,))


def ack_appliance(dev):
    q("UPDATE runs SET ack=1 WHERE dev=? AND end IS NOT NULL", (dev,))
    q("UPDATE appliances SET state='idle' WHERE dev=? AND state='finished'", (dev,))


def appliance_view(a):
    """Zustand fuer die Oberflaeche inkl. Restzeit-Schaetzung aus den letzten Laeufen."""
    dev = a["dev"]
    d = q("SELECT name, online, status FROM devices WHERE id=?", (dev,), one=True)
    st = json.loads(d["status"] or "{}") if d else {}
    hist = q("""SELECT end-start AS dur, wh FROM runs WHERE dev=? AND end IS NOT NULL AND end-start>300
                ORDER BY id DESC LIMIT 10""", (dev,))
    avg_dur = sum(h["dur"] for h in hist) / len(hist) if hist else None
    avg_wh = sum(h["wh"] or 0 for h in hist) / len(hist) if hist else None
    out = {"dev": dev, "name": (d["name"] if d and d["name"] else dev), "online": bool(d and d["online"]),
           "type": a["type"], "label": a["label"], "state": a["state"], "since": a["since"],
           "apower": st.get("apower"), "avg_dur": avg_dur, "avg_wh": avg_wh,
           "cfg": {k: a[k] for k in ("start_w", "start_s", "end_w", "end_s", "remind_min")}}
    if a["state"] == "running" and a["run_id"]:
        r = q("SELECT * FROM runs WHERE id=?", (a["run_id"],), one=True)
        tot = (st.get("aenergy") or {}).get("total")
        wh = max(0.0, float(tot) - r["start_total"]) if (tot is not None and r["start_total"] is not None) else None
        el = time.time() - r["start"]
        out.update({"elapsed": el, "wh": wh, "cost": (wh or 0) / 1000 * price_eur_kwh(), "peak_w": r["peak_w"],
                    "remaining": max(0, avg_dur - el) if avg_dur else None,
                    "progress": min(0.99, el / avg_dur) if avg_dur else None})
    if a["state"] == "finished":
        r = q("SELECT * FROM runs WHERE dev=? AND end IS NOT NULL ORDER BY id DESC LIMIT 1", (dev,), one=True)
        if r:
            out.update({"last": dict(r), "waiting": time.time() - r["end"]})
    m0 = now_local().replace(day=1, hour=0, minute=0, second=0, microsecond=0).timestamp()
    s = q("SELECT COUNT(*) AS n, SUM(wh) AS wh, SUM(cost) AS cost FROM runs WHERE dev=? AND end IS NOT NULL AND start>=?",
          (dev, m0), one=True)
    out["month"] = {"n": s["n"], "wh": s["wh"] or 0, "cost": s["cost"] or 0}
    return out


def _reminders():
    for a in q("SELECT * FROM appliances WHERE state='finished' AND remind_min>0"):
        r = q("SELECT * FROM runs WHERE dev=? AND end IS NOT NULL ORDER BY id DESC LIMIT 1", (a["dev"],), one=True)
        if r and not r["ack"] and not r["reminded"] and time.time() - r["end"] >= a["remind_min"] * 60:
            q("UPDATE runs SET reminded=1 WHERE id=?", (r["id"],))
            alert(a["dev"], "reminder", f"{a['label']} ({dev_name(a['dev'])}) ist seit "
                  f"{fmt_dur(time.time() - r['end'])} fertig und wartet", title=f"{a['label']} wartet")


# ======================= Ziele (Geraet / Raum) =======================
def resolve(target):
    if not target:
        return []
    kind, _, val = target.partition(":")
    if kind == "dev":
        return [val]
    if kind == "room":
        return [r["id"] for r in q("SELECT id, status FROM devices WHERE room=? AND hidden=0", (val,))
                if not json.loads(r["status"] or "{}").get("meter_only")]
    return []


def target_label(target):
    kind, _, val = (target or "").partition(":")
    return f"Raum {val}" if kind == "room" else dev_name(val)


def do_switch(target, mode):
    """mode: 'on' | 'off' | 'toggle'. Liefert Fehlerliste."""
    errs = []
    for dev in resolve(target):
        try:
            BUS.switch(dev, None if mode == "toggle" else mode == "on")
        except Exception as e:
            errs.append(f"{dev_name(dev)}: {e}")
    return errs


# ======================= Sonnenauf-/untergang =======================
def sun_times(day, lat, lon):
    """Sonnenauf- und -untergang (lokale Zeit) nach NOAA-Naeherung; None bei Polartag/-nacht."""
    n = day.timetuple().tm_yday
    g = 2 * math.pi / 365 * (n - 1)
    eqt = 229.18 * (0.000075 + 0.001868 * math.cos(g) - 0.032077 * math.sin(g)
                    - 0.014615 * math.cos(2 * g) - 0.040849 * math.sin(2 * g))
    decl = (0.006918 - 0.399912 * math.cos(g) + 0.070257 * math.sin(g) - 0.006758 * math.cos(2 * g)
            + 0.000907 * math.sin(2 * g) - 0.002697 * math.cos(3 * g) + 0.00148 * math.sin(3 * g))
    lr = math.radians(lat)
    c = (math.cos(math.radians(90.833)) / (math.cos(lr) * math.cos(decl)) - math.tan(lr) * math.tan(decl))
    if abs(c) > 1:
        return None, None
    ha = math.degrees(math.acos(c))
    base = datetime(day.year, day.month, day.day, tzinfo=TZ)
    off = base.utcoffset().total_seconds() / 60
    rise = 720 - 4 * (lon + ha) - eqt + off
    sset = 720 - 4 * (lon - ha) - eqt + off
    return base + timedelta(minutes=rise), base + timedelta(minutes=sset)


def sched_time(spec, day):
    """'HH:MM' oder 'sunrise+30' / 'sunset-15' -> datetime an diesem Tag."""
    spec = (spec or "").strip()
    if spec.startswith(("sunrise", "sunset")):
        loc = setting("location", {}) or {}
        if loc.get("lat") in (None, "") or loc.get("lon") in (None, ""):
            return None
        rise, sset = sun_times(day, float(loc["lat"]), float(loc["lon"]))
        base = rise if spec.startswith("sunrise") else sset
        if base is None:
            return None
        rest = spec[7:] if spec.startswith("sunrise") else spec[6:]
        try:
            off = int(rest) if rest else 0
        except ValueError:
            off = 0
        return base + timedelta(minutes=off)
    hh, mm = spec.split(":")
    return datetime(day.year, day.month, day.day, int(hh), int(mm), tzinfo=TZ)


def _schedules():
    now = now_local()
    for s in q("SELECT * FROM schedules WHERE enabled=1"):
        if not (s["days"] >> now.weekday()) & 1:
            continue
        try:
            t = sched_time(s["time"], now.date())
        except Exception:
            continue
        key = now.strftime("%Y-%m-%d")
        if t is None or s["last_run"] == key:
            continue
        if t <= now < t + timedelta(minutes=10):
            q("UPDATE schedules SET last_run=? WHERE id=?", (key, s["id"]))
            errs = do_switch(s["target"], s["action"])
            if errs:
                alert("cloud", "rule", f"Zeitplan „{s['name']}“: " + "; ".join(errs))


# ======================= Regeln =======================
_rule_since = {}       # rule id -> Zeitpunkt, seit dem die Leistungsbedingung erfuellt ist


def _in_window(trig):
    w = trig.get("window") or {}
    now = now_local()
    days = trig.get("days", 127)
    if not (int(days) >> now.weekday()) & 1:
        return False
    f, t = w.get("from"), w.get("to")
    if not f or not t:
        return True
    cur = now.strftime("%H:%M")
    return f <= cur < t if f <= t else (cur >= f or cur < t)


def fire(rule, why=""):
    trig = json.loads(rule["trig"])
    if not rule["enabled"] or not _in_window(trig):
        return False
    q("UPDATE rules SET last_fire=? WHERE id=?", (time.time(), rule["id"]))
    for step in json.loads(rule["action"] or "[]"):
        delay = float(step.get("delay_s") or 0)
        if delay > 0:
            threading.Timer(delay, _run_step, args=(rule["name"], step, why)).start()
        else:
            threading.Thread(target=_run_step, args=(rule["name"], step, why), daemon=True).start()
    return True


def _run_step(name, step, why):
    try:
        if step.get("type") == "switch":
            mode = step.get("mode", "on")
            errs = do_switch(step.get("target"), mode)
            if errs:
                alert("cloud", "rule", f"Regel „{name}“: " + "; ".join(errs))
            if mode == "on" and float(step.get("auto_off_s") or 0) > 0:
                threading.Timer(float(step["auto_off_s"]), do_switch, args=(step.get("target"), "off")).start()
        elif step.get("type") == "notify":
            text = step.get("text") or f"Regel „{name}“ ausgelöst"
            alert("cloud", "rule", text.replace("{why}", why), title=step.get("title") or name)
    except Exception as e:
        print("Regel", name, e, flush=True)


def _rules(dev=None, kind=None):
    rows = q("SELECT * FROM rules WHERE enabled=1")
    out = []
    for r in rows:
        t = json.loads(r["trig"])
        if kind and t.get("type") != kind:
            continue
        if dev and t.get("dev") != dev:
            continue
        out.append((r, t))
    return out


def _power_rules(dev, p):
    now = time.time()
    for r, t in _rules(dev, "power"):
        cond = p > float(t["w"]) if t.get("op", ">") == ">" else p < float(t["w"])
        if cond:
            since = _rule_since.setdefault(r["id"], now)
            if not r["active"] and now - since >= float(t.get("for_s") or 0):
                q("UPDATE rules SET active=1 WHERE id=?", (r["id"],))
                fire(r, f"{dev_name(dev)}: {p:.0f} W")
        else:
            _rule_since.pop(r["id"], None)
            if r["active"]:
                q("UPDATE rules SET active=0 WHERE id=?", (r["id"],))


def new_token():
    return secrets.token_urlsafe(12)


# ======================= Hooks aus bus.py =======================
def on_status(dev, comp, new, old):
    try:
        _appliance_step(dev, new)
        fridge.step(dev, new)
        if "apower" in new:
            _power_rules(dev, float(new.get("apower") or 0))
        if "output" in new and old.get("output") is not None and new["output"] != old.get("output"):
            for r, t in _rules(dev, "switch"):
                if t.get("state", "on") == ("on" if new["output"] else "off"):
                    fire(r, f"{dev_name(dev)} {'an' if new['output'] else 'aus'}")
    except Exception as e:
        print("Automatik:", e, flush=True)


def on_event(dev, comp, ev):
    try:
        for r, t in _rules(dev, "event"):
            if t.get("event") in (ev, f"{comp.split(':')[0]}.{ev}") and (not t.get("comp") or t["comp"] == comp):
                fire(r, f"{dev_name(dev)}: {ev}")
    except Exception as e:
        print("Automatik:", e, flush=True)


def on_online(dev, online):
    try:
        for r, t in _rules(dev, "online" if online else "offline"):
            fire(r, f"{dev_name(dev)} {'online' if online else 'offline'}")
    except Exception as e:
        print("Automatik:", e, flush=True)


def on_appliance(dev, what, label):
    for r, t in _rules(dev, what):
        fire(r, f"{label} {what}")


def fire_webhook(token):
    r = q("SELECT * FROM rules WHERE token=?", (token,), one=True)
    if not r or json.loads(r["trig"]).get("type") != "webhook":
        return None
    return fire(r, "URL-Aufruf")


# ======================= Konfig-Sicherung =======================
def backup_device(dev):
    info = q("SELECT model, ver FROM devices WHERE id=?", (dev,), one=True)
    cfg = BUS.rpc(dev, "Shelly.GetConfig", timeout=8)
    try:
        hooks = (BUS.rpc(dev, "Webhook.List", timeout=6) or {}).get("hooks", [])
    except Exception:
        hooks = []
    bid = qid("INSERT INTO backups(dev, ts, model, ver, data) VALUES(?,?,?,?,?)",
              (dev, time.time(), info["model"] if info else None, info["ver"] if info else None,
               json.dumps({"config": cfg, "hooks": hooks})))
    old = q("SELECT id FROM backups WHERE dev=? ORDER BY ts DESC LIMIT -1 OFFSET 10", (dev,))
    for o in old:
        q("DELETE FROM backups WHERE id=?", (o["id"],))
    return bid


SKIP_KEYS = {"id"}


def _clean(c):
    return {k: v for k, v in (c or {}).items() if k not in SKIP_KEYS}


def restore_backup(bid, dev, with_hooks=True):
    """Spielt Name, Schalter-/Licht-/Eingangs-Einstellungen und Aktionen zurueck (kein WLAN/MQTT)."""
    b = q("SELECT * FROM backups WHERE id=?", (bid,), one=True)
    if not b:
        raise ValueError("Sicherung nicht gefunden")
    data = json.loads(b["data"])
    cfg = data.get("config") or {}
    log = []

    def step(label, method, params):
        try:
            BUS.rpc(dev, method, params, timeout=8)
            log.append({"step": label, "ok": True})
        except Exception as e:
            log.append({"step": label, "ok": False, "error": str(e)})

    sysc = cfg.get("sys") or {}
    sc = {}
    if (sysc.get("device") or {}).get("name"):
        sc["device"] = {"name": sysc["device"]["name"]}
    for k in ("location", "sntp", "ui"):
        if sysc.get(k):
            sc[k] = {kk: vv for kk, vv in sysc[k].items() if kk in ("tz", "server", "led")}
    if sc:
        step("System (Name, Zeit, LED)", "Sys.SetConfig", {"config": sc})
    for key, c in cfg.items():
        kind, _, cid = key.partition(":")
        m = {"switch": "Switch", "cct": "CCT", "light": "Light", "cover": "Cover", "input": "Input"}.get(kind)
        if m and cid.isdigit():
            step(f"{key}", f"{m}.SetConfig", {"id": int(cid), "config": _clean(c)})
    if with_hooks and data.get("hooks") is not None:
        step("Aktionen löschen", "Webhook.DeleteAll", {})
        for h in data["hooks"]:
            step(f"Aktion {h.get('name') or h.get('event')}", "Webhook.Create", _clean(h))
    return log


def _auto_backups():
    for d in q("SELECT id FROM devices WHERE online=1 AND hidden=0 AND id NOT LIKE '%~%'"):
        last = q("SELECT MAX(ts) AS t FROM backups WHERE dev=?", (d["id"],), one=True)
        if last and last["t"] and time.time() - last["t"] < 20 * 3600:
            continue
        try:
            backup_device(d["id"])
        except Exception as e:
            print("Sicherung", d["id"], e, flush=True)


# ======================= Firmware =======================
HERMIT_UPDATE_URL = "https://raw.githubusercontent.com/oeltensack/hermit-os/main/update.json"
FW = {"hermit": None, "checked": 0, "gen2": {}}


def fw_check(force=False):
    if force or time.time() - FW["checked"] > 6 * 3600:
        try:
            with urllib.request.urlopen(HERMIT_UPDATE_URL + f"?t={int(time.time())}", timeout=15) as r:
                FW["hermit"] = json.loads(r.read()).get("version")
        except Exception as e:
            print("Firmware-Pruefung:", e, flush=True)
        FW["checked"] = time.time()
    return FW["hermit"]


def ver_tuple(v):
    try:
        return tuple(int(x) for x in str(v).split("-")[0].split(".") if x.isdigit())
    except Exception:
        return ()


# ======================= Hintergrund =======================
def loop():
    last_backup_day = None
    while True:
        try:
            _schedules()
            _reminders()
            fridge.checks()
            now = now_local()
            if now.hour == 3 and last_backup_day != now.date():
                last_backup_day = now.date()
                threading.Thread(target=_auto_backups, daemon=True).start()
                threading.Thread(target=fw_check, daemon=True).start()
        except Exception as e:
            print("Automatik:", e, flush=True)
        time.sleep(15)


def start(bus):
    global BUS
    BUS = bus
    threading.Thread(target=loop, daemon=True).start()
    threading.Thread(target=fw_check, daemon=True).start()
