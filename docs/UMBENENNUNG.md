# Umbenennung hermit → pagurus

Stand 27.11.2026. Ziel: Überall „pagurus“, ohne dass ein Gerät im Feld seine Updates, seine
MQTT-Verbindung oder seine Einstellungen in der Cloud verliert.

## Grundregel

Erst lernen alle **Empfänger** beide Namen (Cloud, Portal, App, Flash-Tool), danach ändern die
**Sender** (Firmware) ihren Namen. Alte Namen werden nie entfernt, solange noch Geräte damit laufen.

## Bestandsaufnahme

| Bereich | Wo „hermit“ vorkommt | Wirkung nach außen |
|---|---|---|
| Firmware | `fw_id` = `hermit-os/<ver>` | Cloud, Flash-Tool und Scanner erkennen daran Pagurus OS |
| Firmware | Modell `HERMIT-PLUG`, `HERMIT-1PM` … | Cloud blendet daran Funktionen ein/aus |
| Firmware | Geräte-ID `hermit-plug-<mac>` | **ist zugleich MQTT-Präfix und Schlüssel in der Cloud-Datenbank** |
| Firmware | Variante `hermit-plug` … | bestimmt, welche `update/<variante>.json` das Gerät abfragt |
| Firmware | Zugangspunkt `hermit-…`, Loader `hermit-loader-…` | nur sichtbar beim Einrichten |
| Firmware | `hermit.gpio`, `hermit.stm`, `hermit.testimport` (RPC) | vom Flash-Tool / Diagnose genutzt |
| Firmware | User-Agent, Logtexte, interne `#define` | intern |
| Releases | `bin/hermit-*.bin`, `update/hermit-*.json`, Schlüssel in `update.json` | alte Firmware fragt genau diese Pfade ab |
| Cloud | `hermit_cloud.py`, Container-/Image-Namen, `HERMIT_CLOUD_DIR`, `sw.js`-Cache | intern bzw. nur auf dem Server |
| Cloud | MQTT-Benutzer `hermit` | **steht auf jedem Gerät als Zugangsdaten** |
| Tools | Variantennamen, `fw_id`-Prüfung, `mg2hermit` | Flash-Tool |

## Schritte

### 1 – Empfänger verstehen beide Namen (gefahrlos, sofort möglich)
- Cloud: `fw_id` beginnt mit `hermit-os` **oder** `pagurus-os`; Modell beginnt mit `HERMIT` **oder** `PAGURUS`.
- Cloud: Gerät mit neuer ID, aber bekannter MAC → vorhandenen Eintrag übernehmen (Einstellungen,
  Regeln, Verlauf, Energiemesser, Wandtafeln, Klima-Zeitpläne, Fenster-offen).
- Flash-Tool und Scanner: beide `fw_id`, beide Variantennamen.
- Portal und App: nur Anzeige, prüfen.
- Intern umbenennen: Logtexte, User-Agent, Kommentare, `#define`s, JS-Namen.

### 2 – Firmware 1.20.0 sendet „pagurus“
- `fw_id` = `pagurus-os/<ver>`, Modell `PAGURUS-…`, Zugangspunkt `pagurus-…`, Loader `pagurus-loader-…`.
- RPC: `pagurus.gpio` usw. neu, `hermit.*` bleibt als Alias.
- **Geräte-ID / MQTT-Präfix:** Beim ersten Start nach dem Update wird die bisherige ID fest
  gespeichert und weiter benutzt. Nur neu geflashte Geräte bekommen `pagurus-…`.
  Damit ändert sich bei vorhandenen Geräten weder das MQTT-Thema noch der Cloud-Eintrag.
- Neue Varianten `pagurus-plug` … mit eigener `update/pagurus-*.json`.

### 3 – Release-Dateien
- `bin/pagurus-*.bin` ist die neue Firmware.
- `update/hermit-*.json` bleibt bestehen und zeigt auf die neuen Dateien – alte Geräte finden so ihr Update.
- `update.json` (für sehr alte Firmware) führt beide Schlüssel.
- Flash-Pakete heißen schon `Pagurus-OS-…`.

### 4 – Server (beim Umzug auf den Miniserver)
- `hermit_cloud.py` → `pagurus_cloud.py`, Container/Images `pagurus-…`, `PAGURUS_CLOUD_DIR`.
- Einmal `docker compose down` / `up -d` von Hand – der Updater kann sich nicht selbst umbenennen.
- **MQTT-Benutzer `hermit` bleibt.** Ein neuer Name hieße, auf jedem Gerät die Zugangsdaten zu ändern.

## Nicht umbenennen
- MQTT-Benutzer `hermit` (s. o.).
- IDs vorhandener Geräte.
- Alte `update/hermit-*.json` und die Schlüssel in `update.json` (dauerhaft als Weiterleitung).
