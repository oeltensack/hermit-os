#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Pagurus Cloud Updater: wartet auf data/update.request (von der Cloud-App angelegt),
# holt den neuen Stand von GitHub und baut die Container neu.
CLOUD="${HERMIT_CLOUD_DIR:?HERMIT_CLOUD_DIR fehlt}"
# Uhrzeiten im Protokoll in deutscher Zeit. POSIX-Regel statt Europe/Berlin: funktioniert auch ohne tzdata
# im Updater-Image (busybox date); der Container wird beim Update nicht neu gebaut.
case "$TZ" in ""|UTC|Etc/UTC|Europe/Berlin) export TZ="CET-1CEST,M3.5.0,M10.5.0/3" ;; esac
REPO="$(dirname "$CLOUD")"
DATA="$CLOUD/data"
SRC_TGZ="https://codeload.github.com/oeltensack/pagurus-os/tar.gz/refs/heads/main"
mkdir -p "$DATA"
[ -f "$CLOUD/docker-compose.yml" ] || { echo "Falscher Pfad $CLOUD – docker compose im Ordner cloud/ starten"; sleep 3600; exit 1; }

do_update() {
  echo "== $(date '+%d.%m.%Y %H:%M:%S') Update startet"
  if [ -d "$REPO/.git" ]; then
    cd "$REPO" || return 1
    git config --global --add safe.directory "$REPO"
    echo "-- git pull"
    git pull --ff-only origin main || { echo "git pull fehlgeschlagen (lokale Änderungen im Repo?)"; return 1; }
  else
    echo "-- kein git-Klon, lade Archiv"
    rm -rf /tmp/hos && mkdir -p /tmp/hos
    wget -qO- "$SRC_TGZ" | tar xz -C /tmp/hos || { echo "Download fehlgeschlagen"; return 1; }
    # .env und data/ liegen nicht im Archiv und bleiben erhalten
    cp -a /tmp/hos/*-main/cloud/. "$CLOUD"/ || return 1
  fi
  cd "$CLOUD" || return 1
  echo "-- docker compose build"
  docker compose build cloud || return 1
  echo "-- Container neu starten"
  docker compose up -d mosquitto cloud || return 1
  docker image prune -f >/dev/null 2>&1
  echo "== FERTIG $(date '+%d.%m.%Y %H:%M:%S')"
}

# Waechter: laeuft die Cloud nicht (beendet) oder antwortet sie 4 min lang nicht, wird sie wieder gestartet.
# Protokoll in data/watchdog.log (die letzten 200 Zeilen).
WD_N=0; WD_FAIL=0
watchdog() {
  WD_N=$((WD_N + 1)); [ $((WD_N % 4)) -eq 0 ] || return 0      # etwa jede Minute
  st="$(docker inspect -f '{{.State.Status}} {{.State.ExitCode}} {{.State.FinishedAt}}' hermit-cloud 2>/dev/null)"
  case "$st" in
    running*)
      if wget -q -T 10 -O /dev/null "http://cloud:8080/api/info/public" 2>/dev/null; then WD_FAIL=0; return 0; fi
      WD_FAIL=$((WD_FAIL + 1))
      [ "$WD_FAIL" -ge 4 ] || return 0
      wd_log "Cloud antwortet seit 4 min nicht – Neustart"
      docker logs --tail 40 hermit-cloud >> "$DATA/watchdog.log" 2>&1
      docker restart hermit-cloud >/dev/null 2>&1; WD_FAIL=0 ;;
    "")
      wd_log "Cloud-Container fehlt – starte"; (cd "$CLOUD" && docker compose up -d mosquitto cloud) >> "$DATA/watchdog.log" 2>&1 ;;
    *)
      wd_log "Cloud-Container nicht aktiv ($st) – starte"
      docker logs --tail 40 hermit-cloud >> "$DATA/watchdog.log" 2>&1
      (cd "$CLOUD" && docker compose up -d mosquitto cloud) >> "$DATA/watchdog.log" 2>&1 ;;
  esac
}
wd_log() {
  echo "== $(date '+%d.%m.%Y %H:%M:%S') $*" >> "$DATA/watchdog.log"
  tail -n 200 "$DATA/watchdog.log" > "$DATA/watchdog.tmp" 2>/dev/null && mv "$DATA/watchdog.tmp" "$DATA/watchdog.log"
}

echo "Pagurus Cloud Updater läuft ($CLOUD)"
while true; do
  watchdog
  date +%s > "$DATA/updater.alive"
  if [ -f "$DATA/update.request" ]; then
    { do_update || echo "== FEHLER beim Update"; } > "$DATA/update.log" 2>&1
    rm -f "$DATA/update.request"
    cat "$DATA/update.log"
    # Der Container des Updaters bleibt bestehen. Sein Skript liegt im Repo-Ordner und ist durch den
    # git pull bereits neu – einfach neu einlesen.
    exec sh "$CLOUD/updater/update.sh"
  fi
  sleep 15
done
