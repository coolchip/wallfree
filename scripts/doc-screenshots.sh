#!/bin/sh
# Screenshots fuer docs/anleitung/ neu erzeugen: eigene Simulation (fabrikneu, Telnet 2324,
# damit eine laufende Simulation unberuehrt bleibt), 60-fach beschleunigt; Dauer etwa 8 min.
#   scripts/doc-screenshots.sh
set -e
REPO=$(cd "$(dirname "$0")/.." && pwd)
OUT=$REPO/build/doc-sim
. "$HOME/zephyrproject/.venv/bin/activate"
(cd "$HOME/zephyrproject/zephyr" &&
	west build -b native_sim/native/64 "$REPO/app" -d "$OUT" -- -DCONFIG_SHELL_TELNET_PORT=2324 \
		>"$OUT.log" 2>&1) || { echo "BUILD FAILED (log: $OUT.log)"; exit 1; }
for p in 8097 2324; do
	if ss -ltn "sport = :$p" | grep -q LISTEN; then echo "Port $p belegt (andere Simulation?)"; exit 1; fi
done
rm -f "$OUT/flash.bin"
"$OUT/zephyr/zephyr.exe" --flash="$OUT/flash.bin" --http-port=8097 --rt-ratio=60 >"$OUT/sim.log" 2>&1 &
SIM=$!
trap 'kill $SIM 2>/dev/null' EXIT
sleep 3
mkdir -p "$REPO/docs/anleitung/bilder"
node "$REPO/scripts/doc-screenshots.mjs" 8097 2324 "$REPO/docs/anleitung/bilder"
