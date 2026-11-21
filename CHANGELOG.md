# Änderungen

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
