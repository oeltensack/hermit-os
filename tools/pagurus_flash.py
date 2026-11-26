#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 D.Koch – Pagurus OS
"""
pagurus_flash.py – Pagurus OS ueber WLAN auf einen Shelly 1 / 1PM / Plug / Plug S / Duo / Dimmer 2 (Gen1) schreiben.

Erkennt automatisch, was auf dem Geraet laeuft:
  * Original-Shelly-Firmware (Mongoose OS)  -> mg2hermit: angepasstes mg2tasmota laedt Pagurus OS
  * Tasmota                                  -> Pagurus Loader -> Pagurus OS
  * Pagurus Loader                            -> Pagurus OS
  * Pagurus OS                                -> Firmware-Update

Der PC stellt die Dateien ueber einen eigenen kleinen Webserver bereit – es wird kein Internet
und kein oeffentliches Repo gebraucht (ausser einmalig fuer das mg2tasmota-Paket, siehe --mg2x-zip).

Aufruf:
  python3 tools/pagurus_flash.py 192.168.5.153
  python3 tools/pagurus_flash.py 192.168.5.153 --addon
  python3 tools/pagurus_flash.py 192.168.5.153 --user admin --password geheim
  python3 tools/pagurus_flash.py 192.168.5.153 --ssid MeinWLAN --wifi-pass geheim   (WLAN sicher übernehmen)

Nur Python 3.8+ Standardbibliothek. Der PC muss im selben Netz wie der Shelly sein;
die Windows-Firewall muss eingehende Verbindungen auf dem Port (Standard 8266) erlauben.
"""
import argparse, base64, concurrent.futures, gzip, hashlib, http.server, io, ipaddress, json, os, pathlib, socket, sys, threading, time
import urllib.error, urllib.parse, urllib.request, uuid, zipfile

if getattr(sys, "frozen", False):            # als EXE (PyInstaller): bin/ liegt neben der EXE
    ROOT = pathlib.Path(sys.executable).resolve().parent
    CACHE = ROOT / ".cache"
else:
    ROOT = pathlib.Path(__file__).resolve().parent.parent
    CACHE = pathlib.Path(__file__).resolve().parent / ".cache"
BIN_DIR = ROOT / "bin"
MG2X_URL = "http://ota.tasmota.com/tasmota/shelly/mg2tasmota-{model}.zip"
MG2X_OLD = b"https://ota.tasmota.com/tasmota/release/tasmota.bin"   # in mg2tasmota einkompiliert (51 Zeichen)
GEN1_MODELS = {"SHSW-1": ("Shelly1", "hermit-1"), "SHSW-PM": ("Shelly1PM", "hermit-1pm"),
               "SHPLG-S": ("ShellyPlugS", "hermit-plugs"),
               "SHPLG-1": ("ShellyPlug2", "hermit-plug"), "SHPLG2-1": ("ShellyPlug2", "hermit-plug"),
               "SHBDUO-1": ("ShellyBulbDuo", "hermit-duo"),
               "SHDM-2": ("ShellyDimmer2", "hermit-dimmer2"),
               "SHSW-25": ("Shelly25", "hermit-25")}
ONE_MB = {"hermit-rfbridge": "hermit-loader-1m", "hermit-plug1m": "hermit-loader-1m-plug1m"}   # 1 MB: .bin.gz, Umweg ueber den 1-MB-Loader
TASMOTA_MINIMAL = "http://ota.tasmota.com/tasmota/release/tasmota-minimal.bin.gz"



def is_loader(body):
    """Loader-Seite – neuer Name oder noch der alte (vor der Umbenennung installiert)."""
    return b"Pagurus Loader" in body or b"Hermit Loader" in body

def log(msg):
    print(time.strftime("%H:%M:%S"), msg, flush=True)


CANCEL = threading.Event()               # von der Oberflaeche gesetzt: laufenden Vorgang abbrechen


def check_cancel():
    if CANCEL.is_set():
        log("Abgebrochen")
        sys.exit(1)


def die(msg):
    log("FEHLER: " + msg)
    sys.exit(1)


# ---------------------------------------------------------------- HTTP-Hilfen
class Dev:
    def __init__(self, ip, user, password):
        self.base = "http://" + ip
        mgr = urllib.request.HTTPPasswordMgrWithDefaultRealm()
        if password:
            mgr.add_password(None, self.base, user, password)
        self.opener = urllib.request.build_opener(
            urllib.request.HTTPBasicAuthHandler(mgr), urllib.request.HTTPDigestAuthHandler(mgr))

    def req(self, path, data=None, headers=None, timeout=8):
        r = urllib.request.Request(self.base + path, data=data, headers=headers or {})
        with self.opener.open(r, timeout=timeout) as resp:
            return resp.status, resp.read()

    def get(self, path, timeout=8):
        try:
            return self.req(path, timeout=timeout)
        except urllib.error.HTTPError as e:
            return e.code, e.read()
        except Exception:
            return None, b""

    def upload(self, path, field, filename, data, timeout=180):
        b = "----hermit" + uuid.uuid4().hex
        body = (f"--{b}\r\nContent-Disposition: form-data; name=\"{field}\"; filename=\"{filename}\"\r\n"
                f"Content-Type: application/octet-stream\r\n\r\n").encode() + data + f"\r\n--{b}--\r\n".encode()
        return self.req(path, data=body, headers={"Content-Type": "multipart/form-data; boundary=" + b},
                        timeout=timeout)


def jget(dev, path, timeout=8):
    st, body = dev.get(path, timeout)
    if st != 200:
        return None
    try:
        return json.loads(body)
    except Exception:
        return None


def wait_for(cond, seconds, what):
    log(f"Warte auf {what} (max. {seconds} s) …")
    t = time.time()
    while time.time() - t < seconds:
        check_cancel()
        r = cond()
        if r:
            return r
        for _ in range(6):
            check_cancel()
            time.sleep(0.5)
    return None


def local_ip_for(ip):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect((ip, 80))
        return s.getsockname()[0]
    finally:
        s.close()


# ---------------------------------------------------------------- Erkennung
def detect(dev):
    info = jget(dev, "/shelly")
    if info:
        if str(info.get("fw_id", "")).startswith(("hermit-os", "pagurus-os")):
            return "hermit", info
        if info.get("type") in GEN1_MODELS and "gen" not in info:
            return "stock", info
    st, body = dev.get("/")
    if st == 200 and is_loader(body):
        return "loader", {}
    status = jget(dev, "/cm?cmnd=Status%200")
    if status and "Status" in status:
        return "tasmota", status
    if st == 401:
        die("Gerät verlangt ein Passwort – --user/--password angeben")
    return None, None


def variant_from(dev, kind, info, addon, meter=False, cover=None):
    v = None
    if kind == "stock":
        v = GEN1_MODELS[info["type"]][1]
        if v == "hermit-25" and str((jget(dev, "/settings") or {}).get("mode", "")) == "roller":
            v = "hermit-25-cover"                # Original-Firmware im Rollladenbetrieb
    elif kind == "hermit":                       # Modell z. B. HERMIT-1PM-ADDON
        m = str(info.get("model", "")).lower()
        if m.startswith("pagurus-"):             # ab 1.20 heissen die Modelle PAGURUS-…; Dateien noch hermit-*
            m = "hermit-" + m[len("pagurus-"):]
        v = m.replace("-addon", "").replace("-meter", "") if m.startswith("hermit-") else None
        if v and "addon" in m:
            addon = True
        if v and "meter" in m:
            meter = True
    elif kind == "tasmota":
        t = jget(dev, "/cm?cmnd=Template") or {}
        name = str(t.get("NAME", "")).lower()
        if "plug s" in name:
            v = "hermit-plugs"
        elif "plug" in name and "us" not in name.split():
            v = "hermit-plug"
        elif "2.5" in name:
            v = "hermit-25-cover" if (jget(dev, "/cm?cmnd=SetOption80") or {}).get("SetOption80") == "ON" else "hermit-25"
        elif "dimmer 2" in name:
            v = "hermit-dimmer2"
        elif "duo" in name and "rgbw" not in name:
            v = "hermit-duo"
        elif "bridge" in name or "rf bridge" in str((jget(dev, "/cm?cmnd=Module") or {}).get("Module", "")).lower():
            v = "hermit-rfbridge"
        elif "1pm" in name:
            v = "hermit-1pm"
        elif "shelly 1" in name:
            v = "hermit-1"
    if v and addon:
        if v in ("hermit-plugs", "hermit-plug", "hermit-duo", "hermit-dimmer2", "hermit-25", "hermit-25-cover", "hermit-rfbridge"):
            die("Plug / Plug S / Duo / Dimmer 2 / Shelly 2.5 / RF Bridge haben keinen Add-on-Anschluss")
        v += "-addon"
    if v and cover is not None and v.startswith("hermit-25"):
        v = "hermit-25-cover" if cover else "hermit-25"
    if v and meter:
        if v not in ("hermit-1pm", "hermit-plugs", "hermit-plug"):
            die("Messfirmware gibt es nur für Shelly 1PM, Plug S und Plug")
        v += "-meter"
    return v


def load_bin(args, variant):
    gz = variant in ONE_MB                   # 1 MB: nur die gepackte Datei passt neben den Loader
    p = pathlib.Path(args.bin) if args.bin else BIN_DIR / f"{variant}.bin{'.gz' if gz else ''}"
    if not p.exists():
        die(f"Firmware {p} nicht gefunden (Repo-Ordner bin/ oder --bin angeben)")
    data = p.read_bytes()
    if gz and data[:2] != b"\x1f\x8b":
        die(f"{p.name}: für 1-MB-Geräte die .bin.gz verwenden")
    if not gz and data[:1] != b"\xe9":
        die(f"{p.name} ist keine ESP8266-Firmware (.bin, nicht .gz verwenden)")
    log(f"Firmware: {p.name} ({len(data) // 1024} KB)")
    return data


# ---------------------------------------------------------------- WLAN-Daten in die Firmware eintragen
PROV_MARK = b"PAGURUS-WLAN-SLOT1\0"
PROV_SIZE = 128
_CRC_T = []


def _crc8266(data):
    """CRC wie eboot (CRC-32, MSB zuerst, Start 0xFFFFFFFF, ohne Abschluss-XOR)."""
    if not _CRC_T:
        for i in range(256):
            c = i << 24
            for _ in range(8):
                c = ((c << 1) ^ 0x04C11DB7) if c & 0x80000000 else (c << 1)
            _CRC_T.append(c & 0xFFFFFFFF)
    crc = 0xFFFFFFFF
    for b in data:
        crc = ((crc << 8) & 0xFFFFFFFF) ^ _CRC_T[(crc >> 24) ^ b]
    return crc


def _app_checksum_pos(raw):
    """Position des XOR-Pruefbytes im Programm-Abbild (ab 0x1000, nach dem Bootloader)."""
    off = 0x1000
    if raw[off] != 0xE9:
        return None
    n = raw[off + 1]
    p = off + 8
    for _ in range(n):
        ln = int.from_bytes(raw[p + 4:p + 8], "little")
        p += 8 + ln
    size = p - off + 1
    while size & 15:
        size += 1
    return off + size - 1


def put_wifi(fw, ssid, password):
    """SSID/Passwort in den Platzhalter der Firmware schreiben; Pruefsummen anpassen. -> neue Datei oder None"""
    gz = fw[:2] == b"\x1f\x8b"
    raw = bytearray(gzip.decompress(fw) if gz else fw)
    s, pw = ssid.encode(), password.encode()
    if not (0 < len(s) <= 32) or len(pw) > 64:
        die("WLAN-Name max. 32, Passwort max. 64 Zeichen")
    pos, i = -1, raw.find(PROV_MARK)
    while i >= 0:                                    # der Platzhalter ist der Fundort, auf den nur Nullen folgen
        if not any(raw[i + len(PROV_MARK):i + PROV_SIZE]):
            if pos >= 0:
                return None
            pos = i
        i = raw.find(PROV_MARK, i + 1)
    cpos = _app_checksum_pos(raw)
    if pos < 0 or cpos is None or not (0x1000 < pos < cpos):
        return None
    data = s + b"\0" + pw + b"\0"
    a = pos + len(PROV_MARK)
    x = 0
    for k, b in enumerate(data):
        x ^= raw[a + k] ^ b
        raw[a + k] = b
    raw[cpos] ^= x                                   # XOR-Pruefsumme des Abbilds
    sz = int.from_bytes(raw[0x1010:0x1014], "little")
    if sz == len(raw):                               # CRC des Arduino-Cores (Groesse + CRC ab 0x1010)
        raw[0x1010:0x1018] = bytes(8)
        crc = _crc8266(raw)
        raw[0x1010:0x1014] = sz.to_bytes(4, "little")
        raw[0x1014:0x1018] = crc.to_bytes(4, "little")
    out = bytes(raw)
    return gzip.compress(out, 9) if gz else out


# ---------------------------------------------------------------- Webserver auf dem PC
class Server:
    def __init__(self, port, files):
        self.files, self.served = files, {}
        outer = self

        class H(http.server.BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.0"

            def do_HEAD(self):
                self.do_GET(head=True)

            def do_GET(self, head=False):
                name = self.path.split("?")[0].lstrip("/")
                log(f"  Anfrage von {self.client_address[0]}: {self.command} {self.path} "
                    f"({self.headers.get('User-Agent', '-')}, Range: {self.headers.get('Range', '-')})")
                if name not in outer.files:
                    self.send_response(404); self.end_headers(); return
                data = outer.files[name]
                if head:
                    self.send_response(200)
                    self.send_header("Content-Type", "application/octet-stream")
                    self.send_header("Content-Length", str(len(data)))
                    self.end_headers()
                    return
                log(f"  Gerät {self.client_address[0]} lädt /{name} ({len(data) // 1024} KB) …")
                self.send_response(200)
                self.send_header("Content-Type", "application/octet-stream")
                self.send_header("Content-Length", str(len(data)))
                self.send_header("Connection", "close")
                self.end_headers()
                try:
                    for i in range(0, len(data), 4096):
                        self.wfile.write(data[i:i + 4096])
                    outer.served[name] = outer.served.get(name, 0) + 1
                    log(f"  /{name} vollständig übertragen")
                except (BrokenPipeError, ConnectionResetError):
                    log(f"  /{name}: Verbindung abgebrochen")

            def log_message(self, *a):
                pass

        class S(http.server.ThreadingHTTPServer):
            # Windows: SO_REUSEADDR erlaubt einen zweiten Server auf demselben Port – dann landen die
            # Verbindungen beim alten. Deshalb dort exklusiv binden.
            allow_reuse_address = os.name != "nt"
            daemon_threads = True

            def server_bind(self):
                if os.name == "nt" and hasattr(socket, "SO_EXCLUSIVEADDRUSE"):
                    self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
                super().server_bind()

            def verify_request(self, request, client_address):
                log(f"  Verbindung von {client_address[0]}")
                return True

        self.httpd = None
        for p in range(port, port + 10):
            try:
                self.httpd = S(("0.0.0.0", p), H)
                break
            except OSError:
                log(f"  Port {p} ist belegt – versuche {p + 1}")
        if not self.httpd:
            die(f"Kein freier Port zwischen {port} und {port + 9}")
        self.port = self.httpd.server_address[1]
        threading.Thread(target=self.httpd.serve_forever, daemon=True).start()

    def stop(self):
        self.httpd.shutdown()
        self.httpd.server_close()          # Port wirklich freigeben


# ---------------------------------------------------------------- mg2hermit bauen
def get_mg2x_zip(args, model):
    if args.mg2x_zip:
        return pathlib.Path(args.mg2x_zip).read_bytes()
    CACHE.mkdir(exist_ok=True)
    f = CACHE / f"mg2tasmota-{model}.zip"
    if not f.exists():
        url = MG2X_URL.format(model=model)
        log(f"Lade {url} …")
        try:
            with urllib.request.urlopen(url, timeout=60) as r:
                f.write_bytes(r.read())
        except Exception as e:
            die(f"mg2tasmota konnte nicht geladen werden ({e}). Datei manuell laden und mit --mg2x-zip angeben.")
    return f.read_bytes()


def build_mg2hermit(src_zip, fw_url):
    url = fw_url.encode()
    if len(url) > len(MG2X_OLD):
        die(f"URL zu lang für mg2x ({len(url)} > {len(MG2X_OLD)} Zeichen): {fw_url}")
    zin = zipfile.ZipFile(io.BytesIO(src_zip))
    names = zin.namelist()
    man_name = next(n for n in names if n.endswith("manifest.json"))
    man = json.loads(zin.read(man_name))
    fw_part = man["parts"]["fw"]
    fw_name = man_name.rsplit("/", 1)[0] + "/" + fw_part["src"]
    fw = bytearray(zin.read(fw_name))
    pos = fw.find(MG2X_OLD)
    if pos < 0 or fw.find(MG2X_OLD, pos + 1) >= 0:
        die("Download-Adresse in mg2tasmota nicht eindeutig gefunden – Paketversion nicht unterstützt")
    fw[pos:pos + len(MG2X_OLD)] = url + b"\0" * (len(MG2X_OLD) - len(url))
    fw = bytes(fw)
    fw_part["cs_sha1"] = hashlib.sha1(fw).hexdigest()
    fw_part["cs_sha256"] = hashlib.sha256(fw).hexdigest()
    fw_part["size"] = len(fw)
    out = io.BytesIO()
    # Mongoose-OS-OTA liest das Zip im Datenstrom und kann nicht entpacken: unkomprimiert (STORED) wie das Original
    with zipfile.ZipFile(out, "w", zipfile.ZIP_STORED) as zo:
        for n in names:
            if n == fw_name:
                zo.writestr(n, fw)
            elif n == man_name:
                zo.writestr(n, json.dumps(man, indent=2))
            else:
                zo.writestr(n, zin.read(n))
    return out.getvalue()


# ---------------------------------------------------------------- Ablaeufe
def norm_mac(m):
    return "".join(c for c in str(m or "").upper() if c in "0123456789ABCDEF")


def device_mac(dev, kind, info):
    if kind in ("stock", "hermit"):
        return norm_mac(info.get("mac"))
    if kind == "tasmota":
        st = jget(dev, "/cm?cmnd=Status%205") or {}
        return norm_mac(st.get("StatusNET", {}).get("Mac"))
    return ""


def find_by_mac(mac, near_ip):
    """Sucht im /24-Netz nach Pagurus OS mit dieser MAC (falls das Geraet eine neue IP bekommen hat)."""
    net = ipaddress.ip_network(near_ip + "/24", strict=False)
    log(f"Suche das Gerät per MAC {mac} in {net} …")

    def probe(ip):
        if CANCEL.is_set():
            return None
        try:
            with urllib.request.urlopen(f"http://{ip}/shelly", timeout=1.5) as r:
                j = json.loads(r.read())
            if str(j.get("fw_id", "")).startswith(("hermit-os", "pagurus-os")) and norm_mac(j.get("mac")) == mac:
                return str(ip), j
        except Exception:
            pass
        return None
    with concurrent.futures.ThreadPoolExecutor(64) as ex:
        for r in ex.map(probe, net.hosts()):
            if r:
                return r
    return None


def wait_hermit(dev, seconds=180, mac=""):
    """Wartet auf Pagurus OS unter der alten IP; sucht zwischendurch per MAC, falls es eine neue IP bekommen hat."""
    log(f"Warte auf Pagurus OS (max. {seconds} s) …")
    near = dev.base.split("//")[1]
    t0 = time.time()
    next_mac = t0 + 40
    while time.time() - t0 < seconds:
        check_cancel()
        info = jget(dev, "/shelly", timeout=3) or {}
        if str(info.get("fw_id", "")).startswith(("hermit-os", "pagurus-os")):
            log(f"FERTIG: {info.get('id')} läuft mit {info.get('fw_id')} unter {dev.base}")
            return info
        if mac and time.time() >= next_mac:
            r = find_by_mac(mac, near)
            check_cancel()
            if r:
                log(f"FERTIG: {r[1].get('id')} läuft mit {r[1].get('fw_id')} unter der NEUEN Adresse http://{r[0]}")
                return r[1]
            next_mac = time.time() + 30
        for _ in range(6):
            check_cancel()
            time.sleep(0.5)
    log("Pagurus OS meldet sich nicht im Netz – vermutlich keine Verbindung zum WLAN.")
    log("Das Gerät öffnet dann den Zugangspunkt 'hermit-…' bzw. 'pagurus-…': damit verbinden, http://192.168.33.1 öffnen und das WLAN eintragen.")
    return None


def device_ssid(dev, kind, info):
    """WLAN-Name, mit dem das Geraet gerade verbunden ist (zum Vorbelegen)."""
    try:
        if kind == "stock":
            return ((jget(dev, "/settings", 4) or {}).get("wifi_sta") or {}).get("ssid") or ""
        if kind == "tasmota":
            return ((jget(dev, "/cm?cmnd=Status%2011", 4) or {}).get("StatusSTS", {}).get("Wifi") or {}).get("SSId") or ""
        if kind == "hermit":
            return (jget(dev, "/rpc/WiFi.GetStatus", 4) or {}).get("ssid") or ""
    except Exception:
        pass
    return ""


def apply_wifi(fw, kind, ssid, password):
    """WLAN-Daten in die Firmware schreiben (nur beim Umstieg auf Pagurus OS noetig)."""
    if not ssid or kind == "hermit":
        return fw
    out = put_wifi(fw, ssid, password or "")
    if out is None:
        log("Hinweis: Diese Firmware-Datei hat keinen WLAN-Platzhalter (vor 1.19.1) – WLAN wird aus der alten Firmware übernommen")
        return fw
    log(f"WLAN „{ssid}“ in die Firmware eingetragen")
    return out


def flash_stock(dev, args, info, variant, fw):
    model = GEN1_MODELS[info["type"]][0]
    pc = args.serve_ip or local_ip_for(args.ip)
    srv = Server(args.port, {"h.bin": fw})
    base = f"http://{pc}:{srv.port}"
    srv.files["mg2hermit.zip"] = build_mg2hermit(get_mg2x_zip(args, model), f"{base}/h.bin")
    log(f"Webserver läuft auf {base}")
    try:
        with urllib.request.urlopen(f"{base}/mg2hermit.zip", timeout=5) as r:
            r.read(16)
        log("Selbsttest: Webserver über die LAN-IP erreichbar")
    except Exception as e:
        log(f"Selbsttest fehlgeschlagen ({e}) – falsche PC-IP? Mit --serve-ip angeben")
    try:
        log(f"Starte Update der Original-Firmware ({model}, {info.get('fw', '?')}) …")
        pre = jget(dev, "/ota") or {}
        if pre.get("status") not in (None, "idle"):
            log(f"Hinweis: Update-Status des Geräts ist '{pre.get('status')}' – evtl. hängt ein früherer Versuch."
                " Dann Gerät kurz vom Strom trennen und erneut starten.")
        st, body = dev.get(f"/ota?url={base}/mg2hermit.zip", timeout=15)
        log(f"Antwort des Geräts: HTTP {st} {body[:160].decode(errors='replace').strip()}")
        if st != 200:
            die(f"Gerät lehnt Update ab (HTTP {st})")
        last = [None]

        def zip_loaded():
            if srv.served.get("mg2hermit.zip"):
                return True
            o = jget(dev, "/ota") or {}
            if o.get("status") != last[0]:
                last[0] = o.get("status")
                log(f"  Update-Status am Gerät: {last[0]}")
            return False
        if not wait_for(zip_loaded, 120, "Download von mg2hermit"):
            die("Gerät hat mg2hermit nicht geladen. Gerät kurz vom Strom trennen und erneut versuchen; "
                "sonst Firewall/Port prüfen (eingehend TCP " + str(srv.port) + " für Python).")
        log("mg2hermit installiert, Gerät startet 1–2× neu und lädt dann Pagurus OS …")
        if not wait_for(lambda: srv.served.get("h.bin"), 360, "Download von Pagurus OS"):
            die("Pagurus OS wurde nicht geladen. Gerät aus-/einschalten – mg2hermit versucht es erneut, solange dieses Tool läuft.")
        time.sleep(10)
        wait_hermit(dev, 180, getattr(args, "mac", ""))
    finally:
        srv.stop()


def loader_file(variant):
    name = ONE_MB[variant] + ".bin.gz" if variant in ONE_MB else "hermit-loader.bin.gz"
    f = BIN_DIR / name
    if not f.exists():
        die(f"{f} fehlt")
    return name, f.read_bytes()


def tasmota_upload(dev, name, data):
    dev.get("/up")                                   # setzt bei Tasmota den Upload-Typ "Firmware"
    try:
        st, body = dev.upload(f"/u2?fsz={len(data)}", "u2", name, data)
        return body or b""
    except urllib.error.HTTPError as e:
        if e.code not in (301, 302, 303):
            log(f"(Antwort nach Upload: HTTP {e.code})")
        return e.read() or b""
    except Exception as e:
        log(f"(Antwort nach Upload: {e})")
        return b""


def flash_tasmota(dev, args, variant, fw):
    name, data = loader_file(variant)
    log("Tasmota erkannt – spiele Pagurus Loader auf …")
    body = tasmota_upload(dev, name, data)
    if variant in ONE_MB and (b"space" in body.lower() or b"failed" in body.lower()):
        # 1 MB: im vollen Tasmota ist zu wenig Platz – erst tasmota-minimal (Tasmota laedt es selbst)
        log("Zu wenig Platz in Tasmota – installiere zuerst tasmota-minimal …")
        dev.get("/cm?cmnd=" + urllib.parse.quote("OtaUrl " + TASMOTA_MINIMAL))
        dev.get("/cm?cmnd=Upgrade%201")
        time.sleep(15)
        if not wait_for(lambda: (jget(dev, "/cm?cmnd=Status%202") or {}).get("StatusFWR", {}).get("Version", "").lower().count("minimal"),
                        240, "tasmota-minimal"):
            die("tasmota-minimal meldet sich nicht – Gerät prüfen und erneut starten")
        body = tasmota_upload(dev, name, data)
    if not wait_for(lambda: is_loader(dev.get("/")[1]), 180, "Pagurus Loader"):
        die("Loader meldet sich nicht unter der alten IP – evtl. AP 'hermit-loader-…' bzw. 'pagurus-loader-…' (kein WLAN gefunden)")
    flash_loader(dev, args, variant, fw)


def flash_loader(dev, args, variant, fw):
    log("Pagurus Loader erkannt – lade Pagurus OS hoch …")
    try:
        dev.upload("/upload", "f", f"{variant}.bin" + (".gz" if variant in ONE_MB else ""), fw)
    except urllib.error.HTTPError as e:
        if e.code not in (301, 302, 303):
            log(f"(Antwort nach Upload: HTTP {e.code})")
    except Exception as e:
        log(f"(Antwort nach Upload: {e})")
    time.sleep(5)
    wait_hermit(dev, 180, getattr(args, "mac", ""))


def flash_hermit(dev, args, variant, fw):
    if variant in ONE_MB:
        # 1 MB: Pagurus OS passt nicht zweimal hinein – erst den Loader, dann die Firmware
        name, data = loader_file(variant)
        log("Pagurus OS (1 MB) erkannt – spiele zuerst den Pagurus Loader auf …")
        try:
            dev.upload("/update", "firmware", name, data)
        except Exception as e:
            log(f"(Antwort nach Upload: {e})")
        if not wait_for(lambda: is_loader(dev.get("/")[1]), 120, "Pagurus Loader"):
            die("Loader meldet sich nicht")
        flash_loader(dev, args, variant, fw)
        return
    log("Pagurus OS erkannt – Firmware-Update …")
    try:
        dev.upload("/update", "firmware", f"{variant}.bin", fw)
    except urllib.error.HTTPError as e:
        if e.code not in (301, 302, 303):
            log(f"(Antwort nach Upload: HTTP {e.code})")
    except Exception as e:
        log(f"(Antwort nach Upload: {e})")
    time.sleep(8)
    wait_hermit(dev, 120, getattr(args, "mac", ""))


def main():
    ap = argparse.ArgumentParser(description="Pagurus OS auf Shelly 1 / 1PM / Plug / Plug S / Duo / Dimmer 2 / 2.5 (Gen1) und Sonoff RF Bridge schreiben")
    ap.add_argument("ip", help="IP-Adresse des Shelly")
    ap.add_argument("--addon", action="store_true", help="Variante mit Add-on-Taster installieren")
    ap.add_argument("--meter", action="store_true", help="Messfirmware (1PM / Plug S / Plug): nur Energiemessung, Reset nur am Taster/SW")
    ap.add_argument("--cover", action="store_true", default=None, help="Shelly 2.5: Rollladen-Firmware (hermit-25-cover)")
    ap.add_argument("--switch", dest="cover", action="store_false", help="Shelly 2.5: Schalter-Firmware mit zwei Kanälen (hermit-25)")
    ap.add_argument("--variant", help="z. B. hermit-1pm, hermit-1pm-addon, hermit-1pm-meter, hermit-plugs-meter, hermit-rfbridge (sonst automatisch)")
    ap.add_argument("--bin", help="eigene .bin statt bin/<variante>.bin")
    ap.add_argument("--user", default="admin")
    ap.add_argument("--password", default="")
    ap.add_argument("--port", type=int, default=8266, help="Port des Webservers auf dem PC (Original-Firmware)")
    ap.add_argument("--serve-ip", help="IP des PCs, falls die automatische Erkennung falsch liegt")
    ap.add_argument("--mg2x-zip", help="lokale mg2tasmota-Shelly1(PM).zip statt Download")
    ap.add_argument("--ssid", help="WLAN, mit dem sich Pagurus OS verbinden soll (empfohlen beim Umstieg)")
    ap.add_argument("--wifi-pass", default="", help="Passwort dieses WLANs")
    ap.add_argument("-y", "--yes", action="store_true", help="ohne Rückfrage")
    args = ap.parse_args()

    dev = Dev(args.ip, args.user, args.password)
    kind, info = detect(dev)
    if not kind:
        die(f"Unter {args.ip} kein unterstützter Shelly (Original, Tasmota, Loader oder Pagurus) gefunden")
    names = {"stock": "Original-Shelly-Firmware", "tasmota": "Tasmota", "loader": "Pagurus Loader", "hermit": "Pagurus OS"}
    variant = args.variant or variant_from(dev, kind, info, args.addon, args.meter, args.cover)
    if not variant:
        die("Modell nicht erkannt – bitte --variant angeben")
    log(f"Gefunden: {names[kind]}  →  installiere {variant}")
    if kind == "stock":
        log("Hinweis: mg2hermit überschreibt den Bootloader per OTA. Bricht der Vorgang ab, hilft im Zweifel nur")
        log("Flashen per Kabel." + (" Lampe während des Vorgangs nicht ausschalten." if "duo" in variant
                                    else " Das Licht kann während des Vorgangs kurz ausgehen." if "dimmer" in variant
                                    else " Beide Relais bleiben während des Vorgangs aus." if "hermit-25" in variant
                                    else " Relais bleibt während des Vorgangs aus."))
    if not args.yes and input("Fortfahren? [j/N] ").strip().lower() not in ("j", "ja", "y", "yes"):
        sys.exit(0)
    fw = apply_wifi(load_bin(args, variant), kind, args.ssid, args.wifi_pass)
    args.mac = device_mac(dev, kind, info)
    {"stock": lambda: flash_stock(dev, args, info, variant, fw),
     "tasmota": lambda: flash_tasmota(dev, args, variant, fw),
     "loader": lambda: flash_loader(dev, args, variant, fw),
     "hermit": lambda: flash_hermit(dev, args, variant, fw)}[kind]()


if __name__ == "__main__":
    main()
