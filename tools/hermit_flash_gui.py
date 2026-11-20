#!/usr/bin/env python3
"""
Hermit OS Flasher – grafische Oberflaeche fuer hermit_flash.py

Start (Windows): Doppelklick auf hermit_flash_gui.py  bzw.  pythonw tools\\hermit_flash_gui.py
Nur Python-Standardbibliothek (tkinter).
"""
import argparse, concurrent.futures, ipaddress, json, pathlib, queue, sys, threading, time, urllib.request
import tkinter as tk
from tkinter import ttk, filedialog, messagebox

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import hermit_flash as hf  # noqa: E402

VARIANTS = ["automatisch", "hermit-1", "hermit-1-addon", "hermit-1pm", "hermit-1pm-addon", "hermit-plugs", "hermit-duo"]
KIND_NAMES = {"stock": "Original-Shelly-Firmware", "tasmota": "Tasmota", "loader": "Hermit Loader", "hermit": "Hermit OS"}


class App:
    def __init__(self, root):
        self.root = root
        self.q = queue.Queue()
        self.busy = False
        root.title("Hermit OS Flasher")
        root.minsize(720, 560)
        try:
            root.tk.call("tk", "scaling", 1.3)
        except tk.TclError:
            pass
        st = ttk.Style()
        if "vista" in st.theme_names():
            st.theme_use("vista")
        st.configure("Title.TLabel", font=("Segoe UI", 16, "bold"))
        st.configure("Big.TButton", font=("Segoe UI", 11, "bold"), padding=8)

        main = ttk.Frame(root, padding=12)
        main.pack(fill="both", expand=True)
        ttk.Label(main, text="\U0001F41A  Hermit OS Flasher", style="Title.TLabel").pack(anchor="w")
        ttk.Label(main, text="Shelly 1 · 1PM · Plug S · Duo (Gen1) – von Original-Firmware, Tasmota oder Hermit",
                  foreground="#666").pack(anchor="w", pady=(0, 10))

        # --- Geraet
        dev = ttk.LabelFrame(main, text="Gerät", padding=10)
        dev.pack(fill="x")
        ttk.Label(dev, text="IP-Adresse").grid(row=0, column=0, sticky="w")
        self.ip = tk.StringVar()
        self.ipbox = ttk.Combobox(dev, textvariable=self.ip, width=28)
        self.ipbox.grid(row=0, column=1, sticky="we", padx=6)
        ttk.Button(dev, text="Netz durchsuchen", command=self.scan).grid(row=0, column=2, padx=3)
        ttk.Button(dev, text="Gerät prüfen", command=self.check).grid(row=0, column=3, padx=3)
        self.info = tk.StringVar(value="Noch kein Gerät geprüft")
        ttk.Label(dev, textvariable=self.info, foreground="#0a5").grid(row=1, column=0, columnspan=4, sticky="w", pady=(8, 0))
        dev.columnconfigure(1, weight=1)

        # --- Optionen
        opt = ttk.LabelFrame(main, text="Optionen", padding=10)
        opt.pack(fill="x", pady=10)
        ttk.Label(opt, text="Firmware").grid(row=0, column=0, sticky="w")
        self.variant = tk.StringVar(value="automatisch")
        ttk.Combobox(opt, textvariable=self.variant, values=VARIANTS, state="readonly", width=22).grid(row=0, column=1, sticky="w", padx=6)
        self.addon = tk.BooleanVar()
        ttk.Checkbutton(opt, text="mit Add-on-Taster", variable=self.addon).grid(row=0, column=2, sticky="w", padx=10)

        ttk.Label(opt, text="Passwort (falls gesetzt)").grid(row=1, column=0, sticky="w", pady=(8, 0))
        self.pw = tk.StringVar()
        ttk.Entry(opt, textvariable=self.pw, show="•", width=24).grid(row=1, column=1, sticky="w", padx=6, pady=(8, 0))
        ttk.Label(opt, text="Port PC").grid(row=1, column=2, sticky="e", pady=(8, 0))
        self.port = tk.StringVar(value="8266")
        ttk.Entry(opt, textvariable=self.port, width=7).grid(row=1, column=3, sticky="w", padx=6, pady=(8, 0))

        ttk.Label(opt, text="Eigene .bin (optional)").grid(row=2, column=0, sticky="w", pady=(8, 0))
        self.binpath = tk.StringVar()
        ttk.Entry(opt, textvariable=self.binpath).grid(row=2, column=1, columnspan=2, sticky="we", padx=6, pady=(8, 0))
        ttk.Button(opt, text="Auswählen …", command=self.pick_bin).grid(row=2, column=3, sticky="w", pady=(8, 0))
        opt.columnconfigure(1, weight=1)

        # --- Start
        bar = ttk.Frame(main)
        bar.pack(fill="x")
        self.startbtn = ttk.Button(bar, text="Hermit OS installieren", style="Big.TButton", command=self.start)
        self.startbtn.pack(side="left")
        self.prog = ttk.Progressbar(bar, mode="indeterminate", length=220)
        self.prog.pack(side="left", padx=12)
        self.status = tk.StringVar(value="Bereit")
        ttk.Label(bar, textvariable=self.status).pack(side="left")

        # --- Protokoll
        logf = ttk.LabelFrame(main, text="Protokoll", padding=6)
        logf.pack(fill="both", expand=True, pady=(10, 0))
        self.log = tk.Text(logf, height=14, wrap="word", font=("Consolas", 9), bg="#0f1419", fg="#e6e9ec",
                           insertbackground="#e6e9ec", relief="flat")
        sb = ttk.Scrollbar(logf, command=self.log.yview)
        self.log.configure(yscrollcommand=sb.set, state="disabled")
        self.log.pack(side="left", fill="both", expand=True)
        sb.pack(side="right", fill="y")
        self.log.tag_configure("err", foreground="#ff6b6b")
        self.log.tag_configure("ok", foreground="#3fb950")

        # Ausgaben von hermit_flash ins Fenster umleiten
        hf.log = lambda msg: self.q.put(("log", time.strftime("%H:%M:%S") + " " + msg))
        root.after(100, self.pump)

    # ------------------------------------------------------------ Hilfen
    def write(self, text):
        tag = "err" if "FEHLER" in text else ("ok" if "FERTIG" in text else None)
        self.log.configure(state="normal")
        self.log.insert("end", text + "\n", tag)
        self.log.see("end")
        self.log.configure(state="disabled")

    def pump(self):
        try:
            while True:
                kind, val = self.q.get_nowait()
                if kind == "log":
                    self.write(val)
                elif kind == "info":
                    self.info.set(val)
                elif kind == "ips":
                    self.ipbox["values"] = val
                    if val and not self.ip.get():
                        self.ip.set(val[0].split()[0])
                elif kind == "done":
                    self.set_busy(False, val)
                elif kind == "ask":
                    msg, ev, box = val
                    box.append(messagebox.askyesno("Hermit OS Flasher", msg))
                    ev.set()
        except queue.Empty:
            pass
        self.root.after(100, self.pump)

    def set_busy(self, busy, text):
        self.busy = busy
        self.status.set(text)
        self.startbtn.configure(state="disabled" if busy else "normal")
        if busy:
            self.prog.start(12)
        else:
            self.prog.stop()

    def ask(self, msg):                     # aus dem Arbeits-Thread im GUI-Thread fragen
        ev, box = threading.Event(), []
        self.q.put(("ask", (msg, ev, box)))
        ev.wait()
        return box[0]

    def pick_bin(self):
        f = filedialog.askopenfilename(title="Firmware wählen", filetypes=[("ESP8266-Firmware", "*.bin")],
                                       initialdir=str(hf.BIN_DIR))
        if f:
            self.binpath.set(f)

    def args(self):
        ip = self.ip.get().split()[0].strip() if self.ip.get().strip() else ""
        if not ip:
            raise ValueError("Bitte IP-Adresse eingeben oder Netz durchsuchen")
        return argparse.Namespace(ip=ip, addon=self.addon.get(),
                                  variant=None if self.variant.get() == "automatisch" else self.variant.get(),
                                  bin=self.binpath.get() or None, user="admin", password=self.pw.get(),
                                  port=int(self.port.get() or 8266), serve_ip=None, mg2x_zip=None, yes=True)

    def thread(self, fn, busytext):
        if self.busy:
            return
        self.set_busy(True, busytext)

        def run():
            result = "Bereit"
            try:
                fn()
            except SystemExit:
                result = "Abgebrochen – siehe Protokoll"
            except Exception as e:
                self.q.put(("log", "FEHLER: " + str(e)))
                result = "Fehler"
            self.q.put(("done", result))
        threading.Thread(target=run, daemon=True).start()

    # ------------------------------------------------------------ Aktionen
    def scan(self):
        def job():
            try:
                me = hf.local_ip_for("8.8.8.8")
            except OSError:
                me = hf.local_ip_for("192.168.0.1")
            net = ipaddress.ip_network(me + "/24", strict=False)
            hf.log(f"Durchsuche {net} nach Shellys …")

            def probe(ip):
                try:
                    with urllib.request.urlopen(f"http://{ip}/shelly", timeout=1.2) as r:
                        j = json.loads(r.read())
                    if str(j.get("fw_id", "")).startswith("hermit-os"):
                        return f"{ip}   Hermit OS · {j.get('model')} · {j.get('name') or j.get('id')}"
                    if j.get("type") in hf.GEN1_MODELS:
                        return f"{ip}   Original · {hf.GEN1_MODELS[j['type']][0]}"
                    if j.get("type") or j.get("gen"):
                        return None                         # andere Shellys (Gen2 usw.) ausblenden
                except Exception:
                    pass
                try:
                    with urllib.request.urlopen(f"http://{ip}/cm?cmnd=Template", timeout=1.2) as r:
                        t = json.loads(r.read())
                    n = str(t.get("NAME", ""))
                    if "shelly" in n.lower():
                        return f"{ip}   Tasmota · {n}"
                except Exception:
                    pass
                try:
                    with urllib.request.urlopen(f"http://{ip}/", timeout=1.2) as r:
                        if b"Hermit Loader" in r.read(4000):
                            return f"{ip}   Hermit Loader"
                except Exception:
                    pass
                return None

            hosts = [str(h) for h in net.hosts() if str(h) != me]
            with concurrent.futures.ThreadPoolExecutor(64) as ex:
                found = [r for r in ex.map(probe, hosts) if r]
            found.sort(key=lambda s: tuple(int(x) for x in s.split()[0].split(".")))
            for f in found:
                hf.log("  " + f)
            hf.log(f"{len(found)} passende Geräte gefunden" if found else "Keine passenden Geräte gefunden")
            self.q.put(("ips", found))
        self.thread(job, "Suche …")

    def check(self):
        def job():
            a = self.args()
            dev = hf.Dev(a.ip, a.user, a.password)
            kind, info = hf.detect(dev)
            if not kind:
                self.q.put(("info", "Kein Shelly 1/1PM/Plug S/Duo erkannt"))
                hf.log(f"Unter {a.ip} kein unterstütztes Gerät gefunden")
                return
            v = a.variant or hf.variant_from(dev, kind, info, a.addon) or "? (bitte Firmware wählen)"
            extra = ""
            if kind == "hermit":
                extra = f" · {info.get('fw_id')}"
            elif kind == "stock":
                extra = f" · {info.get('fw', '')}"
            text = f"{KIND_NAMES[kind]}{extra}  →  installiert wird: {v}"
            self.q.put(("info", text))
            hf.log(f"{a.ip}: {text}")
        self.thread(job, "Prüfe …")

    def start(self):
        def job():
            a = self.args()
            dev = hf.Dev(a.ip, a.user, a.password)
            kind, info = hf.detect(dev)
            if not kind:
                hf.die(f"Unter {a.ip} kein unterstütztes Gerät gefunden")
            variant = a.variant or hf.variant_from(dev, kind, info, a.addon)
            if not variant:
                hf.die("Modell nicht erkannt – bitte Firmware im Auswahlfeld wählen")
            warn = ""
            if kind == "stock":
                warn = ("\n\nDabei wird per WLAN der Bootloader ersetzt. Strom während des Vorgangs nicht trennen. "
                        "Die Windows-Firewall muss Port " + str(a.port) + " zulassen.")
            if not self.ask(f"Gefunden: {KIND_NAMES[kind]}\nInstalliert wird: {variant}\nGerät: {a.ip}{warn}\n\nFortfahren?"):
                hf.log("Abgebrochen")
                return
            fw = hf.load_bin(a, variant)
            {"stock": lambda: hf.flash_stock(dev, a, info, variant, fw),
             "tasmota": lambda: hf.flash_tasmota(dev, a, variant, fw),
             "loader": lambda: hf.flash_loader(dev, a, variant, fw),
             "hermit": lambda: hf.flash_hermit(dev, a, variant, fw)}[kind]()
        self.thread(job, "Installiere …")


def main():
    root = tk.Tk()
    App(root)
    root.mainloop()


if __name__ == "__main__":
    main()
