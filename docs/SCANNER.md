# Pagurus Scanner

Windows-Programm (läuft auch unter Linux/macOS mit Python), das das Netz nach **Shelly Gen1**, **Shelly Gen2/Gen3**,
**Tasmota** und **Pagurus OS** durchsucht.

| Spalte | Inhalt |
|---|---|
| Typ | Pagurus OS · Shelly Gen1 · Shelly Gen2+ · Tasmota (farbig) |
| Name, IP, Modell, Firmware, MAC | aus `/shelly`, `/settings`, `Sys.GetConfig` bzw. Tasmota `Status 0`/`Template` |
| Empfang | WLAN-Signal in dBm |
| Zustand, Leistung | Relais/Licht an/aus, Summe der Wirkleistung |
| Update | verfügbare Firmware-Version (falls das Gerät eine meldet) |
| Laufzeit, Hinweis | Uptime; „Passwort“ (geschützt), „Messfirmware“ |

**Bedienung**
- **Netze** eintragen – mehrere durch Komma: `192.168.0.0/24, 192.168.4.0/24, 192.168.5.10-80`.
  „Eigene Netze“ setzt die /24-Netze des PCs ein. Die Eingabe wird gespeichert.
- **Suchen** (oder F5). Eine /24 dauert wenige Sekunden.
- Haken und **Filter** zum Eingrenzen, Klick auf eine Spaltenüberschrift sortiert.
- **Doppelklick** öffnet das Gerät im Browser. **Rechtsklick**: öffnen, neu abfragen, IP/MAC kopieren, neu starten.
- **CSV speichern** – für Excel (Semikolon, Dezimalkomma).
- **Passwort (optional)**: für geschützte Geräte (Benutzer `admin`); sonst steht dort „Passwort“.

**Starten**
- Mit Python 3 für Windows: Doppelklick auf `Pagurus-Scanner.pyw`
- Als EXE: einmal `tools\build_exe.bat` (bzw. `build_scanner.bat` im Scanner-Paket) ausführen → `Pagurus-Scanner.exe`
- Konsole: `python tools/pagurus_scanner.py --cli 192.168.4.0/24 --csv geraete.csv`

Andere Netze (VLANs) werden gefunden, solange der PC sie erreicht (Routing/Firewall zwischen den VLANs).
