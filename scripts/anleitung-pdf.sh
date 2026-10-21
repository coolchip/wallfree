#!/bin/sh
# Anleitung der Weboberflaeche als PDF: pandoc (Markdown -> HTML mit eingebetteten Bildern),
# Chrome druckt A4. Ergebnis: build/anleitung/weboberflaeche.pdf
#   scripts/anleitung-pdf.sh
set -e
REPO=$(cd "$(dirname "$0")/.." && pwd)
SRC=$REPO/docs/anleitung
OUT=$REPO/build/anleitung
mkdir -p "$OUT"
(cd "$SRC" && pandoc -f markdown+gfm_auto_identifiers -t html5 --standalone --embed-resources --css druck.css \
	--metadata pagetitle="Wallfree – Weboberfläche" --metadata lang=de \
	-o "$OUT/weboberflaeche.html" weboberflaeche.md)
google-chrome-stable --headless=new --no-pdf-header-footer --disable-gpu \
	--print-to-pdf="$OUT/weboberflaeche.pdf" "file://$OUT/weboberflaeche.html" 2>/dev/null
echo "$OUT/weboberflaeche.pdf"
