#!/bin/sh
# Firmware als PC-Simulation starten (Browser, curl, spaeter evcc).
#   scripts/sim-run.sh [--build] [Optionen von zephyr.exe]
# Beispiele: --http-port=8081 --dip=0x109 --rotary=5 --reset-button=11000
# Web/API: http://127.0.0.1:8081/ (Werkszugang admin/wallfree),
# Shell: Telnet 127.0.0.1:2323 (nur Simulation), Hardwaremodell mit "sim ..." (docs/simulation-bedienung.md).
# Die Flash-Datei build/sim-flash.bin bleibt ueber Neustarts erhalten;
# loeschen = fabrikneues Geraet. Beenden mit Strg+C.
set -e
REPO=$(cd "$(dirname "$0")/.." && pwd)
OUT=${WALLFREE_SIM_OUT:-$REPO/build/sim}
if [ "$1" = "--build" ] || [ ! -x "$OUT/zephyr/zephyr.exe" ]; then
	[ "$1" = "--build" ] && shift
	. "$HOME/zephyrproject/.venv/bin/activate"
	(cd "$HOME/zephyrproject/zephyr" &&
		west build -b native_sim/native/64 "$REPO/app" -d "$OUT")
fi
# Update-Uebergabe (SOLL-BOOT-30): Ersatz fuer den Update-Betrieb des Bootloaders
export WALLFREE_SIM_BOOTLOADER="${WALLFREE_SIM_BOOTLOADER-$REPO/scripts/bootloader-sim.py}"
exec "$OUT/zephyr/zephyr.exe" --flash="$REPO/build/sim-flash.bin" "$@"
