# Geräte

## Übersicht

| | Shelly 1 | Shelly 1PM | Plug S | Plug (16 A) | Duo (E27/GU10) |
|---|---|---|---|---|---|
| Firmware | `hermit-1` | `hermit-1pm` | `hermit-plugs` | `hermit-plug` | `hermit-duo` |
| Add-on-Variante | `hermit-1-addon` | `hermit-1pm-addon` | – | – | – |
| Messfirmware | – | `hermit-1pm-meter` | `hermit-plugs-meter` | `hermit-plug-meter` | – |
| Gen1-Typ | `SHSW-1` | `SHSW-PM` | `SHPLG-S` | `SHPLG-1` / `SHPLG2-1` | `SHBDUO-1` |
| Hermit-Modell | `HERMIT-1` | `HERMIT-1PM` | `HERMIT-PLUGS` | `HERMIT-PLUG` | `HERMIT-DUO` |
| Gen2-Komponente | `switch:0`, `input:0` | `switch:0`, `input:0` | `switch:0`, `input:0` | `switch:0`, `input:0` | `cct:0` |
| Relais | GPIO4 | GPIO15 | GPIO15 | GPIO4 | – |
| Eingang | SW, GPIO5 | SW, GPIO4 | Taster, GPIO13 (Pull-up) | Taster, GPIO12 (gedrückt = HIGH) | – |
| LED | – | GPIO0 (invertiert) | GPIO0 Relais, GPIO2 WLAN | GPIO14 rot Relais, GPIO16 blau WLAN, GPIO13 grün aus | – |
| Messchip | – | BL0937 (CF GPIO5) | BL0937 (CF GPIO5, CF1 GPIO14, SEL GPIO12) | HLW8012 (nur CF GPIO5) | – |
| Temperatur | – | NTC am ADC | NTC am ADC | – | – |
| Licht | – | – | – | – | PWM kalt GPIO5, warm GPIO4 |
| Abschaltleistung ab Werk | – | 3500 W | 2500 W | 3500 W | – |
| Nach Stromausfall ab Werk | aus | aus | aus | aus | **an** |

Alle Geräte: ESP8266, 2 MB Flash. Die Oberfläche zeigt unter Status → Flash die echte Chipgröße.

---

## Eingang (Shelly 1 / 1PM / Plug S / Plug)

**Eingangsmodus** (Reiter Schalter):

| Modus | Wirkung aufs Relais |
|---|---|
| Folgen | Schalter an = Relais an (Standard bei 1/1PM) |
| Umschalten bei jeder Flanke | jede Änderung schaltet um |
| Taster | jeder Druck schaltet um (Standard bei Plug S) |
| Getrennt | Relais bleibt unberührt, nur Ereignisse |

- **Typ** Schalter oder Taster bestimmt die Ereignisse (`toggle_on/off` bzw. `button_push/doublepush/longpush/down/up`).
- **Eingangsfilter:** *Wechselspannung* überbrückt 50-Hz-Halbwellen, *Nur Pegel* liest den reinen Pegel.
  Beim Shelly 1 liefert der SW-Eingang schon intern einen geglätteten Pegel – dort reicht „Nur Pegel“.
- **Hinweis Shelly 1:** Der SW-Eingang schaltet bei **L und bei N** – das liegt an der nicht getrennten
  Stromversorgung im Gerät und ist bei der Original-Firmware genauso. Für einen potenzialfreien Eingang die
  Add-on-Variante nutzen.
- **Eingang messen (roh):** zeigt HIGH-Anteil und Flanken in 200 ms – hilfreich bei unklarem Verhalten.

## WLAN zurücksetzen

| Gerät | Vorgehen |
|---|---|
| Shelly 1 / 1PM / Plug S | innerhalb der ersten 60 s nach dem Einschalten den Eingang bzw. Taster 5× ein und aus (10 Flanken) |
| Duo | Lampe 5× kurz hintereinander aus- und einschalten, jeweils unter 6 s an. Es zählen nur echte Stromunterbrechungen, keine Neustarts nach Updates |
| alle | Weboberfläche → System → Werkseinstellungen |

Danach öffnet das Gerät den Zugangspunkt `hermit-<modell>-<mac>` auf 192.168.33.1. Nach einem Reset
werden die alten WLAN-Daten aus Original-Firmware oder Tasmota **nicht** erneut übernommen.

---

## Messung und Kalibrierung

### Shelly 1PM

Der 1PM misst mit einem **BL0937** nur die Wirkleistung (CF). Standardfaktor wie bei Tasmota:
**1,7066 W/Hz** (1362 × 12530 / 10 / Pulsdauer in µs). Die Werkskalibrierung der Original-Firmware geht
beim Flashen verloren – deshalb jedes Gerät einmal kalibrieren.

### Shelly Plug S

BL0937 mit Leistung (CF) und – abwechselnd alle 2 s – **Spannung und Strom** (CF1, umgeschaltet über SEL).
Bei der Plug S misst der Chip bei **SEL HIGH die Spannung**. Daraus ergeben sich `voltage`, `current` und `pf`.
Standardfaktoren: 0,16029 V/Hz und 0,01155 A/Hz (wie Tasmota). Strom wird nur angezeigt, solange Leistung fließt.

### Shelly Plug (16 A)

HLW8012, nur Wirkleistung (CF) – keine Spannung, kein Strom, keine Temperatur. Standardfaktor wie Tasmota
für HLW8012: **12,53 W/Hz** (10000 × 12530 / 10 / Pulsdauer in µs). Der Wert ist nicht am Gerät
nachgemessen – einmal mit Referenzlast kalibrieren. Ein `PowerCal` aus Tasmota wird mit der
HLW8012-Konstante umgerechnet. Die LED-Belegung (rot/blau/grün) stammt aus Tasmota/ESPHome; falls die
Farben vertauscht sind, mit `Hermit.Gpio` prüfen.

### Kalibrieren

Weboberfläche → System → Leistung kalibrieren, am besten mit einer **ohmschen Last** (Heizlüfter, Wasserkocher)
und einem Messgerät. Je größer die Last, desto genauer.

| Weg | RPC |
|---|---|
| Referenzleistung | `/rpc/Switch.Calibrate?id=0&apower=1985.4` |
| Spannung / Strom (Plug S) | `/rpc/Switch.Calibrate?id=0&voltage=231.5&current=8.62` |
| Wert aus Tasmota (`PowerCal`, vor dem Flashen auslesen) | `/rpc/Switch.Calibrate?id=0&tasmota_powercal=12530` |
| Faktor direkt | `/rpc/Switch.Calibrate?id=0&factor=1.7066` |
| Zurück auf Standard | `/rpc/Switch.Calibrate?id=0&reset=true` |

Die Faktoren liegen in einer eigenen Datei und bleiben bei Updates **und** Werkseinstellungen erhalten.
`Switch.GetConfig` zeigt sie unter `pm_calibration`.

### Schutzfunktionen

- **Überlast:** über der Abschaltleistung schaltet das Relais ab (`errors: ["overpower"]`, Ereignis
  `switch.overpower`). Erneutes Einschalten setzt den Fehler zurück.
- **Übertemperatur:** über der Abschalttemperatur (Standard 95 °C) aus und gesperrt, bis das Gerät 10 K
  abgekühlt ist (`overtemp`, `switch.overtemperature`).
- **Energiezähler** werden alle 15 min und vor jedem Neustart gespeichert.

---

## Messfirmware (Shelly 1PM / Plug S / Plug)

`hermit-1pm-meter`, `hermit-plugs-meter` und `hermit-plug-meter` sind reine Messgeräte mit Schutzabschaltung:

- Das Relais ist nach dem Einschalten **an** und lässt sich **nicht fernschalten**:
  `Switch.Set`/`Switch.Toggle` antworten mit Fehler −114, `/relay/0?turn=…` wird ignoriert.
- Messwerte, Energie und Ereignisse gehen wie gewohnt an Gen2+-Geräte (Status, Webhooks,
  `switch.power_above`/`power_below`, `switch.overpower`, `switch.overtemperature`).
- Bei Überschreiten der **Abschaltleistung** bzw. **Abschalttemperatur** fällt das Relais ab.
  Die Abschaltung ist gespeichert und bleibt auch nach einem Stromausfall bestehen.
- **Zurücksetzen nur von Hand:** beim Plug S und Plug mit dem Gerätetaster, beim 1PM über den SW-Eingang
  (Schalter: jede Flanke, Taster: Druck). Bei Übertemperatur erst nach Abkühlen um 10 °C.
  Danach kommt das Ereignis `switch.reset` und `switch.on` mit `source: reset`.
- `Switch.GetStatus` meldet zusätzlich `meter_only: true` und `tripped: true|false`.
- Die Eingangsereignisse (`input.*`) werden weiter gemeldet.
- **Update:** trotz angezogenem Relais möglich. Das automatische Update wartet, bis die Last unter 20 W liegt;
  beim Neustart fällt das Relais ca. 1 s ab.

Installieren: Flasher → Haken „Messfirmware“ oder Variante wählen, CLI `--meter`. Zurück zur normalen
Firmware jederzeit über System → Firmware-Datei (`hermit-1pm.bin`, `hermit-plugs.bin` bzw. `hermit-plug.bin`).

## Add-on-Variante (Shelly 1 / 1PM)

Mit dem **Shelly-Temperatur-Add-on (Gen1)** als **zusätzlichem, potenzialfreiem Taster**:

- Anschluss: Taster zwischen **DATA** und **GND** des Add-ons
- Erscheint wie beim Plus-Add-on als `input:100` (`Input.GetStatus?id=100`, `SensorAddon.GetPeripherals`)
- Ereignisse mit `cid: 100`; Wirkung aufs Relais wählbar (keine, umschalten bei Druck, bei jeder Flanke, folgen)
- Pins: GPIO0 = Add-on-Ausgang (Leitung freigeben), GPIO3 = Eingang. Beim 1PM entfällt dadurch die LED,
  die serielle Ausgabe läuft nur noch auf TX.
- Zeigt der Eingang dauerhaft „gedrückt“: **Add-on-Ausgang invertieren**. Ist der Zustand vertauscht:
  **Eingang invertieren**.

Sensoren (DS18B20, DHT22) werden nicht unterstützt.

---

## Shelly Duo

Weiße Duo (E27 und GU10, `SHBDUO-1`) mit **zwei Weißkanälen**: Kaltweiß an GPIO5, Warmweiß an GPIO4
(PWM 1 kHz). Hermit OS verteilt Helligkeit und Farbtemperatur so auf beide Kanäle, dass die Gesamthelligkeit
gleich bleibt – 2700 K nur warm, 6500 K nur kalt, dazwischen gemischt. Gamma 2,2, weiches Überblenden
(Standard 0,5 s).

- **Bedienung** wie die Shelly Duo Bulb Gen3 über `CCT.Set` (siehe [API.md](API.md#cct-duo))
- **Regler und Farbknöpfe** ändern bei ausgeschalteter Lampe nur die gespeicherten Werte
- **Nach Einschalten am Wandschalter:** an (Standard), aus oder letzter Zustand
- Helligkeit und Farbtemperatur werden gespeichert (verzögert, um den Flash zu schonen)
- **Warm-/Kaltkanal tauschen**, falls die Farben vertauscht sind
- Die Duo RGBW (`SHCB-1`) ist eine andere Hardware und wird nicht unterstützt

---

## Weboberfläche

| Reiter | Inhalt |
|---|---|
| Status | Schalten, Messwerte, WLAN, Laufzeit, Flash; bei der Duo Regler für Helligkeit und Farbtemperatur |
| Schalter / Licht | Name, Eingangsmodus, Verhalten nach Stromausfall, Auto-An/Aus, Abschaltgrenzen, Eingang, Add-on, bei der Duo Überblendzeit |
| Aktionen | Webhooks anlegen, aktivieren, löschen |
| WLAN | Netzsuche, DHCP/statisch, Passwort des Zugangspunkts |
| System | Name, Zeitzone, NTP, LED, Passwort, Kalibrierung, Auto-Update, Firmware, Neustart, Werkseinstellungen |
| RPC | Konsole für alle Methoden |
