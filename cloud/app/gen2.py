# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 D.Koch – Hermit OS
"""Original-Shelly Gen2/Gen3/Gen4 neben der Shelly Cloud anbinden.

Gen2+ kann Shelly Cloud und MQTT gleichzeitig. Die Hermit Cloud traegt sich per lokaler RPC
(HTTP, bei Passwort Digest SHA-256) als MQTT-Server ein. App, Szenen und Cloud von Shelly laufen weiter.

Dazu Gen1-Batteriefuehler mit Original-Firmware (Door/Window 1/2, H&T): MQTT wird ueber /settings
eingeschaltet (bei Gen1 schaltet das die Shelly Cloud ab). Sie sind nur erreichbar, solange sie wach sind.
"""
import base64
import concurrent.futures as cf
import hashlib
import ipaddress
import json
import os
import secrets
import urllib.error
import urllib.parse
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


GEN1_SENSORS = {"SHDW-2": "Door/Window 2", "SHDW-1": "Door/Window", "SHHT-1": "H&T"}


def gen1_get(ip, path, password="", timeout=6):
    """GET bei Gen1 (Basic-Auth). password: 'passwort' (Benutzer admin) oder 'benutzer:passwort'."""
    req = urllib.request.Request(f"http://{ip}{path}")
    if password:
        up = password if ":" in password else f"admin:{password}"
        req.add_header("Authorization", "Basic " + base64.b64encode(up.encode()).decode())
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return json.loads(r.read() or b"{}")
    except urllib.error.HTTPError as e:
        if e.code == 401:
            raise RuntimeError("Passwort falsch" if password else "Gerät hat ein Passwort – bitte angeben")
        raise RuntimeError(f"HTTP {e.code}")


def probe(ip, timeout=1.5):
    try:
        with urllib.request.urlopen(f"http://{ip}/shelly", timeout=timeout) as r:
            d = json.loads(r.read())
    except Exception:
        return None
    gen = d.get("gen")
    if not gen and d.get("type") in GEN1_SENSORS:
        did = None
        try:
            did = (gen1_get(ip, "/settings", timeout=3).get("device") or {}).get("hostname")
        except Exception:
            pass                                 # mit Passwort: ID erst beim Verbinden
        return {"ip": str(ip), "id": did, "model": d["type"], "app": GEN1_SENSORS[d["type"]], "gen": 1,
                "name": None, "auth": bool(d.get("auth")), "hermit": False, "sensor": True,
                "known": bool(did and q("SELECT 1 FROM devices WHERE id=?", (did,), one=True))}
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


def _broker(broker):
    broker = broker or os.environ.get("PUBLIC_BROKER", "")
    if not broker:
        raise RuntimeError("Adresse des Brokers fehlt (PUBLIC_BROKER in .env oder hier eintragen)")
    return broker if ":" in broker else broker + ":1883"


def connect_gen1(ip, password="", broker="", force=False):
    """Gen1-Batteriefuehler: MQTT auf die Hermit Cloud stellen (Shelly Cloud geht dabei aus)."""
    st = gen1_get(ip, "/settings", password)
    dev = st.get("device") or {}
    if dev.get("type") not in GEN1_SENSORS:
        raise RuntimeError(f"Kein unterstützter Gen1-Fühler ({dev.get('type')})")
    broker = _broker(broker)
    cur = st.get("mqtt") or {}
    if cur.get("enable") and cur.get("server") and cur.get("server") != broker and not force:
        raise RuntimeError(f"MQTT ist schon auf {cur.get('server')} eingestellt – mit „trotzdem umstellen“ ersetzen.")
    did = cur.get("id") or dev.get("hostname")
    params = {"mqtt_enable": "true", "mqtt_server": broker, "mqtt_id": did, "mqtt_clean_session": "true"}
    if os.environ.get("MQTT_USER"):
        params["mqtt_user"] = os.environ["MQTT_USER"]
        params["mqtt_pass"] = os.environ.get("MQTT_PASS", "")
    gen1_get(ip, "/settings?" + urllib.parse.urlencode(params), password)
    try:
        gen1_get(ip, "/reboot", password, timeout=3)
    except Exception:
        pass
    q("INSERT OR IGNORE INTO devices(id, first_seen) VALUES(?, strftime('%s','now'))", (did,))
    q("""UPDATE devices SET ip=?, model=?, gen=1, comp='sensor:0', mac=COALESCE(mac, ?),
         name=COALESCE(NULLIF(name,''), ?) WHERE id=?""",
      (ip, dev["type"], dev.get("mac"), st.get("name") or None, did))
    return {"id": did, "model": dev["type"], "restarted": True}


def connect(ip, password="", broker="", force=False):
    """MQTT am Geraet auf die Hermit Cloud stellen und neu starten. Liefert Info-Dict."""
    try:
        with urllib.request.urlopen(f"http://{ip}/shelly", timeout=4) as r:
            sh = json.loads(r.read())
    except Exception:
        sh = {}
    if not sh.get("gen") and sh.get("type") in GEN1_SENSORS:
        return connect_gen1(ip, password, broker, force)
    info = rpc(ip, "Shelly.GetDeviceInfo", password=password)
    if int(info.get("gen") or 0) < 2:
        raise RuntimeError("Kein Gen2+-Gerät")
    did = info["id"]
    broker = _broker(broker)
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
