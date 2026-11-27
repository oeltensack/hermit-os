# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 D.Koch – Pagurus OS
"""Heizung: Warmwasser (Oelkessel mit Shelly-Logik-Script), Zirkulationspumpe und Raumheizer
(Heizluefter/Badheizer mit Script v11, Konvektor mit Logik v1.6) an einer Stelle bedienen.

Die Regelung bleibt komplett auf den Shellys. Die Cloud liest nur und schreibt Einstellungen bzw. Befehle
dorthin, wo die Scripts sie ohnehin erwarten:
- Warmwasser: Script-Endpunkte status, cfg (lesen, cfg?schluessel=wert aendern), boost_trigger (ab Logik V13.1)
- Zirkulation: Switch.GetStatus / Switch.Set, Temperatur vom Add-on
- Raumheizer: KVS hl_state, hl_cfg, hl_stats lesen, hl_cfg und hl_cmd schreiben (das Script uebernimmt in 5 s)
Bei Stoerungen gibt es eine Meldung (Art "heating").
"""
import json
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from concurrent.futures import ThreadPoolExecutor

from fastapi import APIRouter, HTTPException, Request

import gen2
from core import alert, q, set_setting, setting

router = APIRouter()
POOL = ThreadPoolExecutor(max_workers=8)
TYPES = {"hl": "Heizlüfter (Script v11)", "kv": "Konvektor (Logik v1.6)"}
DEFAULT = {
    "ww": {"name": "Warmwasser", "dev": "", "ip": "", "sid": 1, "on": True},
    "zirk": {"name": "Zirkulation", "dev": "", "ip": "", "tid": 101, "on": True},
    "rooms": [],
}
# Einstellungen des Warmwasser-Scripts, die die Oberflaeche aendern darf (Bereiche wie im Script, Zeiten HH:MM)
WW_KEYS = {"aS": "t", "aE": "t", "mE": "t", "tS": (40, 70), "tM": (35, 65), "tL": (55, 75), "fr": (3, 20),
           "panH": (1, 10), "ein": (1, 10), "aus": (0, 8), "bMax": (600, 14400), "drop": (0.05, 2),
           "ovMax": (0, 15), "cl": (0, 1), "kOff": (5, 40), "kOH": (1, 10), "kHy": (2, 15), "kMax": (70, 95),
           "kPan": (50, 85), "rest": (40, 70), "pan": (30, 50), "pEin": (3, 25), "pAus": (1, 20), "rAus": (0, 15),
           "pMin": (0, 900), "sMax": (50, 80), "sperr": (0, 3600), "minW": (0, 500), "lph": (0.5, 5)}
ROOM_KEYS = {"t": (5, 28), "bt": (5, 30), "boost": (5, 240), "von": "t", "bis": "t", "max": (1, 3), "on": "b"}
CMDS = ("on", "off", "boost_on", "boost_off", "boost_toggle", "ack")


def need(request, role):
    order = {"viewer": 0, "user": 1, "admin": 2}
    if order.get(request.state.user["role"], -1) < order[role]:
        raise HTTPException(403, "Keine Berechtigung")


# ======================= Konfiguration =======================
def cfg():
    c = setting("heizung")
    if not isinstance(c, dict):
        c = json.loads(json.dumps(DEFAULT))
    for k in ("ww", "zirk"):
        c[k] = {**DEFAULT[k], **(c.get(k) or {})}
    c["rooms"] = [r for r in (c.get("rooms") or []) if isinstance(r, dict)]
    return c


def _ip_ok(s):
    p = s.split(".")
    return len(p) == 4 and all(x.isdigit() and 0 <= int(x) <= 255 for x in p)


def cfg_save(d):
    c = cfg()
    for k in ("ww", "zirk"):
        src = d.get(k) or {}
        n = dict(c[k])
        n["name"] = str(src.get("name") or n["name"]).strip()[:40]
        n["dev"] = str(src.get("dev") or "").strip()
        n["ip"] = str(src.get("ip") or "").strip()
        n["on"] = bool(src.get("on", True))
        if k == "ww":
            n["sid"] = max(1, min(10, int(src.get("sid") or 1)))
        else:
            n["tid"] = max(0, min(199, int(src.get("tid") or 101)))
        if n["ip"] and not _ip_ok(n["ip"]):
            raise ValueError(f"{n['name']}: IP-Adresse ungültig")
        c[k] = n
    rooms = []
    for i, r in enumerate(d.get("rooms") or []):
        ip = str(r.get("ip") or "").strip()
        if ip and not _ip_ok(ip):
            raise ValueError(f"Heizer {i + 1}: IP-Adresse ungültig")
        rooms.append({"id": str(r.get("id") or f"r{int(time.time() * 1000) % 10 ** 9}{i}")[:20],
                      "name": str(r.get("name") or "Heizer").strip()[:40], "dev": str(r.get("dev") or "").strip(),
                      "ip": ip, "typ": r.get("typ") if r.get("typ") in TYPES else "hl", "on": bool(r.get("on", True))})
    if len({r["id"] for r in rooms}) != len(rooms):
        raise ValueError("Doppelte Heizer-ID")
    c["rooms"] = rooms
    set_setting("heizung", c)
    return c


def ip_of(x):
    """IP aus dem Cloud-Geraet (folgt IP-Wechseln), sonst die eingetragene."""
    if x.get("dev"):
        r = q("SELECT ip FROM devices WHERE id=?", (x["dev"].split("~")[0],), one=True)
        if r and r["ip"]:
            return r["ip"]
    return x.get("ip") or ""


def room(rid):
    for r in cfg()["rooms"]:
        if r["id"] == rid:
            if not ip_of(r):
                raise HTTPException(400, "Für diesen Heizer ist keine IP eingetragen")
            return r
    raise HTTPException(404, "Heizer nicht gefunden")


# ======================= Zugriff auf die Geraete =======================
def http_json(url, timeout=5):
    try:
        with urllib.request.urlopen(url, timeout=timeout) as r:
            t = r.read().decode()
    except urllib.error.HTTPError as e:
        raise RuntimeError(f"HTTP {e.code}")
    except Exception as e:
        raise RuntimeError("nicht erreichbar" if "timed out" not in str(e) else "keine Antwort")
    return json.loads(t) if t.strip() else {}


def kvs_get(ip, key):
    try:
        r = gen2.rpc(ip, "KVS.Get", {"key": key}, timeout=4)
    except RuntimeError as e:
        if "not found" in str(e).lower() or "-105" in str(e):
            return None
        raise
    v = (r or {}).get("value")
    if isinstance(v, str):
        try:
            return json.loads(v)
        except Exception:
            return v
    return v


def kvs_set(ip, key, value):
    gen2.rpc(ip, "KVS.Set", {"key": key, "value": value}, timeout=4)


def ww_url(path):
    w = cfg()["ww"]
    return f"http://{ip_of(w)}/script/{w['sid']}/{path}"


def _ww():
    w = cfg()["ww"]
    if not w["on"] or not ip_of(w):
        return None
    out = {"name": w["name"], "ip": ip_of(w)}
    try:
        out["st"] = http_json(ww_url("status"))
        out["ok"] = True
    except Exception as e:
        out.update(ok=False, err=str(e))
        return out
    try:
        j = http_json(ww_url("cfg"))
        out["cfg"], out["ov"] = j.get("cfg"), j.get("ov")
    except Exception:
        out["cfg_err"] = "Das Logik-Script antwortet nicht auf „cfg“ – Heizung Logik V13.1 einspielen."
    return out


def _zirk():
    z = cfg()["zirk"]
    ip = ip_of(z)
    if not z["on"] or not ip:
        return None
    out = {"name": z["name"], "ip": ip}
    try:
        s = gen2.rpc(ip, "Switch.GetStatus", {"id": 0}, timeout=4) or {}
        out.update(ok=True, on=bool(s.get("output")), w=s.get("apower"))
    except Exception as e:
        out.update(ok=False, err=str(e))
        return out
    try:
        t = gen2.rpc(ip, "Temperature.GetStatus", {"id": z["tid"]}, timeout=4) or {}
        out["t"] = t.get("tC")
    except Exception:
        pass
    return out


def _room(r):
    ip = ip_of(r)
    out = {"id": r["id"], "name": r["name"], "typ": r["typ"], "ip": ip}
    if not ip:
        out.update(ok=False, err="keine IP eingetragen")
        return out
    try:
        st = kvs_get(ip, "hl_state")
        out["state"] = st if isinstance(st, dict) else None
        c = kvs_get(ip, "hl_cfg")
        out["cfg"] = c if isinstance(c, dict) else None
        s = kvs_get(ip, "hl_stats")
        out["stats"] = s if isinstance(s, dict) else None
        if r["typ"] == "kv":
            p = 0
            for i in (0, 1):
                p += (gen2.rpc(ip, "Switch.GetStatus", {"id": i}, timeout=4) or {}).get("apower") or 0
            out["pwr"] = p
        out["ok"] = out["state"] is not None
        if not out["ok"]:
            out["err"] = "kein Status – läuft das Heizer-Script?"
    except Exception as e:
        out.update(ok=False, err=str(e))
    return out


_cache = {"t": 0, "v": None}
_clock = threading.Lock()


def view(max_age=2.0):
    """Alles parallel abfragen (kurz zwischengespeichert, damit mehrere Browser die Geraete nicht vervielfachen)."""
    with _clock:
        if _cache["v"] and time.time() - _cache["t"] < max_age:
            return _cache["v"]
    c = cfg()
    fw, fz = POOL.submit(_ww), POOL.submit(_zirk)
    fr = [POOL.submit(_room, r) for r in c["rooms"] if r["on"]]
    v = {"ww": fw.result(), "zirk": fz.result(), "rooms": [f.result() for f in fr], "ts": time.time()}
    with _clock:
        _cache.update(t=time.time(), v=v)
    return v


def _drop_cache():
    with _clock:
        _cache["t"] = 0


# ======================= Meldungen bei Stoerung =======================
_last = {}


def _check():
    try:
        v = view(max_age=10)
    except Exception:
        return
    found = {}
    w = v.get("ww")
    if w and w.get("ok") and (w.get("st") or {}).get("fault"):
        f = w["st"]["fault"].replace("speicher", "Speicherfühler").replace("kessel", "Kesselfühler").replace(",", " und ")
        found["ww"] = (w["name"], f"Störung {f} – Brenner gesperrt")
    for r in v.get("rooms") or []:
        e = (r.get("state") or {}).get("err") if r.get("ok") else ""
        if e:
            found["r:" + r["id"]] = (r["name"], f"Störung: {e}")
    for k, (name, text) in found.items():
        if _last.get(k) != text:
            alert(k, "heating", f"{name}: {text}", title=f"Heizung: {name}")
    _last.clear()
    _last.update({k: t for k, (_, t) in found.items()})


def _loop():
    time.sleep(20)
    while True:
        _check()
        time.sleep(60)


def start():
    threading.Thread(target=_loop, daemon=True).start()


# ======================= API =======================
@router.get("/api/heizung")
def api_view(request: Request):
    return {**view(), "types": TYPES, "can": request.state.user["role"]}


@router.get("/api/heizung/config")
def api_cfg(request: Request):
    need(request, "admin")
    return cfg()


@router.post("/api/heizung/config")
async def api_cfg_save(request: Request):
    need(request, "admin")
    try:
        c = cfg_save(await request.json())
    except (ValueError, TypeError) as e:
        raise HTTPException(400, str(e))
    _drop_cache()
    return c


@router.post("/api/heizung/ww/boost")
async def api_ww_boost(request: Request):
    need(request, "user")
    d = await request.json()
    try:
        j = http_json(ww_url("boost_trigger?on=" + ("1" if d.get("on") else "0")))
    except Exception as e:
        raise HTTPException(502, str(e))
    _drop_cache()
    return j


@router.post("/api/heizung/ww/cfg")
async def api_ww_cfg(request: Request):
    """Nur geaenderte Schluessel schicken; das Script prueft noch einmal und meldet abgelehnte in 'bad'."""
    need(request, "admin")
    d = await request.json()
    par = {}
    for k, v in (d or {}).items():
        rng = WW_KEYS.get(k)
        if k == "ov":
            par["ov"] = str(float(v))
        elif rng == "t":
            if not (isinstance(v, str) and len(v) == 5 and v[2] == ":"):
                raise HTTPException(400, f"{k}: Zeit als HH:MM")
            par[k] = v
        elif rng:
            x = float(v)
            if not rng[0] <= x <= rng[1]:
                raise HTTPException(400, f"{k}: erlaubt {rng[0]}–{rng[1]}")
            par[k] = str(int(x)) if x == int(x) else str(x)
        else:
            raise HTTPException(400, f"Unbekannte Einstellung {k}")
    if not par:
        raise HTTPException(400, "Nichts zu speichern")
    try:
        j = http_json(ww_url("cfg?" + urllib.parse.urlencode(par)))
    except Exception as e:
        raise HTTPException(502, str(e))
    _drop_cache()
    return j


@router.post("/api/heizung/zirk")
async def api_zirk(request: Request):
    need(request, "user")
    d = await request.json()
    z = cfg()["zirk"]
    try:
        gen2.rpc(ip_of(z), "Switch.Set", {"id": 0, "on": bool(d.get("on"))}, timeout=4)
    except Exception as e:
        raise HTTPException(502, str(e))
    _drop_cache()
    return {"ok": True}


@router.post("/api/heizung/room/{rid}/cmd")
async def api_room_cmd(rid: str, request: Request):
    need(request, "user")
    r = room(rid)
    cmd = (await request.json()).get("cmd")
    if cmd not in CMDS:
        raise HTTPException(400, "Unbekannter Befehl")
    try:
        kvs_set(ip_of(r), "hl_cmd", cmd)
    except Exception as e:
        raise HTTPException(502, str(e))
    _drop_cache()
    return {"ok": True}


@router.post("/api/heizung/room/{rid}/cfg")
async def api_room_cfg(rid: str, request: Request):
    """Einstellungen eines Heizers aendern: aktuellen hl_cfg lesen, geaenderte Werte einsetzen, zurueckschreiben."""
    need(request, "user")
    r = room(rid)
    d = await request.json()
    patch = {}
    for k, v in (d or {}).items():
        rng = ROOM_KEYS.get(k)
        if rng == "t":
            if not (isinstance(v, str) and len(v) == 5 and v[2] == ":" and "00:00" <= v <= "23:59"):
                raise HTTPException(400, f"{k}: Zeit als HH:MM")
            patch[k] = v
        elif rng == "b":
            patch[k] = bool(v)
        elif rng:
            x = float(v)
            if not rng[0] <= x <= rng[1]:
                raise HTTPException(400, f"{k}: erlaubt {rng[0]}–{rng[1]}")
            patch[k] = int(round(x)) if k in ("boost", "max") else round(x * 2) / 2
        else:
            raise HTTPException(400, f"Unbekannte Einstellung {k}")
    if "max" in patch and r["typ"] != "kv":
        raise HTTPException(400, "Stufe gibt es nur beim Konvektor")
    ip = ip_of(r)
    try:
        cur = kvs_get(ip, "hl_cfg")
        n = {**(cur if isinstance(cur, dict) else {}), **patch}
        kvs_set(ip, "hl_cfg", json.dumps(n, separators=(",", ":")))
    except Exception as e:
        raise HTTPException(502, str(e))
    _drop_cache()
    return {"cfg": n}
