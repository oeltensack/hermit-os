# Pagurus OS aufspielen

Pagurus OS kommt auf drei Wegen auf ein Gerät – alle über WLAN, ohne das Gerät zu öffnen:

| Auf dem Gerät läuft | Weg | Werkzeug |
|---|---|---|
| **Original-Shelly-Firmware** | mg2hermit lädt Pagurus OS direkt | Flasher |
| **Tasmota** | Pagurus Loader → Pagurus OS | Flasher oder Tasmota-Weboberfläche |
| **Pagurus Loader** | Pagurus OS hochladen | Flasher oder Loader-Weboberfläche |
| **Pagurus OS** | Update | Auto-Update, Weboberfläche oder Flasher |

Als Notlösung bleibt das Flashen per Kabel (siehe unten).

---

## 1. Pagurus OS Flasher (grafisch)

Im Modellpaket `Pagurus-OS-<version>-Shelly-<Modell>.zip` liegen Firmware, Loader und Flasher zusammen.

**Start:** Doppelklick auf `Pagurus-Flasher.pyw` (Python 3 für Windows von python.org, Haken „Add to PATH“).

1. **Netz durchsuchen** – findet im eigenen /24-Netz alle Shelly 1, 1PM, Plug S, Plug und Duo und zeigt, was
   darauf läuft (Original, Tasmota, Loader, Pagurus OS). Gen2-Geräte werden ausgeblendet.
2. **Gerät prüfen** – zeigt Firmware und welche Pagurus-Variante installiert wird.
3. **Optionen** – Firmware automatisch oder von Hand, Add-on-Variante, Passwort (falls am Gerät gesetzt),
   Port für den PC-Webserver, eigene `.bin`.
4. **Pagurus OS installieren** – nach einer Rückfrage läuft alles automatisch; der Fortschritt steht im
   Protokoll (Fehler rot, Erfolg grün).

### Als EXE (ohne Python)

Einmal `tools\build_exe.bat` doppelklicken. Das Script installiert PyInstaller und baut:

- `Pagurus-Flasher.exe` – Oberfläche
- `pagurus-flash.exe` – Konsole

Beide liegen danach neben dem Ordner `bin\` und laufen mit diesem Ordner auch auf anderen PCs.
Windows SmartScreen meldet unsignierte EXE-Dateien manchmal – „Weitere Informationen → Trotzdem ausführen“.
Nach einem neuen Paket die EXE neu bauen, sonst steckt der alte Stand darin.

### Konsole

```bash
python tools/pagurus_flash.py 192.168.5.103                 # Modell und Weg werden erkannt
python tools/pagurus_flash.py 192.168.5.103 --addon         # Variante mit Add-on-Taster
python tools/pagurus_flash.py 192.168.5.103 --password xyz  # Gerät mit Login
python tools/pagurus_flash.py 192.168.5.103 --variant hermit-1pm --bin eigene.bin
```

Weitere Optionen: `--port` (Webserver, Standard 8266), `--serve-ip` (PC-IP, falls falsch erkannt),
`--mg2x-zip` (lokales mg2tasmota-Paket statt Download), `-y` (ohne Rückfrage).

---

## 2. Von der Original-Firmware (mg2hermit)

So läuft es im Hintergrund:

1. Der Flasher lädt einmal das offizielle **mg2tasmota**-Paket für das Modell (wird in `tools/.cache`
   gespeichert) und ersetzt darin nur die eingebaute Download-Adresse durch eine Adresse auf deinem PC.
   Die Prüfsummen im Paket werden neu berechnet; das Paket bleibt unkomprimiert wie das Original.
2. Auf dem PC startet ein kleiner Webserver (Port 8266) mit dem Paket und der Pagurus-Firmware.
3. Der Shelly bekommt `http://<ip>/ota?url=http://<pc>:8266/mg2hermit.zip`, installiert mg2hermit,
   startet 1–2× neu und lädt dann Pagurus OS vom PC.
4. Pagurus OS übernimmt beim ersten Start **WLAN und feste IP** aus der Konfiguration der Original-Firmware.
5. Der Flasher wartet auf Pagurus OS unter der alten IP und sucht das Gerät notfalls **per MAC-Adresse** im Netz.

**Voraussetzungen**
- Die **Windows-Firewall** muss eingehende Verbindungen auf Port 8266 für Python bzw. die EXE erlauben,
  passend zum Netzwerkprofil (privat/öffentlich).
- Während des Vorgangs den Strom nicht trennen – mg2hermit ersetzt per WLAN den Bootloader.
- Internet nur für den einmaligen Download des mg2tasmota-Pakets.

Unterstützte Modelle: Shelly 1 (`SHSW-1`), 1PM (`SHSW-PM`), Plug S (`SHPLG-S`), Plug (`SHPLG-1`, `SHPLG2-1`, über das Paket
`mg2tasmota-ShellyPlug2`), Duo (`SHBDUO-1`).

## 3. Von Tasmota (Pagurus Loader)

Tasmota lässt für Updates nur begrenzt Platz. Der **Pagurus Loader** (`bin/hermit-loader.bin.gz`, ca. 230 KB)
ist klein genug, um direkt von Tasmota aus installiert zu werden – ohne tasmota-minimal.

**Mit dem Flasher:** Gerät wählen, installieren – Loader und Pagurus OS werden nacheinander aufgespielt.
Erkannt wird das Modell über die Tasmota-Vorlage („Shelly 1“, „Shelly 1PM“, „Shelly Plug S“, „Shelly Plug“, „Shelly DUO“).

**Von Hand:**
1. Tasmota → Firmware-Update → `hermit-loader.bin.gz` hochladen
2. Der Loader übernimmt das WLAN aus den zurückgelassenen Tasmota-Einstellungen. Klappt das nicht, öffnet er
   den Zugangspunkt `hermit-loader-xxxxxx` → `http://192.168.33.1` → WLAN eintragen.
3. Im Loader die passende Pagurus-`.bin` hochladen (nicht `.gz`)
4. Pagurus OS startet und übernimmt WLAN und feste IP

Der Loader steuert die Relais nicht an und kann kein HTTPS (dadurch bleibt er klein).

**Sonoff RF Bridge (1 MB):** Hier gehört der **Pagurus Loader 1 MB** (`hermit-loader-1m.bin.gz`, ca. 310 KB) dazu.
Er kann HTTPS und installiert Pagurus OS nach 60 s selbst aus dem Netz (`update.json`), falls bis dahin keine Datei
hochgeladen wurde; von Hand wird `hermit-rfbridge.bin.gz` hochgeladen (hier die `.gz`). Der Flasher erkennt die
Tasmota-Vorlage „Sonoff Bridge“. Hat Tasmota zu wenig Platz, spielt der Flasher erst `tasmota-minimal` auf
(Tasmota lädt es selbst von ota.tasmota.com) und danach den Loader. Der Funkchip (EFM8) bleibt, wie er ist.

## 3b. Von ESPHome (1-MB-Steckdose mit ESP8285, „RowiSteckdose“)

Der Flasher erkennt ESPHome nicht – von Hand:

1. In der ESPHome-Konfiguration den Browser-Upload erlauben und einmal aus ESPHome flashen:
   ```yaml
   web_server:
     port: 80
   ota:
     - platform: esphome
     - platform: web_server
   ```
2. `http://<IP>` → „OTA Update“ → **`bin/hermit-loader-1m-plug1m.bin.gz`** hochladen (die `.gz`)
3. ESPHome-WLAN-Daten kann der Loader nicht übernehmen: mit **`hermit-loader-xxxxxx`** verbinden →
   `http://192.168.33.1` → WLAN eintragen
4. Nach 60 s installiert der Loader `hermit-plug1m` selbst aus dem Netz – oder vorher
   `bin/hermit-plug1m.bin.gz` im Loader hochladen

Belegung: Relais GPIO12, Touch-Taste GPIO13 (gedrückt = LOW), grüne LED GPIO4 (Relais), rote LED GPIO5 (WLAN).
Andere 1-MB-Steckdosen nur mit gleicher Belegung – sonst Variante anlegen (`src/device.h`).

## 4. Per Kabel (Notlösung)

USB-UART-Adapter mit **3,3 V**, Gerät **nur über den Adapter** versorgen – niemals gleichzeitig am Netz.
GPIO0 beim Einschalten auf GND legen, dann:

```bash
esptool.py --port COM5 erase_flash
esptool.py --port COM5 write_flash -fm dout 0x0 hermit-1pm.bin
```

Die Belegung der Stiftleiste (3V3, GND, RX, TX, GPIO0) steht auf der Platine bzw. in der Tasmota-Doku.
Bei der Duo ist das Gehäuse verklebt – dieser Weg ist dort nur mit Aufwand möglich.

---

## Nach dem Flashen

- Das Gerät meldet sich als `hermit-<modell>-<mac>` im WLAN, normalerweise unter derselben IP wie vorher.
- Wurde kein WLAN übernommen, öffnet es den Zugangspunkt `hermit-<modell>-<mac>` (offen) →
  `http://192.168.33.1` → Reiter **WLAN**. Der Zugangspunkt geht 2 Minuten nach der Verbindung aus.
- Für 1PM und Plug S: Leistung kalibrieren (siehe [GERAETE.md](GERAETE.md#messung-und-kalibrierung)).

## Updates

**Automatisch:** Weboberfläche → System → Automatische Updates

| Modus | Verhalten |
|---|---|
| Aus | keine Prüfung |
| Nur prüfen (Standard) | 2 min nach dem Start und täglich zur eingestellten Stunde prüfen, Hinweis in der Oberfläche |
| Automatisch installieren | zusätzlich installieren (Relais bleibt im Zustand), bei Fehler in der Stunde alle 10 min erneut |

- Quelle ist `update.json` in diesem Repo; jedes Gerät lädt nur die Datei seiner Variante.
- Vor dem Umschalten wird die **SHA-256** geprüft; stimmt sie nicht, bleibt die alte Firmware.
- **RF Bridge (1 MB):** Update in zwei Schritten über den Pagurus Loader 1 MB, siehe [GERAETE.md](GERAETE.md#sonoff-rf-bridge-433).
- Das Repo ist öffentlich, ein Token ist nicht nötig. **Nur für einen eigenen privaten Fork:** GitHub-Token
  eintragen (fine-grained, nur dieses Repo, *Contents: Read-only*) und die URL auf den Fork ändern.
- Alternativ `update.json` und `bin/` auf einem eigenen Webserver ablegen und die URL anpassen.

**Von Hand:** System → Firmware manuell → `.bin` hochladen, oder
`curl -F firmware=@hermit-1pm.bin http://<ip>/update`, oder mit dem Flasher.

---

## Fehlersuche

| Meldung / Verhalten | Ursache und Abhilfe |
|---|---|
| „Selbsttest fehlgeschlagen“ | Der PC erreicht seinen eigenen Webserver nicht – falsche PC-IP (`--serve-ip`) oder Port belegt |
| „Port 8266 ist belegt – versuche 8267“ | Eine alte Flasher-Sitzung läuft noch. Alle Flasher-Fenster schließen, im Task-Manager `python(w).exe` beenden |
| „Gerät hat mg2hermit nicht geladen“, keine Zeile „Verbindung von …“ | Firewall blockiert – Freigabe für Port und Netzwerkprofil prüfen |
| Update-Status springt auf `idle`, ohne dass etwas geladen wird | Ein früherer Versuch hängt. Gerät kurz vom Strom trennen und erneut starten |
| „Verbindung abgebrochen“ beim Laden von mg2hermit | Das Gerät hat das Paket verworfen – Protokoll aufheben und melden |
| Gerät meldet sich nicht unter der alten IP | Der Flasher sucht per MAC. Findet er nichts: Zugangspunkt `hermit-…` suchen |
| Nach dem Flashen weder im WLAN noch Zugangspunkt | Gerät aus- und einstecken, **innerhalb der ersten Minute 5× kurz den Taster** (bzw. 10 Flanken am SW-Eingang) drücken: WLAN-Daten werden gelöscht, der Zugangspunkt `hermit-…` öffnet unter 192.168.33.1. Ab 1.12.3 bleibt der Zugangspunkt auch erreichbar, während das Gerät das Heim-WLAN sucht; ab 1.12.4 probiert Pagurus OS beim ersten Start alle gefundenen WLAN-Zugangsdaten nacheinander |
| Tasmota: „Not enough space“ | Den Pagurus Loader (`.bin.gz`) statt Pagurus OS hochladen |
| Auto-Update: `update.json: HTTP 404` | URL falsch; bei privatem Fork Token fehlt oder abgelaufen |

**Diagnose am Gerät** (Reiter RPC): `Pagurus.TestImport` zeigt, welche WLAN- und IP-Daten aus Original-Firmware,
Tasmota und Loader gefunden werden (ohne Passwort). Details in [API.md](API.md#diagnose).
