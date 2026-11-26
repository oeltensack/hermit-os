# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 D.Koch – Pagurus OS
"""Home-Assistant-Kompatibilitaet fuer das Shelly Wall Display.

Das Display oeffnet unter Einstellungen -> Home Assistant nur Adressen, die wie ein Home Assistant antworten
(sonst „keine gueltige HA-Instanz“). Es prueft dazu die Anmelde-Schnittstelle (/auth/login_flow) und ein paar
Basisadressen. Diese Datei beantwortet genau diese Pruefungen nach der oeffentlich dokumentierten
Home-Assistant-API – nur fuer Geraete, die in der Cloud an eine Wandtafel gebunden sind. Die ausgegebenen
„Tokens“ gelten fuer nichts; die Tafel selbst braucht keine Anmeldung (Zuordnung ueber die IP).
Danach laedt das Display die eingetragene Adresse, und die Cloud leitet es auf seine Tafel (/wd).
"""
import json
import secrets
import time
from collections import deque

from fastapi import APIRouter, HTTPException, Request, WebSocket
from fastapi.responses import HTMLResponse, RedirectResponse, Response

import panel

HA_VERSION = "2026.11.0"
router = APIRouter()
# Pfade unter /api/, die ohne Cloud-Anmeldung erreichbar sein muessen (nur fuer gebundene Displays)
PATHS = {"/api", "/api/", "/api/config", "/api/discovery_info"}
LOG = deque(maxlen=40)          # letzte Pruefungen durch Displays (Einstellungen -> Wandtafeln)


def watched(path):
    return path in PATHS or path.startswith("/auth/") or path in ("/manifest.json", "/api/websocket") \
        or path.startswith("/lovelace")


def note(ip, method, path, status):
    LOG.appendleft({"ts": time.time(), "ip": ip, "m": method, "p": path, "s": status,
                    "bound": bool(panel.by_ip(ip))})


def bound(request):
    ip = request.client.host if request.client else None
    if not panel.by_ip(ip):
        raise HTTPException(404, "Not Found")


def _json(data):
    """Kompaktes JSON wie Home Assistant; die Laenge setzt Starlette passend zum Inhalt (Content-Length muss stimmen,
    der Client des Displays wartet sonst auf fehlende Bytes und meldet „keine gueltige HA-Instanz“)."""
    body = json.dumps(data, separators=(",", ":"), ensure_ascii=False).encode()
    return Response(content=body, media_type="application/json", headers={"Cache-Control": "no-store"})


def _config(request):
    from core import setting
    loc = setting("location", {}) or {}
    try:
        lat, lon = float(loc.get("lat") or 0), float(loc.get("lon") or 0)
    except (TypeError, ValueError):
        lat = lon = 0.0
    return {"components": ["api", "auth", "config", "frontend", "http", "lovelace", "websocket_api"],
            "config_dir": "/config", "elevation": 0, "latitude": lat, "longitude": lon, "location_name": "Pagurus", "time_zone": "Europe/Berlin",
            "unit_system": {"length": "km", "mass": "g", "temperature": "°C", "volume": "L", "pressure": "Pa",
                            "wind_speed": "m/s", "accumulated_precipitation": "mm"},
            "version": HA_VERSION, "state": "RUNNING", "external_url": None,
            "internal_url": str(request.base_url).rstrip("/"), "currency": "EUR", "country": "DE", "language": "de",
            "safe_mode": False, "recovery_mode": False, "allowlist_external_dirs": [], "allowlist_external_urls": []}


@router.get("/manifest.json")
def manifest(request: Request):
    bound(request)
    # Felder wie beim Manifest eines Home Assistant – die App des Displays erkennt den Server daran
    # (u. a. related_applications). Nur die Symbole sind die der Pagurus Cloud.
    return _json({"background_color": "#FFFFFF", "description": "Home automation platform that puts local control and "
                  "privacy first.", "dir": "ltr", "display": "standalone",
                  "icons": [{"purpose": "any", "sizes": "192x192", "src": "/icon-192.png", "type": "image/png"},
                            {"purpose": "maskable", "sizes": "192x192", "src": "/icon-192.png", "type": "image/png"}],
                  "id": "/?homescreen=1", "lang": "en-US", "name": "Home Assistant", "prefer_related_applications": True,
                  "related_applications": [{"id": "io.homeassistant.companion.android", "platform": "play"}],
                  "short_name": "Home Assistant", "start_url": "/?homescreen=1", "theme_color": "#03A9F4"})


@router.get("/api")
@router.get("/api/")
def api_root(request: Request):
    bound(request)
    return _json({"message": "API running."})


@router.get("/api/config")
def api_config(request: Request):
    bound(request)
    return _json(_config(request))


@router.get("/api/discovery_info")
def discovery(request: Request):
    bound(request)
    return _json({"base_url": str(request.base_url).rstrip("/"), "installation_type": "Home Assistant Container",
                  "requires_api_password": False, "uuid": "pagurus-cloud", "version": HA_VERSION,
                  "location_name": "Pagurus"})


@router.get("/auth/providers")
def providers(request: Request):
    bound(request)
    return _json({"providers": [{"name": "Home Assistant Local", "id": None, "type": "homeassistant"}],
                  "preselect_remember_me": True})


@router.post("/auth/login_flow")
def login_flow(request: Request):
    bound(request)
    return _json({"type": "form", "flow_id": secrets.token_hex(16), "handler": ["homeassistant", None],
                  "data_schema": [{"type": "string", "name": "username", "required": True},
                                  {"type": "string", "name": "password", "required": True}],
                  "errors": {}, "description_placeholders": None, "last_step": None, "preview": None,
                  "step_id": "init"})


@router.post("/auth/login_flow/{flow_id}")
def login_flow_step(flow_id: str, request: Request):
    bound(request)                                   # Name/Passwort werden nicht gebraucht und nicht gespeichert
    return _json({"version": 1, "type": "create_entry", "flow_id": flow_id, "handler": ["homeassistant", None],
                  "title": "Pagurus", "result": secrets.token_hex(16), "description": None,
                  "description_placeholders": None})


@router.post("/auth/token")
def token(request: Request):
    bound(request)
    return _json({"access_token": "pagurus-" + secrets.token_hex(16), "expires_in": 86400 * 365,
                  "refresh_token": "pagurus-" + secrets.token_hex(16), "token_type": "Bearer"})


@router.get("/auth/authorize")
def authorize(request: Request, redirect_uri: str = "", state: str = "", response_type: str = ""):
    """OAuth-Variante (App-Anmeldung): Seite mit einem Knopf, der mit einem Code zurueckspringt."""
    bound(request)
    if response_type != "code" or not redirect_uri or not redirect_uri.startswith(("homeassistant://", "http")):
        return HTMLResponse('<meta http-equiv="refresh" content="0;url=/">')
    target = redirect_uri + ("&" if "?" in redirect_uri else "?") + "code=" + secrets.token_hex(16)
    if state:
        from urllib.parse import quote
        target += "&state=" + quote(state)
    from html import escape
    return HTMLResponse(f'<!DOCTYPE html><meta name="viewport" content="width=device-width"><body style="background:#0f1419;'
                        f'color:#e6e9ec;font-family:sans-serif;text-align:center;padding-top:30vh">'
                        f'<p>Pagurus Wandtafel</p><a href="{escape(target)}" style="display:inline-block;padding:14px 30px;'
                        f'background:#2f9be0;color:#fff;border-radius:8px;text-decoration:none">Weiter</a>')


@router.get("/lovelace{rest:path}")
def lovelace(rest: str, request: Request):
    """Nach der Anmeldung springen manche Displays auf ein Dashboard – dort steht die Tafel."""
    bound(request)
    return RedirectResponse("/wd", status_code=302)


@router.websocket("/api/websocket")
async def websocket(ws: WebSocket):
    ip = ws.client.host if ws.client else None
    note(ip, "WS", "/api/websocket", 101 if panel.by_ip(ip) else 403)
    if not panel.by_ip(ip):
        await ws.close(code=1008)
        return
    await ws.accept()
    await ws.send_json({"type": "auth_required", "ha_version": HA_VERSION})
    ok = False
    try:
        while True:
            m = await ws.receive_json()
            t = m.get("type")
            if t == "auth":
                ok = True
                await ws.send_json({"type": "auth_ok", "ha_version": HA_VERSION})
            elif not ok:
                await ws.send_json({"type": "auth_invalid", "message": "Not authenticated"})
            elif t == "ping":
                await ws.send_json({"id": m.get("id"), "type": "pong"})
            elif m.get("id") is not None:
                result = {"get_states": [], "get_services": {}, "get_panels": {}}.get(t)
                if t == "get_config":
                    result = _config(ws)
                await ws.send_json({"id": m["id"], "type": "result", "success": True, "result": result})
    except Exception:
        pass
