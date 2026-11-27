# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 D.Koch – Pagurus OS
"""Scripte der Shelly Gen2+ zentral verwalten: Uebersicht mit Version und Zustand, Code ansehen und bearbeiten,
auf ein Geraet uebertragen, starten/stoppen, Autostart. Vor jedem Ueberschreiben sichert die Cloud den alten Code
(Verlauf je Geraet und Script). Dazu eine Bibliothek, um ein Script einmal zu pflegen und auf mehrere Geraete zu
verteilen. Alles ueber die vorhandene MQTT-RPC-Verbindung (BUS.rpc), Pagurus-OS-Geraete haben keine Scripte.
"""
import json
import re
import threading
import time
from concurrent.futures import ThreadPoolExecutor

from fastapi import APIRouter, HTTPException, Request

from core import q, qid

router = APIRouter()
BUS = None
POOL = ThreadPoolExecutor(max_workers=6)
CHUNK = 1024                        # PutCode/GetCode in Stuecken (Shelly verarbeitet grosse Nachrichten schlecht)
KEEP_HIST = 20                      # Sicherungen je Geraet und Script
_cache = {"t": 0, "v": None}
_lock = threading.Lock()


def need_admin(request):
    if request.state.user["role"] != "admin":
        raise HTTPException(403, "Nur für Administratoren")


def init_db():
    q("""CREATE TABLE IF NOT EXISTS script_hist(id INTEGER PRIMARY KEY AUTOINCREMENT, dev TEXT, sid INTEGER,
         name TEXT, ts REAL, code TEXT, note TEXT)""")
    q("""CREATE TABLE IF NOT EXISTS script_lib(id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT, code TEXT,
         ts REAL, note TEXT)""")


# ======================= Version aus dem Kopf des Scripts =======================
_VER = re.compile(r"(?<![\w.])[Vv](\d+(?:\.\d+){0,2})(?![\w.])")


def head_info(code):
    """Titel und Version aus den ersten Kommentarzeilen, z. B. '// --- HEIZLÜFTER 3 LOGIK v11.4 ---'."""
    title, ver = "", ""
    for line in (code or "").splitlines()[:6]:
        t = line.strip()
        if not t.startswith("//"):
            if t:
                break
            continue
        t = t.lstrip("/").strip().strip("-").strip()
        m = _VER.search(t)
        if not title and t:
            title = t[:80]
        if m and not ver:
            ver = m.group(1)
        if title and ver:
            break
    return title, ver


# ======================= Zugriff =======================
def rpc(dev, method, params=None, timeout=8):
    return BUS.rpc(dev, method, params, timeout)


def candidates():
    """Gen2+-Geraete mit Original-Firmware (nur Hauptgeraet, keine Kanaele, keine BLU, kein Pagurus OS)."""
    out = []
    for r in q("SELECT id, name, ip, model, info, online, gen FROM devices WHERE hidden=0 AND COALESCE(gen,0)>=2 "
               "AND id NOT LIKE '%~%' AND id NOT LIKE 'blu-%'"):
        info = json.loads(r["info"] or "{}")
        if str(info.get("fw_id", "")).startswith(("hermit", "pagurus")) or \
                str(r["model"] or "").upper().startswith(("HERMIT", "PAGURUS")):
            continue
        out.append(r)
    return out


def get_code(dev, sid):
    code, off = "", 0
    while True:
        r = rpc(dev, "Script.GetCode", {"id": sid, "offset": off, "len": CHUNK}) or {}
        part = r.get("data") or ""
        code += part
        off += len(part.encode("utf-8")) if part else 0
        if not part or not r.get("left"):
            return code


def _scan_dev(r):
    d = {"dev": r["id"], "name": r["name"] or r["id"], "ip": r["ip"], "model": r["model"], "online": bool(r["online"]),
         "scripts": []}
    if not r["online"]:
        d["err"] = "offline"
        return d
    try:
        lst = (rpc(r["id"], "Script.List") or {}).get("scripts") or []
    except Exception as e:
        d["err"] = str(e)
        return d
    for s in lst:
        x = {"id": s.get("id"), "name": s.get("name") or "", "enable": bool(s.get("enable")),
             "running": bool(s.get("running"))}
        try:
            head = (rpc(r["id"], "Script.GetCode", {"id": x["id"], "offset": 0, "len": 400}) or {}).get("data") or ""
            x["title"], x["ver"] = head_info(head)
        except Exception:
            x["title"], x["ver"] = "", ""
        if x["running"]:
            try:
                st = rpc(r["id"], "Script.GetStatus", {"id": x["id"]}) or {}
                x["mem_used"], x["mem_free"] = st.get("mem_used"), st.get("mem_free")
                x["errors"] = st.get("errors") or []
            except Exception:
                pass
        d["scripts"].append(x)
    return d


def scan(force=False):
    with _lock:
        if not force and _cache["v"] and time.time() - _cache["t"] < 300:
            return _cache["v"]
    devs = list(POOL.map(_scan_dev, candidates()))
    devs.sort(key=lambda d: (not d["scripts"], d["name"].lower()))
    v = {"ts": time.time(), "devices": devs}
    with _lock:
        _cache.update(t=time.time(), v=v)
    return v


def _forget(dev):
    with _lock:
        if _cache["v"]:
            for d in _cache["v"]["devices"]:
                if d["dev"] == dev:
                    _cache["v"]["devices"][_cache["v"]["devices"].index(d)] = _scan_dev(
                        q("SELECT id, name, ip, model, info, online, gen FROM devices WHERE id=?", (dev,), one=True))
                    break


def backup(dev, sid, name, code, note):
    if not code:
        return
    last = q("SELECT code FROM script_hist WHERE dev=? AND sid=? ORDER BY ts DESC LIMIT 1", (dev, sid), one=True)
    if last and last["code"] == code:
        return
    qid("INSERT INTO script_hist(dev, sid, name, ts, code, note) VALUES(?,?,?,?,?,?)",
        (dev, sid, name, time.time(), code, note))
    q("""DELETE FROM script_hist WHERE dev=? AND sid=? AND id NOT IN
         (SELECT id FROM script_hist WHERE dev=? AND sid=? ORDER BY ts DESC LIMIT ?)""", (dev, sid, dev, sid, KEEP_HIST))


def put(dev, sid, code, start=None, note="Übertragen"):
    """Alten Code sichern, Script stoppen, neuen Code in Stuecken schreiben, danach (wieder) starten und pruefen."""
    lst = (rpc(dev, "Script.List") or {}).get("scripts") or []
    cur = next((s for s in lst if s.get("id") == sid), None)
    if not cur:
        raise RuntimeError(f"Script {sid} gibt es auf dem Gerät nicht")
    was = bool(cur.get("running"))
    try:
        backup(dev, sid, cur.get("name") or "", get_code(dev, sid), "vor: " + note)
    except Exception as e:
        raise RuntimeError(f"Alter Code ließ sich nicht sichern ({e}) – nichts geändert")
    if was:
        rpc(dev, "Script.Stop", {"id": sid})
    pos, first = 0, True
    while first or pos < len(code):
        part = code[pos:pos + CHUNK]
        rpc(dev, "Script.PutCode", {"id": sid, "code": part, "append": not first}, timeout=10)
        first = False
        pos += CHUNK
    run = was if start is None else bool(start)
    res = {"ok": True, "running": False, "errors": [], "was_running": was}
    if run:
        try:
            rpc(dev, "Script.Start", {"id": sid})
        except Exception as e:
            res.update(ok=False, errors=[str(e)])
        time.sleep(3)
        try:
            st = rpc(dev, "Script.GetStatus", {"id": sid}) or {}
            res["running"] = bool(st.get("running"))
            res["errors"] += st.get("errors") or []
            res["mem_free"] = st.get("mem_free")
            if not res["running"] or res["errors"]:
                res["ok"] = False
        except Exception as e:
            res.update(ok=False, errors=res["errors"] + [str(e)])
    backup(dev, sid, cur.get("name") or "", code, note)
    _forget(dev)
    return res


# ======================= API =======================
@router.get("/api/scripts")
async def api_list(request: Request, refresh: int = 0):
    need_admin(request)
    import asyncio
    return await asyncio.to_thread(scan, bool(refresh))


@router.get("/api/scripts/{dev}/{sid}")
async def api_code(dev: str, sid: int, request: Request):
    need_admin(request)
    import asyncio
    try:
        code = await asyncio.to_thread(get_code, dev, sid)
    except Exception as e:
        raise HTTPException(502, str(e))
    title, ver = head_info(code)
    hist = [dict(r) for r in q("SELECT id, ts, note, length(code) AS size FROM script_hist WHERE dev=? AND sid=? "
                               "ORDER BY ts DESC", (dev, sid))]
    return {"code": code, "title": title, "ver": ver, "hist": hist}


@router.post("/api/scripts/{dev}/new")
async def api_new(dev: str, request: Request):
    need_admin(request)
    d = await request.json()
    name = str(d.get("name") or "").strip()[:30] or "script"
    import asyncio

    def go():
        sid = (rpc(dev, "Script.Create", {"name": name}) or {}).get("id")
        if sid is None:
            raise RuntimeError("Gerät hat kein Script angelegt")
        res = {"id": sid}
        if d.get("code"):
            res.update(put(dev, sid, d["code"], d.get("start", True), "Neu angelegt"))
            if d.get("enable", True):
                rpc(dev, "Script.SetConfig", {"id": sid, "config": {"enable": True}})
        _forget(dev)
        return res
    try:
        return await asyncio.to_thread(go)
    except Exception as e:
        raise HTTPException(502, str(e))


@router.post("/api/scripts/{dev}/{sid}")
async def api_put(dev: str, sid: int, request: Request):
    need_admin(request)
    d = await request.json()
    code = d.get("code")
    if not isinstance(code, str) or not code.strip():
        raise HTTPException(400, "Kein Code")
    if len(code.encode("utf-8")) > 60000:
        raise HTTPException(400, "Code zu groß für einen Shelly")
    import asyncio
    try:
        return await asyncio.to_thread(put, dev, sid, code, d.get("start"), str(d.get("note") or "Übertragen")[:80])
    except Exception as e:
        raise HTTPException(502, str(e))


@router.post("/api/scripts/{dev}/{sid}/do")
async def api_do(dev: str, sid: int, request: Request):
    """action: start | stop | enable | disable | rename (name)"""
    need_admin(request)
    d = await request.json()
    a = d.get("action")
    import asyncio

    def go():
        if a == "start":
            rpc(dev, "Script.Start", {"id": sid})
        elif a == "stop":
            rpc(dev, "Script.Stop", {"id": sid})
        elif a in ("enable", "disable"):
            rpc(dev, "Script.SetConfig", {"id": sid, "config": {"enable": a == "enable"}})
        elif a == "rename":
            rpc(dev, "Script.SetConfig", {"id": sid, "config": {"name": str(d.get("name") or "")[:30]}})
        else:
            raise ValueError("Unbekannte Aktion")
        time.sleep(1 if a == "start" else 0)
        _forget(dev)
    try:
        await asyncio.to_thread(go)
    except ValueError as e:
        raise HTTPException(400, str(e))
    except Exception as e:
        raise HTTPException(502, str(e))
    return {"ok": True}


@router.get("/api/scripts-hist/{hid}")
def api_hist(hid: int, request: Request):
    need_admin(request)
    r = q("SELECT * FROM script_hist WHERE id=?", (hid,), one=True)
    if not r:
        raise HTTPException(404, "Nicht gefunden")
    return dict(r)


@router.get("/api/scriptlib")
def api_lib(request: Request):
    need_admin(request)
    out = []
    for r in q("SELECT id, name, ts, note, code FROM script_lib ORDER BY name COLLATE NOCASE"):
        title, ver = head_info(r["code"])
        out.append({"id": r["id"], "name": r["name"], "ts": r["ts"], "note": r["note"], "title": title, "ver": ver,
                    "size": len(r["code"] or "")})
    return out


@router.get("/api/scriptlib/{lid}")
def api_lib_get(lid: int, request: Request):
    need_admin(request)
    r = q("SELECT * FROM script_lib WHERE id=?", (lid,), one=True)
    if not r:
        raise HTTPException(404, "Nicht gefunden")
    return dict(r)


@router.post("/api/scriptlib")
async def api_lib_save(request: Request):
    need_admin(request)
    d = await request.json()
    name = str(d.get("name") or "").strip()[:60]
    code = d.get("code")
    if not name or not isinstance(code, str) or not code.strip():
        raise HTTPException(400, "Name und Code angeben")
    if d.get("id"):
        q("UPDATE script_lib SET name=?, code=?, ts=?, note=? WHERE id=?",
          (name, code, time.time(), str(d.get("note") or "")[:200], int(d["id"])))
        return {"id": int(d["id"])}
    return {"id": qid("INSERT INTO script_lib(name, code, ts, note) VALUES(?,?,?,?)",
                      (name, code, time.time(), str(d.get("note") or "")[:200]))}


@router.delete("/api/scriptlib/{lid}")
def api_lib_del(lid: int, request: Request):
    need_admin(request)
    q("DELETE FROM script_lib WHERE id=?", (lid,))
    return {"ok": True}


def start(bus):
    global BUS
    BUS = bus
    init_db()
