# Pagurus Scanner

Windows-Programm (läuft auch unter Linux/macOS mit Python), das das Netz nach **Shelly Gen1**, **Shelly Gen2/Gen3**,
**Tasmota** und **Pagurus OS** durchsucht.

| Spalte | Inhalt |
|---|---|
| Typ | Pagurus OS · Shelly Gen1 · Shelly Gen2+ · Tasmota (farbig) · Unbekannt (grau, Webserver ohne Shelly/Tasmota-Antwort) |
| Name, IP, Modell, Firmware, MAC | aus `/shelly`, `/settings`, `Sys.GetConfig` bzw. Tasmota `Status 0`/`Template` |
| Empfang | WLAN-Signal in dBm |
| Zustand, Leistung | Relais/Licht an/aus, Summe der Wirkleistung |
| Update | verfügbare Firmware-Version (falls das Gerät eine meldet) |
| Laufzeit, Hinweis | Uptime; „Passwort“ (geschützt), „Messfirmware“ |

**Bedienung**
- **Netze** eintragen – mehrere durch Komma: `192.168.0.0/24, 192.168.4.0/24, 192.168.5.10-80`.
  „Eigene Netze“ setzt die /24-Netze des PCs ein. Die Eingabe wird gespeichert.
- **Suchen** (oder F5). Mit **Gründlich** (Standard) sucht der Scanner in zwei Durchgängen:
  1. alle Adressen, dazu gleichzeitig eine mDNS-Abfrage (Shelly und Pagurus OS melden sich darüber)
  2. alle, die nicht geantwortet haben, noch einmal mit längeren Zeitlimits – zuerst die, die per mDNS oder in
     der ARP-Tabelle des PCs auftauchen. Per mDNS gefundene Geräte außerhalb der eingetragenen Netze werden mit abgefragt.

  Jede Abfrage wird bei Zeitüberschreitung bis zu dreimal wiederholt (WLAN-Geräte verlieren unter Last Anfragen,
  ältere Gen1-Shellys antworten teils erst nach 2–3 s). Eine /24 dauert so etwa 15–30 s, ohne „Gründlich“ wenige Sekunden.
- Haken und **Filter** zum Eingrenzen, Klick auf eine Spaltenüberschrift sortiert. „Unbekannt“ ist anfangs ausgeblendet –
  taucht ein Shelly dort auf, hat er nur nicht rechtzeitig geantwortet: Rechtsklick → „Neu abfragen“.
- **Doppelklick** öffnet das Gerät im Browser. **Rechtsklick**: öffnen, neu abfragen, IP/MAC kopieren, neu starten.
- **CSV speichern** – für Excel (Semikolon, Dezimalkomma).
- **Passwort (optional)**: für geschützte Geräte (Benutzer `admin`); sonst steht dort „Passwort“.

**Starten**
- Mit Python 3 für Windows: Doppelklick auf `Pagurus-Scanner.pyw`
- Als EXE: einmal `tools\build_scanner.bat` ausführen → `Pagurus-Scanner.exe` (eine alte `Hermit-Scanner.exe` kann weg;
  die gespeicherten Netze werden übernommen)
- Konsole: `python tools/pagurus_scanner.py --cli 192.168.4.0/24 --csv geraete.csv` (`--schnell` = ein Durchgang)

Andere Netze (VLANs) werden gefunden, solange der PC sie erreicht (Routing/Firewall zwischen den VLANs).
