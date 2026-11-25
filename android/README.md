# Pagurus App (Android)

Eigene Android-App für die Pagurus Cloud – ohne Shelly Cloud, ohne Google-Dienste.

- **Zuhause direkt, unterwegs übers Portal:** Ist die Cloud im WLAN erreichbar, spricht die App sie direkt an
  (schnell, auch ohne Internet). Sonst gehen alle Anfragen über das Pagurus Portal auf dem Webhosting – die
  App kann dann trotzdem alles, was die Cloud kann. Umgeschaltet wird automatisch.
- **Geräte:** Schalten, Rollläden (Auf/Stopp/Zu/Position), Szenen, Fühler (auch BLU), Leistung und Energie heute
- **Meldungen** mit „Alle quittieren“ / „Löschen“
- **Cloud:** die vollständige Cloud-Oberfläche (Energie, Automatik, Beschattung, Einstellungen) – auch unterwegs
- **Push-Meldungen** als Android-Benachrichtigung über einen eigenen Hintergrunddienst (stille Daueranzeige
  „Wartet auf Meldungen“). Online/Reset/Update werden nicht gemeldet.
- **Widget** mit bis zu 6 Favoriten (Szenen/Geräte, grün = an)
- **Schnellkacheln** „Pagurus 1–3“ in den Schnelleinstellungen

## Installieren

`release/Pagurus-App-x.y.z.apk` aufs Handy laden und öffnen (einmalig „Installation aus dieser Quelle zulassen“).
Updates einfach drüber installieren – Einstellungen bleiben.

## Einrichten

1. *Einstellungen* → **Cloud zu Hause**: Adresse (z. B. `192.168.0.50:8080`), Benutzer, Passwort der Cloud
   (leer lassen, wenn die Cloud ohne Anmeldung läuft)
2. **Portal für unterwegs**: Adresse des Portals und Portal-Passwort
3. „Speichern und prüfen“
4. **Push** einschalten, Benachrichtigungen erlauben und unter „Akku-Optimierung öffnen“ Pagurus auf
   „Nicht optimieren“ stellen – sonst beendet Android den Dienst irgendwann
5. Favoriten fürs Widget anhaken, Kacheln belegen; Widget über langes Drücken auf den Startbildschirm →
   Widgets → Pagurus

Voraussetzung: Pagurus Cloud ab 1.12 und Pagurus Portal ab 1.3 (Weiterleitung für die App).

## Sicherheit

Über das Portal-Passwort hat die App unterwegs Admin-Rechte in der Cloud. Ist im Portal `READ_ONLY` gesetzt,
kann die App unterwegs nur anzeigen.

## Selbst bauen

```bash
cd android
echo "sdk.dir=/pfad/zum/android-sdk" > local.properties
gradle assembleRelease          # signiert mit pagurus.jks, wenn vorhanden (Passwort in PAGURUS_KS_PASS)
```

Der Signaturschlüssel `pagurus.jks` liegt nicht im Repo. Updates lassen sich nur mit demselben Schlüssel
über die installierte App installieren – ihn also gut aufheben.
