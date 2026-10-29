# Test und Simulation

Stand: 2026-10-26. Die Tests hier laufen auf dem PC (ausser dem Netz-Lasttest, der auch gegen die
Steuerung laeuft). Sie belegen nichts ueber die reale Hardware (Pegel, Zeiten, Interrupts) und
**keine elektrische Sicherheit**; Pruefungen auf der Steuerung:
[Implementierung](implementierung.md#pruefungen-auf-box-t).

| Ebene | Umfang | Stand |
| --- | --- | --- |
| PC-Tests (ztest) | Ladelogik, Parameterspeicher, API-Bausteine, Energiezaehler, MQTT ohne Netz | 219 Tests in sechs Testprogrammen, alle bestanden; Abdeckung `lib/` zuletzt (vor dem Zaehler) 96 % Zeilen, 88 % Verzweigungen (nicht abgedeckt sind vor allem Lesefehler des Flashs) |
| Ende-zu-Ende | ganze Anwendung als `native_sim`-Programm gegen HTTP, Digest, WebSocket, Shell, Neustarts, simulierten Zaehler, lokalen MQTT-Broker | 69 Tests, alle bestanden, ca. 420 s (T5Mqtt gelegentlich wegen eines Simulationsfehlers, Fallstricke) |
| Browser-Durchlauf | Weboberflaeche in headless Chrome gegen die Simulation | ohne Zaehler 20 Pruefungen bestanden (2026-10-26); mit Zaehler zuletzt 2026-10-21 bestanden |
| Netz-Lasttest | `scripts/net-load.py` gegen Simulation (nur Skriptpruefung) und das Testgerät | auf dem Testgerät bestanden ([Implementierung](implementierung.md#pruefungen-auf-box-t)) |
| MQTT im LAN | Simulation gegen Mosquitto 2.1.2 im Heimnetz (von Hand, 2026-10-21) | Zustaende, Zaehlerwerte (etwa 1/s), Ereignisse, Befehl Ladestrom, `online` true/false |
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
| `tests/api` | MD5 (RFC-1321-Vektoren), Digest (RFC-2617-Beispiel, Rundlauf wie evcc, Fehlformen), JSON, Systemparameter inkl. Zaehler- und MQTT-Parameter (Grenzen, Praefixregeln, Passwort nicht lesbar, sofort wirksame Werte, Korrektur beim Start), MQTT-Versandregel und Befehlstabelle, Resetgrund, Bremse, Ereignisprotokoll (inkl. Loeschen) |
| `tests/meter` | Zaehler mit gescriptetem Bus: Anfragen je Modell, Dekodierung mit Registerwerten von Hand (ABB-Ganzzahlen, Eastron-Float32), Ungueltig-Kennungen, NaN/unendlich, Grenzen, Wiederholung und Zusatzpause nach Timeout, Zaehler je Fehlerart, Teilerfolg im Zyklus, Zustandsfolge, Veralten nach 10 s, gesunkener Zaehlerstand, Rundlauf mit dem simulierten Zaehler, JSON |

Zuordnung Modul -> Anforderung -> Test: [Implementierung](implementierung.md#module-der-ladelogik-und-bausteine).

## Ende-zu-Ende-Simulation

Dieselbe Anwendung (`app/`) baut fuer `native_sim/native/64` als Linux-Programm. Nur die
Hardwareschicht ist ersetzt; Threads, Ladelogik, Parameterspeicher, HTTP/Digest/WebSocket,
Weboberflaeche und Telnet-Shell sind der Code der Steuerung.

```sh
scripts/run-e2e.sh                # bauen (build/sim und build/sim-hw wie die Steuerung) und alle Tests
scripts/run-e2e.sh -k Faults      # Auswahl (unittest -k)
scripts/sim-run.sh [--build] [--dip=0x109] [--rotary=5] [--http-port=8081] [--reset-button=11000]
```

`sim-run.sh` startet die Simulation zum Ausprobieren ([Bedienung](simulation-bedienung.md)): Web/API `http://127.0.0.1:8081/`
(Werkszugang `admin`/`wallfree`), Telnet-Shell `127.0.0.1:2323`, Flash in `build/sim-flash.bin`
(loeschen = fabrikneu). Ports unter 1024 braeuchten Root-Rechte, daher 8081 und 2323.
Ohne Simulation zeigt `scripts/web-preview.py` die Oberflaeche mit Attrappen der API (darunter ein 40-h-Verlauf)
(`http://127.0.0.1:8080/`).

| Teil | Umsetzung |
| --- | --- |
| Messung | [hw_sim.c](../app/src/hw_sim.c): Zeitgeber 1 ms, Messblock alle 8 ms; Pegel wie das Modell der Ladelogik-Tests (Fahrzeug A..E, Diode, PP-Kabel 13/20/32/63 A, Eingaenge, Versorgung) |
| RCM | Bender-Ablauf beim Testimpuls (SWITCH_2 330 ms, SWITCH_1 770 ms, Ende 1,9 s), Fehlerstrom, ERR_OUT, Sensor ohne Reaktion; Signalflanke setzt den Schutz-Latch sofort (wie der Interrupt) |
| Aktor, Schuetz | Fahrt 200 ms, LD gepulst/permanent/keine, Klemmen; Sense 50 Hz bei Schuetz ein oder verschweisst |
| IWDG | Software-Zeitgeber in simulierter Zeit (250 ms, beim Flash-Loeschen 32 s) |
| Reset | Prozess startet sich per `execv` neu ([sim_host.c](../app/src/sim_host.c)); Flags/BKP19 und der physische Modellzustand (Fahrzeug bleibt angesteckt, Aktorstellung) gehen ueber Umgebungsvariablen mit; neuer Prozess = Einschalten |
| Bootloader | bei der Update-Uebergabe startet der Prozess [scripts/bootloader-sim.py](../scripts/bootloader-sim.py) (Pfad in `WALLFREE_SIM_BOOTLOADER`, `sim-run.sh` setzt ihn): gleicher Port, Fristen des Originals, Empfang Paket fuer Paket mit dessen Zaehlweise (einschliesslich der Schwachstelle beim ersten Paket, Aufteilung `WALLFREE_SIM_BOOTLOADER_SEG`), gemeinsamer Upload-Zustand (jedes GET bricht einen Upload ab, danach bleibt er im Update-Betrieb), Loeschpause und Tempo wie am Testgerät gemessen (`_ERASE_S`, `_RATE`); danach Neustart mit dem Ergebnis in BKP12 und dem Zustand des Abbilds (`WALLFREE_SIM_IMAGE`); ohne Variable gilt das Update sofort als fehlgeschlagen. `--image-bad` laesst die Pruefung des eigenen Abbilds scheitern |
| Flash | Zephyr-Flash-Simulator mit Datei (`--flash=`), Partitionen an den Stellen von Sektor 3 und 7 ([Overlay](../app/boards/native_sim_native_64.overlay)) |
| Netz | Host-Sockets (`NET_NATIVE_OFFLOADED_SOCKETS`), keine Adress-/DHCP-Konfiguration; ohne MAC sind `uid` und MQTT-Praefix `000000`, der Hostname `wallfree-000000`; ein MQTT-Broker ist ueber 127.0.0.1 oder das LAN des PCs erreichbar |
| Energiezaehler | hinter `meter_bus.h` statt Zephyr-Modbus: Antworten des konfigurierten Modells aus `meter_sim_encode`; 230/230,7/231,4 V, Strom = Strom des Tastgrads bei Schuetz ein und Fahrzeug C/D, Energie integriert; Stoerungen `sim meter off` (keine Antwort, 500 ms), `frame`, `flaky` (jede zweite Anfrage ohne Antwort), `bad` (Spannung ausserhalb der Grenze), `energy <Wh>` |
| Bedienung | Shell-Kommando `sim` (`status`, `car`, `diode`, `cable`, `din`, `ld`, `rcm`, `actuator`, `weld`, `link`, `supply`, `hang`, `meter`); DIP/Drehschalter/Reset-Taster per Kommandozeile |
| Logging | verzoegert mit Log-Thread wie auf der Steuerung; `NET_LOG` an; stdout zeilengepuffert |

Tests ([tests/e2e/test_e2e.py](../tests/e2e/test_e2e.py), Hilfen in [wfsim.py](../tests/e2e/wfsim.py)):

| Klasse | Inhalt | Anforderungen |
| --- | --- | --- |
| T1Api | 401 mit Challenge; Werkszugang nur `/credentials` und Oberflaeche (403 sonst); Passwortregeln und Aenderung; `/info/*`, `/system/*` (Abgleich `mcu_temp_offset` sofort wirksam), alle Webdateien gzip; Parameter lesen/Beschreibung/Grenzen/Syntaxfehler/Schreiben; Laden B -> PWM -> C -> Schuetz; Sollstrom 10 A (Tastgrad 166, 9960 mA), Ablehnung 3 A, 0 = aus, wieder ein; WebSocket Erstsynchronisation und Push < 1 s; 3 WebSockets gleichzeitig, Wiederverwendung der Plaetze, Abbruch ohne Close-Rahmen; WebSocket ohne bzw. mit falscher Anmeldung geschlossen; Abstecken; Neustart mit Persistenz (Zugangsdaten, Parameter, Hostname), Resetgrund `user_restart`, Sollstrom wieder Startwert; Neustart waehrend des Ladens geordnet; Bremse 5 x 401, dann 429 mit `Retry-After` | SOLL-API-01..07/30/31/35, SOLL-CUR-21, SOLL-NVM-03, SOLL-PAR-03, SOLL-RT-14/22 |
| T1Api (Ereignisse, LED, Protokoll) | Ereignisse beim Sollstrom 0 und wieder ein (`seq`/`t_ms` steigend, Logzeile); Netzwerk-LED an, bei `sim link 0` 1 Hz; `/system/log`: ohne Anmeldung 401, Fehlversuch mit Absender, `count` und Zeit, Ereignisse enthalten, Antwort > 1,5 KB in Teilen; `POST /system/log/clear` ohne Anmeldung 401, danach nur der Loescheintrag mit Absender | SOLL-ERR-10..13, SOLL-LED-06 |
| T2Faults | Fehlerstrom beim Laden: Schuetz aus, `error_state` 3, Ereignisse (RCM, Schuetz aus, S9); Messbloecke stehen -> Reset `supervision_limit`; Reset-Taster beim Start 7 s -> Uebergabe an den Bootloader ohne Adresse, Einstellungen bleiben, 11 s -> Werkszugang wieder aktiv | SOLL-PROT-01/05, SOLL-RT-10/14, SOLL-BOOT-20/22, SOLL-ERR-10 |
| T5Mqtt | gegen einen lokalen Mosquitto (Test wird ohne `mosquitto` uebersprungen): Werkszustand aus, Einschalten nur mit Broker, Verbinden ohne Neustart, Passwort `null`; neuer Abonnent erhaelt `online` und die Zustaende retained; Aenderung und Ereignis (nicht retained); Befehl ohne `mqtt_commands` wirkungslos; mit Befehlen Zahl und JSON angenommen, 3 A abgelehnt mit Text; gespeicherter Befehl nach Neuverbindung ignoriert; Broker-Neustart -> Neuverbindung, Laden unberuehrt; Abschalten sendet `online` = `false`; harter Abbruch -> Last Will; Home-Assistant-Erkennung: Konfigurationen retained unter `homeassistant/`, Ladestrom-Feld nur mit Befehlen, ohne Zaehler keine Leistung, ausgeschaltet entfernt | SOLL-MQTT-01..04/10/11/20..22/30/31/40/41 |
| T6Update | mit Ersatz-Bootloader: `scripts/update.sh` ueber die API -> Bootloader erhaelt genau die Datei, Resetgrund `update`, `update_result` ok, `image` mit CRC der Datei, Ereignis `update`; `bootloader` und Upload von Hand mit ungueltigem Start-SP -> fehlgeschlagen; ohne Upload Zeitablauf -> fehlgeschlagen, zweite Anforderung 409; Update waehrend des Ladens geordnet; Upload wie ein Browser mit zu kurzem letzten Paket -> Bootloader meldet Erfolg, Abbild abgeschnitten, `image.ok` false, ERR-INTERNAL, kein Laden; GET waehrend des Uploads -> Abbruch, Bootloader bleibt, zweiter Upload stellt her; ohne Bootloader -> fehlgeschlagen | SOLL-BOOT-30..35, SOLL-SH-10 |
| T7Zeit | eigener SNTP-Server auf 127.0.0.1: ohne Zeitserver `time` = null; `ntp_server` setzen wirkt sofort, Uhrzeit des Servers in `/system/info`, neue Protokolleintraege mit `wall_ms`, aeltere ohne; `ntp_enable` aus -> `time` null; ungueltiger Server -> 400 | SOLL-TIME-01..03, SOLL-ERR-12 |
| T8StufeC | Probe des Pruefablaufs Stufe C ([Pruefstecker](pruefstecker.md#5-pruefablauf-stufe-c)) mit den Einstellungen des Testgeräts (DIP 111, Drehschalter 4, LD 22, CR 40): C1 PP-Klassen, C2 Verriegeln, Selbsttest, PWM 26,6 %/16,6 %, Schluessel, Weckfolge, C3 Laden, B/C/D/E, Diode, Schluessel aus mit `stop_timeout`, C4 Fehlerstrom in Ruhe (Selbsttest scheitert unter Fehlerstrom, besteht danach) und beim Laden (kein Laden bis Selbsttest in A), C6 WARP-Zustaende und Strom 0 als Stromsperre | SOLL-PP, SOLL-CP, SOLL-SM, SOLL-PROT, SOLL-CUR-11 |
| T9WieSteuerung | zweiter Bau mit der Log- und Shell-Konfiguration der Steuerung (kein Konsolen-Log, kein Telnet): startet ohne Startschleife, Konsole antwortet, `log` enthaelt die Startmeldungen und `clear` leert ihn | SOLL-SH-22 |
| T3Konsole | Konsole `POST /system/shell`: im Werkszugang 403, ohne Anmeldung 401; Text, Filter `param list lock`, Kuerzung bei mehr als 2 KB, unbekanntes Kommando, zu lange Zeile 413, `threads`; Kommandobaum `/system/shell_commands` in mehreren Teilen, ohne `kernel`; keine Zephyr-Zusatzkommandos (`kernel`, `devmem`, `device`, `date`); Kommando wirkt (`current`); `telnet_enable` gibt es nicht mehr (400); `sim sleep 30000` ueber die Konsole haelt den HTTP-Server an -> Neustart `interface_hang`/`http_server` | SOLL-SH-19/21/23, SOLL-RT-11 |

### Abweichungen der Simulation von der Steuerung

- Messwerte unabhaengig vom Tastgrad; kein ADC-Rauschen, keine Zeitfehler.
- Thread-Prioritaeten wirken, aber `native_sim` laeuft nicht praeemptiv in Echtzeit: Eine
  Endlosschleife ohne Warten haelt die simulierte Zeit an (Schutzthread und IWDG eingeschlossen).
- Netzparameter werden nicht angewandt; `/info/name` liefert `uid` 000000 (keine MAC).
- Sektor 3 ist leer (Import aus dem Original nicht pruefbar ohne Abbild).
- Telnet-Shell (Port 2323, ohne Anmeldung) nur in der Simulation, auf der Steuerung gibt es nur die
  Konsole (SOLL-SH-19): Telnet ist hier die Bedienung des Hardwaremodells (`sim ...`) fuer Tests
  und Nutzer, auch im Werkszustand.
- Reset-Taster (`--reset-button`) wirkt nur beim Programmstart, nicht nach einem Neustart der Firmware.
- Ruhende HTTP-Verbindungen werden nie getrennt (kein `shutdown` im Socket-Treiber); daher 8
  statt 4 HTTP-Client-Plaetze und 24 statt 16 Deskriptoren
  ([native_sim_native_64.conf](../app/boards/native_sim_native_64.conf)).
- Das Modellfahrzeug bleibt in C, wenn die PWM endet (ein echtes Fahrzeug geht nach B).
- Ersatz-Bootloader: nur ein Versuch statt BKP11, kein Wechsel auf 192.168.0.8; das hochgeladene
  Abbild wird nicht ausgefuehrt (die Simulation startet wieder mit derselben Firmware, meldet aber
  dessen Pruefsumme); die Uebergabe schreibt keine IP (0); der Empfang nimmt die Anfrage am Stueck
  und teilt sie danach in Pakete, die echte Aufteilung des Senders ist nicht nachgebildet.

## Browser-Durchlauf

`node scripts/web-browser-check.mjs <port> <user> <passwort> <ausgabe> [meter]` gegen eine laufende
Simulation (Zugangsdaten vorher geaendert; laeuft schon eine zweite, eigenen Telnet-Port bauen,
`-DCONFIG_SHELL_TELNET_PORT=2324`, und `WALLFREE_TELNET_PORT=2324` setzen, sonst gehen die
`sim`-Befehle an die andere): headless Chrome ueber das DevTools-Protokoll, Digest
beantwortet Chrome selbst. Ablauf: Seite laden ("verbunden" ueber WebSocket), einfache Ansicht
ab Werk (Status mit Verbindungen, ohne Rohwerte; Einstellungen ohne "Laden" und ohne Zustandszeile), Expertenansicht auf der Seite
System einschalten (im `localStorage` gespeichert), `sim car C` (Laden; Ring
zeigt "begrenzt durch Maximum Drehschalter (32 A)", ohne Zaehler "32 A erlaubt"), Knopf
"Laden aus" (S6 -> S7 -> S3, "Vorgabe: Laden aus"), Status (Ladestrom-Tabelle, Verlauf
mit laufendem Intervall, Ereignisprotokoll mit Fehlversuch und Taste "Protokoll loeschen"), Einstellungen/Netzwerk und /Zaehler,
System mit Konsole (`status` ausfuehren, Tab ergaenzt `sta`, Vorschlaege nach
`param get lock_`), Zurueck-Taste (Adresse im Hash), `sim car A`, Status neu geladen: alle Abfragen ueber eine TCP-Verbindung; Tab verborgen
(`document.hidden`): WebSocket zu, 12 s keine Anfrage, wieder sichtbar sofort live; Netz im Browser 7 s trennen ("getrennt", grau)
und wieder verbinden ("verbunden"). Danach Bedienung wie ein Nutzer (Rueckfragen werden bestaetigt
und mitgeschrieben): Einstellungen/Laden je ein Wert ueber Auswahlliste (`wakeup_mode`) und
Kaestchen (`reject_state_d`) mit "OK", Meldung "gespeichert", Wert per API; "Protokoll loeschen"
(danach nur "Ereignisprotokoll geloescht von 127.0.0.1"); Netzwerk: Hostname speichern, Hinweis
"wirkt nach Neustart", "Jetzt neu starten" -> Resetgrund `user_restart`, Hostname aktiv, neue
Startnummer; Anmeldedaten: Benutzer vorbelegt, Passwort aendern, Neuanmeldung, zurueck. Geaenderte
Werte setzt der Lauf zurueck. Screenshots und Texte im Ausgabeverzeichnis.
Mit `meter` (in der Simulation `meter_type` != 0) zusaetzlich: Zaehlerkarte mit Leistung beim
Laden, Messwert mit Leistung im Ring, Messwerte und Fehlerzaehler, Leistung im Verlauf, Auswahlliste des Zaehlertyps mit Zustand;
ohne Zaehler zeigt der Verlauf nur den Strom. Nicht abgedeckt: erste Anmeldung im Werkszugang
(Seite "Zugangsdaten festlegen"; Ende-zu-Ende-Test prueft die API), Firmware-Update (eigener
Durchlauf, unten). Simulation 2026-10-27: bestanden.

### Firmware-Update im Browser

`node scripts/web-update-check.mjs <port> <user> <passwort> <ausgabe>` gegen eine Simulation mit
Ersatz-Bootloader (`sim-run.sh` setzt `WALLFREE_SIM_BOOTLOADER`; mit
`WALLFREE_SIM_BOOTLOADER_RATE=5000` dauert der Upload etwa 12 s, dann faellt jede Abfrage der
Seite in den Upload und braeche ihn ab): erzeugt Testabbilder mit der Kennung der laufenden
Version und Anhang, prueft die Vorpruefung (ungueltige Datei abgelehnt, Wallfree mit
Version erkannt), die Rueckfrage, waehrend des Ablaufs Kopf "Update laeuft" ohne Ausgrauen, den
Ablauf bis "Update erfolgreich" und nach dem Neuladen
"Letztes Update: erfolgreich" (Simulation 2026-10-27: bestanden).

## Netz-Lasttest

`python3 -I scripts/net-load.py HOST -u USER:PW STUFE ... [-t SEK] [-n ABFRAGER]` gegen die
Steuerung oder die Simulation (`--http`, `--telnet` fuer andere Ports). Liest nur (GET, WebSocket,
Telnet-Diagnose). Stufen: `http` (parallele Abfragen, je eine Verbindung), `keep` (Keep-Alive),
`ws` (4 WebSockets, 3 erlaubt, Takt `evse/state`), `churn` (Aufbau ohne Anfrage, RST, halbe
Anfrage), `idle` (stumme Verbindungen belegen die HTTP-Plaetze), `mixed` (alles zugleich),
`long` (Browser + evcc, Schnappschuss alle 5 min), `tabs` (Hintergrund-Tabs wachen zugleich
auf und halten ihre Verbindung, daneben Erreichbarkeit fuer neue Verbindungen; nur auf der
Steuerung aussagekraeftig, die Simulation trennt ruhende Verbindungen nie). Vor und nach jeder Stufe Laufzeit, Resetgrund,
TCP-Zustaende und Stack-Auslastung; ein Neustart gilt als Fehler. Die Simulation taugt nur zum
Pruefen des Skripts: unter Volllast laeuft ihre Uhr langsamer als die Wanduhr.

`node scripts/web-tabs-load.mjs <host> <user> <passwort> <tabs> <sekunden> <verborgen> <ausgabe>`
oeffnet echte Tabs (je eine headless-Chrome-Instanz) auf Status, die ersten verborgen, und meldet
je Tab Anfragen und benutzte Verbindungen; die Erreichbarkeit misst man daneben. Die Chrome-Prozesse
werden am Ende beendet.

## Bilder und PDF der Anleitung

`scripts/doc-screenshots.sh` erzeugt die Bilder in `docs/anleitung/bilder/` (etwa 8 min): eigener
Build `build/doc-sim` mit Telnet-Port 2324, fabrikneue Simulation auf Port 8097 mit
`--rt-ratio=60`, damit eine Ladegeschichte (Abend 16 A, geregelt 10/7 A, voll, erneut
angesteckt mit 11 A) den Verlauf in wenigen Minuten mit Stunden fuellt; die Oberflaeche
rechnet alle Zeiten relativ zu "jetzt", die Bilder sehen daher wie von einem echten Geraet
aus. Bricht ab, wenn Port 8097 oder 2324 belegt ist. `DOC_QUICK=1` laesst die Ladegeschichte
weg (Selektoren pruefen). `scripts/anleitung-pdf.sh` baut `build/anleitung/weboberflaeche.pdf`:
pandoc (`markdown+gfm_auto_identifiers`, Bilder eingebettet, [druck.css](anleitung/druck.css))
und Druck mit Chrome; kein LaTeX noetig.

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
