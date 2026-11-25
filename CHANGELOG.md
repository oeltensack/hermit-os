# Änderungen

## Pagurus Cloud 1.9.0 / Pagurus Portal 1.0
- **Pagurus Portal** (neu, Ordner `portal/`): Fernzugang als PHP-Seite für ein normales Webhosting.
  Die Cloud meldet sich dort von sich aus per HTTPS (kein offener Port, kein VPN), überträgt Geräte und
  Meldungen und holt Schaltbefehle per Long-Polling ab – Befehle kommen sofort an. Schalten, Rollläden,
  Fühlerwerte, Meldungen; Anmeldung mit Passwort und Sperre nach Fehlversuchen, optional nur Anzeige
- Cloud: *Einstellungen → Fernzugang-Portal* (Adresse, Schlüssel, Verbindungsstatus)

## Pagurus Cloud 1.8.2
- Geräteübersicht: Tasten „alle an“ / „alle aus“ bei den Räumen entfernt

## 1.17.2
- Update-Prüfung: sporadisches „update.json ungültig“ behoben (v. a. RF Bridge). Die Datei wird nicht mehr
  komplett in den RAM geladen; das Gerät liest nur Version und den eigenen Eintrag direkt aus dem Datenstrom,
  mit bis zu 3 Versuchen. Schlägt es trotzdem fehl, steht der Grund und der freie RAM in der Meldung

## 1.17.1 / Pagurus Cloud 1.8.1
- RF Bridge: Taste **„Leeren“** bei „Zuletzt empfangen“ (neue RPC `RF.ClearRecent`); gespeicherte Codes bleiben
- Cloud: **„Alle löschen“** bei den Meldungen (mit Rückfrage), neben „Alle quittieren“

## 1.17.0 / Pagurus Cloud 1.8.0
- **Neuer Name: Pagurus OS** (vorher Hermit OS – der Name gehört schon dem Rust-Unikernel HermitOS).
  Pagurus ist der Einsiedlerkrebs. Umbenannt sind Oberfläche, Doku, Flasher (`Pagurus-Flasher`, `pagurus-flash`),
  Scanner (`Pagurus-Scanner`), Loader-Seite, Cloud und das GitHub-Repo (`oeltensack/pagurus-os`)
- Unverändert für die Verträglichkeit: Gerätekennungen (`hermit-duo-…`), Varianten (`hermit-1pm`, Dateien in `bin/`),
  `fw_id` (`hermit-os/…`), MQTT-Topics, Zugangspunkt `hermit-…`. Vorhandene Geräte updaten weiter und bleiben in der Cloud
- Flasher erkennt den Loader unter altem und neuem Namen

## 1.16.1
- Update-Fehler behoben: Ein einmal gefundenes Update wurde nie neu geprüft. Kam danach eine neuere Version,
  passte die SHA-256 nicht mehr zur Datei in `bin/` und das Update wurde jedes Mal verworfen (z. B. Duo blieb
  auf 1.12 mit „1.13.0 verfügbar“). Jetzt wird vor jeder Installation und bei jedem Termin frisch geprüft

## 1.16.0
- **Sonoff RF Bridge 433** (`hermit-rfbridge`, R2 mit EFM8BB1, Original- oder Portisch-Firmware im Funkchip):
  - Empfangene Codes anzeigen, übernehmen und speichern als Taste, Kontakt (offen/zu) oder Schalter (Funksteckdose)
  - Funksteckdosen erscheinen als `switch:<n>` – schaltbar per RPC, Zeitplan, Regel, Hermit Cloud; Fernbedienung
    aktualisiert den Zustand
  - Ereignisse `rf.button`, `rf.open`, `rf.closed`, `rf.unknown`, `switch.on/off` je gespeichertem Code (Aktionen, MQTT)
  - Empfangsmodi Standard (24 Bit), alle Protokolle und Rohdaten (Portisch); Senden beliebiger Codes; Summer
  - RPC `RF.*`, Reiter „Funk“ in der Oberfläche
- **1-MB-Geräte:** Update über den neuen **Hermit Loader 1 MB** (lädt per HTTPS, prüft SHA-256, Einstellungen
  bleiben); WLAN aus Tasmota wird vor dem ersten Formatieren gelesen
- Flasher: RF Bridge von Tasmota (bei Platzmangel automatisch über tasmota-minimal) und Hermit-zu-Hermit-Update über den Loader

## Hermit Cloud 1.7.3
- Regeln mit Ereignis einer bestimmten Komponente, z. B. `rf:3.button` oder `rf:2.open` (RF Bridge)

## Hermit Cloud 1.7.2
- Fehler aus 1.7.1 behoben: Beim Löschen kam das Gerät sofort ohne Daten zurück (leere Nachricht zum
  Entfernen der gespeicherten Daten wurde als „offline“ gelesen). Leere Nachrichten und die Offline-Meldung
  eines unbekannten Geräts legen kein Gerät mehr an

## Hermit Cloud 1.7.1
- Gelöschte Geräte kommen nach einem Neustart oder Update der Cloud nicht mehr zurück: Beim Löschen werden
  die beim Broker gespeicherten (retained) Nachrichten des Geräts entfernt, und gespeicherte Nachrichten
  legen kein unbekanntes Gerät mehr an – nur ein Gerät, das wirklich sendet

## Hermit Cloud 1.7.0
- **Shelly Door/Window 2 und H&T (Gen1, Original-Firmware) als Fühler:** MQTT im Gen1-Format
  (`shellies/<id>/sensor/…`); Temperatur, Helligkeit, Feuchte und Batterie gehen in die Fühler und in die
  Beschattung, Kontakt/Neigung/Vibration werden ignoriert
- Gen1-Fühler werden beim Netzsuchen erkannt (wenn wach) und per Klick auf die Cloud als MQTT-Server gestellt
- Gerätekarte für Batteriefühler: Temperatur, Helligkeit, Feuchte, Batterie, letzte Meldung; kein Schalten,
  keine Sicherung, nicht in Raum-Schaltungen

## Hermit Cloud 1.6.0
- **Beschattung:** neuer Reiter – Rollläden nach Sonnenstand (Azimut/Höhe je Fassade) und Temperatur
  (Fühler oder Wetter von Open-Meteo), optional Innentemperatur, Höchstwert-Vorhersage, Einstrahlung;
  Verzögerung, Zeitfenster, Tage, Frostschutz; nur nach unten, Handbedienung hat Vorrang, „heute pausieren“;
  Kompass mit Sonnenstand und Fassaden
- **Shelly Gen2+ mit Original-Firmware parallel zur Shelly Cloud:** Netz durchsuchen, mit einem Klick als
  MQTT-Server eintragen (auch mit Gerätepasswort, Digest SHA-256), Warnung bei vorhandenem MQTT-Server
- Mehrkanal-Geräte (Plus 2PM, Pro 4PM, Pro Dual Cover) je Kanal als eigenes Gerät
- Fühler (Temperatur, Feuchte, Helligkeit) werden gesammelt
- Zeitpläne und Regeln: Aktion „Rollladen auf Position“; Rollläden in der Zielauswahl


## 1.15.0
- **Shelly 2.5** – zwei Firmwares:
  - `hermit-25`: zwei Schaltkanäle wie Shelly Plus 2PM (`switch:0`, `switch:1`), je Kanal Leistung, Spannung,
    Strom, cos φ, Frequenz und Energie (ADE7953), eigene Einstellungen, Timer, Aktionen, Kalibrierung
  - `hermit-25-cover`: Rollladen wie Shelly Plus 2PM (`cover:0`) – Auf/Ab/Stopp, Position nach Kalibrierfahrt,
    Endlagen am Motorstrom, Höchstfahrzeit, Hinderniserkennung, ein oder zwei Eingänge (Schalter/Taster),
    Richtungs- und Eingangstausch, 0,5 s Pause beim Richtungswechsel, nie beide Relais an
  - WLAN-Reset auch über den Taster auf der Platine (10 s)
- Firmware intern auf mehrere Kanäle umgebaut (Relais, Timer, Messung, Einstellungen)
- Flasher: Shelly 2.5 von Original-Firmware (erkennt Rollladenbetrieb) und Tasmota; `--cover` / `--switch`

## Hermit Cloud 1.5.0
- Shelly 2.5: Kanal 2 erscheint als eigenes Gerät (eigene Leistung, Energie, Raum, Regeln)
- Rollladen: Auf/Stopp/Zu und Positionsregler in der Gerätekarte; in Zeitplänen und Regeln gilt
  „an“ = öffnen, „aus“ = schließen; Meldungen bei Hindernis und fehlgeschlagener Kalibrierung
- Gerät löschen entfernt auch die Stundenwerte


## 1.14.0
- **Shelly Dimmer 2** (`hermit-dimmer2`): verhält sich wie ein Shelly Dimmer Gen3 (`light:0`, `Light.Set`,
  `Light.DimUp/DimDown/DimStop`), zwei Eingänge (`input:0` SW1, `input:1` SW2)
  - Taster: kurz an/aus, halten dimmt (Richtung wechselt); oder zwei Taster (SW1 heller, SW2 dunkler); oder Schalter
  - Mindest-/Höchsthelligkeit, Phasenab-/-anschnitt, Überblendzeit, Dimmgeschwindigkeit
  - Leistung und Energie vom STM32, Temperatur, Abschaltung bei 250 W / 85 °C
  - Licht bleibt bei Update und Neustart an; nach Stromausfall standardmäßig letzter Zustand
  - Läuft mit der Original-STM32-Firmware (2.x) und der quelloffenen (51.x) – es wird nichts am STM32 geflasht
- Flasher: Dimmer 2 von Original-Firmware (über mg2tasmota-ShellyDimmer2) und Tasmota

## Hermit Cloud 1.4.1
- Dimmer: Helligkeitsregler in der Gerätekarte (auch für die Duo), Fehler „Dimmer-Chip antwortet nicht“


## Hermit Scanner 1.0.0
- Neues Windows-Programm `Hermit-Scanner` (tools/hermit_scanner.py): findet Shelly Gen1, Shelly Gen2/Gen3, Tasmota und
  Hermit OS in einem oder mehreren Netzen; Name, Modell, Firmware, MAC, Empfang, Zustand, Leistung, Update, Laufzeit;
  Filter, Sortierung, Browser öffnen, Neustart, CSV-Export; EXE über tools/build_exe.bat

## Hermit Cloud 1.4.0
- **Verbrauch je Zeitraum** (wie in der Shelly-App): Tag (Stunden), Woche, Monat, Jahr – mit Blättern,
  gestrichelter Linie für den Zeitraum davor, Gesamt mit Kosten, Abweichung in %, Durchschnitt, Max, Min
- Stundenwerte werden ab jetzt gespeichert (für ältere Tage aus dem Leistungsverlauf berechnet)
- Diagramme: feinere Skalen, auf dem Handy größere Beschriftung

## Hermit Cloud 1.3.1
- Kühlgerät anlegen ergab „HTTP 500“ (Geräte-ID fehlte beim Speichern)

## Hermit Cloud 1.3.0
- Neuer Reiter **Kühlgeräte** (Kühlschrank, Gefrierschrank, Kombi): Kompressor-Zeitleiste der letzten 24 h,
  Laufanteil, Zyklen, Ø Laufzeit/Pause, Energie heute und Ø pro Tag, Kosten pro Jahr
- Warnungen per Push: Kompressor läuft ohne Pause (Tür offen?), läuft nicht mehr an (kühlt es noch?),
  Tagesverbrauch deutlich über dem Schnitt der letzten 14 Tage (Vereisung, Dichtung)

## 1.13.0
- **Mehrere Access Points mit gleichem Namen:** Hermit OS sucht vor dem Verbinden den stärksten AP und verbindet
  sich gezielt mit ihm. Liegt der Empfang unter −70 dBm (einstellbar), sucht es alle 5 min nach einem AP, der
  mindestens 8 dB besser ist, und wechselt. Ist der gewählte AP weg, wird nach 30 s neu gesucht.
  Einstellung unter WLAN, Status zeigt AP (BSSID), Kanal und Anzahl der Wechsel

## Hermit Cloud 1.2.3
- Updater erneuert seinen eigenen Container nicht mehr (das hat ihn zweimal gestoppt). Sein Skript liegt im
  Repo-Ordner und ist nach dem `git pull` schon neu – er liest es nach dem Update einfach neu ein.

## Hermit Cloud 1.2.2
- Kurze Verbindungsaussetzer (z. B. bei schwachem WLAN) erzeugen keine Meldungen mehr: „offline“ wird erst
  gemeldet, wenn ein Gerät länger als 2 Minuten weg ist (`OFFLINE_ALERT_AFTER`), „wieder online“ nur danach.
  Auch Regeln mit Auslöser offline/online warten diese Zeit ab.

## 1.12.4
- WLAN-Übernahme von der Original-Firmware: Mongoose OS hat zwei Dateisystem-Bereiche, ein Werksreset leert
  nur den aktiven. Bisher wurde der erste Treffer aus dem zweiten Bereich genommen – das konnte ein altes
  Passwort sein. Jetzt werden alle gefundenen Zugangsdaten (Chip-Speicher, beide Bereiche, Tasmota)
  gesammelt und beim ersten Start der Reihe nach je 25 s probiert, sichtbare Netze zuerst
- `Hermit.TestImport` zeigt alle gefundenen Einträge (`mgos_all`) und die probierten (`tried`)

## 1.12.3
- WLAN-Übernahme von der Original-Firmware: deren Konfiguration hat jetzt Vorrang vor dem SDK-Speicher
  (der kann einen alten Stand enthalten); feste IP nur, wenn IP, Maske und Gateway zusammenpassen
- Zugangspunkt bleibt erreichbar, während das Gerät das Heim-WLAN sucht: Suche nur noch alle 3 min für 30 s
  und nie, solange jemand mit dem Zugangspunkt verbunden ist
- Absturzschutz: nach 3 Abstürzen in Folge abgesicherter Start (keine Übernahme, kein MQTT, Zugangspunkt sofort)

## Hermit Cloud 1.2.1
- Updater: erneuert sich jetzt über einen Hilfscontainer – vorher blieb er nach einem Update, das ihn selbst
  betraf, gestoppt („Updater-Container läuft nicht“)

## Hermit Cloud 1.2.0
- **Wäsche:** Fertig-Erkennung für Waschmaschine, Trockner, Spülmaschine und beliebige Geräte – Läufe mit Dauer,
  Energie, Kosten und Spitzenleistung, Restzeit-Schätzung, Push bei „fertig“ und Erinnerung
- **Kosten:** Strompreis einstellbar, Euro überall neben kWh
- **Push** über ntfy (optional eigener Server per Compose-Profil) oder Telegram, Arten wählbar, Testnachricht
- **Räume** und Schalten je Raum
- **Zeitpläne** mit Uhrzeit oder Sonnenauf-/untergang, **Regeln** (Leistung, Schalter, Ereignis, online/offline,
  fertig/startet, URL-Aufruf) mit Zeitfenster, Verzögerung, automatischem Ausschalten und Push
- **Grundlast/Standby** je Gerät mit Kosten pro Jahr, **CSV-Export** (Energie, Leistung, Läufe)
- Leistungsverlauf 6 h bis 30 Tage mit Spitzenwerten und Zoom
- **Firmware-Übersicht** mit Update einzeln/alle, **Sicherung und Rücksicherung** der Geräte-Einstellungen
- **Benutzer mit Rollen** (Administrator, Benutzer, Betrachter)
- **PWA:** als App auf dem Handy-Startbildschirm

## Hermit Cloud 1.1.1 / Hermit OS 1.12.2
- Cloud: Nach einem Neustart der Cloud (z. B. Update) wurden abgezogene Geräte durch gespeicherte
  Broker-Nachrichten wieder als online angezeigt – gespeicherte Nachrichten zählen jetzt nicht mehr als Lebenszeichen
- Firmware: MQTT-Keepalive 30 s – ein abgezogenes Gerät erscheint nach ca. 45 s als offline

## Hermit Cloud 1.1.0
- Selbst-Update: Prüfung auf GitHub, Knopf „Jetzt aktualisieren“, automatische Installation zur eingestellten
  Stunde; neuer Container `hermit-cloud-updater` (git pull bzw. Archiv, Neubau, Neustart)

## 1.12.1
- Repo ist öffentlich: Auto-Update ohne Token, Hinweise zum Token nur noch für private Forks
- README überarbeitet (Download, Hermit Cloud mit Bild, Mitmachen, English summary), Issue-Vorlagen
- Hermit Cloud: Link „Energie“ nur bei Geräten mit Messung

## 1.12.0
- MQTT im Gen2-Format (optional): `MQTT.GetConfig/SetConfig/GetStatus`, Topics `…/online`, `…/rpc`,
  `…/events/rpc` (NotifyEvent/NotifyStatus), `…/status/<komponente>`, `…/info`; Karte „Cloud / MQTT“ unter System
- Neu: **Hermit Cloud** (`cloud/`) – Heim-Cloud als Docker-Paket mit Mosquitto: Geräteübersicht, Schalten,
  Energieverlauf (24 h, Tage, Monate, je Gerät) und Meldungen (Überlast, Übertemperatur, offline)

## 1.11.0
- Relais bleibt bei Software-Neustart (Update, Neustart-Befehl, Absturz) im bisherigen Zustand – wie bei der
  Original-Firmware. Der Arduino-Core setzte vorher alle GPIOs beim Start auf Eingang.
- Updates deshalb auch bei eingeschaltetem Relais (alle Geräte); die 20-W-Wartegrenze der Messfirmware entfällt

## 1.10.2
- Messfirmware: Auto-Update und Update per RPC trotz dauerhaft angezogenem Relais; automatisch erst bei Last unter 20 W.
  Beim Neustart fällt das Relais ca. 1 s ab, eine gespeicherte Schutzabschaltung bleibt erhalten.

## 1.10.1
- Messfirmware auch für den Shelly Plug (16 A): `hermit-plug-meter` (Reset am Gerätetaster, ohne Temperaturschutz)

## 1.10.0
- Neue Messfirmware für Shelly 1PM (`hermit-1pm-meter`) und Plug S (`hermit-plugs-meter`): nur
  Energiemessung, Relais dauerhaft an, nicht fernschaltbar; Schutzabschaltung bei Überlast/Übertemperatur
  bleibt gespeichert und wird nur am Taster bzw. SW-Eingang zurückgesetzt (Ereignis `switch.reset`)
- Flasher: Option „Messfirmware“ (`--meter`)

## 1.9.0
- Neues Gerät: Shelly Plug (16 A, `SHPLG-1`/`SHPLG2-1`) als `hermit-plug` – Relais GPIO4, Taster GPIO12,
  HLW8012-Leistungsmessung (12,53 W/Hz), LEDs rot/blau
- LED-Polarität je Gerät einstellbar (`LED_ON_LEVEL`)
- Tasmota-`PowerCal` wird je nach Messchip (BL0937/HLW8012) richtig umgerechnet
- Flasher: Plug von Original-Firmware (mg2tasmota-ShellyPlug2) und von Tasmota

## 1.8.7
- Geräte ohne Eingang (Duo) bieten keine Eingangs-Ereignisse mehr für Aktionen an
- Dokumentation neu gegliedert: README, `docs/FLASHEN.md`, `docs/GERAETE.md`, `docs/API.md`

## 1.8.6
- Duo: Regler und Farbknöpfe schalten die Lampe nicht mehr ein
- Duo: Reiter „Licht“ statt „Schalter“, nur passende Einstellungen

## 1.8.5
- Duo: zwei Weißkanäle (GPIO5 kalt, GPIO4 warm) statt Helligkeit/Farbtemperatur – Ein/Aus und Dimmen funktionieren
- Duo: WLAN-Reset zählt nur echte Stromunterbrechungen
- `Sys.GetStatus` meldet `reset_reason`

## 1.8.4
- Diagnose `Hermit.Gpio` für unbekannte Hardware

## 1.8.3
- Feste IP wird aus Original-Firmware und Tasmota übernommen
- Flasher sucht das Gerät nach dem Flashen per MAC-Adresse, falls es eine neue IP hat
- Duo: Anzeige „Eingang SW“ entfällt

## 1.8.2
- WLAN-Übernahme aus der Original-Firmware erkennt die Mongoose-Schreibweise mit Leerzeichen
- Diagnose `Hermit.TestImport`

## 1.8.1
- Plug S: Spannung und Strom waren vertauscht (SEL HIGH = Spannung)

## Flasher
- Grafische Oberfläche mit Netzsuche (`Hermit-Flasher.pyw`), EXE-Bau per `tools/build_exe.bat`
- mg2hermit wird unkomprimiert gepackt (die Original-Firmware kann kein DEFLATE)
- Port wird nach Abbruch freigegeben und unter Windows exklusiv belegt
- Protokoll zeigt OTA-Antwort, Update-Status, jede Verbindung und einen Selbsttest
- Ein Zip-Paket pro Modell

## 1.8.0
- Shelly Duo (E27/GU10) mit `cct:0` wie die Duo Bulb Gen3, Gen1-Endpunkt `/light/0`
- WLAN-Übernahme nur noch beim allerersten Start

## 1.7.0
- Shelly Plug S mit Leistung, Spannung, Strom, cos φ, Gerätetaster und LEDs

## 1.6.0
- Flash-Tool `hermit_flash.py`: Original-Firmware (mg2hermit), Tasmota, Loader, Hermit OS
- WLAN-Übernahme aus der Konfiguration der Original-Firmware

## 1.5.x
- Hermit Loader für den direkten Wechsel von Tasmota (ca. 230 KB)
- WLAN-Übernahme aus den Tasmota-Einstellungen und dem WLAN-Speicher des ESP

## 1.4.x
- Auto-Update über `update.json` mit SHA-256-Prüfung, `Shelly.CheckForUpdate`/`Shelly.Update`
- Eingangsdiagnose und Eingangsfilter Wechselspannung/Pegel

## 1.3.0
- Add-on-Variante: Gen1-Temperatur-Add-on als potenzialfreier Taster (`input:100`)

## 1.2.0
- Umbenennung in Hermit OS

## 1.1.0
- 1PM: Standardkalibrierung für den BL0937 (vorher ca. 7-fach zu hoch), Kalibrierung über Referenz,
  Tasmota-`PowerCal` oder Faktor, bleibt bei Werkseinstellungen erhalten

## 1.0.0
- Erste Version für Shelly 1 und 1PM: Gen2-RPC, Webhooks, Weboberfläche, OTA, Energiemessung
