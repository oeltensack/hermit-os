# Pagurus OS für ESP32 – Klimaanlagen und Heizungsanzeige

Ersetzt das WLAN-Modul bzw. ESPHome auf einem ESP32 am UART der Klimaanlage. Spricht dieselbe
Schnittstelle wie Pagurus OS auf den Shellys (JSON-RPC über HTTP und MQTT im Gen2-Format) und
erscheint dadurch in Pagurus Cloud, Portal und App.

| Variante | Anlage | Chip | UART | Belegung |
| :------- | :----- | :--- | :--- | :------- |
| `hermit-tclac-esp32` | TCL / Kesser (statt Tuya-Modul) | ESP32 | 9600 8E1 | TX GPIO16, RX GPIO17, DHT22 GPIO18 |
| `hermit-midea-esp32c3` | Midea-Protokoll: Midea, Dimstal, Comfee, Qlima, Electrolux, Artel, Inventor, Carrier … | ESP32-C3 | 9600 8N1 | TX GPIO21, RX GPIO20 |

Beide Belegungen entsprechen den bisherigen ESPHome-Konfigurationen.

## `climate:0`

| Feld | TCL | Midea |
| :--- | :-- | :---- |
| `output` (an/aus), `mode`, `target_C`, `current_C`, `fan`, `swing_v`, `link` | ✓ | ✓ |
| `mode` | cool, heat, dry, fan, auto | was die Anlage meldet (Liste in `modes`) |
| `target_C` | 16–31 °C, ganze Grad | 16–30 °C in 0,5er-Schritten (`min_C`, `max_C`, `step_C`) |
| `fan` | auto, low, low_mid, mid, mid_high, high | auto, low, mid, high (Liste in `fans`) |
| `swing_h` | ✓ | wenn die Anlage es meldet |
| `eco`, `turbo`, `mute` (Leise) | ✓ | ✓ (`extras` listet, was angeboten wird) |
| `sleep` (Nachtmodus) | – | ✓ |
| `outlet_C`, `compressor` | ✓ | – |
| `outdoor_C` (Außengerät) | – | ✓, wenn ein plausibler Wert kommt |
| DHT22: `ext_temperature_C`, `ext_humidity`, `temperature:0`, `humidity:0` | ✓ | – |

RPC: `Climate.Set` (beliebige Felder oben, `on` für an/aus), `Climate.Toggle`, `Climate.GetStatus`,
`Climate.SetConfig` (`beep`, `swap_pins`), Midea zusätzlich `Climate.DisplayToggle` bzw. `Climate.Set` mit
`display_toggle: true` (Anzeige am Innengerät),
TCL zusätzlich `Temperature.SetConfig` (`offset_C`); dazu `Shelly.GetDeviceInfo/GetStatus/GetConfig`,
`Sys.SetConfig`, `WiFi.SetConfig/Scan`, `MQTT.SetConfig`, `Shelly.CheckForUpdate/Update/Reboot`.

Cloud und Portal zeigen auf der Klima-Karte nur die Modi und Lüfterstufen, die das Gerät in `modes`/`fans`
meldet (ohne Angabe die TCL-Werte).

## Protokolle

- **TCL:** eigene Umsetzung nach der Beschreibung in [adaasch/AC-hack](https://github.com/adaasch/AC-hack)
  (Rahmenaufbau, Befehle 0x03/0x04/0x06).
- **Midea:** Bibliothek [dudanov/MideaUART](https://github.com/dudanov/MideaUART) 1.1.9 (MIT-Lizenz laut
  `library.json`), dieselbe, die ESPHome für `climate: platform: midea` verwendet. Nach dem Start fragt die
  Firmware die Fähigkeiten der Anlage ab (Autoconf, Befehl 0xB5) und bietet nur an, was sie meldet. Antwortet
  die Anlage darauf nicht, wird alles angeboten (Diagnosezeile: „Fähigkeiten: keine Antwort“).

## Von ESPHome umsteigen (ohne Kabel)

1. In der ESPHome-YAML den Upload im Browser freischalten und einmal aus ESPHome flashen:
   ```yaml
   web_server:
     port: 80
   ota:
     - platform: esphome
     - platform: web_server
   ```
   (bei `web_server:` eine Zeile `ota: false` entfernen)
2. `http://<IP>` → „OTA Update“ → die passende Datei hochladen:
   **`bin/hermit-tclac-esp32.bin`** (TCL) bzw. **`bin/hermit-midea-esp32c3.bin`** (Midea)
3. Das WLAN aus ESPHome wird nicht übernommen: nach dem Neustart mit **`hermit-tclac-xxxxxx`** bzw.
   **`hermit-midea-xxxxxx`** verbinden → `http://192.168.33.1` → Reiter *WLAN* → Netz wählen, Passwort, speichern
4. Unter der neuen IP Reiter *MQTT*: Server der Pagurus Cloud (`192.168.4.102:1883`), Benutzer/Passwort aus
   der `.env` der Cloud

Die Partitionstabelle von ESPHome bleibt dabei bestehen; beide Programme passen hinein.

## Per Kabel

```
esptool.py --chip esp32   --port COM5 write_flash 0x0 hermit-tclac-esp32-factory.bin
esptool.py --chip esp32c3 --port COM5 write_flash 0x0 hermit-midea-esp32c3-factory.bin
```

Beim ESP32-C3 liegt die Klimaanlage auf den UART0-Pins (GPIO20/21); Pagurus OS benutzt dafür UART1, `Serial`
ist der USB-Anschluss. Nach dem Einschalten schreibt der Bootloader des C3 ein paar Zeilen auf GPIO21 –
das war mit ESPHome genauso und stört die Anlage nicht.

## Updates

Wie bei den Shellys über `update.json` (Reiter *System*: prüfen, installieren oder automatisch).
Jede Datei wird vor dem Umschalten per SHA-256 geprüft.

## Hinweise

- Im Lüftermodus ignoriert die Anlage die Solltemperatur.
- Midea: Im Automatikmodus und mit Eco/Turbo/Nacht regelt die Anlage den Lüfter selbst (Lüfter-Befehle
  werden dann übergangen). Eco nur beim Kühlen, Turbo beim Kühlen oder Heizen, Nacht nicht beim Entfeuchten
  oder Lüften. Turbo nutzt das Turbo-Preset, wenn die Anlage es meldet, sonst die Turbo-Lüfterstufe.
- Midea: Einen Modus bei ausgeschalteter Anlage wählen merkt ihn nur vor; er gilt beim Einschalten.
- Kommt keine Antwort von der Anlage, zeigt die Oberfläche „Keine Antwort“ – dann TX/RX prüfen
  (`Climate.SetConfig` mit `{"swap_pins": true}` tauscht sie ohne Umlöten).

## Heizungsanzeige AZ-Touch (`pagurus-aztouch-esp32`)

Die Anzeige der Arduino-Sketche *AZ_Touch_Heizung* (v12.1) auf Pagurus OS: AZ-Touch MOD 2,4" (ESP32,
ILI9341, XPT2046). Gleiche Bedienung in neuer, dunkler Oberfläche – Speicher (mit Soll) und Kessel (mit Rücklauf),
Pumpe, Brenner, Öl heute, Leitungstemperatur der Zirkulation, unten links **BOOST**, unten rechts **ZIRKULATION** mit Temperatur. Optional eine zweite
Seite **BAD** für einen Heizlüfter (Script ab v11) wie in v13.0: Raum, Soll, Status, Lüfter (L), Heizung (H), Leistung,
Verbrauch heute; Tasten **SOLL −**, **BETRIEB** ein/aus, **BOOST** (bei Störung **QUITTIEREN**), **SOLL +**. Wechsel durch
Tipp auf die Kopfzeile, nach 2 Minuten zurück zu HEIZUNG; abgefragt wird nur die sichtbare Seite. Soll und Betrieb
schreibt das Display in `hl_cfg` (die übrigen Einstellungen bleiben), Boost und Quittieren in `hl_cmd`. Anders als im Sketch:

- **Kein WLAN und keine Adressen im Programm.** WLAN über den Zugangspunkt `pagurus-aztouch-xxxxxx`
  (192.168.33.1), die Adressen im Reiter *Anzeige* der Weboberfläche: Shelly mit der Warmwasser-Logik und
  Nummer des Logik-Scripts (liest `/script/<Nr>/api_data`, Boost über `boost_trigger`), Shelly der
  Zirkulation und Nummer des Temperaturfühlers (`Switch.GetStatus`/`Switch.Set` für `switch:0`,
  `Temperature.GetStatus`). Bis das eingerichtet ist, zeigt das Display, was zu tun ist.
- **Pagurus Cloud:** MQTT wie bei den anderen Geräten, Firmware-Updates über `update.json`.
- **Weboberfläche:** Reiter *Heizung* mit den Werten, Boost und Zirkulation; dazu WLAN, MQTT, System.
- Kopfzeile **orange**, wenn von der Warmwasser-Logik seit 30 s keine Daten kamen. Antwortet eine Quelle
  nicht, wird sie nur noch alle 15 s gefragt, damit die Bedienung flott bleibt.
- Watchdog (30 s), Neustart bei weniger als 20 KB freiem Speicher – wie v12.1. Ohne WLAN startet nach
  1 Minute der Zugangspunkt, das Display versucht es parallel weiter.

| `display:0` | |
| :--- | :--- |
| Status | `page`, `bad` (`ok`, `temp`, `soll`, `status`, `on`, `boost`, `pwr`), `ww` (`ok`, `age_s`, `temp_s`, `temp_k`, `burn`, `pump`, `boost`, `cons_today`, `error`), `zirk` (`ok`, `output`, `tC`, `error`) |
| Config | `ww_ip`, `ww_sid`, `zirk_ip`, `zirk_tid` (-1 = kein Fühler), `hl_ip`, `hl_name` (Seite BAD, leer = keine), `rotation` (3 normal, 1 auf dem Kopf) |
| RPC | `Display.GetStatus`, `Display.GetConfig`, `Display.SetConfig`, `Display.Boost`, `Display.Zirk` (`on` true/false, ohne = umschalten), `Display.Page` (`page` heizung/bad) |

Erstes Aufspielen per USB (der alte Sketch hat kein OTA):

```
esptool.py --chip esp32 --port COM5 write_flash 0x0 pagurus-aztouch-esp32-factory.bin
```

Das Komplettabbild überschreibt auch die gespeicherten Einstellungen (WLAN, Adressen, MQTT). Spätere Versionen
deshalb über die Weboberfläche einspielen (*System* → *Firmware-Datei hochladen*, `pagurus-aztouch-esp32.bin`)
oder per Update – dann bleibt alles erhalten.

Danach mit `pagurus-aztouch-xxxxxx` verbinden → `http://192.168.33.1` → *WLAN* → Netz und Passwort →
nach dem Neustart unter der neuen IP Reiter *Anzeige* (Adressen) und *MQTT* (Cloud).
