#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Firmware-Update ueber den Original-Bootloader (SOLL-BOOT-30..33), Notweg zur Weboberflaeche.
#   scripts/update.sh -u benutzer:passwort adresse[:port] datei.bin
#       Uebergabe ueber die API, Upload, Warten auf den Neustart, Ergebnis
#   scripts/update.sh [-u benutzer:passwort] --upload-only adresse[:port] datei.bin
#       nur Upload, z. B. nach "bootloader" in der Shell
# Der Bootloader wartet bis 2 min auf den Upload; die Uebertragung muss in 30 s fertig sein.
# Jede andere HTTP-Anfrage an die Wallbox waehrend des Uploads bricht ihn ab (wallbe2
# docs/bootloader.md). Ein abgebrochener Upload wird bis zu zweimal wiederholt: Nach der
# Uebergabe hat der Bootloader 3 Versuche unter derselben Adresse, danach wartet er ohne
# Programm unter 192.168.0.8.
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
# Ein Upload gilt als angekommen, wenn der Bootloader mit 200 und einer Seite ohne
# Upload-Formular antwortet. Abbruch: Verbindung ohne Antwort geschlossen; ohne Datei: Formular.
upload() {
	resp=$(curl -s --max-time 30 -w '\n%{http_code}' \
		-F "datafile=@$image;type=application/octet-stream" "http://$host/upload.cgi" 2>/dev/null) ||
		return 1
	code=$(printf '%s\n' "$resp" | tail -n 1)
	[ "$code" = 200 ] || return 1
	printf '%s\n' "$resp" | sed '$d' | grep -qi 'upload.cgi' && return 1
	printf '%s\n' "$resp" | sed '$d' | grep -q . || return 1
	return 0
}

echo "Warten auf den Bootloader ..."
wait_for bootloader 60 || { echo "Bootloader antwortet nicht unter $host" >&2; exit 1; }
n=1
while :; do
	echo "Hochladen ($size Byte, Versuch $n von 3) ..."
	upload && break
	echo "Upload nicht bestaetigt (abgebrochen, z. B. durch eine andere Anfrage an die Wallbox)"
	[ $n -lt 3 ] || {
		echo "Update gescheitert. Ohne Programm wartet der Bootloader unter 192.168.0.8:" >&2
		echo "  scripts/update.sh --upload-only 192.168.0.8 $image" >&2
		exit 1
	}
	n=$((n + 1))
	# Nach einem verfruehten Ende kann schon die neue Firmware laufen
	i=0
	while :; do
		st=$(probe)
		[ "$st" = bootloader ] && break
		[ "$st" = wallfree ] && break 2
		i=$((i + 1))
		[ $i -lt 90 ] || { echo "Bootloader antwortet nicht mehr unter $host" >&2; exit 1; }
		sleep 1
	done
done
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
