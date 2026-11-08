#!/bin/sh
# Wallfree-Patches auf den Zephyr-Baum anwenden (idempotent).
#   scripts/zephyr-patches.sh          anwenden, bereits angewendete ueberspringen
#   scripts/zephyr-patches.sh --check  nur pruefen (Exit 1, wenn einer fehlt)
# Zephyr-Baum: $ZEPHYR_BASE, sonst ~/zephyrproject/zephyr.
set -e
REPO=$(cd "$(dirname "$0")/.." && pwd)
ZB=${ZEPHYR_BASE:-$HOME/zephyrproject/zephyr}
missing=0
for p in "$REPO"/patches/zephyr/*.patch; do
	name=$(basename "$p")
	if git -C "$ZB" apply --reverse --check "$p" 2>/dev/null; then
		echo "ok        $name"
	elif [ "$1" = "--check" ]; then
		echo "fehlt     $name"
		missing=1
	elif git -C "$ZB" apply --check "$p" 2>/dev/null; then
		git -C "$ZB" apply "$p"
		echo "angewandt $name"
	else
		echo "FEHLER    $name passt nicht auf $ZB" >&2
		exit 1
	fi
done
exit $missing
