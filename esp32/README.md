# Pagurus OS für ESP32 – TCL-/Kesser-Klimaanlage

Ersetzt das Tuya-WLAN-Modul bzw. ESPHome auf einem ESP32 am UART der Klimaanlage. Spricht dieselbe
Schnittstelle wie Pagurus OS auf den Shellys (JSON-RPC über HTTP und MQTT im Gen2-Format) und
erscheint dadurch in Pagurus Cloud, Portal und App.

| Komponente | Inhalt |
| :--------- | :----- |
| `climate:0` | `output` (an/aus), `mode` (cool, heat, dry, fan, auto), `target_C` (16–31), `current_C` (Raum am Innengerät), `outlet_C`, `fan` (auto, low, low_mid, mid, mid_high, high), `swing_v`, `swing_h`, `eco`, `turbo`, `mute`, `compressor`, `link` |
| `temperature:0` / `humidity:0` | DHT22 (Temperatur mit einstellbarer Korrektur) |

RPC: `Climate.Set` (beliebige Felder oben, `on` für an/aus), `Climate.Toggle`, `Climate.GetStatus`,
`Climate.SetConfig` (`beep`), `Temperature.SetConfig` (`offset_C`), dazu `Shelly.GetDeviceInfo/GetStatus/GetConfig`,
`Sys.SetConfig`, `WiFi.SetConfig/Scan`, `MQTT.SetConfig`, `Shelly.CheckForUpdate/Update/Reboot`.

## Belegung (wie in der bisherigen ESPHome-Konfiguration)

| ESP32 | an |
| :---- | :- |
| GPIO16 (TX) | RX der Klimaanlage |
| GPIO17 (RX) | TX der Klimaanlage |
| GPIO18 | DHT22 Daten |

UART 9600 8E1. Das Protokoll ist eine eigene Umsetzung nach der Beschreibung in
[adaasch/AC-hack](https://github.com/adaasch/AC-hack) (Rahmenaufbau, Befehle 0x03/0x04/0x06).

## Von ESPHome umsteigen (ohne Kabel)

1. In der ESPHome-YAML den Upload im Browser freischalten und einmal aus ESPHome flashen:
   ```yaml
   web_server:
     port: 80
   ota:
     - platform: esphome
     - platform: web_server
   ```
   (bei `web_server:` die Zeile `ota: false` entfernen)
2. `http://<IP>` → „OTA Update“ → **`bin/hermit-tclac-esp32.bin`** hochladen
3. Das WLAN aus ESPHome wird nicht übernommen: nach dem Neustart mit **`hermit-tclac-xxxxxx`** verbinden →
   `http://192.168.33.1` → Reiter *WLAN* → Netz wählen, Passwort, speichern
4. Unter der neuen IP Reiter *MQTT*: Server der Pagurus Cloud (`192.168.4.102:1883`), Benutzer/Passwort aus
   der `.env` der Cloud

Die Partitionstabelle von ESPHome bleibt dabei bestehen (je 1,25 MB für zwei Programme) – das passt.

## Per Kabel

```
esptool.py --chip esp32 --port COM5 write_flash 0x0 hermit-tclac-esp32-factory.bin
```

## Updates

Wie bei den Shellys über `update.json` (Reiter *System*: prüfen, installieren oder automatisch).
Jede Datei wird vor dem Umschalten per SHA-256 geprüft.

## Hinweise

- Im Lüftermodus ignoriert die Anlage die Solltemperatur.
- Kommt keine Antwort von der Anlage, zeigt die Oberfläche „Keine Antwort“ – dann TX/RX tauschen.
