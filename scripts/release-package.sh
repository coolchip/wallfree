#!/bin/sh
# Veroeffentlichungspaket zusammenstellen:
#   scripts/release-package.sh [abbild.bin]
# Ohne Angabe build/wallfree-<VERSION>.bin (veroeffentlichter Stand aus build-image.sh, Tag
# v<VERSION>). Legt build/wallfree-<name>.zip an mit Abbild, README.txt (Version, Build,
# Pruefsummen, Hinweise), LICENSE (Apache-2.0), lizenzen.txt (wie /lizenzen.txt der Steuerung),
# COPYING.picolibc aus dem Zephyr SDK und SHA256SUMS. Das Abbild wird vorher geprueft (Anhang
# mit CRC32, Kennung WALLFREE-IMAGE).
set -e
REPO=$(cd "$(dirname "$0")/.." && pwd)
VERSION=$(head -n 1 "$REPO/VERSION")
IMG=${1:-$REPO/build/wallfree-$VERSION.bin}
if [ ! -f "$IMG" ]; then
	echo "Abbild fehlt: $IMG (erst scripts/build-image.sh auf dem Tag v$VERSION)"
	exit 1
fi
NAME=$(basename "$IMG" .bin)
CHECK=$(python3 -I "$REPO/scripts/image-trailer.py" --check "$IMG")
TAG=$(grep -a -o 'WALLFREE-IMAGE:[0-9A-Za-z.+-]*' "$IMG" | head -1 | cut -d: -f2)
[ -n "$TAG" ] || { echo "keine Kennung WALLFREE-IMAGE im Abbild"; exit 1; }
case $TAG in
*-dirty) echo "Abbild aus nicht committetem Stand ($TAG), wird nicht verpackt"; exit 1 ;;
esac
SDK=${ZEPHYR_SDK_INSTALL_DIR:-$(ls -d "$HOME"/zephyr-sdk-* 2>/dev/null | sort -V | tail -1)}
PICOLIBC=$SDK/gnu/arm-zephyr-eabi/share/licenses/picolibc/COPYING.picolibc
[ -f "$PICOLIBC" ] || { echo "COPYING.picolibc fehlt ($PICOLIBC)"; exit 1; }

STAGE=$REPO/build/release/$NAME
rm -rf "$STAGE"
mkdir -p "$STAGE"
cp "$IMG" "$STAGE/$NAME.bin"
cp "$REPO/LICENSE" "$STAGE/LICENSE"
cp "$REPO/web/lizenzen.txt" "$STAGE/lizenzen.txt"
cp "$PICOLIBC" "$STAGE/COPYING.picolibc"
SIZE=$(wc -c <"$IMG" | tr -d ' ')
CRC=$(python3 -I -c 'import sys,zlib; d=open(sys.argv[1],"rb").read(); print("%08x" % zlib.crc32(d[:-16]))' "$IMG")
cat >"$STAGE/README.txt" <<EOF
Wallfree $TAG

Freie Ersatzfirmware für die Ladesteuerung Phoenix Contact EV-CC-AC1-M3-CBC-RCM-ETH
(u. a. in der wallbe Eco 2.0). Lizenz: Apache-2.0 (LICENSE).

Abbild:    $NAME.bin
Größe:     $SIZE Byte
CRC32:     $CRC (zeigt die Weboberfläche bei der Vorprüfung des Updates und danach unter
           System)
SHA-256:   siehe SHA256SUMS

Aufspielen und Rückweg zur Originalfirmware: docs/umruesten.md im Projekt.

Eine Ladesteuerung schaltet Netzspannung und überwacht Fehlerströme. Fehler in der Firmware
können Menschen gefährden. Software-Tests belegen keine elektrische Sicherheit. Einbau und
Prüfung nur durch eine Elektrofachkraft.

Enthaltene Fremdsoftware und ihre Lizenzen: lizenzen.txt (auch unter /lizenzen.txt auf der
Steuerung), Lizenz von picolibc: COPYING.picolibc.
EOF
(cd "$STAGE" && sha256sum -- * >SHA256SUMS.tmp && grep -v ' SHA256SUMS' SHA256SUMS.tmp >SHA256SUMS && rm SHA256SUMS.tmp)
ZIP=$REPO/build/$NAME.zip
rm -f "$ZIP"
(cd "$REPO/build/release" && zip -q -r -X "$ZIP" "$NAME")
echo "$CHECK"
echo "Paket: $ZIP"
unzip -l "$ZIP" | tail -n +4 | head -n -2
echo "Vor dem Veroeffentlichen: Commit-Zeiten im Verlauf pruefen (git log --format='%ad %cd')"
