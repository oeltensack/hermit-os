# Pagurus Portal – Fernzugang über ein Webhosting

Kleine PHP-Seite für ein normales Webhosting-Paket (z. B. netcup Webhosting). Die Pagurus Cloud zu Hause
meldet sich dort von sich aus per HTTPS: Sie überträgt Gerätezustand und Meldungen und holt die im Portal
ausgelösten Befehle ab. Zu Hause muss **kein Port** geöffnet werden, es braucht keinen VPN-Tunnel.

Im Portal: Szenen, Geräte nach Räumen, Schalten, Rollläden (Auf/Stopp/Zu/Position), Fühlerwerte,
Leistung jetzt und Energie heute, Meldungen.
Einrichten, Regeln, Energieverlauf usw. bleiben in der Cloud zu Hause.

## Voraussetzungen

- PHP 8.0 oder neuer mit `pdo_sqlite` (bei netcup Webhosting vorhanden)
- HTTPS für die Domain/Subdomain (bei netcup im Kundenbereich: Let's Encrypt einschalten)

## Einrichten

1. Den Ordner `portal/` auf das Webhosting laden, z. B. nach `httpdocs/pagurus/`
   (FTP oder Dateimanager im Kundenbereich).
2. `config.example.php` als `config.php` kopieren und darin eintragen:
   - `SYNC_TOKEN` – langer Zufallswert, z. B. von `php -r "echo bin2hex(random_bytes(24));"`
     oder `openssl rand -hex 24`
   - `LOGIN_PASSWORD` – Passwort für die Anmeldung im Portal
   - `READ_ONLY` auf `true`, wenn im Portal nur angezeigt, nicht geschaltet werden soll
3. Der Ordner `data/` muss für PHP beschreibbar sein (Datenbank `portal.sqlite`). Er ist über `.htaccess`
   vom Web aus gesperrt; `https://…/pagurus/data/portal.sqlite` muss „403“ liefern – einmal prüfen.
4. In der Pagurus Cloud: *Einstellungen → Fernzugang-Portal* → Adresse (`https://deine-domain.de/pagurus/`)
   und denselben Schlüssel eintragen, speichern. Nach wenigen Sekunden steht dort „verbunden“.
5. `https://deine-domain.de/pagurus/` aufrufen und anmelden.

**Als App:** Android/Chrome: Menü → „App installieren“ bzw. „Zum Startbildschirm“. iPhone/Safari: Teilen →
„Zum Home-Bildschirm“. Das Portal startet dann im Vollbild mit eigenem Symbol.

**Update des Portals:** neue Dateien hochladen und überschreiben – `config.php` und `data/` bleiben.

Die **Android-App** (separat) nutzt das Portal unterwegs ebenfalls: Es reicht ihre Anfragen an die Cloud
durch, dadurch kann die App auch unterwegs alles, was die Cloud kann. Das Portal-Passwort gibt damit volle
Rechte – bei `READ_ONLY = true` kann die App unterwegs nur anzeigen.

## Wie es läuft

- Ist niemand im Portal, wartet die Cloud dort jeweils bis zu 20 s auf einen Befehl (Long-Polling) –
  etwa 3 Abrufe pro Minute. Ein Befehl kommt sofort an.
- Ist das Portal offen, gleicht die Cloud alle 2 s ab, Werte sind dann aktuell.
- Befehle, die die Cloud 60 s lang nicht abholt (Cloud aus, Internet weg), verfallen und werden nicht
  später ausgeführt. Oben rechts zeigt das Portal, wann sich die Cloud zuletzt gemeldet hat.
- Geschaltet wird über die Cloud wie aus ihrer eigenen Oberfläche (Frostschutz der Beschattung gilt auch).
- Nach 5 falschen Passwörtern ist die Anmeldung von dieser IP für 15 Minuten gesperrt.

Statt in den Einstellungen kann die Cloud das Portal auch über die `.env` bekommen:
`PORTAL_URL=https://deine-domain.de/pagurus/` und `PORTAL_TOKEN=…`. Werte aus den Einstellungen haben Vorrang.
