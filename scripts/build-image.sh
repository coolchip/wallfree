#!/bin/sh
# Abbild fuer die Steuerung bauen und ablegen:
#   scripts/build-image.sh [--keep N]
# Baut pristine fuer ev_cc_ac1 (Versionskennung = Commit, bei Aenderungen "-dirty"), prueft den
# Anhang mit der Pruefsumme, kopiert nach build/wallfree-<version>.bin und loescht aeltere
# Abbilder: Die N neuesten bleiben (Default 3). Jedes geloeschte laesst sich aus seinem Commit
# neu bauen (git worktree add ... <commit>; dann dieses Skript dort).
set -e
REPO=$(cd "$(dirname "$0")/.." && pwd)
KEEP=3
if [ "$1" = "--keep" ]; then
	KEEP=$2
fi
OUT=$REPO/build/ev_cc_ac1
if [ -n "$(git -C "$REPO" status --porcelain --untracked-files=no)" ]; then
	echo "Hinweis: Arbeitsstand nicht committet, Version endet auf -dirty"
fi
. "$HOME/zephyrproject/.venv/bin/activate"
(cd "$HOME/zephyrproject/zephyr" &&
	west build -p always -b ev_cc_ac1 "$REPO/app" -d "$OUT" >"$OUT.log" 2>&1) ||
	{ echo "Bauen fehlgeschlagen (Log: $OUT.log)"; exit 1; }
VER=$(grep -a -o 'WALLFREE-IMAGE:[0-9A-Za-z-]*' "$OUT/zephyr/zephyr.bin" | head -1 | cut -d: -f2)
DST=$REPO/build/wallfree-$VER.bin
cp "$OUT/zephyr/zephyr.bin" "$DST"
python3 -I "$REPO/scripts/image-trailer.py" --check "$DST"
grep -A2 "Memory region" "$OUT.log" | tail -2
# Aufraeumen: Abbilder eines nicht committeten Stands (-dirty) ausser dem gerade gebauten
# immer; von den committeten die KEEP neuesten behalten (nach Aenderungszeit)
for f in "$REPO"/build/wallfree-*-dirty.bin; do
	if [ -e "$f" ] && [ "$f" != "$DST" ]; then
		rm -f "$f"
		echo "geloescht: $(basename "$f")"
	fi
done
ls -t "$REPO"/build/wallfree-*.bin | grep -v -- '-dirty\.bin$' | tail -n +"$((KEEP + 1))" |
	while read -r f; do
		rm -f "$f"
		echo "geloescht: $(basename "$f")"
	done
