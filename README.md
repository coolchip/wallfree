# Wallfree

Freie Ersatzfirmware fuer die Ladesteuerung **EV-CC-AC1-M3-CBC-RCM-ETH** (STM32F207VET6),
wie sie u. a. in der wallbe eco 2.0 verbaut ist. Zielgruppe: versierte Anwender.

**Status:** Firmware vollstaendig nach Spezifikation implementiert und auf dem PC getestet
(Unit-Tests, Ende-zu-Ende-Simulation, Browser, evcc). Auf einer Steuerung (Testgerät) **ohne Last**
geprueft: Ein-/Ausgaenge, Verriegelung, Netz unter Last, MQTT, evcc ohne Fahrzeug, Update ueber
die Weboberflaeche und Rueckweg zur Originalfirmware. **Laden (CP-PWM, Schuetz) ist auf Hardware
noch nicht geprueft.**
Eine Ladesteuerung schaltet Netzspannung und ueberwacht Fehlerstroeme. Fehler in der Firmware
koennen Menschen gefaehrden. Software-Tests belegen keine elektrische Sicherheit.

## Inhalt

| Pfad | Inhalt |
| --- | --- |
| [docs/soll-spezifikation.md](docs/soll-spezifikation.md) | Soll-Verhalten der Firmware (Kennungen `SOLL-*`), Entscheidungen F1..F32 |
| [docs/architektur.md](docs/architektur.md) | Rahmenentscheidungen mit Begruendung: Zephyr, Speicheraufteilung, Bootloader, Parameterspeicher, evcc/WARP, Inbetriebnahmestufen |
| [docs/implementierung.md](docs/implementierung.md) | Implementierungsstand, Zuordnung Modul -> SOLL, Festlegungen, Ressourcen, Fallstricke |
| [docs/test-und-simulation.md](docs/test-und-simulation.md) | PC-Tests, Ende-zu-Ende-Simulation, Browser-Durchlauf, evcc-Test |
| [docs/simulation-bedienung.md](docs/simulation-bedienung.md) | Simulation starten und bedienen (Optionen, Shell-Kommando `sim`, Beispielablaeufe) |
| [docs/pruefstecker.md](docs/pruefstecker.md) | Pruefstecker (Bauanleitung) und Pruefablauf mit Fahrzeugnachbildung |
| [docs/anleitung/weboberflaeche.md](docs/anleitung/weboberflaeche.md) | Bedienungsanleitung der Weboberflaeche (PDF: `scripts/anleitung-pdf.sh`) |
| [boards/phoenix/ev_cc_ac1/](boards/phoenix/ev_cc_ac1/) | Zephyr-Board-Beschreibung (Pins aus der Originalfirmware, am Testgerät bis auf Schuetz und PWM bestaetigt) |
| [app/](app/) | Anwendung fuer die Steuerung: Hardwareschicht, Threads, Shell; zusaetzlich als PC-Simulation |
| [spike-size/](spike-size/) | Groessentest (nur bauen) |
| [include/wallfree/evse/](include/wallfree/evse/), [lib/evse/](lib/evse/) | Ladelogik als Zephyr-Modul, reines C ohne Hardwarebezug |
| [include/wallfree/nvm/](include/wallfree/nvm/), [lib/nvm/](lib/nvm/) | Parameterspeicher (Log-Format in Sektor 7) und Leser fuer den Originalbereich, reines C |
| [include/wallfree/api/](include/wallfree/api/), [lib/api/](lib/api/) | Digest-Anmeldung, JSON, Abbildung der Ladelogik auf die WARP-angelehnte API, reines C |
| [include/wallfree/meter/](include/wallfree/meter/), [lib/meter/](lib/meter/) | Energiezaehler ueber Modbus RTU, nur Anzeige: Modelltabellen, Dekodierung, Plausibilitaet, Fehlerzaehler; reines C, Bus gekapselt |
| [web/](web/) | Weboberflaeche (Preact + htm, gzip im Flash); Fremdcode unter [web/vendor/](web/vendor/) mit eigenen Lizenzen |
| [tests/](tests/) | PC-Tests (ztest auf `native_sim/native/64`), Aufruf `scripts/run-tests.sh`; Ende-zu-Ende-Tests in [tests/e2e/](tests/e2e/), Aufruf `scripts/run-e2e.sh` |

## Bauen

Zephyr v4.4.2, Zephyr-SDK 1.0.1:

```sh
cd ~/zephyrproject && . .venv/bin/activate && cd zephyr
west build -p always -b ev_cc_ac1 /pfad/zu/wallfree/app -d /pfad/zu/wallfree/build/ev_cc_ac1
```

Das Abbild `build/ev_cc_ac1/zephyr/zephyr.bin` traegt am Ende eine Pruefsumme
(`scripts/image-trailer.py`) und wird ueber die Weboberflaeche (System, Firmware-Update) an den
Bootloader der Steuerung uebergeben; Ablauf in der Anleitung.

## Testen (PC)

Ladelogik (`lib/evse`), Parameterspeicher (`lib/nvm`), API-Bausteine (`lib/api`) und Zaehler (`lib/meter`) sind reines C
und werden fuer `native_sim/native/64` als Linux-Programm gebaut und mit ztest geprueft:

```sh
scripts/run-tests.sh              # alle Suiten ueber twister
scripts/run-tests.sh --simple     # ohne twister; Logs unter build/tests/<suite>.log/.run
scripts/run-tests.sh --coverage   # zusaetzlich Abdeckung von lib/ (gcovr)
```

Ende-zu-Ende: die ganze Anwendung als PC-Simulation (`native_sim`, Hardwaremodell statt
TIM1/ADC/GPIO, Host-Sockets, Flash-Datei) gegen HTTP, Digest, WebSocket, Shell, Neustarts und
einen lokalen MQTT-Broker (Mosquitto, sonst werden diese Tests uebersprungen):

```sh
scripts/run-e2e.sh                # bauen und tests/e2e ausfuehren
scripts/sim-run.sh                # zum Ausprobieren: http://127.0.0.1:8081/, Telnet 127.0.0.1:2323
```

Bedienung der Simulation: [docs/simulation-bedienung.md](docs/simulation-bedienung.md).

Details und Ergebnisse: [docs/test-und-simulation.md](docs/test-und-simulation.md). Nicht
abgedeckt: die echte Hardwareschicht (`hw_stm32.c`, `hw_gpio.c`) und das Ethernet; diese sind
nur auf der Steuerung geprueft (Stand in [docs/implementierung.md](docs/implementierung.md#pruefungen-auf-box-t)).
Bestandene Tests belegen keine elektrische Sicherheit.

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
- Telnet-Shell ohne Anmeldung, ab Werk aus (F20); in der Weboberflaeche einschaltbar, nur fuer Diagnose.

## Lizenz

Apache-2.0, siehe [LICENSE](LICENSE). Ausnahme: Fremdcode unter [web/vendor/](web/vendor/) (htm: Apache-2.0, Preact: MIT, siehe dortige README).
