#!/bin/sh
# Erzeugt die Passwortdatei aus MQTT_USER/MQTT_PASS und startet Mosquitto.
set -e
CONF=/mosquitto/config/mosquitto.conf
cp /hermit/mosquitto.conf "$CONF"
if [ -n "$MQTT_USER" ]; then
  : > /mosquitto/config/passwd
  mosquitto_passwd -b /mosquitto/config/passwd "$MQTT_USER" "$MQTT_PASS"
  chmod 0700 /mosquitto/config/passwd
  chown mosquitto:mosquitto /mosquitto/config/passwd 2>/dev/null || true
else
  echo "WARNUNG: MQTT_USER leer – Broker ohne Anmeldung" >&2
  sed -i 's/^password_file.*//; s/^allow_anonymous false/allow_anonymous true/' "$CONF"
fi
exec mosquitto -c "$CONF"
