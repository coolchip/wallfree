#!/bin/sh
# evcc (Docker, Vorlage tinkerforge-warp-ws) gegen die laufende Simulation.
#   scripts/sim-run.sh --http-port=8091     (in einem zweiten Terminal)
#   scripts/evcc-sim.sh [Image]             evcc-Oberflaeche: http://127.0.0.1:7070/
# Setzt in einer fabrikneuen Simulation die Testzugangsdaten aus tools/evcc/evcc.yaml.
# Netz: --network host, damit evcc die Simulation unter 127.0.0.1 erreicht.
# Die evcc-Datenbank liegt in build/evcc (loeschen = frisches evcc).
set -e
REPO=$(cd "$(dirname "$0")/.." && pwd)
IMAGE=${1:-evcc/evcc:0.317.2}
URL=http://127.0.0.1:8091
curl -sf -o /dev/null --max-time 3 "$URL/info/name" ||
	[ "$(curl -s -o /dev/null -w '%{http_code}' --max-time 3 "$URL/info/name")" = 401 ] ||
	{ echo "Simulation auf $URL nicht erreichbar"; exit 1; }
if curl -s --digest -u admin:wallfree "$URL/credentials" | grep -q '"must_change":true'; then
	curl -s --digest -u admin:wallfree -H 'Content-Type: application/json' \
		-d '{"user":"evcc","password":"evcc-sim-test"}' "$URL/credentials" >/dev/null
	echo "Zugangsdaten der Simulation gesetzt (evcc / evcc-sim-test)"
fi
mkdir -p "$REPO/build/evcc"
exec docker run --rm --name wallfree-evcc --network host \
	-v "$REPO/tools/evcc/evcc.yaml:/etc/evcc.yaml:ro" \
	-v "$REPO/build/evcc:/root/.evcc" \
	"$IMAGE" evcc -c /etc/evcc.yaml
