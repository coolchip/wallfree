#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Firmware-Update ueber den Original-Bootloader (SOLL-BOOT-30..33), Notweg zur Weboberflaeche.
#   scripts/update.sh -u benutzer:passwort adresse[:port] datei.bin
#       Uebergabe ueber die API, Upload, Warten auf den Neustart, Ergebnis
#   scripts/update.sh [-u benutzer:passwort] --upload-only adresse[:port] datei.bin
#       nur Upload, z. B. nach "evse bootloader" in der Shell
# Der Bootloader wartet bis 2 min auf den Upload; die Uebertragung muss in 30 s fertig sein.
set -eu
creds=
only=0
while [ $# -gt 2 ]; do
	case "$1" in
	-u) creds=$2; shift 2 ;;
	--upload-only) only=1; shift ;;
	*) break ;;
	esac
done
[ $# -eq 2 ] || { sed -n '3,8p' "$0"; exit 2; }
host=$1
image=$2
[ -f "$image" ] || { echo "Datei fehlt: $image" >&2; exit 2; }
size=$(wc -c < "$image")
[ "$size" -le 327680 ] || { echo "Abbild zu gross ($size Byte, hoechstens 320 KB)" >&2; exit 2; }
[ $only -eq 1 ] || [ -n "$creds" ] || { echo "Zugangsdaten fehlen (-u)" >&2; exit 2; }

# Antwort auf GET /system/info ohne Anmeldung: "wallfree" (JSON, auch 401), "bootloader" (HTML)
# oder "weg"
probe() {
	body=$(curl -s --max-time 2 "http://$host/system/info" 2>/dev/null) ||
		{ echo weg; return; }
	case "$body" in
	"{"*) echo wallfree ;;
	"") echo weg ;;
	*) echo bootloader ;;
	esac
}

wait_for() { # $1 Zustand, $2 Sekunden
	i=0
	while [ "$(probe)" != "$1" ]; do
		i=$((i + 1))
		[ $i -lt "$2" ] || return 1
		sleep 1
	done
}

if [ $only -eq 0 ]; then
	echo "Uebergabe an den Bootloader ..."
	curl -sf --digest -u "$creds" -X POST -H 'Content-Type: application/json' -d '{}' \
		"http://$host/system/update" || { echo "Uebergabe abgelehnt" >&2; exit 1; }
	echo
fi
echo "Warten auf den Bootloader ..."
wait_for bootloader 60 || { echo "Bootloader antwortet nicht unter $host" >&2; exit 1; }
echo "Hochladen ($size Byte) ..."
curl -s --max-time 30 -o /dev/null -F "datafile=@$image;type=application/octet-stream" \
	"http://$host/upload.cgi" || true
echo "Warten auf den Neustart ..."
if wait_for wallfree 120; then
	if [ -n "$creds" ]; then
		curl -s --digest -u "$creds" "http://$host/system/info"
		echo
	else
		echo "Wallfree laeuft wieder (Ergebnis: GET /system/info, update_result)"
	fi
else
	echo "Keine Wallfree-Antwort (anderes Abbild, z. B. Originalfirmware, oder Update gescheitert)"
fi
