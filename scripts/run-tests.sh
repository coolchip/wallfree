#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Alle PC-Tests (Ladelogik, Parameterspeicher) auf native_sim/native/64 ausfuehren.
# Voraussetzung: Zephyr-Umgebung unter ~/zephyrproject (siehe README).
#   scripts/run-tests.sh            twister (Standard)
#   scripts/run-tests.sh --simple   ohne twister: jede Suite mit west build
#   scripts/run-tests.sh --coverage Zeilen-/Zweigabdeckung von lib/evse (gcovr)
set -e
REPO=$(cd "$(dirname "$0")/.." && pwd)
OUT=${WALLFREE_TEST_OUT:-$REPO/build/tests}
mkdir -p "$OUT"
. "$HOME/zephyrproject/.venv/bin/activate"
cd "$HOME/zephyrproject/zephyr"

suites=$(find "$REPO/tests" -name testcase.yaml -exec dirname {} \; | sort)

build_and_run() { # $1 Suite-Verzeichnis, $2 Zusatzoptionen fuer CMake
	name=$(echo "${1#$REPO/tests/}" | tr / _)
	echo "=== $name"
	west build -p always -b native_sim/native/64 "$1" -d "$OUT/$name" -- $2 \
		>"$OUT/$name.log" 2>&1 || { echo "BUILD FAILED (log: $OUT/$name.log)"; return 1; }
	"$OUT/$name/zephyr/zephyr.exe" >"$OUT/$name.run" 2>&1 || true
	grep -E "^ FAIL|TESTSUITE .* (succeeded|failed)" "$OUT/$name.run" || true
	grep -q "PROJECT EXECUTION SUCCESSFUL" "$OUT/$name.run" || { echo "TESTS FAILED"; return 1; }
}

case "$1" in
--simple|--coverage)
	opt=""
	[ "$1" = "--coverage" ] && opt="-DCONFIG_COVERAGE=y"
	fail=0
	dirs=""
	for t in $suites; do
		build_and_run "$t" "$opt" || fail=1
		dirs="$dirs $OUT/$(echo "${t#$REPO/tests/}" | tr / _)"
	done
	if [ "$1" = "--coverage" ]; then
		cd "$REPO"
		gcovr -r . --filter 'lib/' $dirs
		gcovr -r . --filter 'lib/' --txt-metric branch $dirs | tail -n 14
	fi
	exit $fail
	;;
*)
	exec west twister -T "$REPO/tests" -p native_sim/native/64 -O "$OUT/twister" "$@"
	;;
esac
