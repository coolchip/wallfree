# Test und Simulation

Stand: 2026-10-26. Die Tests hier laufen auf dem PC (außer dem Netz-Lasttest, der auch gegen die
Steuerung läuft). Sie belegen nichts über die reale Hardware (Pegel, Zeiten, Interrupts) und
**keine elektrische Sicherheit**; Prüfungen auf der Steuerung:
[Implementierung](implementierung.md#prüfungen-auf-box-t).

| Ebene | Umfang | Stand |
| --- | --- | --- |
| PC-Tests (ztest) | Ladelogik, Parameterspeicher, API-Bausteine, Energiezähler, MQTT ohne Netz | 243 Tests in sechs Testprogrammen, alle bestanden; Abdeckung `lib/` zuletzt (vor dem Zähler) 96 % Zeilen, 88 % Verzweigungen (nicht abgedeckt sind vor allem Lesefehler des Flashs) |
| Ende-zu-Ende | ganze Anwendung als `native_sim`-Programm gegen HTTP, Digest, WebSocket, Shell, Neustarts, simulierten Zähler, lokalen MQTT-Broker | 74 Tests, alle bestanden, ca. 460 s (T5Mqtt gelegentlich wegen eines Simulationsfehlers, Fallstricke) |
| Browser-Durchlauf | Weboberfläche in headless Chrome gegen die Simulation | ohne Zähler 20 Prüfungen bestanden (2026-10-26); mit Zähler zuletzt 2026-10-21 bestanden |
| Netz-Lasttest | `scripts/net-load.py` gegen Simulation (nur Skriptprüfung) und das Testgerät | auf dem Testgerät bestanden ([Implementierung](implementierung.md#prüfungen-auf-box-t)) |
| MQTT im LAN | Simulation gegen Mosquitto 2.1.2 im Heimnetz (von Hand, 2026-10-21) | Zustände, Zählerwerte (etwa 1/s), Ereignisse, Befehl Ladestrom, `online` true/false |
| evcc | evcc 0.317.2 mit Vorlage `tinkerforge-warp-ws` gegen die Simulation | Laden, Aus/Sofort, Abstecken, Neustart; mit Zähler Leistung, Zählerstand, Phasenströme und -spannungen |

## PC-Tests

```sh
scripts/run-tests.sh              # alle Suiten ueber twister
scripts/run-tests.sh --simple     # ohne twister; Logs unter build/tests/<suite>.log/.run
scripts/run-tests.sh --coverage   # zusaetzlich Abdeckung von lib/ (gcovr)
```

| Suite | Inhalt |
| --- | --- |
| `tests/evse/meas` | CP, PP, Eingänge, Stromvorgabe |
| `tests/evse/prot` | Verriegelung, RCM-Selbsttest, Schützüberwachung |
| `tests/evse/charger` | Automat mit Simulationsmodell (`sim.c`: Fahrzeug, Kabel, RCM-Sensor, Aktor, Schütz, 1-ms-Schritte), Ausgänge, Parameter, Persistenz, API-Abbildung, Ereignisse |
| `tests/nvm` | Speicherformat mit simuliertem Flash: Stromausfall nach jedem Byte beim Anhängen und Verdichten, Ausfall beim Löschen, Prüfsummenfehler, voller Bereich; Sektor-3-Leser |
| `tests/api` | MD5 (RFC-1321-Vektoren), Digest (RFC-2617-Beispiel, Rundlauf wie evcc, Fehlformen), JSON, Systemparameter inkl. Zähler- und MQTT-Parameter (Grenzen, Präfixregeln, Passwort nicht lesbar, sofort wirksame Werte, Korrektur beim Start), MQTT-Versandregel und Befehlstabelle, Resetgrund, Bremse, Ereignisprotokoll (inkl. Löschen), `flashread`-Bereiche (Sektor 7 und Grenzen ausgeschlossen, Überlauf) und Base64 (RFC-4648-Vektoren) |
| `tests/meter` | Zähler mit gescriptetem Bus: Anfragen je Modell, Dekodierung mit Registerwerten von Hand (ABB-Ganzzahlen, Eastron-Float32), Ungültig-Kennungen, NaN/unendlich, Grenzen, Wiederholung und Zusatzpause nach Timeout, Zähler je Fehlerart, Teilerfolg im Zyklus, Zustandsfolge, Veralten nach 10 s, gesunkener Zählerstand, Rundlauf mit dem simulierten Zähler, JSON |

Zuordnung Modul -> Anforderung -> Test: [Implementierung](implementierung.md#module-der-ladelogik-und-bausteine).

## Ende-zu-Ende-Simulation

Dieselbe Anwendung (`app/`) baut für `native_sim/native/64` als Linux-Programm. Nur die
Hardwareschicht ist ersetzt; Threads, Ladelogik, Parameterspeicher, HTTP/Digest/WebSocket,
Weboberfläche und Telnet-Shell sind der Code der Steuerung.

```sh
scripts/run-e2e.sh                # bauen (build/sim und build/sim-hw wie die Steuerung) und alle Tests
scripts/run-e2e.sh -k Faults      # Auswahl (unittest -k)
scripts/sim-run.sh [--build] [--dip=0x109] [--rotary=5] [--http-port=8081] [--reset-button=11000]
```

`sim-run.sh` startet die Simulation zum Ausprobieren ([Bedienung](simulation-bedienung.md)): Web/API `http://127.0.0.1:8081/`
(Werkszugang `admin`/`wallfree`), Telnet-Shell `127.0.0.1:2323`, Flash in `build/sim-flash.bin`
(löschen = fabrikneu). Ports unter 1024 bräuchten Root-Rechte, daher 8081 und 2323.
Ohne Simulation zeigt `scripts/web-preview.py` die Oberfläche mit Attrappen der API (darunter ein 40-h-Verlauf)
(`http://127.0.0.1:8080/`).

| Teil | Umsetzung |
| --- | --- |
| Messung | [hw_sim.c](../app/src/hw_sim.c): Zeitgeber 1 ms, Messblock alle 8 ms; Pegel wie das Modell der Ladelogik-Tests (Fahrzeug A..E, Diode, PP-Kabel 13/20/32/63 A, Eingänge, Versorgung) |
| RCM | Bender-Ablauf beim Testimpuls (SWITCH_2 330 ms, SWITCH_1 770 ms, Ende 1,9 s), Fehlerstrom, ERR_OUT, Sensor ohne Reaktion; Signalflanke setzt den Schutz-Latch sofort (wie der Interrupt) |
| Aktor, Schütz | Fahrt 200 ms, LD gepulst/permanent/keine, Klemmen; Sense 50 Hz bei Schütz ein oder verschweißt |
| IWDG | Software-Zeitgeber in simulierter Zeit (250 ms, beim Flash-Löschen 32 s) |
| Reset | Prozess startet sich per `execv` neu ([sim_host.c](../app/src/sim_host.c)); Flags/BKP19 und der physische Modellzustand (Fahrzeug bleibt angesteckt, Aktorstellung) gehen über Umgebungsvariablen mit; neuer Prozess = Einschalten |
| Bootloader | bei der Update-Übergabe startet der Prozess [scripts/bootloader-sim.py](../scripts/bootloader-sim.py) (Pfad in `WALLFREE_SIM_BOOTLOADER`, `sim-run.sh` setzt ihn): gleicher Port, Fristen des Originals, Empfang Paket für Paket mit dessen Zählweise (einschließlich der Schwachstelle beim ersten Paket, Aufteilung `WALLFREE_SIM_BOOTLOADER_SEG`), gemeinsamer Upload-Zustand (jedes GET bricht einen Upload ab, danach bleibt er im Update-Betrieb), Löschpause und Tempo wie am Testgerät gemessen (`_ERASE_S`, `_RATE`); danach Neustart mit dem Ergebnis in BKP12 und dem Zustand des Abbilds (`WALLFREE_SIM_IMAGE`); ohne Variable gilt das Update sofort als fehlgeschlagen. `--image-bad` lässt die Prüfung des eigenen Abbilds scheitern |
| Flash | Zephyr-Flash-Simulator mit Datei (`--flash=`), Partitionen an den Stellen von Sektor 3 und 7 ([Overlay](../app/boards/native_sim_native_64.overlay)) |
| Netz | Host-Sockets (`NET_NATIVE_OFFLOADED_SOCKETS`), keine Adress-/DHCP-Konfiguration; ohne MAC sind `uid` und MQTT-Präfix `000000`, der Hostname `wallfree-000000`; ein MQTT-Broker ist über 127.0.0.1 oder das LAN des PCs erreichbar |
| Energiezähler | hinter `meter_bus.h` statt Zephyr-Modbus: Antworten des konfigurierten Modells aus `meter_sim_encode`; 230/230,7/231,4 V, Strom = Strom des Tastgrads bei Schütz ein und Fahrzeug C/D, Energie integriert; Störungen `sim meter off` (keine Antwort, 500 ms), `frame`, `flaky` (jede zweite Anfrage ohne Antwort), `bad` (Spannung außerhalb der Grenze), `energy <Wh>` |
| Bedienung | Shell-Kommando `sim` (`status`, `car`, `diode`, `cable`, `din`, `ld`, `rcm`, `actuator`, `weld`, `link`, `supply`, `hang`, `meter`); DIP/Drehschalter/Reset-Taster per Kommandozeile |
| Logging | verzögert mit Log-Thread wie auf der Steuerung; `NET_LOG` an; stdout zeilengepuffert |

Tests ([tests/e2e/test_e2e.py](../tests/e2e/test_e2e.py), Hilfen in [wfsim.py](../tests/e2e/wfsim.py)):

| Klasse | Inhalt | Anforderungen |
| --- | --- | --- |
| T1Api | 401 mit Challenge; Werkszugang nur `/credentials` und Oberfläche (403 sonst); Passwortregeln und Änderung; `/info/*`, `/system/*` (Abgleich `mcu_temp_offset` sofort wirksam), alle Webdateien gzip; Parameter lesen/Beschreibung/Grenzen/Syntaxfehler/Schreiben; Laden B -> PWM -> C -> Schütz; Sollstrom 10 A (Tastgrad 166, 9960 mA), Ablehnung 3 A, 0 = aus, wieder ein; WebSocket Erstsynchronisation und Push < 1 s; 3 WebSockets gleichzeitig, Wiederverwendung der Plätze, Abbruch ohne Close-Rahmen; WebSocket ohne bzw. mit falscher Anmeldung geschlossen; Abstecken; Neustart mit Persistenz (Zugangsdaten, Parameter, Hostname), Resetgrund `user_restart`, Sollstrom wieder Startwert; Neustart während des Ladens geordnet; Bremse 5 x 401, dann 429 mit `Retry-After` | SOLL-API-01..07/30/31/35, SOLL-CUR-21, SOLL-NVM-03, SOLL-PAR-03, SOLL-RT-14/22 |
| T1Api (Ereignisse, LED, Protokoll) | Ereignisse beim Sollstrom 0 und wieder ein (`seq`/`t_ms` steigend, Logzeile); Netzwerk-LED an, bei `sim link 0` 1 Hz; `/system/log`: ohne Anmeldung 401, Fehlversuch mit Absender, `count` und Zeit, Ereignisse enthalten, Antwort > 1,5 KB in Teilen; `POST /system/log/clear` ohne Anmeldung 401, danach nur der Löscheintrag mit Absender | SOLL-ERR-10..13, SOLL-LED-06 |
| T2Faults | Fehlerstrom beim Laden: Schütz aus, `error_state` 3, Ereignisse (RCM, Schütz aus, S9); Messblöcke stehen -> Reset `supervision_limit`; Reset-Taster beim Start 7 s -> Übergabe an den Bootloader ohne Adresse, Einstellungen bleiben, 11 s -> Werkszugang wieder aktiv | SOLL-PROT-01/05, SOLL-RT-10/14, SOLL-BOOT-20/22, SOLL-ERR-10 |
| T5Mqtt | gegen einen lokalen Mosquitto (Test wird ohne `mosquitto` übersprungen): Werkszustand aus, Einschalten nur mit Broker, Verbinden ohne Neustart, Passwort `null`; neuer Abonnent erhält `online` und die Zustände retained; Änderung und Ereignis (nicht retained); Befehl ohne `mqtt_commands` wirkungslos; mit Befehlen Zahl und JSON angenommen, 3 A abgelehnt mit Text; gespeicherter Befehl nach Neuverbindung ignoriert; Broker-Neustart -> Neuverbindung, Laden unberührt; Abschalten sendet `online` = `false`; harter Abbruch -> Last Will; Home-Assistant-Erkennung: Konfigurationen retained unter `homeassistant/`, Ladestrom-Feld nur mit Befehlen, ohne Zähler keine Leistung, ausgeschaltet entfernt | SOLL-MQTT-01..04/10/11/20..22/30/31/40/41 |
| T6Update | mit Ersatz-Bootloader: `scripts/update.sh` über die API -> Bootloader erhält genau die Datei, gelungener Upload trotz offen gehaltener Verbindung ohne zweiten Versuch, Resetgrund `update`, `update_result` ok, `image` mit CRC der Datei, Ereignis `update`; `bootloader` und Upload von Hand mit ungültigem Start-SP -> fehlgeschlagen; ohne Upload Zeitablauf -> fehlgeschlagen, zweite Anforderung 409; Update während des Ladens geordnet; Upload wie ein Browser mit zu kurzem letzten Paket -> Bootloader meldet Erfolg, Abbild abgeschnitten, `image.ok` false, ERR-INTERNAL, kein Laden; GET während des Uploads -> Abbruch, Bootloader bleibt, zweiter Upload stellt her; `update.sh` über die API mit einer fremden Abfrage im ersten Upload -> Abbruch erkannt (keine Antwort), "Versuch 2 von 3", Update ok; ohne Bootloader -> fehlgeschlagen | SOLL-BOOT-30..35, SOLL-SH-10 |
| T7Zeit | eigener SNTP-Server auf 127.0.0.1: ohne Zeitserver `time` = null; `ntp_server` setzen wirkt sofort, Uhrzeit des Servers in `/system/info`, neue Protokolleinträge mit `wall_ms`, ältere ohne; `ntp_enable` aus -> `time` null; ungültiger Server -> 400; Neustart: Uhrzeit aus der RTC mit leerem `server`, Startereignis mit `wall_ms`, Abgleich löst sie ab; mit `ntp_enable` aus bleibt `time` null | SOLL-TIME-01..04, SOLL-ERR-12 |
| T8StufeC | Probe des Prüfablaufs Stufe C ([Prüfstecker](pruefstecker.md#5-prüfablauf-stufe-c)) mit den Einstellungen des Testgeräts (DIP 111, Drehschalter 4, LD 22, CR 40): C1 PP-Klassen, C2 Verriegeln, Selbsttest, PWM 26,6 %/16,6 %, Schlüssel, Weckfolge, C3 Laden, B/C/D/E, Diode, Schlüssel aus mit `stop_timeout`, C4 Fehlerstrom in Ruhe (Selbsttest scheitert unter Fehlerstrom, besteht danach) und beim Laden (kein Laden bis Selbsttest in A), C6 WARP-Zustände und Strom 0 als Stromsperre | SOLL-PP, SOLL-CP, SOLL-SM, SOLL-PROT, SOLL-CUR-11 |
| T9WieSteuerung | zweiter Bau mit der Log- und Shell-Konfiguration der Steuerung (kein Konsolen-Log, kein Telnet): startet ohne Startschleife, Konsole antwortet, `log` enthält die Startmeldungen und `clear` leert ihn | SOLL-SH-22 |
| T3Konsole | Konsole `POST /system/shell`: im Werkszugang 403, ohne Anmeldung 401; Text, Filter `param list lock`, Kürzung bei mehr als 2 KB, unbekanntes Kommando, zu lange Zeile 413, `threads`; Kommandobaum `/system/shell_commands` in mehreren Teilen, ohne `kernel`; keine Zephyr-Zusatzkommandos (`kernel`, `devmem`, `device`, `date`); `flashread` (SOLL-SH-24): Simulation "nicht verfügbar", Sektor 7, über das Ende von Sektor 6 hinaus und RAM "nicht erlaubt", Länge über 1024, ungültige Adresse; Kommando wirkt (`current`); `telnet_enable` gibt es nicht mehr (400); `sim sleep 30000` über die Konsole hält den HTTP-Server an -> Neustart `interface_hang`/`http_server` | SOLL-SH-19/21/23/24, SOLL-RT-11 |

### Abweichungen der Simulation von der Steuerung

- Messwerte unabhängig vom Tastgrad; kein ADC-Rauschen, keine Zeitfehler.
- Thread-Prioritäten wirken, aber `native_sim` läuft nicht präemptiv in Echtzeit: Eine
  Endlosschleife ohne Warten hält die simulierte Zeit an (Schutzthread und IWDG eingeschlossen).
- Netzparameter werden nicht angewandt; `/info/name` liefert `uid` 000000 (keine MAC).
- Sektor 3 ist leer (Import aus dem Original nicht prüfbar ohne Abbild).
- Telnet-Shell (Port 2323, ohne Anmeldung) nur in der Simulation, auf der Steuerung gibt es nur die
  Konsole (SOLL-SH-19): Telnet ist hier die Bedienung des Hardwaremodells (`sim ...`) für Tests
  und Nutzer, auch im Werkszustand.
- Reset-Taster (`--reset-button`) wirkt nur beim Programmstart, nicht nach einem Neustart der Firmware.
- Ruhende HTTP-Verbindungen werden nie getrennt (kein `shutdown` im Socket-Treiber); daher 8
  statt 4 HTTP-Client-Plätze und 24 statt 16 Deskriptoren
  ([native_sim_native_64.conf](../app/boards/native_sim_native_64.conf)).
- Das Modellfahrzeug bleibt in C, wenn die PWM endet (ein echtes Fahrzeug geht nach B).
- Ersatz-Bootloader: nur ein Versuch statt BKP11, kein Wechsel auf 192.168.0.8; das hochgeladene
  Abbild wird nicht ausgeführt (die Simulation startet wieder mit derselben Firmware, meldet aber
  dessen Prüfsumme); die Übergabe schreibt keine IP (0); der Empfang nimmt die Anfrage am Stück
  und teilt sie danach in Pakete, die echte Aufteilung des Senders ist nicht nachgebildet.

## Browser-Durchlauf

`node scripts/web-browser-check.mjs <port> <user> <passwort> <ausgabe> [meter]` gegen eine laufende
Simulation (Zugangsdaten vorher geändert; läuft schon eine zweite, eigenen Telnet-Port bauen,
`-DCONFIG_SHELL_TELNET_PORT=2324`, und `WALLFREE_TELNET_PORT=2324` setzen, sonst gehen die
`sim`-Befehle an die andere): headless Chrome über das DevTools-Protokoll, Digest
beantwortet Chrome selbst. Ablauf: Seite laden ("verbunden" über WebSocket), einfache Ansicht
ab Werk (Status mit Verbindungen, ohne Rohwerte; Einstellungen ohne "Laden" und ohne Zustandszeile), Expertenansicht auf der Seite
System einschalten (im `localStorage` gespeichert), `sim car C` (Laden; Ring
zeigt "begrenzt durch Maximum Drehschalter (32 A)", ohne Zähler "32 A erlaubt"), Knopf
"Laden aus" (S6 -> S7 -> S3, "Vorgabe: Laden aus"), Status (Ladestrom-Tabelle, Verlauf
mit laufendem Intervall, Ereignisprotokoll mit Fehlversuch und Taste "Protokoll löschen"), Einstellungen/Netzwerk und /Zaehler,
System mit Konsole (`status` ausführen, Tab ergänzt `sta`, Vorschläge nach
`param get lock_`), Zurück-Taste (Adresse im Hash), `sim car A`, Status neu geladen: alle Abfragen über eine TCP-Verbindung; Tab verborgen
(`document.hidden`): WebSocket zu, 12 s keine Anfrage, wieder sichtbar sofort live; Netz im Browser 7 s trennen ("getrennt", grau)
und wieder verbinden ("verbunden"). Danach Bedienung wie ein Nutzer (Rückfragen werden bestätigt
und mitgeschrieben): Einstellungen/Laden je ein Wert über Auswahlliste (`wakeup_mode`) und
Kästchen (`reject_state_d`) mit "OK", Meldung "gespeichert", Wert per API; "Protokoll löschen"
(danach nur "Ereignisprotokoll gelöscht von 127.0.0.1"); Prozessortemperatur: "-" und "-3," bleiben beim
Tippen stehen, "-3,5" und "-3.5" ergeben -35, "abc" -> "keine Zahl"; Netzwerk: Hostname speichern, Hinweis
"wirkt nach Neustart", "Jetzt neu starten" -> Resetgrund `user_restart`, Hostname aktiv, neue
Startnummer; Anmeldedaten: Benutzer vorbelegt, Passwort ändern, Neuanmeldung, zurück. Geänderte
Werte setzt der Lauf zurück. Screenshots und Texte im Ausgabeverzeichnis.
Mit `meter` (in der Simulation `meter_type` != 0) zusätzlich: Zählerkarte mit Leistung beim
Laden, Messwert mit Leistung im Ring, Messwerte und Fehlerzähler, Leistung im Verlauf, Auswahlliste des Zählertyps mit Zustand;
ohne Zähler zeigt der Verlauf nur den Strom. Nicht abgedeckt: erste Anmeldung im Werkszugang
(Seite "Zugangsdaten festlegen"; Ende-zu-Ende-Test prüft die API), Firmware-Update (eigener
Durchlauf, unten). Simulation 2026-10-27: bestanden.

### Firmware-Update im Browser

`node scripts/web-update-check.mjs <port> <user> <passwort> <ausgabe>` gegen eine Simulation mit
Ersatz-Bootloader (`sim-run.sh` setzt `WALLFREE_SIM_BOOTLOADER`; mit
`WALLFREE_SIM_BOOTLOADER_RATE=5000` dauert der Upload etwa 12 s, dann fällt jede Abfrage der
Seite in den Upload und bräche ihn ab): erzeugt Testabbilder mit der Kennung der laufenden
Version und Anhang, prüft die Vorprüfung (ungültige Datei abgelehnt, Wallfree mit
Version erkannt), die Rückfrage, während des Ablaufs Kopf "Update läuft" ohne Ausgrauen, den
Ablauf bis "Update erfolgreich" und nach dem Neuladen
"Letztes Update: erfolgreich". Mit fünftem Argument `stoerer` fragt der Lauf während des ersten
Uploads selbst ab; die Seite muss den Abbruch erkennen und mit "Versuch 2 von 3" neu hochladen;
ohne Störer darf kein zweiter Versuch vorkommen. Der Ersatz-Bootloader hält die Verbindung nach
einem gelungenen Upload offen wie die Steuerung (Antwort ohne Länge, kein Verbindungsende;
ein Kindprozess hält sie 60 s). Simulation 2026-11-09, `WALLFREE_SIM_BOOTLOADER_RATE=5000`:
beide Läufe bestanden.

## Netz-Lasttest

`python3 -I scripts/net-load.py HOST -u USER:PW STUFE ... [-t SEK] [-n ABFRAGER]` gegen die
Steuerung oder die Simulation (`--http`, `--telnet` für andere Ports). Liest nur (GET, WebSocket,
Telnet-Diagnose). Stufen: `http` (parallele Abfragen, je eine Verbindung), `keep` (Keep-Alive),
`ws` (4 WebSockets, 3 erlaubt, Takt `evse/state`), `churn` (Aufbau ohne Anfrage, RST, halbe
Anfrage), `idle` (stumme Verbindungen belegen die HTTP-Plätze), `mixed` (alles zugleich),
`long` (Browser + evcc, Schnappschuss alle 5 min), `wsreject` (WebSocket-Aufbau ohne Pause mit veralteter Nonce wie evcc nach einem Neustart der Box, sofortige Antwort auf den Close-Rahmen; Abbruch nach 30 s ohne Ping), `tabs` (Hintergrund-Tabs wachen zugleich
auf und halten ihre Verbindung, daneben Erreichbarkeit für neue Verbindungen; nur auf der
Steuerung aussagekräftig, die Simulation trennt ruhende Verbindungen nie). Vor und nach jeder Stufe Laufzeit, Resetgrund,
TCP-Zustände und Stack-Auslastung; ein Neustart gilt als Fehler. Die Simulation taugt nur zum
Prüfen des Skripts: unter Volllast läuft ihre Uhr langsamer als die Wanduhr.

`node scripts/web-tabs-load.mjs <host> <user> <passwort> <tabs> <sekunden> <verborgen> <ausgabe>`
öffnet echte Tabs (je eine headless-Chrome-Instanz) auf Status, die ersten verborgen, und meldet
je Tab Anfragen und benutzte Verbindungen; die Erreichbarkeit misst man daneben. Die Chrome-Prozesse
werden am Ende beendet.

## Zähler-Nachbildung am RS-485-Bus

`scripts/meter-emu.py` spielt am USB-RS-485-Adapter einen Eastron SDM72D-M V2 (`meter_type` = 3)
oder mit `--model abb` einen ABB B23 (`meter_type` = 1) als Modbus-Slave (Adresse 1, 9600 Bd, 8N1,
mit `--parity E` 8E1), um die Zähleranbindung der Steuerung ohne echten
Zähler zu prüfen. Es liefert feste Spannungen, 3450 W und einen mitlaufenden Zählerstand.
`--ctl <datei>` schaltet zur Laufzeit die Betriebsart um (`ok`, `silent`, `badcrc`, `exception`,
`nan`), damit die Fehlerzähler (SOLL-MTR-11/15) jede Fehlerart zeigen. Ohne `--port` greift es
über libftdi auf einen FT232R zu und braucht kein Kernel-Modul; andere Adapter über ihre
serielle Schnittstelle, z. B. CH343 mit `--port /dev/ttyACM1`; `--check` prüft nur den
Protokollteil. Ein echter Zähler ersetzt das nicht: Antwortzeiten stammen vom Skript, und die
Register folgen derselben Tabelle wie die Firmware; ein Fehler darin fiele nicht auf.

## Bilder und PDF der Anleitung

`scripts/doc-screenshots.sh` erzeugt die Bilder in `docs/anleitung/bilder/` (etwa 8 min): eigener
Build `build/doc-sim` mit Telnet-Port 2324, fabrikneue Simulation auf Port 8097 mit
`--rt-ratio=60`, damit eine Ladegeschichte (Abend 16 A, geregelt 10/7 A, voll, erneut
angesteckt mit 11 A) den Verlauf in wenigen Minuten mit Stunden füllt; die Oberfläche
rechnet alle Zeiten relativ zu "jetzt", die Bilder sehen daher wie von einem echten Gerät
aus. Bricht ab, wenn Port 8097 oder 2324 belegt ist. `DOC_QUICK=1` lässt die Ladegeschichte
weg (Selektoren prüfen). `scripts/anleitung-pdf.sh` baut `build/anleitung/weboberflaeche.pdf`:
pandoc (`markdown+gfm_auto_identifiers`, Bilder eingebettet, [druck.css](anleitung/druck.css))
und Druck mit Chrome; kein LaTeX nötig.

## evcc gegen die Simulation

evcc **0.317.2** im Docker-Container (`evcc/evcc`, Netz `host`), Vorlage `tinkerforge-warp-ws`,
Konfiguration [tools/evcc/evcc.yaml](../tools/evcc/evcc.yaml) (Netz-Zähler konstant 0 W,
Modus "sofort"):

```sh
scripts/sim-run.sh --http-port=8091   # Terminal 1
scripts/evcc-sim.sh                   # Terminal 2, setzt Testzugangsdaten, evcc auf :7070
```

Gegen das zweite Gerät statt der Simulation: `scripts/evcc-boxg.sh` (eigene Konfiguration und Datenbank in
`build/evcc-boxg/`, siehe [Prüfstecker Abschnitt 8](pruefstecker.md#8-stufe-d-laden-mit-fahrzeug-box-g)).

| Ablauf | Ergebnis |
| --- | --- |
| Start, kein Fahrzeug | evcc verbindet (Digest, `/info/*`, WebSocket), "car disconnected" |
| Fahrzeug B, dann C | "car connected", evcc setzt 16 000 mA, Laden (S6, Tastgrad 266 = 15 960 mA) |
| Modus "aus" | `external_current` 0, geordnetes Ende, Schütz aus nach 5 s |
| Modus "sofort" | 16 000 mA, PWM, nach C wieder Laden |
| Abstecken | "stop charging", "car disconnected" binnen eines evcc-Intervalls |
| mit Zähler (`meter_type` 2, Simulation) | evcc meldet `charger: power ✓ energy ✓ currents ✓`; beim Laden 11 046 W, 15,96 A je Phase, Spannungen 230/230,7/231,4 V, Zählerstand und geladene Energie steigen |
| Zähler fällt aus (`sim meter off`) | Laden läuft weiter; evcc zeigt 0 W und 0 A (Werte `null` werden in evcc zu 0), Zählerstand bleibt auf dem letzten Wert |
| `POST /system/restart` beim Laden | geordnetes Ende; evcc meldet währenddessen `unknown evse status: 4` (S10 -> F -> 4, F23), danach WebSocket neu aufgebaut |
| D1, D4, D7, D10 wie das zweite Gerät (`--dip=0x249 --rotary=4`, Kabel 32 A), F34 | nach dem Start `external` 0, ohne Schlüssel kein Laden; Fahrzeug B -> evcc setzt 16 000 mA und startet allein ("start charging"), C lädt. Modus "aus" -> Ende. Schlüssel an (`sim din en 1`) bei evcc "aus": lädt mit 16 A, `external_ignored` true, `external` bleibt 0; evcc meldet alle 5 s `charger logic error: disabled but charging`, schreibt aber nichts und lädt nicht neu. Schlüssel aus -> sofort Ende. `POST /system/restart` beim Laden mit "sofort": nach dem Start `external` 0, evcc meldet etwa 30 s "disabled but charging" (Fahrzeug noch C), sendet dann wieder 16 000 mA, Schütz 36 s nach dem Start ein |
| Neustart mit neuen Nonces (`--seed-random`, WebSocket-Vorprüfung) | genau eine Abweisung (401 `stale=true`), evcc meldet sich ohne Neustart neu an und sieht danach "car connected" über den WebSocket |

Verhalten von evcc (Grundlage für F12 und SOLL-API-32):

- evcc schreibt `external_current` nur, wenn sich sein eigenes Ziel ändert (Anstecken,
  Moduswechsel, PV-Regelung); von unserem Wert prüft es nur `> 0`. Ein Wert aus der
  Oberfläche (gleicher Slot) bleibt daher bis zur nächsten evcc-Änderung stehen. So
  belassen (Nutzer): kein eigener Benutzer-Slot wie WARP `user_current`.
- Nach einem Neustart während des Ladens steht `external_current` auf dem Startwert
  (80 000 mA); evcc sendet seinen Wert nicht erneut, das Fahrzeug lädt mit dem Höchstwert von
  Kabel/Drehschalter, bis evcc den Strom ändert oder neu angesteckt wird.
- Mit `external_current_default` = 0 lädt nach dem Neustart nichts. Wechselt das Fahrzeug nach
  B, meldet evcc `charger out of sync: expected enabled, got disabled`, sendet sofort wieder
  seinen Strom, und nach C wird geladen. WARP löst dasselbe über `evse/external_defaults`.
- Ohne Zähler schätzt evcc die Ladeleistung aus seinem eigenen Sollwert (16 A x 3 x 230 V =
  11 040 W), auch wenn wirksam weniger gilt. Mit Zähler nimmt evcc die gemessenen Werte
  (`meters/0/values`); die Fähigkeiten liest es nur beim Verbinden, nach Änderung von
  `meter_type` evcc neu starten.
- `charger logic error: disabled but charging` erscheint, solange nach "aus" der CP-Zustand C
  bleibt (nur beim Modellfahrzeug dauerhaft; WARP meldet dasselbe).
