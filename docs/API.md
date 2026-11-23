# API

Hermit OS spricht die **RPC-API von Shelly Gen2/Gen3** über HTTP. Alle Methoden sind unabhängig von
Groß-/Kleinschreibung. `/rpc/Shelly.ListMethods` liefert die Liste des jeweiligen Geräts.

## Aufruf

```
GET  http://<ip>/rpc/Switch.Set?id=0&on=true&toggle_after=60
POST http://<ip>/rpc/Switch.Set              Body: {"id":0,"on":true}
POST http://<ip>/rpc                         Body: {"id":1,"src":"script","method":"Switch.Set","params":{"id":0,"on":true}}
```

- GET-Parameter werden umgewandelt: `true`/`false`/`null`, Zahlen, JSON (`{…}`, `[…]`, `"…"`), sonst Text.
- `GET`/`POST /rpc/<Methode>` liefert das Ergebnis direkt bzw. `{"code":…, "message":…}` mit HTTP 400/404/500.
- `POST /rpc` antwortet im Gen2-Rahmen `{"id":…, "src":"<geräte-id>", "dst":…, "result":…}` bzw. `"error"`.
- Mit Passwort: Benutzer `admin`, Digest oder Basic (`http://admin:<pass>@<ip>/rpc/…`).
- CORS ist offen (`Access-Control-Allow-Origin: *`).

---

## Shelly

| Methode | Parameter | Ergebnis |
|---|---|---|
| `Shelly.GetDeviceInfo` | – | `name, id, mac, model, gen:2, fw_id, ver, app, auth_en, type` |
| `Shelly.GetStatus` | – | `sys`, `wifi`, `switch:0` bzw. `cct:0`, `input:0`, `input:100` |
| `Shelly.GetConfig` | – | `sys`, `wifi`, `switch:0` bzw. `cct:0`, `input:0`, `input:100` |
| `Shelly.ListMethods` | – | `methods[]` |
| `Shelly.Reboot` | `delay_ms` | – |
| `Shelly.FactoryReset` | – | löscht Einstellungen, Aktionen, Energie; Kalibrierung bleibt |
| `Shelly.SetAuth` | `pass` (Text oder `null`) | Passwort setzen/entfernen *(Gen2: `ha1`)* |
| `Shelly.CheckForUpdate` | – | `{"stable":{"version":"x.y.z"}}` oder `{}` |
| `Shelly.Update` | `stage:"stable"` oder `url` | installiert im Hintergrund, Relais bleibt im Zustand |

## Switch (Shelly 1 / 1PM / Plug S / Plug)

| Methode | Parameter |
|---|---|
| `Switch.Set` | `id:0`, `on`, `toggle_after` (s) → `{was_on}` |
| `Switch.Toggle` | `id:0` → `{was_on}` |
| `Switch.GetStatus` | `id:0` |
| `Switch.GetConfig` / `Switch.SetConfig` | `id:0`, `config{…}` |
| `Switch.ResetCounters` | `id:0` (Energie nullen) |
| `Switch.Calibrate` | `apower` / `voltage` / `current` / `tasmota_powercal` / `factor` / `reset` |

**Status `switch:0`**

```json
{"id":0, "source":"http", "output":true, "timer_started_at":1795220000, "timer_duration":60,
 "apower":998.3, "voltage":229.8, "current":4.351, "pf":1.0,
 "aenergy":{"total":1234.567, "by_minute":[16.6,16.6,16.5], "minute_ts":1795221960},
 "temperature":{"tC":41.2, "tF":106.2}, "errors":["overpower"]}
```

`apower`/`aenergy` beim 1PM, Plug S und Plug, `voltage`/`current`/`pf` nur bei der Plug S, `temperature` beim 1PM
und Plug S. `aenergy.total` in Wh, `by_minute` in mWh der letzten drei vollen Minuten.

**Konfiguration `switch:0`**

| Feld | Werte |
|---|---|
| `name` | Text |
| `in_mode` | `follow`, `flip`, `momentary`, `detached` |
| `initial_state` | `off`, `on`, `restore_last`, `match_input` |
| `auto_on`, `auto_on_delay`, `auto_off`, `auto_off_delay` | bool, Sekunden |
| `power_limit` | W, 0 = aus (1PM, Plug S, Plug) |
| `temp_limit` | °C (1PM, Plug S) |
| `pm_calibration` | nur lesen: `factor`, `source`, `default`, bei Plug S `voltage_factor`, `current_factor` |

## CCT (Duo)

Wie die Shelly Duo Bulb Gen3.

| Methode | Parameter |
|---|---|
| `CCT.Set` | `id:0`, `on`, `brightness` (1–100), `ct` (2700–6500), `transition_duration` (s), `toggle_after` (s) |
| `CCT.Toggle` | `id:0` |
| `CCT.GetStatus` | `id:0` → `{id, source, output, brightness, ct, timer_*}` |
| `CCT.GetConfig` / `CCT.SetConfig` | `name`, `initial_state` (`off`/`on`/`restore_last`), `auto_on(_delay)`, `auto_off(_delay)`, `transition_duration`, `ct_invert` |

Ohne `on` ändert `CCT.Set` nur Helligkeit/Farbtemperatur – eine ausgeschaltete Lampe bleibt aus.
`Switch.*` gibt es bei der Duo nicht.

## Input

| Methode | Parameter |
|---|---|
| `Input.GetStatus` | `id:0` bzw. `id:100` (Add-on) → `{id, state}` |
| `Input.GetConfig` / `Input.SetConfig` | `id:0`: `name`, `type` (`switch`/`button`), `invert`, `filter` (`ac`/`dc`) |
| | `id:100`: zusätzlich `relay_action` (`none`/`momentary`/`flip`/`follow`), `addon_out_invert` |
| `Input.Diag` | `id:0` – roher Pin 200 ms: `high_pct`, `edges`, `approx_hz` |
| `SensorAddon.GetPeripherals` | nur Add-on-Variante: `{"digital_in":{"input:100":{}}}` |

## Sys

| Methode | Parameter / Ergebnis |
|---|---|
| `Sys.GetStatus` | `mac, time, unixtime, uptime, reset_reason, ram_free, fs_size, fs_free, flash_size, flash_real, flash_id, ota_free, cfg_rev, webhook_rev, available_updates` |
| `Sys.GetConfig` | `device.name`, `location.tz`, `sntp.server`, `ui.led`, `updates{url, mode, hour, token_set, variant, state, last_error, last_check, available}` |
| `Sys.SetConfig` | `config.device.name`, `config.location.tz` (POSIX), `config.sntp.server`, `config.ui.led`, `config.updates{url, mode (off/check/install), hour, token}` |

## WiFi

| Methode | Parameter |
|---|---|
| `WiFi.GetStatus` | → `sta_ip, status, ssid, rssi, bssid, channel, ap_active, roams` |
| `WiFi.GetConfig` | → `sta{ssid, ipv4mode, ip, netmask, gw, nameserver}`, `ap{ssid, is_open}` |
| `WiFi.SetConfig` | `config.sta{ssid, pass, enable, ipv4mode (dhcp/static), ip, netmask, gw, nameserver}`, `config.ap{pass}`, `config.roam{enable, rssi_thr}` (stärksten AP wählen, unter `rssi_thr` alle 5 min nach besserem suchen) |
| `WiFi.Scan` | → `results[{ssid, rssi, channel, auth}]` |

## Webhook (Aktionen)

| Methode | Parameter |
|---|---|
| `Webhook.ListSupported` | → `types{…}` des Geräts |
| `Webhook.List` | → `hooks[{id, cid, enable, event, name, urls, threshold}]`, `rev` |
| `Webhook.Create` | `event`, `cid` (0 oder 100), `name`, `enable`, `urls[]` (max. 5), `threshold` (W) |
| `Webhook.Update` | `id` + Felder wie bei Create |
| `Webhook.Delete` / `Webhook.DeleteAll` | `id` |

Höchstens 20 Aktionen. Ausgeführt wird per HTTP(S)-GET, nacheinander, Zeitlimit 3 s.

**Ereignisse**

| Ereignis | Geräte | Hinweis |
|---|---|---|
| `switch.on`, `switch.off` | 1, 1PM, Plug S, Plug | |
| `cct.on`, `cct.off` | Duo | |
| `switch.overpower` | 1PM, Plug S, Plug | |
| `switch.overtemperature` | 1PM, Plug S | |
| `switch.reset` | Messfirmware 1PM / Plug S / Plug | Schutzabschaltung am Gerät zurückgesetzt |
| `switch.power_above`, `switch.power_below` | 1PM, Plug S, Plug | `threshold` in W, Hysterese 5 % |
| `input.toggle_on`, `input.toggle_off` | Geräte mit Eingang | Typ Schalter |
| `input.button_down`, `input.button_up`, `input.button_push`, `input.button_doublepush`, `input.button_longpush` | Geräte mit Eingang | Typ Taster; lang = ab 1 s |
| `sys.boot` | alle | nach dem ersten WLAN-Verbindungsaufbau |

**Platzhalter in URLs:** `${event}` `${cid}` `${output}` `${input}` `${apower}` `${aenergy}` (Wh) `${tC}`
`${id}` `${name}` `${ip}`

```
/rpc/Webhook.Create?event="input.button_push"&cid=100&urls=["http://192.168.0.60/rpc/Switch.Toggle?id=0"]
/rpc/Webhook.Create?event="switch.power_above"&threshold=1000&urls=["http://192.168.0.60/script/1/alt?p=${apower}"]
```

---

## MQTT (optional)

Format wie Shelly Gen2/Gen3. Eingeschaltet über die Oberfläche (*System → Cloud / MQTT*) oder `MQTT.SetConfig`.

| Methode | Parameter | |
|---|---|---|
| `MQTT.GetConfig` | – | `enable`, `server`, `client_id`, `user`, `pass_set`, `topic_prefix`, `rpc_ntf`, `status_ntf` |
| `MQTT.SetConfig` | `config:{enable, server:"host:port", user, pass, client_id, topic_prefix, rpc_ntf, status_ntf}` | sofort wirksam |
| `MQTT.GetStatus` | – | `connected` |

| Topic | Richtung | Inhalt |
|---|---|---|
| `<präfix>/online` | Gerät → | `true` / `false` (retained, Last Will) |
| `<präfix>/rpc` | → Gerät | JSON-RPC `{"id":1,"src":"client","method":"Switch.Set","params":{"id":0,"on":true}}` |
| `<src>/rpc` | Gerät → | Antwort mit `id`, `src`, `dst`, `result` / `error` |
| `<präfix>/events/rpc` | Gerät → | `NotifyEvent` (jedes Ereignis aus der Webhook-Liste) und `NotifyStatus` |
| `<präfix>/status/<komponente>` | Gerät → | Status je Komponente (retained): `switch:0`/`cct:0`, `input:0`, `input:100`, `sys`, `wifi` |
| `<präfix>/info` | Gerät → | `Shelly.GetDeviceInfo` + `ip` (retained, Hermit-Erweiterung) |

Präfix und Client-ID sind standardmäßig die Geräte-ID (`hermit-1pm-a1b2c3`). Status wird bei jeder Änderung
und sonst alle 15 s (Geräte mit Messung) bzw. 60 s gesendet. Ohne TLS – für das eigene Netz oder über WireGuard.

## Gen1-Endpunkte

| Endpunkt | Wirkung |
|---|---|
| `/shelly` | Geräteinfo (ohne Passwort), Gen2-Format plus `type` |
| `/status` | Gen1-Status mit `relays`, `meters`, `inputs`, `wifi_sta`, `temperature` |
| `/relay/0?turn=on|off|toggle&timer=10` | schalten (Shelly 1/1PM/Plug S/Plug; bei der Duo Licht an/aus) |
| `/meter/0` | `power`, `total` (Watt-Minuten wie Gen1) |
| `/light/0?turn=on&brightness=50&temp=3000&transition=500` | Duo (`transition` in ms) |
| `/update` | Firmware hochladen (Formularfeld beliebig) |

## Diagnose

| Methode | Zweck |
|---|---|
| `Hermit.TestImport` | Führt die WLAN-Übernahme erneut aus, ohne etwas zu ändern: `mgos` (Original-Firmware), `tasmota`, `sdk` (Loader) mit SSID, Passwortlänge und fester IP; `raw_ssid_hits` zeigt Fundstellen bis zum Ende des SSID-Werts. Passwörter werden nie ausgegeben. |
| `Hermit.Gpio` | `{"pin":5,"value":512}` PWM 0–1023 ausgeben, `{"pin":5}` Pegel lesen, `{"scan":true}` alle freien Pins lesen. Nur zur Analyse unbekannter Hardware – ändert Pins bis zum nächsten Schaltvorgang bzw. Neustart. |
| `Input.Diag` | Rohsignal des Eingangs |
| `Sys.GetStatus` → `reset_reason` | Grund des letzten Neustarts |

---

## Unterschiede zu Gen2

- Kein WebSocket-RPC und keine Shelly-Cloud; MQTT ohne TLS, eigene Cloud: [Hermit Cloud](../cloud/)
- `Shelly.SetAuth` mit `pass` statt `ha1`
- Webhook-Bedingung als Zahl (`threshold`) statt JS-Ausdruck; ein numerisches `condition` wird ebenfalls angenommen
- Eigene Methoden: `Switch.Calibrate`, `Input.Diag`, `Hermit.TestImport`, `Hermit.Gpio`
- Keine Scripte, Zeitpläne (Schedules) oder KVS auf dem Gerät – das übernimmt ein Gen2/Gen3-Gerät
