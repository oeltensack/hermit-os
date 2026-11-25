# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 D.Koch – Pagurus OS
"""Fernzugang-Portal: haelt eine PHP-Seite auf einem Webhosting (Ordner portal/ im Repo) aktuell.

Die Cloud baut die Verbindung selbst auf (HTTPS nach aussen), es muss kein Port offen sein.
Sie schickt Geraetezustand und Meldungen und holt dabei die im Portal ausgeloesten Befehle ab.
Takt: alle 2 s, solange jemand das Portal offen hat. Sonst wartet jede Anfrage im Portal bis zu 20 s
auf einen Befehl oder einen Besucher (Long-Polling) – Befehle kommen so ohne Verzoegerung an.
"""
import hashlib
import json
import os
import threading
import time
import urllib.error
import urllib.request

from core import q, setting

PORT = int(os.environ.get("PORT", "8080"))
FAST, WAIT, RETRY, FULL_EVERY = 2, 20, 15, 60
STATE = {"ok": None, "err": "", "last": 0, "active": False}


def cfg():
    c = setting("portal", {}) or {}
    url = (c.get("url") or os.environ.get("PORTAL_URL", "")).strip()
    tok = (c.get("token") or os.environ.get("PORTAL_TOKEN", "")).strip()
    if url and not url.endswith(".php"):
        url = url.rstrip("/") + "/api.php"
    return url, tok


def _compact(d):
    s = d.get("status") or {}
    t = s.get("temperature")
    out = {"id": d["id"], "name": d["name"] or d["id"], "room": d["room"], "model": d.get("model") or "",
           "online": d["online"], "last_seen": d.get("last_seen"), "comp": d.get("comp") or "",
           "output": s.get("output"), "apower": s.get("apower"), "wh": d.get("today_wh"),
           "tC": t.get("tC") if isinstance(t, dict) else None, "meter_only": bool(s.get("meter_only"))}
    if (d.get("comp") or "").startswith("cover:"):
        out.update(state=s.get("state"), pos=s.get("current_pos"), output=None)
    if d.get("comp") == "sensor:0":
        h = s.get("humidity")
        out.update(lux=s.get("lux"), battery=s.get("battery"), rh=h.get("rh") if isinstance(h, dict) else None,
                   contact=s.get("contact"), motion=s.get("motion"), apower=None, wh=None)
    if d.get("appliance"):
        out["appliance"] = {"label": d["appliance"]["label"], "state": d["appliance"]["state"]}
    if d.get("fridge") and d["fridge"].get("alarm"):
        out["fridge_alarm"] = True
    return {k: v for k, v in out.items() if v is not None or k in ("output",)}


def _alerts():
    rows = q("""SELECT a.id, a.ts, a.dev, a.kind, a.text, a.ack, COALESCE(NULLIF(d.name,''), a.dev) AS name
                FROM alerts a LEFT JOIN devices d ON d.id=a.dev ORDER BY a.id DESC LIMIT 100""")
    return [dict(r) for r in rows]


def _run(c):
    import engine
    from bus import BUS
    dev, act = c.get("dev", ""), c.get("act", "")
    try:
        if act == "scene":
            errs = engine.run_scene(int(dev.partition(":")[2] or 0))
        elif act == "stop":
            BUS.switch(dev, cover="stop")
            errs = []
        elif act == "pos":
            errs = engine.do_switch(f"dev:{dev}", f"pos:{int(c.get('pos') or 0)}")
        else:
            mode = {"open": "on", "close": "off"}.get(act, act)
            if mode not in ("on", "off", "toggle"):
                raise RuntimeError("Unbekannte Aktion")
            errs = engine.do_switch(f"dev:{dev}", mode)
        return {"id": c["id"], "error": "; ".join(errs)}
    except Exception as e:
        return {"id": c["id"], "error": str(e)}


def _relay_one(url, tok, r):
    """Vom Portal durchgereichte Anfrage (App unterwegs) an die eigene Schnittstelle stellen."""
    import base64
    import hermit_cloud
    path = str(r.get("path") or "/")
    out = {"id": r["id"], "status": 502, "ctype": "text/plain", "body": ""}
    if not path.startswith("/") or path.startswith("//"):
        out["body"] = base64.b64encode(b"Pfad ungueltig").decode()
    else:
        body = r.get("body")
        req = urllib.request.Request(f"http://127.0.0.1:{PORT}{path}", method=r.get("method") or "GET",
                                     data=body.encode() if body else None,
                                     headers={"Content-Type": r.get("ctype") or "application/json",
                                              "X-Pagurus-Internal": hermit_cloud.INTERNAL_KEY,
                                              "X-Pagurus-Role": r.get("role") or "viewer"})
        try:
            with urllib.request.urlopen(req, timeout=60) as resp:
                out.update(status=resp.status, ctype=resp.headers.get("Content-Type", ""),
                           body=base64.b64encode(resp.read()).decode())
        except urllib.error.HTTPError as e:
            out.update(status=e.code, ctype=e.headers.get("Content-Type", ""), body=base64.b64encode(e.read()).decode())
        except Exception as e:
            out["body"] = base64.b64encode(str(e).encode()).decode()
    try:
        rq = urllib.request.Request(url + "?a=relay_result", data=json.dumps(out).encode(), method="POST",
                                    headers={"Content-Type": "application/json", "Authorization": "Bearer " + tok,
                                             "X-Pagurus-Token": tok, "User-Agent": "PagurusCloud"})
        urllib.request.urlopen(rq, timeout=30).read()
    except Exception as e:
        print("Portal: Antwort nicht zustellbar:", e, flush=True)


def _post(url, tok, body, wait=0):
    req = urllib.request.Request(url + f"?a=sync&wait={wait}", data=json.dumps(body).encode(), method="POST",
                                 headers={"Content-Type": "application/json", "Authorization": "Bearer " + tok,
                                          "X-Pagurus-Token": tok, "User-Agent": "PagurusCloud"})
    try:
        with urllib.request.urlopen(req, timeout=wait + 15) as r:
            return json.loads(r.read() or b"{}")
    except urllib.error.HTTPError as e:
        try:
            msg = json.loads(e.read()).get("error")
        except Exception:
            msg = None
        raise RuntimeError(msg or f"HTTP {e.code}")


def loop(snapshot):
    last_dev_hash = last_al_hash = None
    last_full = 0
    results = []
    while True:
        url, tok = cfg()
        if not url or not tok:
            STATE.update(ok=None, err="", active=False)
            time.sleep(10)
            continue
        try:
            body = {"v": 1, "cloud": {"ver": _version()}}
            devs = [_compact(d) for d in snapshot()]
            scn = [{"id": r["id"], "name": r["name"]} for r in q("SELECT id, name FROM scenes ORDER BY sort, name COLLATE NOCASE")]
            h = hashlib.sha1(json.dumps([devs, scn], sort_keys=True, default=str).encode()).hexdigest()
            if h != last_dev_hash or time.time() - last_full > FULL_EVERY:
                body["devices"], body["scenes"] = devs, scn
            al = _alerts()
            ah = hashlib.sha1(json.dumps([(a["id"], a["ack"]) for a in al]).encode()).hexdigest()
            if ah != last_al_hash:
                body["alerts"], body["alerts_reset"] = al, True
            if results:
                body["results"] = results
            r = _post(url, tok, body, 0 if STATE["active"] else WAIT)
            results = []
            if "devices" in body:
                last_dev_hash, last_full = h, time.time()
            if "alerts" in body:
                last_al_hash = ah
            if r.get("need_full"):
                last_dev_hash = last_al_hash = None
            STATE.update(ok=True, err="", last=time.time(), active=bool(r.get("active")))
            for rl in r.get("relay") or []:
                threading.Thread(target=_relay_one, args=(url, tok, rl), daemon=True).start()
            cmds = r.get("commands") or []
            for c in cmds:
                results.append(_run(c))
            if cmds or r.get("relay"):
                last_dev_hash = None              # neuen Zustand gleich mitschicken
                time.sleep(0.7)
                continue
        except Exception as e:
            STATE.update(ok=False, err=str(e)[:200])
            time.sleep(RETRY)
            continue
        if STATE["active"]:
            time.sleep(FAST)


def _version():
    try:
        with open(os.path.join(os.path.dirname(__file__), "version.json")) as f:
            return json.load(f).get("version")
    except Exception:
        return None


def start(snapshot):
    threading.Thread(target=loop, args=(snapshot,), daemon=True).start()
