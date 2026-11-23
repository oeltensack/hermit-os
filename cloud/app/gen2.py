# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 D.Koch – Hermit OS
"""Original-Shelly Gen2/Gen3/Gen4 neben der Shelly Cloud anbinden.

Gen2+ kann Shelly Cloud und MQTT gleichzeitig. Die Hermit Cloud traegt sich per lokaler RPC
(HTTP, bei Passwort Digest SHA-256) als MQTT-Server ein. App, Szenen und Cloud von Shelly laufen weiter.
"""
import concurrent.futures as cf
import hashlib
import ipaddress
import json
import os
import secrets
import urllib.error
import urllib.request

from core import q


def _sha(s):
    return hashlib.sha256(s.encode()).hexdigest()


def _digest(header, password, uri="/rpc", method="POST"):
    parts = {}
    for item in header[len("Digest "):].split(","):
        k, _, v = item.strip().partition("=")
        parts[k] = v.strip('"')
    realm, nonce = parts.get("realm", ""), parts.get("nonce", "")
    cnonce, nc = secrets.token_hex(8), "00000001"
    ha1 = _sha(f"admin:{realm}:{password}")
    ha2 = _sha(f"{method}:{uri}")
    resp = _sha(f"{ha1}:{nonce}:{nc}:{cnonce}:auth:{ha2}")
    return (f'Digest username="admin", realm="{realm}", nonce="{nonce}", uri="{uri}", algorithm=SHA-256, '
            f'response="{resp}", qop=auth, nc={nc}, cnonce="{cnonce}"')


def rpc(ip, method, params=None, password="", timeout=6):
    body = json.dumps({"id": 1, "method": method, "params": params or {}}).encode()
    url = f"http://{ip}/rpc"

    def send(auth=None):
        req = urllib.request.Request(url, data=body, headers={"Content-Type": "application/json"})
        if auth:
            req.add_header("Authorization", auth)
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return json.loads(r.read())

    try:
        d = send()
    except urllib.error.HTTPError as e:
        if e.code != 401:
            raise RuntimeError(f"HTTP {e.code}")
        if not password:
            raise RuntimeError("Gerät hat ein Passwort – bitte angeben")
        try:
            d = send(_digest(e.headers.get("WWW-Authenticate", ""), password))
        except urllib.error.HTTPError as e2:
            raise RuntimeError("Passwort falsch" if e2.code == 401 else f"HTTP {e2.code}")
    if "error" in d:
        raise RuntimeError(d["error"].get("message") or str(d["error"]))
    return d.get("result")


def probe(ip, timeout=1.5):
    try:
        with urllib.request.urlopen(f"http://{ip}/shelly", timeout=timeout) as r:
            d = json.loads(r.read())
    except Exception:
        return None
    gen = d.get("gen")
    if not gen or int(gen) < 2:
        return None
    fw = str(d.get("fw_id", ""))
    return {"ip": str(ip), "id": d.get("id"), "model": d.get("model"), "app": d.get("app"), "gen": gen,
            "name": d.get("name"), "auth": bool(d.get("auth_en")), "hermit": fw.startswith("hermit-os"),
            "known": bool(q("SELECT 1 FROM devices WHERE id=?", (d.get("id"),), one=True))}


def scan(net):
    """'192.168.0.0/24' oder '192.168.0.10-80' -> gefundene Gen2+-Geraete."""
    net = (net or "").strip()
    ips = []
    if "/" in net:
        n = ipaddress.ip_network(net, strict=False)
        if n.num_addresses > 1024:
            raise ValueError("Höchstens /22 (1024 Adressen)")
        ips = list(n.hosts())
    elif "-" in net:
        a, b = net.split("-", 1)
        base = ipaddress.ip_address(a.strip())
        end = ipaddress.ip_address(b.strip()) if "." in b else ipaddress.ip_address(a.rsplit(".", 1)[0] + "." + b.strip())
        if int(end) - int(base) > 1024 or end < base:
            raise ValueError("Bereich ungültig")
        ips = [ipaddress.ip_address(i) for i in range(int(base), int(end) + 1)]
    else:
        ips = [ipaddress.ip_address(net)]
    out = []
    with cf.ThreadPoolExecutor(64) as ex:
        for r in ex.map(probe, ips):
            if r:
                out.append(r)
    return sorted(out, key=lambda x: int(ipaddress.ip_address(x["ip"])))


def connect(ip, password="", broker="", force=False):
    """MQTT am Geraet auf die Hermit Cloud stellen und neu starten. Liefert Info-Dict."""
    info = rpc(ip, "Shelly.GetDeviceInfo", password=password)
    if int(info.get("gen") or 0) < 2:
        raise RuntimeError("Kein Gen2+-Gerät")
    did = info["id"]
    broker = broker or os.environ.get("PUBLIC_BROKER", "")
    if not broker:
        raise RuntimeError("Adresse des Brokers fehlt (PUBLIC_BROKER in .env oder hier eintragen)")
    if ":" not in broker:
        broker += ":1883"
    cur = rpc(ip, "MQTT.GetConfig", password=password) or {}
    if cur.get("enable") and cur.get("server") and cur.get("server") != broker and not force:
        raise RuntimeError(f"MQTT ist schon auf {cur.get('server')} eingestellt – ein Gerät kann nur einen "
                           "MQTT-Server. Mit „trotzdem umstellen“ ersetzen.")
    cfg = {"enable": True, "server": broker, "client_id": did, "topic_prefix": did,
           "rpc_ntf": True, "status_ntf": True}
    if os.environ.get("MQTT_USER"):
        cfg["user"] = os.environ["MQTT_USER"]
        cfg["pass"] = os.environ.get("MQTT_PASS", "")
    res = rpc(ip, "MQTT.SetConfig", {"config": cfg}, password=password) or {}
    if res.get("restart_required"):
        try:
            rpc(ip, "Shelly.Reboot", password=password, timeout=3)
        except Exception:
            pass
    q("INSERT OR IGNORE INTO devices(id, first_seen) VALUES(?, strftime('%s','now'))", (did,))
    q("UPDATE devices SET ip=?, model=COALESCE(model, ?), gen=?, name=COALESCE(NULLIF(name,''), ?) WHERE id=?",
      (ip, info.get("model"), info.get("gen"), info.get("name"), did))
    return {"id": did, "model": info.get("model"), "restarted": bool(res.get("restart_required"))}
