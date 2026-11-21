#!/usr/bin/env python3
"""
hermit_flash.py – Hermit OS ueber WLAN auf einen Shelly 1 / 1PM / Plug S / Duo (Gen1) schreiben.

Erkennt automatisch, was auf dem Geraet laeuft:
  * Original-Shelly-Firmware (Mongoose OS)  -> mg2hermit: angepasstes mg2tasmota laedt Hermit OS
  * Tasmota                                  -> Hermit Loader -> Hermit OS
  * Hermit Loader                            -> Hermit OS
  * Hermit OS                                -> Firmware-Update

Der PC stellt die Dateien ueber einen eigenen kleinen Webserver bereit – es wird kein Internet
und kein oeffentliches Repo gebraucht (ausser einmalig fuer das mg2tasmota-Paket, siehe --mg2x-zip).

Aufruf:
  python3 tools/hermit_flash.py 192.168.5.153
  python3 tools/hermit_flash.py 192.168.5.153 --addon
  python3 tools/hermit_flash.py 192.168.5.153 --user admin --password geheim

Nur Python 3.8+ Standardbibliothek. Der PC muss im selben Netz wie der Shelly sein;
die Windows-Firewall muss eingehende Verbindungen auf dem Port (Standard 8266) erlauben.
"""
import argparse, base64, concurrent.futures, hashlib, http.server, io, ipaddress, json, os, pathlib, socket, sys, threading, time
import urllib.error, urllib.request, uuid, zipfile

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
               "SHPLG-S": ("ShellyPlugS", "hermit-plugs"), "SHBDUO-1": ("ShellyBulbDuo", "hermit-duo")}


def log(msg):
    print(time.strftime("%H:%M:%S"), msg, flush=True)


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


def jget(dev, path):
    st, body = dev.get(path)
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
        r = cond()
        if r:
            return r
        time.sleep(3)
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
        if str(info.get("fw_id", "")).startswith("hermit-os"):
            return "hermit", info
        if info.get("type") in GEN1_MODELS and "gen" not in info:
            return "stock", info
    st, body = dev.get("/")
    if st == 200 and b"Hermit Loader" in body:
        return "loader", {}
    status = jget(dev, "/cm?cmnd=Status%200")
    if status and "Status" in status:
        return "tasmota", status
    if st == 401:
        die("Gerät verlangt ein Passwort – --user/--password angeben")
    return None, None


def variant_from(dev, kind, info, addon):
    v = None
    if kind == "stock":
        v = GEN1_MODELS[info["type"]][1]
    elif kind == "hermit":                       # Modell z. B. HERMIT-1PM-ADDON
        m = str(info.get("model", "")).lower()
        v = m.replace("-addon", "") if m.startswith("hermit-") else None
        if v and "addon" in m:
            addon = True
    elif kind == "tasmota":
        t = jget(dev, "/cm?cmnd=Template") or {}
        name = str(t.get("NAME", "")).lower()
        if "plug s" in name:
            v = "hermit-plugs"
        elif "duo" in name and "rgbw" not in name:
            v = "hermit-duo"
        elif "1pm" in name:
            v = "hermit-1pm"
        elif "shelly 1" in name:
            v = "hermit-1"
    if v and addon:
        if v in ("hermit-plugs", "hermit-duo"):
            die("Plug S / Duo haben keinen Add-on-Anschluss")
        v += "-addon"
    return v


def load_bin(args, variant):
    p = pathlib.Path(args.bin) if args.bin else BIN_DIR / f"{variant}.bin"
    if not p.exists():
        die(f"Firmware {p} nicht gefunden (Repo-Ordner bin/ oder --bin angeben)")
    data = p.read_bytes()
    if data[:1] != b"\xe9":
        die(f"{p.name} ist keine ESP8266-Firmware (.bin, nicht .gz verwenden)")
    log(f"Firmware: {p.name} ({len(data) // 1024} KB)")
    return data


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
    """Sucht im /24-Netz nach Hermit OS mit dieser MAC (falls das Geraet eine neue IP bekommen hat)."""
    net = ipaddress.ip_network(near_ip + "/24", strict=False)
    log(f"Suche das Gerät per MAC {mac} in {net} …")

    def probe(ip):
        try:
            with urllib.request.urlopen(f"http://{ip}/shelly", timeout=1.5) as r:
                j = json.loads(r.read())
            if str(j.get("fw_id", "")).startswith("hermit-os") and norm_mac(j.get("mac")) == mac:
                return str(ip), j
        except Exception:
            pass
        return None
    with concurrent.futures.ThreadPoolExecutor(64) as ex:
        for r in ex.map(probe, net.hosts()):
            if r:
                return r
    return None


def wait_hermit(dev, seconds=240, mac=""):
    info = wait_for(lambda: (jget(dev, "/shelly") or {}).get("fw_id", "").startswith("hermit-os") and jget(dev, "/shelly"),
                    seconds, "Hermit OS")
    if info:
        log(f"FERTIG: {info.get('id')} läuft mit {info.get('fw_id')} unter {dev.base}")
        return info
    if mac:
        for _ in range(3):
            r = find_by_mac(mac, dev.base.split("//")[1])
            if r:
                log(f"FERTIG: {r[1].get('id')} läuft mit {r[1].get('fw_id')} unter der NEUEN Adresse http://{r[0]}")
                return r[1]
            time.sleep(20)
    log("Hermit OS meldet sich nicht im Netz. Vermutlich wurde kein WLAN übernommen –")
    log("dann im WLAN nach 'hermit-…' suchen und http://192.168.33.1 öffnen.")
    return None


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
        log("mg2hermit installiert, Gerät startet 1–2× neu und lädt dann Hermit OS …")
        if not wait_for(lambda: srv.served.get("h.bin"), 360, "Download von Hermit OS"):
            die("Hermit OS wurde nicht geladen. Gerät aus-/einschalten – mg2hermit versucht es erneut, solange dieses Tool läuft.")
        time.sleep(10)
        wait_hermit(dev, 180, getattr(args, "mac", ""))
    finally:
        srv.stop()


def flash_tasmota(dev, args, variant, fw):
    loader = BIN_DIR / "hermit-loader.bin.gz"
    if not loader.exists():
        die(f"{loader} fehlt")
    data = loader.read_bytes()
    log("Tasmota erkannt – spiele Hermit Loader auf …")
    dev.get("/up")                                   # setzt bei Tasmota den Upload-Typ "Firmware"
    try:
        dev.upload(f"/u2?fsz={len(data)}", "u2", "hermit-loader.bin.gz", data)
    except urllib.error.HTTPError as e:
        if e.code not in (301, 302, 303):
            log(f"(Antwort nach Upload: HTTP {e.code})")
    except Exception as e:
        log(f"(Antwort nach Upload: {e})")
    if not wait_for(lambda: b"Hermit Loader" in dev.get("/")[1], 180, "Hermit Loader"):
        die("Loader meldet sich nicht unter der alten IP – evtl. AP 'hermit-loader-…' (kein WLAN gefunden)")
    flash_loader(dev, args, variant, fw)


def flash_loader(dev, args, variant, fw):
    log("Hermit Loader erkannt – lade Hermit OS hoch …")
    try:
        dev.upload("/upload", "f", f"{variant}.bin", fw)
    except urllib.error.HTTPError as e:
        if e.code not in (301, 302, 303):
            log(f"(Antwort nach Upload: HTTP {e.code})")
    except Exception as e:
        log(f"(Antwort nach Upload: {e})")
    time.sleep(5)
    wait_hermit(dev, 180, getattr(args, "mac", ""))


def flash_hermit(dev, args, variant, fw):
    log("Hermit OS erkannt – Firmware-Update …")
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
    ap = argparse.ArgumentParser(description="Hermit OS auf Shelly 1 / 1PM (Gen1) schreiben")
    ap.add_argument("ip", help="IP-Adresse des Shelly")
    ap.add_argument("--addon", action="store_true", help="Variante mit Add-on-Taster installieren")
    ap.add_argument("--variant", help="hermit-1 | hermit-1pm | hermit-1-addon | hermit-1pm-addon (sonst automatisch)")
    ap.add_argument("--bin", help="eigene .bin statt bin/<variante>.bin")
    ap.add_argument("--user", default="admin")
    ap.add_argument("--password", default="")
    ap.add_argument("--port", type=int, default=8266, help="Port des Webservers auf dem PC (Original-Firmware)")
    ap.add_argument("--serve-ip", help="IP des PCs, falls die automatische Erkennung falsch liegt")
    ap.add_argument("--mg2x-zip", help="lokale mg2tasmota-Shelly1(PM).zip statt Download")
    ap.add_argument("-y", "--yes", action="store_true", help="ohne Rückfrage")
    args = ap.parse_args()

    dev = Dev(args.ip, args.user, args.password)
    kind, info = detect(dev)
    if not kind:
        die(f"Unter {args.ip} kein Shelly 1/1PM (Original, Tasmota, Loader oder Hermit) gefunden")
    names = {"stock": "Original-Shelly-Firmware", "tasmota": "Tasmota", "loader": "Hermit Loader", "hermit": "Hermit OS"}
    variant = args.variant or variant_from(dev, kind, info, args.addon)
    if not variant:
        die("Modell nicht erkannt – bitte --variant angeben")
    log(f"Gefunden: {names[kind]}  →  installiere {variant}")
    if kind == "stock":
        log("Hinweis: mg2hermit überschreibt den Bootloader per OTA. Bricht der Vorgang ab, hilft im Zweifel nur")
        log("Flashen per Kabel." + (" Lampe während des Vorgangs nicht ausschalten." if "duo" in variant else " Relais bleibt während des Vorgangs aus."))
    if not args.yes and input("Fortfahren? [j/N] ").strip().lower() not in ("j", "ja", "y", "yes"):
        sys.exit(0)
    fw = load_bin(args, variant)
    args.mac = device_mac(dev, kind, info)
    {"stock": lambda: flash_stock(dev, args, info, variant, fw),
     "tasmota": lambda: flash_tasmota(dev, args, variant, fw),
     "loader": lambda: flash_loader(dev, args, variant, fw),
     "hermit": lambda: flash_hermit(dev, args, variant, fw)}[kind]()


if __name__ == "__main__":
    main()
