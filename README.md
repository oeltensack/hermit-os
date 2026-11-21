# Hermit OS 🐚

*Wie ein Einsiedlerkrebs, der in ein altes Schneckenhaus zieht: neues Leben für alte Shelly-Gehäuse.*

Eigene Firmware für **Shelly 1** und **Shelly 1PM** (Gen1, ESP8266, 2 MB Flash).
Die Geräte sprechen damit die **RPC-API von Shelly Gen2/Gen3**. Scripte auf Plus-/Pro-/Gen3-Geräten
können sie genauso ansprechen wie ein neues Shelly, und die alten Geräte melden Ereignisse per Webhook zurück.

## Funktionen

- Gen2-RPC: `GET /rpc/Switch.Set?id=0&on=true` und `POST /rpc` (JSON-RPC 2.0-artig, mit `id`, `src`, `dst`)
- Methoden: `Shelly.*`, `Switch.*`, `Input.*`, `Sys.*`, `WiFi.*`, `Webhook.*` (Liste: `/rpc/Shelly.ListMethods`)
- Statusfelder wie bei Gen2: `output`, `source`, `timer_*`, `apower`, `aenergy.total/by_minute`, `temperature.tC`, `errors`
- Webhooks (Aktionen) mit Platzhaltern: `switch.on/off`, `input.toggle_on/off`, `input.button_push/longpush/doublepush/down/up`,
  `switch.power_above/below` (mit Schwelle in W, Hysterese 5 %), `switch.overpower`, `switch.overtemperature`, `sys.boot`
- Eingangsmodi: follow, flip, momentary, detached; Typ Schalter/Taster; invertierbar; AC-tauglich (50-Hz-Halbwellen werden überbrückt)
- Startzustand: off, on, restore_last, match_input; Auto-An/Auto-Aus; `toggle_after`
- 1PM: Wirkleistung über HLW8012 (CF an GPIO5), Energiezähler (alle 15 min ins Flash), NTC-Temperatur, Abschaltung bei Überlast/Übertemperatur
- Gen1-kompatibel: `/relay/0?turn=on&timer=10`, `/status`, `/meter/0`, `/shelly`
- WLAN mit DHCP/statisch; ohne Verbindung nach 60 s Access-Point `hermit-1-<mac>` bzw. `hermit-1pm-<mac>` auf **192.168.33.1** mit Captive-Portal
- Weboberfläche auf Deutsch, RPC-Konsole, OTA-Update über die Weboberfläche (`/update`)
- Optional Passwort (Benutzer `admin`, Digest- oder Basic-Auth)
- mDNS: `hermit-1-<mac>.local` bzw. `hermit-1pm-<mac>.local`, Dienst `_shelly._tcp`

## Shelly Plug S

Eigene Variante `hermit-plugs` (`bin/hermit-plugs.bin`):

- Relais GPIO15, Taster am Gerät GPIO13 (Standard: Druck schaltet um), Relais-LED GPIO0, WLAN-LED GPIO2
- BL0937 mit **Leistung, Spannung und Strom** (CF GPIO5, CF1 GPIO14, SEL GPIO12 – HIGH = Spannung) – `voltage`, `current`, `pf`
  im Status wie bei Gen2; Spannung/Strom werden abwechselnd alle 2 s gemessen
- Standardfaktoren wie Tasmota (BL0937); Kalibrieren unter System → Leistung kalibrieren
  (`Switch.Calibrate` mit `apower`, `voltage`, `current`)
- Abschaltleistung ab Werk 2500 W (Plug S: max. 10 A), Übertemperatur über internen NTC
- Flash-Tool erkennt die Plug S mit Original-Firmware (`SHPLG-S`) und Tasmota-Template „Shelly Plug S“

## Shelly Duo (E27 / GU10)

Eigene Variante `hermit-duo` (`bin/hermit-duo.bin`) für die weiße Duo (SHBDUO-1, nicht Duo RGBW):

- API wie die **Shelly Duo Bulb Gen3**: Komponente `cct:0` mit `CCT.Set` (`on`, `brightness` 1–100,
  `ct` 2700–6500 K, `transition_duration`, `toggle_after`), `CCT.Toggle`, `CCT.GetStatus`, `CCT.SetConfig`
- Gen1-kompatibel: `/light/0?turn=on&brightness=50&temp=3000`
- Weiches Überblenden (Standard 0,5 s), Gamma-Korrektur, Helligkeit/Farbtemperatur bleiben gespeichert
- Nach Stromzufuhr **an** (Lampe am Wandschalter); umstellbar auf aus / letzter Zustand
- Aktionen: `cct.on`, `cct.off`, `sys.boot`
- **WLAN-Reset:** Lampe 5× kurz hintereinander aus- und einschalten (jeweils weniger als 6 s an)
- PWM: Helligkeit GPIO5, Farbtemperatur GPIO4 (1 kHz). Sind warm/kalt vertauscht:
  „Farbtemperatur-Kanal umkehren“

Gen2-Script-Beispiel:

```js
Shelly.call("HTTP.GET", {url: "http://192.168.0.70/rpc/CCT.Set?id=0&on=true&brightness=40&ct=2700"});
```

## Pinbelegung

| | Shelly 1 | Shelly 1PM |
|---|---|---|
| Relais | GPIO4 | GPIO15 |
| SW-Eingang | GPIO5 | GPIO4 |
| LED | – | GPIO0 (invertiert) |
| HLW8012 CF | – | GPIO5 |
| NTC | – | ADC |

## Bauen

```bash
pip install platformio
pio run -e shelly1      # .pio/build/shelly1/firmware.bin
pio run -e shelly1pm    # .pio/build/shelly1pm/firmware.bin
```

Fertige Binärdateien liegen unter `bin/`.

## Flashen

**Seriell** (USB-UART 3,3 V, Gerät nur über den Adapter versorgt, nicht am Netz):
GPIO0 beim Einschalten auf GND legen, dann

```bash
esptool.py --port /dev/ttyUSB0 erase_flash
esptool.py --port /dev/ttyUSB0 write_flash -fm dout 0x0 hermit-1pm.bin
```

Die Belegung der Stiftleiste (3V3, GND, RX, TX, GPIO0) steht auf der Platine bzw. in der Tasmota-Doku zum jeweiligen Gerät.

**Ohne Öffnen:** Über das Projekt *mgos-to-tasmota* (arendst) lässt sich die Original-Firmware per OTA durch Tasmota ersetzen.
Danach in Tasmota unter „Firmware-Update“ die passende `.bin` dieser Firmware hochladen. Die Seriell-Variante ist der sichere Weg,
falls dabei etwas schiefgeht.

Spätere Updates: Weboberfläche → System → Firmware, oder `curl -F firmware=@hermit-1pm.bin http://<ip>/update`.

## Hermit OS Flasher (grafisch)

Doppelklick auf **`Hermit-Flasher.pyw`** (Windows, Python 3 installiert) öffnet die Oberfläche:

- **Netz durchsuchen** findet Shelly 1/1PM/Plug S/Duo mit Original-Firmware, Tasmota, Loader oder Hermit OS
- **Gerät prüfen** zeigt, was läuft und welche Firmware installiert wird
- Firmware automatisch oder von Hand wählen, Add-on-Variante, Passwort, eigene .bin
- **Hermit OS installieren** – Fortschritt im Protokoll

Alternativ: `python tools/hermit_flash_gui.py`. Unter Linux ggf. `python3-tk` installieren.

**Als EXE:** Einmal `tools\build_exe.bat` doppelklicken (installiert PyInstaller und baut). Danach liegen
`Hermit-Flasher.exe` (Oberfläche) und `hermit-flash.exe` (Konsole) neben dem Ordner `bin\` und laufen
ohne Python – zusammen mit `bin\` auch auf anderen PCs.

## Flash-Tool: `tools/hermit_flash.py`

Schreibt Hermit OS über WLAN auf einen Shelly 1 / 1PM und erkennt selbst, was gerade läuft:

| Auf dem Gerät | Ablauf |
|---|---|
| **Original-Shelly-Firmware** | angepasstes mg2tasmota („mg2hermit“) → lädt Hermit OS direkt |
| **Tasmota** | Hermit Loader → Hermit OS |
| **Hermit Loader** | Hermit OS |
| **Hermit OS** | Firmware-Update |

```bash
python3 tools/hermit_flash.py 192.168.5.153            # Modell wird erkannt
python3 tools/hermit_flash.py 192.168.5.153 --addon    # Add-on-Variante
python3 tools/hermit_flash.py 192.168.5.153 --password geheim   # bei Login-Schutz
```

- Nur Python 3 (Standardbibliothek), PC im selben Netz wie der Shelly
- Für die Original-Firmware startet das Tool auf dem PC einen kleinen Webserver (Port 8266) – die
  **Windows-Firewall** muss den Zugriff erlauben. Internet braucht es nur einmal für das mg2tasmota-Paket
  (wird in `tools/.cache` gespeichert; alternativ `--mg2x-zip`).
- mg2hermit ist mg2tasmota (GPL-3.0, Mark Dornbach) mit geänderter Download-Adresse; das Tool lädt das
  Original und passt es lokal an.
- Hermit OS übernimmt das WLAN **und eine feste IP** aus der Original-Firmware bzw. aus Tasmota automatisch.
- Taucht das Gerät trotzdem unter einer anderen IP auf, sucht der Flasher es per MAC-Adresse im Netz.
- Wie bei mg2tasmota wird per OTA der Bootloader ersetzt: Strom während des Vorgangs nicht trennen.

## Von Tasmota direkt zu Hermit OS (Hermit Loader)

Ist in Tasmota zu wenig Platz für Hermit OS („Not enough space“), geht es ohne tasmota-minimal über den
**Hermit Loader** (`bin/hermit-loader.bin.gz`, ca. 230 KB):

1. Tasmota → Firmware-Update → `hermit-loader.bin.gz` hochladen
2. Der Loader übernimmt das WLAN aus den zurückgelassenen Tasmota-Einstellungen (bzw. dem WLAN-Speicher des ESP);
   klappt das nicht, öffnet er den AP
   `hermit-loader-xxxxxx` → `http://192.168.33.1` → WLAN eintragen
3. Im Loader die passende Hermit-`.bin` hochladen (`hermit-1.bin`, `hermit-1pm.bin`, `…-addon.bin`)
4. Hermit OS startet und übernimmt das WLAN aus dem Loader automatisch

Der Loader steuert die Relais nicht an.

## Ersteinrichtung

1. Nach dem Flashen öffnet das Gerät den AP `hermit-1-xxxxxxxxxxxx` bzw. `hermit-1pm-…` (offen).
2. Verbinden, `http://192.168.33.1` öffnen, Reiter **WLAN**, SSID und Passwort eintragen.
3. Der AP geht 2 Minuten nach erfolgreicher Verbindung aus.

**WLAN zurücksetzen:** innerhalb der ersten 60 s nach dem Einschalten den SW-Eingang 5× ein und aus (10 Flanken).

## Leistung kalibrieren (1PM)

Der Shelly 1PM misst mit einem **BL0937** (nur Wirkleistung über CF). Ab Werk rechnet die Firmware mit demselben
Standardfaktor wie Tasmota für den BL0937: **1,7066 W/Hz** (1362 × 12530 / 10 / Pulsdauer in µs).
Die Werkskalibrierung der Original-Shelly-Firmware ist nach dem Flashen nicht mehr vorhanden.

Möglichkeiten (Weboberfläche → System → Leistung kalibrieren, oder per RPC):

| Weg | RPC |
|---|---|
| Referenzlast messen (genaueste Methode) | `/rpc/Switch.Calibrate?id=0&apower=1985.4` |
| Tasmota-Wert übernehmen (Konsole `PowerCal`, vor dem Flashen auslesen) | `/rpc/Switch.Calibrate?id=0&tasmota_powercal=12530` |
| Faktor direkt setzen | `/rpc/Switch.Calibrate?id=0&factor=1.7066` |
| Zurück auf Standard | `/rpc/Switch.Calibrate?id=0&reset=true` |

Der Faktor liegt in einer eigenen Datei und bleibt bei Firmware-Updates **und** Werkseinstellungen erhalten.
`Switch.GetConfig` zeigt ihn unter `pm_calibration`.

## Variante mit Add-on (externer Taster)

Für Shelly 1 / 1PM mit dem **Shelly-Temperatur-Add-on (Gen1)** gibt es eigene Firmware-Dateien:
`bin/hermit-1-addon.bin` und `bin/hermit-1pm-addon.bin`. Das Add-on wird nicht für Sensoren genutzt,
sondern als **zusätzlicher, potenzialfreier Tastereingang**.

- Anschluss: Taster zwischen **DATA** und **GND** des Add-ons
- Erscheint wie beim Plus-Add-on als `input:100` (`Input.GetStatus?id=100`, `SensorAddon.GetPeripherals`)
- Typ Taster oder Schalter, Ereignisse `input.button_push/doublepush/longpush/down/up` bzw. `input.toggle_on/off`
  mit `cid: 100` als Webhook
- Wirkung aufs Relais wählbar: keine (Standard), umschalten bei Druck, umschalten bei jeder Flanke, folgen
- Pinbelegung: GPIO0 = Add-on-Ausgang (Leitung freigeben), GPIO3 = Eingang. Beim 1PM entfällt dadurch die LED,
  die serielle Ausgabe läuft nur noch auf TX.
- Zeigt der Eingang dauerhaft „gedrückt“, in den Einstellungen „Add-on-Ausgang invertieren“ setzen;
  ist der Zustand vertauscht, „Eingang invertieren“.

Webhook für den Add-on-Taster per RPC:

```
/rpc/Webhook.Create?event="input.button_push"&cid=100&urls=["http://192.168.0.60/rpc/Switch.Toggle?id=0"]
```

## Automatische Updates

Die Geräte lesen `update.json` aus diesem Repo und holen sich die Datei ihrer Variante
(`hermit-1`, `hermit-1pm`, `hermit-1-addon`, `hermit-1pm-addon`).

- Weboberfläche → System → Automatische Updates: **Aus**, **Nur prüfen** (Standard) oder **Automatisch installieren**
- Prüfung 2 min nach dem Start und täglich zur eingestellten Stunde (Standard 3 Uhr)
- Installiert wird **nur bei ausgeschaltetem Relais**; ist es an, wird es in der Stunde alle 10 min erneut versucht
- Vor dem Umschalten wird die **SHA-256** aus `update.json` geprüft; bei Abweichung bleibt die alte Firmware
- API wie Gen2: `Shelly.CheckForUpdate`, `Shelly.Update` (`{"stage":"stable"}` oder `{"url":"…"}`),
  `Sys.GetStatus` → `available_updates`
- Einstellungen per RPC: `Sys.SetConfig` mit `{"config":{"updates":{"mode":"install","hour":3,"url":"…","token":"…"}}}`

**Privates Repo:** Die Geräte brauchen dann einen GitHub-Token (fine-grained, nur Leserecht „Contents“ auf dieses Repo),
einzutragen unter „GitHub-Token“. Alternativ das Repo öffentlich machen oder `update.json` + `bin/` auf einem eigenen
Webserver ablegen und die URL anpassen.

**Neue Version veröffentlichen:** Version in `platformio.ini` erhöhen, dann

```bash
python3 tools/make_release.py   # baut alle Varianten, füllt bin/ und schreibt update.json
git add -A && git commit -m "Version x.y.z" && git push
```

## Zusammenspiel mit Gen2/Gen3-Scripten

**Altgerät aus einem Script schalten:**

```js
Shelly.call("HTTP.GET", { url: "http://192.168.0.50/rpc/Switch.Set?id=0&on=true" });
```

**Status/Leistung lesen:**

```js
Shelly.call("HTTP.GET", { url: "http://192.168.0.51/rpc/Switch.GetStatus?id=0" }, function (r) {
  let st = JSON.parse(r.body);   // st.output, st.apower, st.aenergy.total, st.temperature.tC
});
```

**Ereignisse vom Altgerät empfangen:** Im Gen2-Script `HTTPServer.registerEndpoint("alt", …)`,
im Altgerät eine Aktion mit der URL `http://<gen2-ip>/script/1/alt?ev=${event}&out=${output}`.

**Altgerät schaltet direkt ein Gen2-Gerät** (ohne Script): Aktion `input.toggle_on` →
`http://<gen2-ip>/rpc/Switch.Set?id=0&on=true`.

Mit Passwort: `http://admin:<pass>@<ip>/rpc/...`

Fertige Scripte liegen in `examples/`:

- `gen2_schaltet_altgeraet.js` – Gen2-Relais wird aufs Altgerät gespiegelt
- `gen2_empfaengt_ereignisse.js` – Endpoint für Webhooks vom Altgerät
- `gen2_liest_leistung.js` – Leistung des 1PM abfragen, eigenes Relais mit Nachlauf schalten

### Platzhalter in Aktions-URLs

`${event}` `${output}` `${input}` `${apower}` `${aenergy}` (Wh) `${tC}` `${id}` `${name}` `${ip}`

## Abweichungen von echtem Gen2

- Kein WebSocket-RPC, kein MQTT, keine Cloud, keine Shelly-App. Die Home-Assistant-Shelly-Integration erwartet bei Gen2
  einen WebSocket und funktioniert deshalb nicht. Für HA eignen sich REST-Sensoren und -Schalter.
- `Shelly.SetAuth` nimmt `{"pass":"..."}` bzw. `{"pass":null}` statt `ha1`.
- Webhook-Bedingung als Zahl (`threshold` in W) statt als JS-Ausdruck. Ein numerisches `condition` wird ebenfalls angenommen.
- `Switch.Calibrate` ist eine eigene Methode.
- Keine Scripte auf dem Gerät selbst (dafür reicht der ESP8266 mit 80 KB RAM nicht sinnvoll); die Logik liegt auf dem Gen2/Gen3.
