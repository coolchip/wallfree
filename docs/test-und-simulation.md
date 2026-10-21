# Test und Simulation

Stand: 2026-10-21. Alle Tests laufen auf dem PC. Sie belegen nichts ueber die reale Hardware
(Pegel, Zeiten, Interrupts) und **keine elektrische Sicherheit**.

| Ebene | Umfang | Stand |
| --- | --- | --- |
| PC-Tests (ztest) | Ladelogik, Parameterspeicher, API-Bausteine, Energiezaehler | 207 Tests in sechs Testprogrammen, alle bestanden; Abdeckung `lib/` zuletzt (vor dem Zaehler) 96 % Zeilen, 88 % Verzweigungen (nicht abgedeckt sind vor allem Lesefehler des Flashs) |
| Ende-zu-Ende | ganze Anwendung als `native_sim`-Programm gegen HTTP, Digest, WebSocket, Shell, Neustarts, simulierten Zaehler | 32 Tests, alle bestanden, ca. 110 s |
| Browser-Durchlauf | Weboberflaeche in headless Chrome gegen die Simulation | ohne Zaehler 9, mit Zaehler 12 Pruefungen bestanden |
| evcc | evcc 0.317.2 mit Vorlage `tinkerforge-warp-ws` gegen die Simulation | Laden, Aus/Sofort, Abstecken, Neustart; mit Zaehler Leistung, Zaehlerstand, Phasenstroeme und -spannungen |

## PC-Tests

```sh
scripts/run-tests.sh              # alle Suiten ueber twister
scripts/run-tests.sh --simple     # ohne twister; Logs unter build/tests/<suite>.log/.run
scripts/run-tests.sh --coverage   # zusaetzlich Abdeckung von lib/ (gcovr)
```

| Suite | Inhalt |
| --- | --- |
| `tests/evse/meas` | CP, PP, Eingaenge, Stromvorgabe |
| `tests/evse/prot` | Verriegelung, RCM-Selbsttest, Schuetzueberwachung |
| `tests/evse/charger` | Automat mit Simulationsmodell (`sim.c`: Fahrzeug, Kabel, RCM-Sensor, Aktor, Schuetz, 1-ms-Schritte), Ausgaenge, Parameter, Persistenz, API-Abbildung, Ereignisse |
| `tests/nvm` | Speicherformat mit simuliertem Flash: Stromausfall nach jedem Byte beim Anhaengen und Verdichten, Ausfall beim Loeschen, Pruefsummenfehler, voller Bereich; Sektor-3-Leser |
| `tests/api` | MD5 (RFC-1321-Vektoren), Digest (RFC-2617-Beispiel, Rundlauf wie evcc, Fehlformen), JSON, Systemparameter inkl. Zaehlerparameter, Resetgrund, Bremse, Zugriffsprotokoll |
| `tests/meter` | Zaehler mit gescriptetem Bus: Anfragen je Modell, Dekodierung mit Registerwerten von Hand (ABB-Ganzzahlen, Eastron-Float32), Ungueltig-Kennungen, NaN/unendlich, Grenzen, Wiederholung und Zusatzpause nach Timeout, Zaehler je Fehlerart, Teilerfolg im Zyklus, Zustandsfolge, Veralten nach 10 s, gesunkener Zaehlerstand, Rundlauf mit dem simulierten Zaehler, JSON |

Zuordnung Modul -> Anforderung -> Test: [Implementierung](implementierung.md#module-der-ladelogik-und-bausteine).

## Ende-zu-Ende-Simulation

Dieselbe Anwendung (`app/`) baut fuer `native_sim/native/64` als Linux-Programm. Nur die
Hardwareschicht ist ersetzt; Threads, Ladelogik, Parameterspeicher, HTTP/Digest/WebSocket,
Weboberflaeche und Telnet-Shell sind der Code der Steuerung.

```sh
scripts/run-e2e.sh                # bauen (build/sim) und alle Tests (Python, nur Standardbibliothek)
scripts/run-e2e.sh -k Faults      # Auswahl (unittest -k)
scripts/sim-run.sh [--build] [--dip=0x109] [--rotary=5] [--http-port=8081] [--reset-button=6000]
```

`sim-run.sh` startet die Simulation zum Ausprobieren ([Bedienung](simulation-bedienung.md)): Web/API `http://127.0.0.1:8081/`
(Werkszugang `admin`/`wallfree`), Telnet-Shell `127.0.0.1:2323`, Flash in `build/sim-flash.bin`
(loeschen = fabrikneu). Ports unter 1024 braeuchten Root-Rechte, daher 8081 und 2323.
Ohne Simulation zeigt `scripts/web-preview.py` die Oberflaeche mit Attrappen der API
(`http://127.0.0.1:8080/`).

| Teil | Umsetzung |
| --- | --- |
| Messung | [hw_sim.c](../app/src/hw_sim.c): Zeitgeber 1 ms, Messblock alle 8 ms; Pegel wie das Modell der Ladelogik-Tests (Fahrzeug A..E, Diode, PP-Kabel 13/20/32/63 A, Eingaenge, Versorgung) |
| RCM | Bender-Ablauf beim Testimpuls (SWITCH_2 330 ms, SWITCH_1 770 ms, Ende 1,9 s), Fehlerstrom, ERR_OUT, Sensor ohne Reaktion; Signalflanke setzt den Schutz-Latch sofort (wie der Interrupt) |
| Aktor, Schuetz | Fahrt 200 ms, LD gepulst/permanent/keine, Klemmen; Sense 50 Hz bei Schuetz ein oder verschweisst |
| IWDG | Software-Zeitgeber in simulierter Zeit (250 ms, beim Flash-Loeschen 32 s) |
| Reset | Prozess startet sich per `execv` neu ([sim_host.c](../app/src/sim_host.c)); Flags/BKP19 und der physische Modellzustand (Fahrzeug bleibt angesteckt, Aktorstellung) gehen ueber Umgebungsvariablen mit; neuer Prozess = Einschalten |
| Flash | Zephyr-Flash-Simulator mit Datei (`--flash=`), Partitionen an den Stellen von Sektor 3 und 7 ([Overlay](../app/boards/native_sim_native_64.overlay)) |
| Netz | Host-Sockets (`NET_NATIVE_OFFLOADED_SOCKETS`), keine Adress-/DHCP-Konfiguration |
| Energiezaehler | hinter `meter_bus.h` statt Zephyr-Modbus: Antworten des konfigurierten Modells aus `meter_sim_encode`; 230/230,7/231,4 V, Strom = Strom des Tastgrads bei Schuetz ein und Fahrzeug C/D, Energie integriert; Stoerungen `sim meter off` (keine Antwort, 500 ms), `frame`, `flaky` (jede zweite Anfrage ohne Antwort), `bad` (Spannung ausserhalb der Grenze), `energy <Wh>` |
| Bedienung | Shell-Kommando `sim` (`status`, `car`, `diode`, `cable`, `din`, `ld`, `rcm`, `actuator`, `weld`, `link`, `supply`, `hang`, `meter`); DIP/Drehschalter/Reset-Taster per Kommandozeile |
| Logging | verzoegert mit Log-Thread wie auf der Steuerung; `NET_LOG` an; stdout zeilengepuffert |

Tests ([tests/e2e/test_e2e.py](../tests/e2e/test_e2e.py), Hilfen in [wfsim.py](../tests/e2e/wfsim.py)):

| Klasse | Inhalt | Anforderungen |
| --- | --- | --- |
| T1Api | 401 mit Challenge; Werkszugang nur `/credentials` und Oberflaeche (403 sonst); Passwortregeln und Aenderung; `/info/*`, `/system/*`, alle Webdateien gzip; Parameter lesen/Beschreibung/Grenzen/Syntaxfehler/Schreiben; Laden B -> PWM -> C -> Schuetz; Sollstrom 10 A (Tastgrad 166, 9960 mA), Ablehnung 3 A, 0 = aus, wieder ein; WebSocket Erstsynchronisation und Push < 1 s; 3 WebSockets gleichzeitig, Wiederverwendung der Plaetze, Abbruch ohne Close-Rahmen; WebSocket ohne bzw. mit falscher Anmeldung geschlossen; Abstecken; Neustart mit Persistenz (Zugangsdaten, Parameter, Hostname), Resetgrund `user_restart`, Sollstrom wieder Startwert; Neustart waehrend des Ladens geordnet; Bremse 5 x 401, dann 429 mit `Retry-After` | SOLL-API-01..07/30/31, SOLL-CUR-21, SOLL-NVM-03, SOLL-PAR-03, SOLL-RT-14/22 |
| T1Api (Ereignisse, LED, Protokoll) | Ereignisse beim Sollstrom 0 und wieder ein (`seq`/`t_ms` steigend, Logzeile); Netzwerk-LED an, bei `sim link 0` 1 Hz; `/system/log`: ohne Anmeldung 401, Fehlversuch mit Absender, `count` und Zeit, Ereignisse enthalten, Antwort > 1,5 KB in Teilen | SOLL-ERR-10/11, SOLL-LED-06 |
| T2Faults | Fehlerstrom beim Laden: Schuetz aus, `error_state` 3, Ereignisse (RCM, Schuetz aus, S9); Messbloecke stehen -> Reset `supervision_limit`; Reset-Taster 6 s beim Start -> Werkszugang wieder aktiv | SOLL-PROT-01/05, SOLL-RT-10/14, SOLL-BOOT-20, SOLL-ERR-10 |
| T3ZephyrTelnet | harter Telnet-Abbruch: HTTP, WebSocket, neue Telnet-Sitzung, geordneter Neustart und Logausgabe laufen weiter | SOLL-RT-11 |

### Abweichungen der Simulation von der Steuerung

- Messwerte unabhaengig vom Tastgrad; kein ADC-Rauschen, keine Zeitfehler.
- Thread-Prioritaeten wirken, aber `native_sim` laeuft nicht praeemptiv in Echtzeit: Eine
  Endlosschleife ohne Warten haelt die simulierte Zeit an (Schutzthread und IWDG eingeschlossen).
- Netzparameter werden nicht angewandt; `/info/name` liefert `uid` 000000 (keine MAC).
- Sektor 3 ist leer (Import aus dem Original nicht pruefbar ohne Abbild).
- Ruhende HTTP-Verbindungen werden nie getrennt (kein `shutdown` im Socket-Treiber); daher 8
  statt 4 HTTP-Client-Plaetze und 24 statt 16 Deskriptoren
  ([native_sim_native_64.conf](../app/boards/native_sim_native_64.conf)).
- Das Modellfahrzeug bleibt in C, wenn die PWM endet (ein echtes Fahrzeug geht nach B).

## Browser-Durchlauf

`node scripts/web-browser-check.mjs <port> <user> <passwort> <ausgabe> [meter]` gegen eine laufende
Simulation (Zugangsdaten vorher geaendert): headless Chrome ueber das DevTools-Protokoll, Digest
beantwortet Chrome selbst. Ablauf: Seite laden (WebSocket "live"), `sim car C` (Laden), Knopf
"Laden aus" (S6 -> S7 -> S3, Schuetz nach 5 s aus), Ansicht "Experte" (Zugriffsprotokoll mit
Fehlversuch), `sim car A`; Screenshots und Texte im Ausgabeverzeichnis. Mit `meter` (in der
Simulation `meter_type` != 0) zusaetzlich: Zaehlerkarte mit Leistung beim Laden, Istwert in der
Ladestrom-Anzeige, Messwerte und Fehlerzaehler, Auswahlliste des Zaehlertyps. Nicht abgedeckt:
Zugangsdaten-Seite, Parameter schreiben, Netz/System.

## evcc gegen die Simulation

evcc **0.317.2** im Docker-Container (`evcc/evcc`, Netz `host`), Vorlage `tinkerforge-warp-ws`,
Konfiguration [tools/evcc/evcc.yaml](../tools/evcc/evcc.yaml) (Netz-Zaehler konstant 0 W,
Modus "sofort"):

```sh
scripts/sim-run.sh --http-port=8091   # Terminal 1
scripts/evcc-sim.sh                   # Terminal 2, setzt Testzugangsdaten, evcc auf :7070
```

| Ablauf | Ergebnis |
| --- | --- |
| Start, kein Fahrzeug | evcc verbindet (Digest, `/info/*`, WebSocket), "car disconnected" |
| Fahrzeug B, dann C | "car connected", evcc setzt 16 000 mA, Laden (S6, Tastgrad 266 = 15 960 mA) |
| Modus "aus" | `external_current` 0, geordnetes Ende, Schuetz aus nach 5 s |
| Modus "sofort" | 16 000 mA, PWM, nach C wieder Laden |
| Abstecken | "stop charging", "car disconnected" binnen eines evcc-Intervalls |
| mit Zaehler (`meter_type` 2, Simulation) | evcc meldet `charger: power ✓ energy ✓ currents ✓`; beim Laden 11 046 W, 15,96 A je Phase, Spannungen 230/230,7/231,4 V, Zaehlerstand und geladene Energie steigen |
| Zaehler faellt aus (`sim meter off`) | Laden laeuft weiter; evcc zeigt 0 W und 0 A (Werte `null` werden in evcc zu 0), Zaehlerstand bleibt auf dem letzten Wert |
| `POST /system/restart` beim Laden | geordnetes Ende; evcc meldet waehrenddessen `unknown evse status: 4` (S10 -> F -> 4, F23), danach WebSocket neu aufgebaut, keine abgewiesene Anmeldung; evcc kommt mit den neuen Nonces zurecht |

Verhalten von evcc (Grundlage fuer F12 und SOLL-API-32):

- evcc schreibt `external_current` nur, wenn sich sein eigenes Ziel aendert (Anstecken,
  Moduswechsel, PV-Regelung); von unserem Wert prueft es nur `> 0`. Ein Wert aus der
  Oberflaeche (gleicher Slot) bleibt daher bis zur naechsten evcc-Aenderung stehen. So
  belassen (Nutzer): kein eigener Benutzer-Slot wie WARP `user_current`.
- Nach einem Neustart waehrend des Ladens steht `external_current` auf dem Startwert
  (80 000 mA); evcc sendet seinen Wert nicht erneut, das Fahrzeug laedt mit dem Hoechstwert von
  Kabel/Drehschalter, bis evcc den Strom aendert oder neu angesteckt wird.
- Mit `external_current_default` = 0 laedt nach dem Neustart nichts. Wechselt das Fahrzeug nach
  B, meldet evcc `charger out of sync: expected enabled, got disabled`, sendet sofort wieder
  seinen Strom, und nach C wird geladen. WARP loest dasselbe ueber `evse/external_defaults`.
- Ohne Zaehler schaetzt evcc die Ladeleistung aus seinem eigenen Sollwert (16 A x 3 x 230 V =
  11 040 W), auch wenn wirksam weniger gilt. Mit Zaehler nimmt evcc die gemessenen Werte
  (`meters/0/values`); die Faehigkeiten liest es nur beim Verbinden, nach Aenderung von
  `meter_type` evcc neu starten.
- `charger logic error: disabled but charging` erscheint, solange nach "aus" der CP-Zustand C
  bleibt (nur beim Modellfahrzeug dauerhaft; WARP meldet dasselbe).
