#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Ende-zu-Ende-Test auf dem PC: Anwendung fuer native_sim/native/64 bauen
# (simulierte Hardware, Host-Sockets, Flash-Datei) und die Python-Tests in
# tests/e2e gegen den laufenden Prozess ausfuehren.
#   scripts/run-e2e.sh              bauen und alle Tests
#   scripts/run-e2e.sh -k Faults    nur passende Tests (unittest -k)
# WALLFREE_E2E_KEEP=1 behaelt Flash-Dateien und Logs (Pfad wird ausgegeben).
set -e
REPO=$(cd "$(dirname "$0")/.." && pwd)
OUT=${WALLFREE_SIM_OUT:-$REPO/build/sim}
. "$HOME/zephyrproject/.venv/bin/activate"
(cd "$HOME/zephyrproject/zephyr" &&
	west build -b native_sim/native/64 "$REPO/app" -d "$OUT" >"$OUT.log" 2>&1) ||
	{ echo "BUILD FAILED (log: $OUT.log)"; exit 1; }
# zweiter Bau wie die Steuerung: ohne Konsolen-Log und Telnet (T9WieSteuerung)
(cd "$HOME/zephyrproject/zephyr" &&
	west build -b native_sim/native/64 "$REPO/app" -d "$OUT-hw" -- \
		-DCONFIG_LOG_BACKEND_NATIVE_POSIX=n -DCONFIG_SHELL_BACKEND_TELNET=n >"$OUT-hw.log" 2>&1) ||
	{ echo "BUILD FAILED (log: $OUT-hw.log)"; exit 1; }
export WALLFREE_SIM_EXE="$OUT/zephyr/zephyr.exe"
export WALLFREE_SIM_EXE_HW="$OUT-hw/zephyr/zephyr.exe"
cd "$REPO/tests/e2e"
exec python3 -m unittest -v test_e2e "$@"
