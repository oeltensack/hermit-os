# Hermit Cloud

Kleine Heim-Cloud für Hermit-OS-Geräte (und Shelly Plus/Pro Gen2/Gen3). Läuft zu Hause als
Docker-Paket. Sie ist **optional**: Die Geräte arbeiten ohne sie genauso, die Cloud liest nur mit.

**Funktionen**
- Geräteübersicht: online/offline, Modell, Firmware, Leistung, Energie heute, Temperatur, Fehler
- Schalten aus der Oberfläche (bei der Messfirmware gesperrt, Reset nur am Gerät)
- Energieverlauf: Leistung der letzten 24 h, Energie pro Tag (31 Tage), pro Monat, Verbrauch je Gerät
- Meldungen: Überlast, Übertemperatur, Gerät offline/wieder online, Reset am Gerät – mit Quittieren
- Umbenennen (auch am Gerät), Link zur Geräteoberfläche

```
Hermit-Geräte ──MQTT (Gen2-Format)──►  Mosquitto :1883  ◄──►  Hermit Cloud :8080  ──► Browser
```

## Installation auf Proxmox

Am einfachsten ein Debian-12-LXC-Container (1 CPU, 512 MB RAM, 4 GB Disk). Für Docker im LXC unter
*Optionen → Features* **Nesting** einschalten.

```bash
# im Container
apt update && apt install -y curl git
curl -fsSL https://get.docker.com | sh        # Docker inkl. „docker compose“
git clone https://github.com/oeltensack/hermit-os.git
cd hermit-os/cloud
cp .env.example .env
nano .env          # MQTT_USER, MQTT_PASS, CLOUD_PASSWORD, PUBLIC_BROKER (IP des Containers)
docker compose up -d
```

Weboberfläche: `http://<IP-des-Containers>:8080`

Alternativ geht jeder Rechner mit Docker (VM, Raspberry Pi, NAS).

## Geräte verbinden

**Hermit OS** (ab 1.12.0): Geräteoberfläche → *System* → *Cloud / MQTT*
- Haken „MQTT einschalten“
- Server: `<IP-des-Containers>:1883`
- Benutzer/Passwort: `MQTT_USER` / `MQTT_PASS` aus der `.env`
- Topic-Präfix leer lassen (= Geräte-ID)

**Shelly Plus/Pro**: *Einstellungen → MQTT*, gleicher Server und Zugang,
„RPC status notifications over MQTT“ und „Generic status update over MQTT“ einschalten.

Die Geräte erscheinen automatisch, sobald sie verbunden sind.

## Update

```bash
cd hermit-os && git pull && cd cloud && docker compose up -d --build
```

Die Daten liegen in `cloud/data/cloud.db` (SQLite) und bleiben beim Update erhalten.
Sicherung: den Ordner `data/` kopieren.

## Einstellungen (.env)

| Variable | Bedeutung |
|---|---|
| `MQTT_USER`, `MQTT_PASS` | Zugang zum Broker für Geräte und Cloud (leer = Broker ohne Anmeldung) |
| `CLOUD_PASSWORD` | Passwort der Weboberfläche (leer = ohne Anmeldung) |
| `PUBLIC_BROKER` | IP des Rechners, wird in der Oberfläche unter *Info* angezeigt |
| `WEB_PORT` | Port der Weboberfläche (Standard 8080) |
| `TZ` | Zeitzone für die Tageswerte |

Weitere Werte im Container: `KEEP_POWER_DAYS` (Leistungsverlauf, Standard 31 Tage),
`OFFLINE_AFTER` (s ohne Lebenszeichen bis „offline“, Standard 180).

## Von unterwegs

Nicht direkt ins Internet stellen. Am besten über den WireGuard-Tunnel ins Heimnetz
oder über einen Reverse-Proxy mit HTTPS und gesetztem `CLOUD_PASSWORD`.

## Schnittstelle

| Methode | Pfad | |
|---|---|---|
| GET | `/api/devices` | alle Geräte mit Status und Energie heute |
| POST | `/api/devices/{id}/switch` | `{}` umschalten, `{"on":true}` setzen |
| POST | `/api/devices/{id}/rpc` | `{"method":"Switch.GetStatus","params":{"id":0}}` beliebige RPC |
| POST | `/api/devices/{id}/name` | `{"name":"…"}` |
| DELETE | `/api/devices/{id}` | Gerät und gespeicherte Werte entfernen |
| GET | `/api/devices/{id}/power?hours=24` | Leistung je Minute |
| GET | `/api/energy?dev=…&days=31` | Energie pro Tag (ohne `dev`: alle) |
| GET | `/api/energy/months?dev=…` | Energie pro Monat |
| GET | `/api/energy/by_device?days=30` | Verbrauch je Gerät |
| GET | `/api/alerts` · POST `/api/alerts/ack` | Meldungen, `{"id":…}` oder alle quittieren |
