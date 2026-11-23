#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 D.Koch – Hermit OS
"""Hermit Scanner – findet Shelly (Gen1, Gen2/Gen3), Tasmota und Hermit OS im Netz.

Oberflaeche:   python hermit_scanner.py          (bzw. Hermit-Scanner.exe)
Konsole:       python hermit_scanner.py --cli 192.168.4.0/24 192.168.5.0/24
               python hermit_scanner.py --cli --csv geraete.csv

Netzbereiche: CIDR (192.168.4.0/24), Bereich (192.168.4.10-192.168.4.80) oder einzelne IP,
mehrere durch Komma oder Leerzeichen getrennt. Ohne Angabe: die /24-Netze des eigenen Rechners.
Nur die Python-Standardbibliothek wird benoetigt.
"""
import argparse
import concurrent.futures
import csv
import ipaddress
import json
import os
import pathlib
import socket
import threading
import time
import urllib.error
import urllib.parse
import urllib.request

VERSION = "1.0.0"
PORT = int(os.environ.get("HERMIT_SCAN_PORT", "80"))       # nur fuer Tests anders
TIMEOUT = 2.0
WORKERS = 96

GEN1_NAMES = {
    "SHSW-1": "Shelly 1", "SHSW-PM": "Shelly 1PM", "SHSW-L": "Shelly 1L", "SHSW-25": "Shelly 2.5",
    "SHSW-21": "Shelly 2", "SHSW-44": "Shelly 4Pro", "SHPLG-S": "Shelly Plug S", "SHPLG-1": "Shelly Plug",
    "SHPLG2-1": "Shelly Plug", "SHPLG-U1": "Shelly Plug US", "SHBDUO-1": "Shelly Duo", "SHVIN-1": "Shelly Vintage",
    "SHBLB-1": "Shelly Bulb", "SHCB-1": "Shelly Duo RGBW", "SHDM-1": "Shelly Dimmer", "SHDM-2": "Shelly Dimmer 2",
    "SHRGBW2": "Shelly RGBW2", "SHEM": "Shelly EM", "SHEM-3": "Shelly 3EM", "SHHT-1": "Shelly H&T",
    "SHDW-1": "Shelly Door/Window", "SHDW-2": "Shelly Door/Window 2", "SHIX3-1": "Shelly i3",
    "SHUNI-1": "Shelly UNI", "SHMOS-01": "Shelly Motion", "SHMOS-02": "Shelly Motion 2", "SHWT-1": "Shelly Flood",
    "SHBTN-1": "Shelly Button1", "SHBTN-2": "Shelly Button1", "SHGS-1": "Shelly Gas", "SHSM-01": "Shelly Smoke",
    "SHSEN-1": "Shelly Sense", "SH2LED-1": "Shelly RGBW2 LED", "SHSPOT-1": "Shelly Spot", "SHSPOT-2": "Shelly Spot 2",
}
KINDS = ("Hermit OS", "Shelly Gen1", "Shelly Gen2+", "Tasmota")


# ======================= Netzbereiche =======================
def local_ipv4():
    ips = set()
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            ips.add(info[4][0])
    except OSError:
        pass
    try:                                        # Standardroute
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("10.255.255.255", 1))
        ips.add(s.getsockname()[0])
        s.close()
    except OSError:
        pass
    return sorted(ip for ip in ips if not ip.startswith(("127.", "169.254.")))


def default_ranges():
    nets = []
    for ip in local_ipv4():
        n = str(ipaddress.ip_network(ip + "/24", strict=False))
        if n not in nets:
            nets.append(n)
    return nets


def parse_ranges(text):
    """'192.168.4.0/24, 192.168.5.10-192.168.5.50 192.168.0.7' -> Liste von IP-Strings."""
    out = []
    for part in text.replace(";", ",").replace(",", " ").split():
        part = part.strip()
        if not part:
            continue
        if "-" in part:
            a, b = part.split("-", 1)
            a = ipaddress.ip_address(a.strip())
            b = b.strip()
            if "." not in b:                         # 192.168.4.10-80
                b = ".".join(str(a).split(".")[:3] + [b])
            b = ipaddress.ip_address(b)
            if int(b) < int(a):
                a, b = b, a
            if int(b) - int(a) > 65535:
                raise ValueError(f"Bereich zu groß: {part}")
            out += [str(ipaddress.ip_address(i)) for i in range(int(a), int(b) + 1)]
        elif "/" in part:
            net = ipaddress.ip_network(part, strict=False)
            if net.num_addresses > 65536:
                raise ValueError(f"Netz zu groß: {part}")
            out += [str(h) for h in (net.hosts() if net.num_addresses > 2 else net)]
        else:
            out.append(str(ipaddress.ip_address(part)))
    seen, res = set(), []
    for ip in out:
        if ip not in seen:
            seen.add(ip)
            res.append(ip)
    return res


# ======================= Abfragen =======================
def http_json(ip, path, timeout=TIMEOUT, auth=None):
    url = f"http://{ip}:{PORT}{path}" if PORT != 80 else f"http://{ip}{path}"
    req = urllib.request.Request(url, headers={"User-Agent": f"HermitScanner/{VERSION}"})
    opener = urllib.request.build_opener()
    if auth:
        pm = urllib.request.HTTPPasswordMgrWithDefaultRealm()
        pm.add_password(None, url, auth[0], auth[1])
        opener = urllib.request.build_opener(urllib.request.HTTPDigestAuthHandler(pm),
                                             urllib.request.HTTPBasicAuthHandler(pm))
    try:
        with opener.open(req, timeout=timeout) as r:
            body = r.read(200_000)
        return json.loads(body.decode("utf-8", "replace"))
    except urllib.error.HTTPError as e:
        if e.code == 401:
            return {"__auth__": True}
        return None
    except Exception:
        return None


def port_open(ip, timeout=0.7):
    try:
        with socket.create_connection((ip, PORT), timeout=timeout):
            return True
    except OSError:
        return False


def fmt_mac(m):
    m = (m or "").replace(":", "").upper()
    return ":".join(m[i:i + 2] for i in range(0, 12, 2)) if len(m) == 12 else m


def fmt_uptime(s):
    if s is None:
        return ""
    s = int(s)
    d, h, m = s // 86400, s % 86400 // 3600, s % 3600 // 60
    return f"{d} T {h} h" if d else (f"{h} h {m} min" if h else f"{m} min")


def blank(ip):
    return {"ip": ip, "kind": "", "name": "", "model": "", "fw": "", "mac": "", "rssi": None, "state": "",
            "power": None, "update": "", "uptime": "", "note": "", "gen": None, "url": f"http://{ip}/"}


def probe(ip, auth=None):
    """Erkennt Geraetetyp und liest die wichtigsten Werte. None = kein bekanntes Geraet."""
    if not port_open(ip):
        return None
    d = blank(ip)
    sh = http_json(ip, "/shelly")
    if isinstance(sh, dict) and ("type" in sh or "gen" in sh or "model" in sh):
        gen = sh.get("gen") or (1 if "type" in sh else 2)
        hermit = str(sh.get("fw_id", "")).startswith("hermit") or str(sh.get("model", "")).upper().startswith("HERMIT")
        d["gen"] = gen
        d["mac"] = fmt_mac(sh.get("mac"))
        if hermit or gen >= 2:
            d["kind"] = "Hermit OS" if hermit else "Shelly Gen2+"
            d["model"] = sh.get("model") or ""
            if not hermit and sh.get("app"):
                d["model"] = f"{sh['app']} ({sh.get('model', '')})"
            d["fw"] = sh.get("ver") or sh.get("fw") or ""
            d["name"] = sh.get("name") or ""
            st = http_json(ip, "/rpc/Shelly.GetStatus", auth=auth)
            if isinstance(st, dict) and st.get("__auth__"):
                d["note"] = "Passwort"
            elif isinstance(st, dict):
                w = st.get("wifi") or {}
                d["rssi"] = w.get("rssi")
                sy = st.get("sys") or {}
                d["uptime"] = fmt_uptime(sy.get("uptime"))
                upd = (sy.get("available_updates") or {}).get("stable") or {}
                d["update"] = upd.get("version", "")
                outs, pw = [], 0.0
                has_pw = False
                for k, v in sorted(st.items()):
                    if not isinstance(v, dict):
                        continue
                    if k.startswith(("switch:", "light:", "cct:", "rgb:", "rgbw:", "cover:")):
                        if "output" in v:
                            outs.append("an" if v["output"] else "aus")
                        elif "state" in v:
                            outs.append(str(v["state"]))
                        if "apower" in v:
                            pw += float(v.get("apower") or 0)
                            has_pw = True
                        if v.get("meter_only"):
                            d["note"] = "Messfirmware"
                    if k.startswith(("pm1:", "em1:")) and "act_power" in v:
                        pw += float(v["act_power"] or 0)
                        has_pw = True
                d["state"] = " / ".join(outs)
                d["power"] = round(pw, 1) if has_pw else None
            if not d["name"]:
                cfg = http_json(ip, "/rpc/Sys.GetConfig", auth=auth)
                if isinstance(cfg, dict):
                    d["name"] = ((cfg.get("device") or {}).get("name")) or ""
        else:
            d["kind"] = "Shelly Gen1"
            t = sh.get("type", "")
            d["model"] = f"{GEN1_NAMES.get(t, t)} ({t})" if t in GEN1_NAMES else t
            d["fw"] = str(sh.get("fw", "")).split("/")[-1].split("@")[0]
            if sh.get("auth"):
                d["note"] = "Passwort"
            st = http_json(ip, "/status", auth=auth)
            if isinstance(st, dict) and not st.get("__auth__"):
                d["rssi"] = (st.get("wifi_sta") or {}).get("rssi")
                d["uptime"] = fmt_uptime(st.get("uptime"))
                u = st.get("update") or {}
                if u.get("has_update"):
                    d["update"] = str(u.get("new_version", "ja")).split("/")[-1].split("@")[0]
                rel = st.get("relays") or st.get("lights") or []
                d["state"] = " / ".join("an" if r.get("ison") else "aus" for r in rel if "ison" in r)
                meters = st.get("meters") or st.get("emeters") or []
                if meters:
                    d["power"] = round(sum(float(m.get("power") or 0) for m in meters), 1)
            se = http_json(ip, "/settings", auth=auth)
            if isinstance(se, dict) and not se.get("__auth__"):
                d["name"] = se.get("name") or ""
        return d
    ta = http_json(ip, "/cm?cmnd=Status%200")
    if isinstance(ta, dict) and ("Status" in ta or "StatusFWR" in ta or "WARNING" in ta):
        d["kind"] = "Tasmota"
        if "WARNING" in ta:
            d["note"] = "Passwort"
            return d
        s = ta.get("Status") or {}
        d["name"] = s.get("DeviceName") or (s.get("FriendlyName") or [""])[0]
        d["fw"] = str((ta.get("StatusFWR") or {}).get("Version", "")).split("(")[0]
        net = ta.get("StatusNET") or {}
        d["mac"] = fmt_mac(net.get("Mac"))
        sts = ta.get("StatusSTS") or {}
        d["rssi"] = (sts.get("Wifi") or {}).get("Signal")
        d["uptime"] = sts.get("Uptime", "").replace("T", " T ")
        outs = [sts[k] for k in sorted(sts) if k.startswith("POWER")]
        d["state"] = " / ".join("an" if str(o).upper() == "ON" else "aus" for o in outs)
        en = (ta.get("StatusSNS") or {}).get("ENERGY") or {}
        if "Power" in en:
            p = en["Power"]
            d["power"] = round(sum(p) if isinstance(p, list) else float(p), 1)
        tpl = http_json(ip, "/cm?cmnd=Template")
        hw = (ta.get("StatusFWR") or {}).get("Hardware", "")
        d["model"] = (tpl.get("NAME") if isinstance(tpl, dict) and tpl.get("NAME") else f"Modul {s.get('Module', '')}") \
            + (f" ({hw})" if hw else "")
        return d
    return None


def scan(ips, progress=None, stop=None, auth=None):
    found = []
    done = 0
    lock = threading.Lock()
    with concurrent.futures.ThreadPoolExecutor(WORKERS) as ex:
        futs = {ex.submit(probe, ip, auth): ip for ip in ips}
        for f in concurrent.futures.as_completed(futs):
            if stop and stop.is_set():
                for x in futs:
                    x.cancel()
                break
            r = None
            try:
                r = f.result()
            except Exception:
                pass
            with lock:
                done += 1
                if r:
                    found.append(r)
            if progress:
                progress(done, len(ips), r)
    found.sort(key=lambda x: tuple(int(p) for p in x["ip"].split(".")))
    return found


def reboot(d):
    if d["kind"] == "Tasmota":
        return http_json(d["ip"], "/cm?cmnd=Restart%201") is not None
    if d["kind"] == "Shelly Gen1":
        return http_json(d["ip"], "/reboot") is not None
    return http_json(d["ip"], "/rpc/Shelly.Reboot") is not None


COLS = [("kind", "Typ"), ("name", "Name"), ("ip", "IP"), ("model", "Modell"), ("fw", "Firmware"), ("mac", "MAC"),
        ("rssi", "Empfang dBm"), ("state", "Zustand"), ("power", "Leistung W"), ("update", "Update"),
        ("uptime", "Laufzeit"), ("note", "Hinweis")]


def write_csv(path, rows):
    with open(path, "w", newline="", encoding="utf-8-sig") as f:
        w = csv.writer(f, delimiter=";")
        w.writerow([c[1] for c in COLS])
        for r in rows:
            w.writerow([("" if r.get(k) is None else str(r.get(k)).replace(".", ",") if k == "power" else r.get(k, ""))
                        for k, _ in COLS])


# ======================= Konsole =======================
def cli(args):
    text = " ".join(args.ranges) if args.ranges else ", ".join(default_ranges())
    ips = parse_ranges(text)
    print(f"Hermit Scanner {VERSION} – durchsuche {len(ips)} Adressen ({text})")
    t0 = time.time()

    def prog(done, total, r):
        if r:
            print(f"  {r['ip']:15} {r['kind']:12} {r['name'][:22]:22} {r['model'][:30]:30} {r['fw']}")
    rows = scan(ips, prog)
    print(f"\n{len(rows)} Geräte in {time.time() - t0:.1f} s: " +
          ", ".join(f"{k}: {sum(1 for r in rows if r['kind'] == k)}" for k in KINDS))
    if args.csv:
        write_csv(args.csv, rows)
        print("CSV gespeichert:", args.csv)


# ======================= Oberflaeche =======================
def settings_path():
    base = os.environ.get("APPDATA") or str(pathlib.Path.home() / ".config")
    p = pathlib.Path(base) / "HermitScanner"
    p.mkdir(parents=True, exist_ok=True)
    return p / "settings.json"


def gui():
    import tkinter as tk
    import webbrowser
    from tkinter import filedialog, messagebox, ttk

    try:
        cfg = json.loads(settings_path().read_text("utf-8"))
    except Exception:
        cfg = {}

    root = tk.Tk()
    root.title(f"Hermit Scanner {VERSION} – Shelly · Tasmota · Hermit OS")
    root.geometry("1440x720")
    try:
        root.tk.call("tk", "scaling", 1.25)
    except Exception:
        pass
    style = ttk.Style()
    if "vista" in style.theme_names():
        style.theme_use("vista")
    style.configure("Treeview", rowheight=24)

    top = ttk.Frame(root, padding=8)
    top.pack(fill="x")
    ttk.Label(top, text="Netze:").pack(side="left")
    rng = tk.StringVar(value=cfg.get("ranges") or ", ".join(default_ranges()) or "192.168.0.0/24")
    ent = ttk.Entry(top, textvariable=rng, width=60)
    ent.pack(side="left", padx=6, fill="x", expand=True)
    ttk.Label(top, text="Passwort (optional):").pack(side="left", padx=(10, 2))
    pw = tk.StringVar(value="")
    ttk.Entry(top, textvariable=pw, show="•", width=14).pack(side="left")
    btn = ttk.Button(top, text="Suchen")
    btn.pack(side="left", padx=6)
    ttk.Button(top, text="Eigene Netze", command=lambda: rng.set(", ".join(default_ranges()))).pack(side="left")

    flt = ttk.Frame(root, padding=(8, 0))
    flt.pack(fill="x")
    show = {k: tk.BooleanVar(value=True) for k in KINDS}
    for k in KINDS:
        ttk.Checkbutton(flt, text=k, variable=show[k], command=lambda: refill()).pack(side="left", padx=(0, 10))
    ttk.Label(flt, text="Filter:").pack(side="left", padx=(20, 4))
    q = tk.StringVar()
    fe = ttk.Entry(flt, textvariable=q, width=24)
    fe.pack(side="left")
    q.trace_add("write", lambda *a: refill())

    mid = ttk.Frame(root, padding=8)
    mid.pack(fill="both", expand=True)
    tv = ttk.Treeview(mid, columns=[c[0] for c in COLS], show="headings", selectmode="extended")
    widths = {"kind": 105, "name": 170, "ip": 110, "model": 210, "fw": 120, "mac": 135, "rssi": 90, "state": 80,
              "power": 85, "update": 90, "uptime": 95, "note": 100}
    sort_state = {"col": "ip", "rev": False}
    for k, t in COLS:
        tv.heading(k, text=t, command=lambda k=k: sort_by(k))
        tv.column(k, width=widths[k], anchor="e" if k in ("rssi", "power") else "w")
    vs = ttk.Scrollbar(mid, orient="vertical", command=tv.yview)
    hs_ = ttk.Scrollbar(mid, orient="horizontal", command=tv.xview)
    tv.configure(yscrollcommand=vs.set, xscrollcommand=hs_.set)
    tv.grid(row=0, column=0, sticky="nsew")
    vs.grid(row=0, column=1, sticky="ns")
    hs_.grid(row=1, column=0, sticky="ew")
    mid.rowconfigure(0, weight=1)
    mid.columnconfigure(0, weight=1)
    tv.tag_configure("Hermit OS", background="#dbeeff")
    tv.tag_configure("Shelly Gen1", background="#f2f2f2")
    tv.tag_configure("Shelly Gen2+", background="#e3f6e8")
    tv.tag_configure("Tasmota", background="#fff4d6")

    bot = ttk.Frame(root, padding=8)
    bot.pack(fill="x")
    status = tk.StringVar(value="Bereit. Netze eintragen (z. B. 192.168.4.0/24, 192.168.5.0/24) und „Suchen“.")
    ttk.Label(bot, textvariable=status).pack(side="left")
    pb = ttk.Progressbar(bot, length=220, mode="determinate")
    pb.pack(side="right")
    ttk.Button(bot, text="CSV speichern", command=lambda: save_csv()).pack(side="right", padx=8)

    rows = []
    stop = threading.Event()
    running = {"v": False}

    def fmt(r, k):
        v = r.get(k)
        if v is None:
            return ""
        if k == "power":
            return f"{v:.1f}".replace(".", ",")
        return str(v)

    def visible():
        t = q.get().lower().strip()
        return [r for r in rows if show[r["kind"]].get()
                and (not t or any(t in str(r.get(k, "")).lower() for k, _ in COLS))]

    def refill():
        tv.delete(*tv.get_children())
        vis = visible()
        col, rev = sort_state["col"], sort_state["rev"]

        def key(r):
            v = r.get(col)
            if col == "ip":
                return tuple(int(p) for p in r["ip"].split("."))
            if isinstance(v, (int, float)):
                return (0, v)
            return (1, str(v or "").lower())
        for r in sorted(vis, key=key, reverse=rev):
            tv.insert("", "end", iid=r["ip"], values=[fmt(r, k) for k, _ in COLS], tags=(r["kind"],))
        counts = ", ".join(f"{k}: {sum(1 for r in rows if r['kind'] == k)}" for k in KINDS)
        if not running["v"]:
            status.set(f"{len(rows)} Geräte ({counts}) – angezeigt: {len(vis)}. Doppelklick öffnet das Gerät.")

    def sort_by(k):
        sort_state["rev"] = not sort_state["rev"] if sort_state["col"] == k else False
        sort_state["col"] = k
        refill()

    def do_scan():
        if running["v"]:
            stop.set()
            return
        try:
            ips = parse_ranges(rng.get())
        except Exception as e:
            messagebox.showerror("Hermit Scanner", f"Netzbereich nicht verstanden:\n{e}")
            return
        if not ips:
            return
        cfg["ranges"] = rng.get()
        try:
            settings_path().write_text(json.dumps(cfg), "utf-8")
        except Exception:
            pass
        rows.clear()
        refill()
        stop.clear()
        running["v"] = True
        btn.config(text="Abbrechen")
        pb.config(maximum=len(ips), value=0)
        auth = ("admin", pw.get()) if pw.get() else None
        t0 = time.time()

        def prog(done, total, r):
            def ui():
                pb.config(value=done)
                status.set(f"Suche … {done}/{total} Adressen, {len(rows)} Geräte gefunden")
                if r:
                    rows.append(r)
                    refill()
            root.after(0, ui)

        def work():
            scan(ips, prog, stop, auth)

            def fin():
                running["v"] = False
                btn.config(text="Suchen")
                refill()
                status.set(status.get() + f"  ({time.time() - t0:.0f} s)")
            root.after(0, fin)
        threading.Thread(target=work, daemon=True).start()

    btn.config(command=do_scan)
    ent.bind("<Return>", lambda e: do_scan())

    def sel():
        return [r for r in rows if r["ip"] in tv.selection()]

    def open_sel(_=None):
        for r in sel()[:8]:
            webbrowser.open(r["url"])

    def copy(field):
        root.clipboard_clear()
        root.clipboard_append("\n".join(str(r.get(field, "")) for r in sel()))

    def do_reboot():
        s = sel()
        if not s or not messagebox.askyesno("Neu starten", f"{len(s)} Gerät(e) neu starten?\n\n" +
                                            "\n".join(f"{r['ip']}  {r['name']}" for r in s[:12])):
            return

        def work():
            res = [(r, reboot(r)) for r in s]
            bad = [r["ip"] for r, ok in res if not ok]
            root.after(0, lambda: status.set("Neustart gesendet" + (f" – fehlgeschlagen: {', '.join(bad)}" if bad else "")))
        threading.Thread(target=work, daemon=True).start()

    def refresh_sel():
        s = sel()

        def work():
            for r in s:
                n = probe(r["ip"], ("admin", pw.get()) if pw.get() else None)
                if n:
                    rows[rows.index(r)] = n
            root.after(0, refill)
        threading.Thread(target=work, daemon=True).start()

    def save_csv():
        if not rows:
            return
        p = filedialog.asksaveasfilename(defaultextension=".csv", filetypes=[("CSV (Excel)", "*.csv")],
                                         initialfile=time.strftime("geraete_%Y-%m-%d.csv"))
        if p:
            write_csv(p, visible())
            status.set(f"Gespeichert: {p}")

    menu = tk.Menu(root, tearoff=0)
    menu.add_command(label="Im Browser öffnen", command=open_sel)
    menu.add_command(label="Neu abfragen", command=refresh_sel)
    menu.add_separator()
    menu.add_command(label="IP kopieren", command=lambda: copy("ip"))
    menu.add_command(label="MAC kopieren", command=lambda: copy("mac"))
    menu.add_separator()
    menu.add_command(label="Neu starten …", command=do_reboot)

    def popup(e):
        iid = tv.identify_row(e.y)
        if iid and iid not in tv.selection():
            tv.selection_set(iid)
        if tv.selection():
            menu.tk_popup(e.x_root, e.y_root)
    tv.bind("<Double-1>", open_sel)
    tv.bind("<Button-3>", popup)
    root.bind("<F5>", lambda e: do_scan())
    root.mainloop()


def main():
    ap = argparse.ArgumentParser(description="Findet Shelly (Gen1/Gen2+), Tasmota und Hermit OS im Netz.")
    ap.add_argument("--cli", action="store_true", help="ohne Oberfläche, Ausgabe in der Konsole")
    ap.add_argument("--csv", help="Ergebnis als CSV speichern (mit --cli)")
    ap.add_argument("ranges", nargs="*", help="Netze, z. B. 192.168.4.0/24 192.168.5.10-80")
    a = ap.parse_args()
    if a.cli or a.csv:
        cli(a)
    else:
        gui()


if __name__ == "__main__":
    main()
