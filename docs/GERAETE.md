# Geräte

## Übersicht

| | Shelly 1 | Shelly 1PM | Plug S | Plug (16 A) | Duo (E27/GU10) | Dimmer 2 |
|---|---|---|---|---|---|---|
| Firmware | `hermit-1` | `hermit-1pm` | `hermit-plugs` | `hermit-plug` | `hermit-duo` | `hermit-dimmer2` |
| Add-on-Variante | `hermit-1-addon` | `hermit-1pm-addon` | – | – | – | – |
| Messfirmware | – | `hermit-1pm-meter` | `hermit-plugs-meter` | `hermit-plug-meter` | – | – |
| Gen1-Typ | `SHSW-1` | `SHSW-PM` | `SHPLG-S` | `SHPLG-1` / `SHPLG2-1` | `SHBDUO-1` | `SHDM-2` |
| Hermit-Modell | `HERMIT-1` | `HERMIT-1PM` | `HERMIT-PLUGS` | `HERMIT-PLUG` | `HERMIT-DUO` | `HERMIT-DIMMER2` |
| Gen2-Komponente | `switch:0`, `input:0` | `switch:0`, `input:0` | `switch:0`, `input:0` | `switch:0`, `input:0` | `cct:0` | `light:0`, `input:0`, `input:1` |
| Relais | GPIO4 | GPIO15 | GPIO15 | GPIO4 | – | – (STM32, Reset GPIO5, BOOT0 GPIO4) |
| Eingang | SW, GPIO5 | SW, GPIO4 | Taster, GPIO13 (Pull-up) | Taster, GPIO12 (gedrückt = HIGH) | – | SW1 GPIO14, SW2 GPIO12 |
| LED | – | GPIO0 (invertiert) | GPIO0 Relais, GPIO2 WLAN | GPIO14 rot Relais, GPIO16 blau WLAN, GPIO13 grün aus | – | GPIO16 (invertiert) |
| Messchip | – | BL0937 (CF GPIO5) | BL0937 (CF GPIO5, CF1 GPIO14, SEL GPIO12) | HLW8012 (nur CF GPIO5) | – | im STM32 (UART GPIO1/3) |
| Temperatur | – | NTC am ADC | NTC am ADC | – | – | NTC am ADC |
| Licht | – | – | – | – | PWM kalt GPIO5, warm GPIO4 | Phasenabschnitt/-anschnitt über STM32 |
| Abschaltleistung ab Werk | – | 3500 W | 2500 W | 3500 W | – | 250 W |
| Nach Stromausfall ab Werk | aus | aus | aus | aus | **an** | letzter Zustand |

Alle Shellys: ESP8266, 2 MB Flash. Die Sonoff RF Bridge hat 1 MB (siehe unten). Die Oberfläche zeigt unter
Status → Flash die echte Chipgröße.

---

## WLAN mit mehreren Access Points

Bei mehreren APs mit gleichem Namen (Mesh, Omada, UniFi …) verbindet sich der ESP8266 von sich aus mit dem
ersten gefundenen und bleibt dort, auch wenn ein näherer AP da ist. Hermit OS wählt deshalb vor dem Verbinden
den stärksten AP. Fällt der Empfang unter −70 dBm, sucht es alle 5 Minuten nach einem mindestens 8 dB
besseren AP und wechselt (kurze Unterbrechung von 1–3 s). Einstellbar unter **WLAN**; dort steht auch, mit
welchem AP (BSSID) und Kanal das Gerät verbunden ist.

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
| Shelly 1 / 1PM / 2.5 / Plug S / Dimmer 2 | innerhalb der ersten 60 s nach dem Einschalten den Eingang bzw. Taster (beim Dimmer SW1) 5× ein und aus (10 Flanken) |
| Duo | Lampe 5× kurz hintereinander aus- und einschalten, jeweils unter 6 s an. Es zählen nur echte Stromunterbrechungen, keine Neustarts nach Updates |
| Shelly 2.5 | zusätzlich: Taster auf der Platine 10 s gedrückt halten |
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
- **Update:** trotz angezogenem Relais; das Relais bleibt beim Neustart angezogen.

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

## Shelly Dimmer 2

Der Dimmer 2 (`SHDM-2`) hat neben dem ESP8266 einen **STM32**, der den Phasenan- bzw. -abschnitt und die
Leistungsmessung macht. Hermit OS spricht mit ihm über UART (GPIO1/3, 115200 Baud) und spielt **keine**
STM32-Firmware auf – die Original-STM32-Firmware bleibt, der Weg zurück zur Shelly-Firmware bleibt offen.
War vorher Tasmota oder ESPHome drauf (quelloffene STM32-Firmware v51.x), wird das erkannt und ebenfalls bedient.

- **Bedienung** wie Shelly Dimmer Gen3 über `Light.Set` (siehe [API.md](API.md#light-dimmer-2))
- **Taster** (Standard „Ein Taster“): kurz = an/aus, halten = dimmen, die Richtung wechselt bei jedem Halten;
  aus und gehalten = ab Minimum heller. „Zwei Taster“: SW1 kurz an / halten heller, SW2 kurz aus / halten dunkler.
  Für Schalter: „Licht folgt SW1“ oder „jede Flanke schaltet um“
- **Minimale Helligkeit** gegen Flackern bei LED-Lampen, **maximale Helligkeit** zum Begrenzen
- **Dimmart:** Phasenabschnitt (Standard, für LED und elektronische Trafos), Phasenanschnitt (Halogen, Wickeltrafo)
- **Überblendzeit** (Standard 1 s) und **Dimmgeschwindigkeit** der Taster einstellbar
- **Update und Neustart:** Der STM32 läuft weiter, das Licht bleibt in Helligkeit und Zustand
- **Schutz:** Abschaltung über 250 W bzw. 85 °C (einstellbar)
- **Leistung:** kommt vom STM32. Ab STM32-Firmware 2.63 meldet er Watt, ältere Stände eine Periodendauer –
  beides wird ausgewertet. Mit `Light.Calibrate` (Referenzlast) lässt sich der Wert korrigieren.
  Ohne Neutralleiter misst der Dimmer ungenau.
- **Diagnose:** `Hermit.Stm` zeigt Protokoll, STM32-Version, Rohwerte und Rahmenzähler

---

## Shelly 2.5

| | Schalter (`hermit-25`) | Rollladen (`hermit-25-cover`) |
|---|---|---|
| Gen1-Typ / Modell | `SHSW-25` / `HERMIT-25` | `SHSW-25` / `HERMIT-25-COVER` |
| Gen2-Komponenten | `switch:0`, `switch:1`, `input:0`, `input:1` | `cover:0`, `input:0`, `input:1` |
| Relais | GPIO4 (Kanal 1), GPIO15 (Kanal 2) | GPIO4 auf, GPIO15 zu (tauschbar) |
| Eingänge | SW1 GPIO13 → Kanal 1, SW2 GPIO5 → Kanal 2 | SW1 auf, SW2 zu |
| Messung | ADE7953 an I2C (SDA GPIO12, SCL GPIO14), je Kanal Leistung, Spannung, Strom, cos φ, Frequenz | Summe beider Kanäle |
| Sonstiges | LED GPIO0 (invertiert), Taster GPIO2, NTC am ADC | wie links |

Die Messwerte rechnet Hermit OS wie Tasmota um; mit „Leistung kalibrieren“ (je Kanal, Referenzlast,
Spannung oder Strom) lassen sie sich korrigieren. Abschaltung über 2300 W je Kanal bzw. 95 °C.

Die Original-Firmware im Rollladenbetrieb erkennt der Flasher und spielt dann `hermit-25-cover` auf,
sonst `hermit-25` (umstellbar mit `--cover` bzw. `--switch` oder in der Auswahl der Oberfläche).

### Rollladen

- **Bedienung** wie Shelly Plus 2PM: `Cover.Open`, `Cover.Close`, `Cover.Stop`, `Cover.GoToPosition`, `Cover.Calibrate`
- **Nie beide Relais zugleich**; beim Richtungswechsel hält der Motor 0,5 s an
- **Endlagen** erkennt Hermit OS am Abschalten des Motors: Die Leistung fällt unter 5 W (einstellbar) für 0,5 s.
  Ohne messbaren Motorstrom gilt die Höchstfahrzeit (Standard 60 s) als Endlage
- **Kalibrierung:** fährt ganz zu, ganz auf, wieder zu und misst die Fahrzeiten. Danach Positionen 0–100 %
  (0 = zu). Die Fahrzeiten lassen sich auch von Hand eintragen
- **Eingänge:** zwei (SW1 auf, SW2 zu) oder einer (auf → stopp → zu → stopp), jeweils als Schalter
  (an = fahren, aus = stopp) oder Taster (drücken = fahren bzw. stoppen); tauschbar
- **Hinderniserkennung** (aus ab Werk): Leistung über Schwelle nach der Anlaufzeit → anhalten oder kurz zurückfahren
- **Nach Stromausfall:** stehen bleiben (Standard), öffnen oder schließen. Nach einem Neustart des ESP (z. B. Update)
  steht der Motor; die Position bleibt gespeichert
- Gen1: `/roller/0?go=open|close|stop|to_pos&roller_pos=50`

---

## Sonoff RF Bridge 433

| | |
|---|---|
| Firmware / Modell | `hermit-rfbridge` / `HERMIT-RFBRIDGE` (Gen1-Typ `RFBRIDGE`) |
| Chip | ESP8285, **1 MB Flash** |
| Funkchip | EFM8BB1 an UART0 (GPIO1/3, 19200 Baud) – Original-Firmware oder [Portisch](https://github.com/Portisch/RF-Bridge-EFM8BB1) |
| LED | GPIO13 (invertiert, blinkt im Zugangspunkt-Modus) |
| Taster | GPIO0 (10 s halten = WLAN zurücksetzen) |
| Gen2-Komponenten | `rf` (Empfang, gespeicherte Codes), `switch:<n>` je gespeicherter Funksteckdose |

Nur die Hardware mit EFM8BB1 (R2 bis v1.0). Die R2 **v2.2** hat keinen EFM8 mehr – dafür ist Hermit OS nicht gedacht.
Am Funkchip wird nichts geflasht; Hermit OS spricht beide Firmwares.

**Empfangsmodus** (Reiter „Funk“):

| Modus | Firmware im EFM8 | Erkennt |
|---|---|---|
| Standard | Original und Portisch | 24-Bit-Codes (EV1527, PT2262 …): die meisten Fernbedienungen, Tür-/Fensterkontakte, Bewegungsmelder, Klingeltaster |
| Alle Protokolle | nur Portisch | alle Protokolle der Portisch-Liste, längere Codes |
| Rohdaten | nur Portisch | Zeitstufen + Daten (B1) – für Geräte, die keins der Protokolle sprechen |

**Codes:** Unter Status erscheinen die zuletzt empfangenen Codes (`S:` Standard, `P:` Protokoll, `R:` Rohdaten).
„Übernehmen“ setzt einen Code in das Formular unter „Funk“; gespeichert wird er als

- **Taste** – ein Code; Ereignis `rf.button` (Klingel, Fernbedienungstaste, Bewegungsmelder)
- **Kontakt** – Code „offen“ und optional „geschlossen“; Ereignisse `rf.open` / `rf.closed`, Zustand wird gehalten
- **Schalter** – Code „an“ und „aus“ (Funksteckdose); erscheint als `switch:<n>` wie bei einem Shelly und lässt sich
  per `Switch.Set`, Zeitplan, Regel und Hermit Cloud schalten. Drückt jemand die Original-Fernbedienung, zieht der
  Zustand mit

Bis zu 40 Einträge, Nummer `id` 0–63. Wiederholungen derselben Taste innerhalb von 0,7 s zählen einmal.
Unbekannte Codes lösen `rf.unknown` aus. Gesendet wird mit 8 Wiederholungen (einstellbar 1–40).

**Flash:** In 1 MB passt Hermit OS nicht zweimal. Updates laufen deshalb in zwei Schritten über den
**Hermit Loader 1 MB** (`hermit-loader-1m.bin.gz`): Hermit OS installiert den Loader, startet neu, der Loader lädt
die neue Firmware (SHA-256-geprüft) und startet sie. Einstellungen und gespeicherte Codes bleiben erhalten.
Schlägt es fehl, bleibt der Loader mit seiner Seite zum Hochladen (`hermit-rfbridge.bin.gz`) erreichbar.

---

## Weboberfläche

| Reiter | Inhalt |
|---|---|
| Status | Schalten, Messwerte, WLAN, Laufzeit, Flash; bei Duo und Dimmer Helligkeitsregler (Duo auch Farbtemperatur) |
| Schalter / Licht | Name, Eingangsmodus, Verhalten nach Stromausfall, Auto-An/Aus, Abschaltgrenzen, Eingang, Add-on, bei Duo und Dimmer Überblendzeit, beim Dimmer Bereich, Dimmart, SW2 |
| Aktionen | Webhooks anlegen, aktivieren, löschen |
| WLAN | Netzsuche, DHCP/statisch, Passwort des Zugangspunkts |
| System | Name, Zeitzone, NTP, LED, Passwort, Kalibrierung, Auto-Update, Firmware, Neustart, Werkseinstellungen |
| RPC | Konsole für alle Methoden |
