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
