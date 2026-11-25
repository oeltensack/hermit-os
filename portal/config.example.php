<?php
// Pagurus Portal – Konfiguration. Als config.php speichern und beide Werte ändern.

// Gleicher Wert wie in der Pagurus Cloud unter Einstellungen → Fernzugang-Portal → Schlüssel.
// Lang und zufällig, z. B.:  php -r "echo bin2hex(random_bytes(24));"
define('SYNC_TOKEN', 'HIER-LANGEN-ZUFAELLIGEN-SCHLUESSEL-EINTRAGEN');

// Passwort für die Anmeldung im Portal
define('LOGIN_PASSWORD', 'HIER-PASSWORT-EINTRAGEN');

// Nur Anzeigen, kein Schalten (true = schreibgeschützt)
define('READ_ONLY', false);
