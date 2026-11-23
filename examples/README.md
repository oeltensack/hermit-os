# Beispielscripte für Shelly Gen2/Gen3

Die Scripte laufen auf einem **Plus-, Pro- oder Gen3-Gerät** und arbeiten mit Pagurus-OS-Geräten zusammen.
IP-Adressen oben im Script anpassen, dann in der Weboberfläche des Gen2-Geräts unter *Scripts* anlegen und starten.

| Script | Zweck |
|---|---|
| `gen2_schaltet_altgeraet.js` | Das Relais des Gen2-Geräts wird auf ein altes Shelly 1/1PM gespiegelt |
| `gen2_empfaengt_ereignisse.js` | Endpunkt `/script/<id>/alt` für Aktionen vom Altgerät, z. B. Wandschalter am Altgerät schaltet das Gen2-Relais |
| `gen2_liest_leistung.js` | Fragt alle 10 s die Leistung eines alten 1PM ab und schaltet das eigene Relais mit Nachlauf (z. B. Absaugung) |
| `plus2pm_heizluefter_stufe2.js` | Heizlüfter: 2. Heizstufe am 1PM, gesteuert vom Plus 2PM – mit Verriegelung (nur bei laufendem Lüfter und Stufe 1), Watchdog über `toggle_after`, Leistungsprüfung und Erkennung eines klebenden Relais |

## Sicherheit bei Heizgeräten

Wenn ein Pagurus-Gerät eine Heizung schaltet, sollte es **nie ohne Ablaufzeit** eingeschaltet werden:
`Switch.Set?id=0&on=true&toggle_after=45` und den Befehl regelmäßig erneuern. Fällt das WLAN oder das
steuernde Script aus, schaltet das Gerät selbst ab. Zusätzlich `auto_off` im Gerät setzen – das wirkt auch,
wenn jemand über die Weboberfläche einschaltet. Das Heizlüfter-Script macht genau das.
