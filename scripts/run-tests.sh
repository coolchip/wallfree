#!/bin/sh
# Alle PC-Tests der Ladelogik auf native_sim/native/64 bauen und ausfuehren.
# Voraussetzung: Zephyr-Umgebung unter ~/zephyrproject (siehe README).
# Ohne twister (dessen Python-Abhaengigkeiten sind nicht installiert).
set -e
REPO=$(cd "$(dirname "$0")/.." && pwd)
OUT=${WALLFREE_TEST_OUT:-$REPO/build/tests}
mkdir -p "$OUT"
. "$HOME/zephyrproject/.venv/bin/activate"
cd "$HOME/zephyrproject/zephyr"
fail=0
for t in $(find "$REPO/tests" -name testcase.yaml -exec dirname {} \; | sort); do
	name=$(echo "${t#$REPO/tests/}" | tr / _)
	echo "=== $name"
	west build -p always -b native_sim/native/64 "$t" -d "$OUT/$name" >"$OUT-$name.log" 2>&1 ||
		{ echo "BUILD FAILED (log: $OUT-$name.log)"; fail=1; continue; }
	"$OUT/$name/zephyr/zephyr.exe" >"$OUT-$name.run" 2>&1 || true
	grep -E "^ (PASS|FAIL|SKIP)|TESTSUITE .* (succeeded|failed)" "$OUT-$name.run" || true
	grep -q "PROJECT EXECUTION SUCCESSFUL" "$OUT-$name.run" || { echo "TESTS FAILED"; fail=1; }
done
exit $fail
