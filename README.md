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
| [app/](app/) | Anwendung fuer die Steuerung: Hardwareschicht, Threads, Shell (nur bauen) |
| [spike-size/](spike-size/) | Groessentest (nur bauen) |
| [include/wallfree/evse/](include/wallfree/evse/), [lib/evse/](lib/evse/) | Ladelogik als Zephyr-Modul, reines C ohne Hardwarebezug |
| [include/wallfree/nvm/](include/wallfree/nvm/), [lib/nvm/](lib/nvm/) | Parameterspeicher (Log-Format in Sektor 7) und Leser fuer den Originalbereich, reines C |
| [include/wallfree/api/](include/wallfree/api/), [lib/api/](lib/api/) | Digest-Anmeldung, JSON, Abbildung der Ladelogik auf die WARP-angelehnte API, reines C |
| [web/](web/) | Weboberflaeche (Preact + htm, gzip im Flash); Fremdcode unter [web/vendor/](web/vendor/) mit eigenen Lizenzen |
| [docs/implementierung.md](docs/implementierung.md) | Implementierungsstand, Zuordnung Modul -> SOLL, offene Punkte |
| [tests/](tests/) | PC-Tests (ztest auf `native_sim/native/64`), Aufruf `scripts/run-tests.sh` |

## Bauen

Zephyr v4.4.2, Zephyr-SDK 1.0.1:

```sh
cd ~/zephyrproject && . .venv/bin/activate && cd zephyr
west build -p always -b ev_cc_ac1 /pfad/zu/wallfree/app -d ~/zephyrproject/build-wallfree
```

## Herkunft der Befunde

Die Spezifikation stuetzt sich auf eine statische Analyse der Originalfirmware. Die
Analysebelege liegen in einem separaten, nicht veroeffentlichten Repository (`wallbe2`), weil
es urheberrechtlich geschuetzte Originaldateien enthaelt. Verweise der Form
`../../wallbe2/...` funktionieren nur, wenn beide Repositories nebeneinander liegen.

## Zugriff ueber das Netz

- HTTP-API auf Port 80 mit HTTP-Digest (MD5). Werkszugang `admin` / `wallfree`; bis zur
  Aenderung ueber `POST /credentials` (`{"user": ..., "password": ...}`) ist nur diese
  Ressource erreichbar.
- Weboberflaeche unter `http://<Adresse>/` mit denselben Zugangsdaten. Vorschau ohne Hardware:
  `scripts/web-preview.py` (Attrappen der API, nur lokal), dann `http://127.0.0.1:8080/`.
- evcc: Vorlage `tinkerforge-warp-ws` mit Adresse, Benutzer und Passwort.
- **Keine Verschluesselung (kein TLS):** Wer im LAN mitliest, sieht alle Daten und kann
  mitgeschnittene Anfragen bis zum naechsten Neustart wiederholen. Das Passwort selbst wird
  nicht uebertragen. Die Wallbox nur in einem vertrauenswuerdigen Netz betreiben.
- Telnet-Shell ohne Anmeldung (vorlaeufig, F20).

## Lizenz

Apache-2.0, siehe [LICENSE](LICENSE). Ausnahme: Fremdcode unter [web/vendor/](web/vendor/) (htm: Apache-2.0, Preact: MIT, siehe dortige README).
