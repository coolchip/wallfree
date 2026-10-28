# Wallfree

Freie Ersatzfirmware für die Ladesteuerung **EV-CC-AC1-M3-CBC-RCM-ETH** (STM32F207VET6),
wie sie u. a. in der wallbe Eco 2.0 verbaut ist. Zielgruppe: versierte Anwender.
Vermutlich ebenfalls passend, aber **ungetestet:** wallbe Eco 2.0s, Compleo eco und
E3/DC easy connect (gleiche Ladesteuerung; Bootloader und Bestückung nicht geprüft).

**Status:** Firmware vollständig nach Spezifikation implementiert und auf dem PC getestet
(Unit-Tests, Ende-zu-Ende-Simulation, Browser, evcc). An der Steuerung einer wallbe Eco 2.0 **ohne Last**
geprüft: Ein-/Ausgänge, Verriegelung, Netz unter Last, MQTT, evcc ohne Fahrzeug, Update über
die Weboberfläche und Rückweg zur Originalfirmware. **Laden (CP-PWM, Schütz) ist auf Hardware
noch nicht geprüft.**
Eine Ladesteuerung schaltet Netzspannung und überwacht Fehlerströme. Fehler in der Firmware
können Menschen gefährden. Software-Tests belegen keine elektrische Sicherheit.

## Funktionen

**Laden**

- Ladeablauf nach IEC 61851-1: Control Pilot mit PWM, Kabelerkennung (Proximity), Ladeschütz,
  Steckerverriegelung mit Rückmeldung (dauernd wie bei Phoenix oder gepulst wie bei wallbe,
  einstellbar).
- Alle Betriebsarten, die DIP-Schalter und Drehschalter der Steuerung erlauben, dazu die
  Funktionen der fünf Eingänge und vier Ausgänge wie in der Originalfirmware.
- Fehlerstromüberwachung mit Selbsttest des Sensors, Schützüberwachung (Spannung hinter dem
  Schütz), Prüfung von CP, Kabelkodierung und Versorgung; bei Fehlern sicherer Zustand (Schütz
  aus, Ladesignal weg). Überstromabschaltung ist vorbereitet, aber ohne Messquelle der
  Ladelogik nicht verfügbar (der Energiezähler dient nur der Anzeige).
- Eigener Schutzthread im 1-ms-Takt, Watchdog, geordnetes Ladeende vor jedem Neustart, Prüfsumme
  des eigenen Abbilds (ein beschädigtes Abbild lädt nicht).

**Anbindung**

- [evcc](https://evcc.io) über die Vorlage `tinkerforge-warp-ws`: Wallfree spricht die WARP-API
  (HTTP und WebSocket) mit Benutzer und Passwort (Digest).
- MQTT mit Zuständen, Ereignissen und optionaler Stromvorgabe; Home-Assistant-Erkennung
  (Gerät mit Sensoren und Ladestrom-Feld legt sich selbst an); Broker per Name oder Adresse.
- Energiezähler über RS-485/Modbus RTU zur Anzeige und für evcc: ABB B23, Eastron SDM630,
  Eastron SDM72D-M (beide Versionen). Nicht für Schutzfunktionen.
- Uhrzeit per Zeitserver (SNTP).

**Weboberfläche** (im Flash, ohne Cloud)

- Übersicht mit Zustand, Ladestrom und Vorgabe-Tasten, letzten Ereignissen.
- Status: Verlauf über 48 h, Zustand von Netzwerk, Uhrzeit, MQTT und Zähler, Fehler mit
  Rücksetzbedingung, Ereignisprotokoll (übersteht Neustarts, mit Fehlversuchen bei der Anmeldung).
- Einstellungen für Netzwerk, MQTT, Zähler und Anmeldedaten.
- Expertenansicht (je Browser einschaltbar): Parameter der Ladelogik mit Bereich,
  Werkseinstellung und Erklärung, Ladestromgrenzen, Zählerwerte, innere Zustände der
  Ladelogik, CP-Oszilloskop.
- Firmware-Update über den Bootloader der Steuerung, mit Vorprüfung der Datei und Prüfung
  des Ergebnisses.
- Bedienungsanleitung: [docs/anleitung/weboberflaeche.md](docs/anleitung/weboberflaeche.md);
  Vergleich mit der Originaloberfläche: [docs/vergleich-weboberflaeche.md](docs/vergleich-weboberflaeche.md).

**Wartung**

- Rückweg zur Originalfirmware über denselben Bootloader; die Originaleinstellungen bleiben
  dabei erhalten (auf einer Steuerung geprüft). Ein Import dieser Einstellungen in Wallfree ist
  eingebaut, auf einer Steuerung aber noch nicht ausgeführt.
- Konsole in der Weboberfläche für Diagnose (Expertenansicht, mit Anmeldung).
- Notweg zum Bootloader über den Reset-Taster, falls die Weboberfläche nicht mehr erreichbar ist.
- Kompletter Simulator auf dem PC: die ganze Firmware mit nachgebildeter Hardware, Fahrzeug,
  Zähler und Bootloader, Weboberfläche und evcc wie an der echten Steuerung.

**Nicht enthalten:** OCPP, RFID (als Erweiterung vorgemerkt), Modbus TCP, Mobilfunk, Lastmanagement mehrerer Boxen (kann
evcc), HTTPS.

Was Wallfree anders macht als die Originalfirmware und worauf man beim Umrüsten achten muss:
[Unterschiede zur Originalfirmware](docs/unterschiede-originalfirmware.md). Aufspielen und
Rückweg Schritt für Schritt: [Umrüsten auf Wallfree und zurück](docs/umruesten.md).

## Inhalt

| Pfad | Inhalt |
| --- | --- |
| [docs/umruesten.md](docs/umruesten.md) | Wallfree aufspielen, Rückweg zur Originalfirmware, wenn etwas schiefgeht |
| [docs/unterschiede-originalfirmware.md](docs/unterschiede-originalfirmware.md) | Unterschiede zur Originalfirmware mit Grund, Hinweise zum Umrüsten und zum Rückweg |
| [docs/technik-und-lizenzen.md](docs/technik-und-lizenzen.md) | Verwendete Technik (Zephyr, Preact/htm, Testwerkzeuge) und Lizenzen der eingebundenen Software |
| [docs/soll-spezifikation.md](docs/soll-spezifikation.md) | Soll-Verhalten der Firmware (Kennungen `SOLL-*`), Entscheidungen F1..F32 |
| [docs/architektur.md](docs/architektur.md) | Rahmenentscheidungen mit Begründung: Zephyr, Speicheraufteilung, Bootloader, Parameterspeicher, evcc/WARP, Inbetriebnahmestufen |
| [docs/implementierung.md](docs/implementierung.md) | Implementierungsstand, Zuordnung Modul -> SOLL, Festlegungen, Ressourcen, Fallstricke |
| [docs/test-und-simulation.md](docs/test-und-simulation.md) | PC-Tests, Ende-zu-Ende-Simulation, Browser-Durchlauf, evcc-Test |
| [docs/simulation-bedienung.md](docs/simulation-bedienung.md) | Simulation starten und bedienen (Optionen, Shell-Kommando `sim`, Beispielabläufe) |
| [docs/pruefstecker.md](docs/pruefstecker.md) | Prüfstecker (Bauanleitung) und Prüfablauf mit Fahrzeugnachbildung |
| [docs/vergleich-weboberflaeche.md](docs/vergleich-weboberflaeche.md) | Weboberfläche vorher und nachher: was aus den Seiten der Originaloberfläche wird |
| [docs/anleitung/weboberflaeche.md](docs/anleitung/weboberflaeche.md) | Bedienungsanleitung der Weboberfläche (PDF: `scripts/anleitung-pdf.sh`) |
| [boards/phoenix/ev_cc_ac1/](boards/phoenix/ev_cc_ac1/) | Zephyr-Board-Beschreibung (Pins aus der Originalfirmware, an einer wallbe Eco 2.0 bis auf Schütz und PWM bestätigt) |
| [app/](app/) | Anwendung für die Steuerung: Hardwareschicht, Threads, Shell; zusätzlich als PC-Simulation |
| [spike-size/](spike-size/) | Größentest (nur bauen) |
| [include/wallfree/evse/](include/wallfree/evse/), [lib/evse/](lib/evse/) | Ladelogik als Zephyr-Modul, reines C ohne Hardwarebezug |
| [include/wallfree/nvm/](include/wallfree/nvm/), [lib/nvm/](lib/nvm/) | Parameterspeicher (Log-Format in Sektor 7) und Leser für den Originalbereich, reines C |
| [include/wallfree/api/](include/wallfree/api/), [lib/api/](lib/api/) | Digest-Anmeldung, JSON, Abbildung der Ladelogik auf die WARP-angelehnte API, reines C |
| [include/wallfree/meter/](include/wallfree/meter/), [lib/meter/](lib/meter/) | Energiezähler über Modbus RTU, nur Anzeige: Modelltabellen, Dekodierung, Plausibilität, Fehlerzähler; reines C, Bus gekapselt |
| [web/](web/) | Weboberfläche (Preact + htm, gzip im Flash); Fremdcode unter [web/vendor/](web/vendor/) mit eigenen Lizenzen |
| [tests/](tests/) | PC-Tests (ztest auf `native_sim/native/64`), Aufruf `scripts/run-tests.sh`; Ende-zu-Ende-Tests in [tests/e2e/](tests/e2e/), Aufruf `scripts/run-e2e.sh` |

## Bauen

Zephyr v4.4.2, Zephyr-SDK 1.0.1:

```sh
cd ~/zephyrproject && . .venv/bin/activate && cd zephyr
west build -p always -b ev_cc_ac1 /pfad/zu/wallfree/app -d /pfad/zu/wallfree/build/ev_cc_ac1
```

Einfacher: `scripts/build-image.sh` baut und legt das Abbild als `build/wallfree-<version>.bin`
ab (Version aus der Datei `VERSION`, bei Zwischenständen mit Commit im Namen).

Das Abbild `build/ev_cc_ac1/zephyr/zephyr.bin` trägt am Ende eine Prüfsumme
(`scripts/image-trailer.py`) und wird über die Weboberfläche (System, Firmware-Update) an den
Bootloader der Steuerung übergeben; Ablauf in der Anleitung.

## Testen (PC)

Ladelogik (`lib/evse`), Parameterspeicher (`lib/nvm`), API-Bausteine (`lib/api`) und Zähler
(`lib/meter`) sind reines C und werden für `native_sim/native/64` als Linux-Programm gebaut und
mit ztest geprüft:

```sh
scripts/run-tests.sh              # alle Suiten über twister
scripts/run-tests.sh --simple     # ohne twister; Logs unter build/tests/<suite>.log/.run
scripts/run-tests.sh --coverage   # zusätzlich Abdeckung von lib/ (gcovr)
```

Ende-zu-Ende: die ganze Anwendung als PC-Simulation (`native_sim`, Hardwaremodell statt
TIM1/ADC/GPIO, Host-Sockets, Flash-Datei) gegen HTTP, Digest, WebSocket, Shell, Neustarts und
einen lokalen MQTT-Broker (Mosquitto, sonst werden diese Tests übersprungen):

```sh
scripts/run-e2e.sh                # bauen und tests/e2e ausführen
scripts/sim-run.sh                # zum Ausprobieren: http://127.0.0.1:8081/, Telnet 127.0.0.1:2323
```

Bedienung der Simulation: [docs/simulation-bedienung.md](docs/simulation-bedienung.md).

Details und Ergebnisse: [docs/test-und-simulation.md](docs/test-und-simulation.md). Nicht
abgedeckt: die echte Hardwareschicht (`hw_stm32.c`, `hw_gpio.c`) und das Ethernet; diese sind
nur auf der Steuerung geprüft (Stand in [docs/implementierung.md](docs/implementierung.md#pruefungen-auf-box-t)).
Bestandene Tests belegen keine elektrische Sicherheit.

## Herkunft der Befunde

Die Spezifikation stützt sich auf eine statische Analyse der Originalfirmware. Die
Analysebelege liegen in einem separaten, nicht veröffentlichten Repository (`wallbe2`), weil
es urheberrechtlich geschützte Originaldateien enthält. Verweise der Form
`../../wallbe2/...` funktionieren nur, wenn beide Repositories nebeneinander liegen.

## Zugriff über das Netz

- HTTP-API auf Port 80 mit HTTP-Digest (MD5). Werkszugang `admin` / `wallfree`; bis zur
  Änderung über `POST /credentials` (`{"user": ..., "password": ...}`) ist nur diese
  Ressource erreichbar.
- Weboberfläche unter `http://<Adresse>/` mit denselben Zugangsdaten. Vorschau ohne Hardware:
  `scripts/web-preview.py` (Attrappen der API, nur lokal), dann `http://127.0.0.1:8080/`.
- evcc: Vorlage `tinkerforge-warp-ws` mit Adresse, Benutzer und Passwort.
- **Keine Verschlüsselung (kein TLS):** Wer im LAN mitliest, sieht alle Daten und kann
  mitgeschnittene Anfragen bis zum nächsten Neustart wiederholen. Das Passwort selbst wird
  nicht übertragen. Die Wallbox nur in einem vertrauenswürdigen Netz betreiben.
- Kein Zugang ohne Anmeldung: kein Telnet; die Konsole nutzt dieselbe Anmeldung wie Oberfläche und API.

## Lizenz

Apache-2.0, siehe [LICENSE](LICENSE). Ausnahme: Fremdcode unter [web/vendor/](web/vendor/) (htm: Apache-2.0, Preact: MIT, siehe dortige README). Lizenzen der Software im Firmware-Abbild: [Technik und Lizenzen](docs/technik-und-lizenzen.md); die Steuerung zeigt sie unter System.
