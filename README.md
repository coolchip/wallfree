# Wallfree

Freie Ersatzfirmware fuer die Ladesteuerung **EV-CC-AC1-M3-CBC-RCM-ETH** (STM32F207VET6),
wie sie u. a. in der wallbe eco 2.0 verbaut ist. Zielgruppe: versierte Anwender.

**Status:** Spezifikation, keine lauffaehige Firmware. Nichts davon ist auf Hardware getestet.
Eine Ladesteuerung schaltet Netzspannung und ueberwacht Fehlerstroeme. Fehler in der Firmware
koennen Menschen gefaehrden. Software-Tests belegen keine elektrische Sicherheit.

## Inhalt

| Pfad | Inhalt |
| --- | --- |
| [docs/soll-spezifikation.md](docs/soll-spezifikation.md) | Soll-Verhalten der Firmware (Kennungen `SOLL-*`) |
| [docs/architektur.md](docs/architektur.md) | Rahmen: Zephyr, Speicheraufteilung, Bootloader, Parameterspeicher, WARP-/evcc-Schnittstelle |
| [boards/phoenix/ev_cc_ac1/](boards/phoenix/ev_cc_ac1/) | vorlaeufige Zephyr-Board-Beschreibung |
| [spike-size/](spike-size/) | Groessentest (nur bauen) |
| [include/wallfree/evse/](include/wallfree/evse/), [lib/evse/](lib/evse/) | Ladelogik als Zephyr-Modul, reines C ohne Hardwarebezug |
| [docs/implementierung.md](docs/implementierung.md) | Implementierungsstand, Zuordnung Modul -> SOLL, offene Punkte |
| [tests/](tests/) | PC-Tests (ztest auf `native_sim/native/64`), Aufruf `scripts/run-tests.sh` |

## Bauen

Zephyr v4.4.2, Zephyr-SDK 1.0.1:

```sh
cd ~/zephyrproject && . .venv/bin/activate && cd zephyr
west build -p always -b ev_cc_ac1 /pfad/zu/wallfree/spike-size -d ~/zephyrproject/build-wallfree-spike
```

## Herkunft der Befunde

Die Spezifikation stuetzt sich auf eine statische Analyse der Originalfirmware. Die
Analysebelege liegen in einem separaten, nicht veroeffentlichten Repository (`wallbe2`), weil
es urheberrechtlich geschuetzte Originaldateien enthaelt. Verweise der Form
`../../wallbe2/...` funktionieren nur, wenn beide Repositories nebeneinander liegen.

## Lizenz

Apache-2.0, siehe [LICENSE](LICENSE).
