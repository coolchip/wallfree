# Architektur und Rahmenentscheidungen

Stand: 2026-10-21. Rahmen der Ersatzfirmware mit Begruendungen. Das Soll-Verhalten steht in
der [Soll-Spezifikation](soll-spezifikation.md), der Umsetzungsstand in
[Implementierung](implementierung.md), Tests und Simulation in
[Test und Simulation](test-und-simulation.md). Belege zur Originalfirmware liegen in
[wallbe2](../../wallbe2/docs/analysestand.md).

## 1. Vorgaben des Nutzers

- **Schmal bleiben**, damit Platz fuer Erweiterungen bleibt.
- **Rueckkehr zur Originalfirmware ist sehr wichtig:** Bootloader und Originalparameter
  bleiben unangetastet; die Ersatzfirmware hat einen eigenen Parameterbereich.
- **evcc-Anbindung ist sehr wichtig**, ueber die vorhandene evcc-Vorlage
  `tinkerforge-warp-ws`, ohne neuen Wallbox-Typ in evcc. Die WARP-Schnittstelle wird
  schrittweise nach Bedarf nachgebaut, nicht vollstaendig uebernommen.
- **Alles ueber HTTP:** alle Messwerte, Statuswerte und Parameter lesbar, schreibbare Werte
  schreibbar. Zielgruppe sind versierte Anwender; die Weboberflaeche hat zusaetzlich eine
  einfache Ansicht.
- Shell auch ueber das Netz (Telnet, ohne Anmeldung; spaeter entfernen oder Default aus).
- **OCPP perspektivisch** als allgemeine Implementierung, nicht aus der wallbe-Firmware
  abgeleitet. **TLS vorerst nicht. Modbus TCP entfaellt**; die evcc-Vorlage fuer die
  Phoenix-Steuerung (Modbus TCP) wird wegen frueherer Probleme bewusst nicht genutzt.
- **Energiezaehler ueber Modbus RTU nur zur Anzeige** (Nutzer 2026-10-21, F28): Messwerte fuer
  Weboberflaeche und evcc; die Steuerung arbeitet ohne Zaehler vollstaendig.
- **MQTT-Client** (Nutzer 2026-10-21, F30): Zustaende als WARP-JSON an einen Broker, Ladestrom
  als erster Befehl, weitere Befehle spaeter ergaenzbar; ohne TLS, ohne Home-Assistant-Erkennung.
- Hardwaretests erst nach ausdruecklicher Freigabe, in Stufen (Abschnitt 7).

## 2. Entscheidungen

| Thema | Entscheidung | Begruendung |
| --- | --- | --- |
| Rahmenwerk | **Zephyr v4.4.2** | Vorerfahrung des Nutzers; Treiber fuer STM32F2, STM32-Ethernet und KSZ8081 vorhanden; IP-Stack, HTTP-Server mit WebSocket, Shell mit Telnet, Watchdog/ADC/Flash-APIs; per Kconfig schmal konfigurierbar; `native_sim` erlaubt PC-Tests der ganzen Anwendung. Der Groessentest (Abschnitt 5) zeigte, dass es passt. Verworfen: FreeRTOS + STM32Cube-HAL + lwIP (wie Original, aber mehr Eigenbau bei HTTP/Settings/Treiberintegration) und Bare-Metal (am meisten Eigenarbeit im Netzwerkteil) |
| IP-Stack | Zephyr-eigener Stack | lwIP waere nur ohne Zephyr sinnvoll |
| Systemtakt | **100 MHz** wie Original (HSE 25 MHz, PLL M 25/N 200/P 2) | 120 MHz waeren ohne zusaetzliche Wartezyklen moeglich (RM0033 Tab. 3: 3 WS fuer 90..120 MHz bei 2,7..3,6 V), bringen der Ladelogik aber nichts und weichen von der vom Hersteller erprobten Konfiguration (EMV) ab. Spaetere Option, falls TLS/OCPP Rechenzeit brauchen |
| Ethernet | RMII, Autonegotiation (10/100 Mbit/s) | wie Original; der Zephyr-Treiber verhandelt ebenfalls, an einem 100-Mbit-Switch also 100 Mbit/s Vollduplex |
| Ladeadresse | Rohabbild ab `0x08010000` (Sektoren 4..6, max. 320 KB) | Platz und Startvertrag der Originalanwendung; Bootloader bleibt nutzbar (Abschnitt 4) |
| Parameterspeicher | **P1: Sektor 7 mit eigenem Log-Format**, Sektor 3 nur lesen | siehe Abschnitt 3 |
| HTTP-Schnittstelle | an WARP angelehnt, Stroeme in **mA**, Typ `warp2`, erster Umfang = evcc-Mindestumfang, danach alle Parameter ueber eigene Ressourcen | evcc-Treiber `warp-ws.go` bestimmt Mindestumfang und Verhalten; mA wie WARP (16000 = 16 A) |
| Zugriffsschutz | HTTP-Digest (MD5, `qop=auth`) fuer Web und API, Werkszugang mit Aenderungszwang | evcc spricht WARP mit Digest (`charger/warp/connection.go`); Passwort wird nie uebertragen. Ohne TLS bleibt Mitlesen im LAN moeglich |
| Weboberflaeche | **Preact + htm** mit eigenem kleinem CSS, gzip im Flash | Komponenten wie React ohne Build-Schritt, ~5 KB; WARP nutzt ebenfalls Preact. Verglichen mit Vanilla JS (kleiner, mehr Handarbeit), petite-vue (~7 KB), Alpine.js (~15 KB). Gemessen: 11,7 KB gzip fuer die ganze Oberflaeche |
| Projekt | Name **Wallfree**, eigenes veroeffentlichbares Repository, **Apache-2.0** | wallbe2 enthaelt Originalfirmware und Handbuecher und bleibt privat; Befunde werden hier nur in eigenen Worten mit Verweis genutzt |

## 3. Speicheraufteilung und Parameterspeicher

| Bereich | Sektoren | Inhalt | Beleg |
| --- | --- | --- | --- |
| `0x08000000..0x0800BFFF` | 0..2 (je 16 KB) | Bootloader (nicht im Abbild, Hypothese) | [Bootloader](../../wallbe2/docs/bootloader.md) |
| `0x0800C000..0x0800FFFF` | 3 (16 KB) | Originalparameter, **nur lesen** | [Parameterspeicher](../../wallbe2/docs/parameterspeicher.md) |
| `0x08010000..0x0805FFFF` | 4 (64 KB), 5..6 (je 128 KB) | Anwendung, max. 320 KB (Original belegt bis `0x0805F677`) | [Ladeadresse](../../wallbe2/docs/firmware-ladeadresse.md) |
| `0x08060000..0x0807FFFF` | 7 (128 KB) | **Wallfree-Parameter** (vom Original nicht belegt) | |

MCU STM32F207VET6: 512 KB Flash, 128 KB SRAM (RM0033 Tab. 2). Der Flash hat nur eine Bank:
Waehrend eines Loeschvorgangs steht der Bus (Sektor 7 laut Zephyr-Devicetree bis 4 s, nicht am
Datenblatt geprueft). Deshalb wird nur im sicheren Zustand geloescht (SOLL-NVM-04), mit vorher
verlaengertem IWDG (SOLL-RT-13). Das Original hat dieselbe Eigenschaft bei Sektor 3.

**Warum P1:** Betrachtet wurden vier Varianten.

| Variante | Ergebnis |
| --- | --- |
| **P1** Sektor 7, eigenes Format; Sektor 3 nur lesen | **gewaehlt:** Original voellig unangetastet, Sektor 7 vom Original ungenutzt; Anwendung behaelt 320 KB wie das Original. Nachteil: nur eine Loescheinheit, beim Verdichten bleibt ein kurzes Fenster ohne Zweitkopie (wie im Original) |
| P2 Sektoren 6+7 als Zwei-Seiten-Speicher | verworfen: Anwendung nur 192 KB |
| P3 Sektor 3 mitbenutzen | Reserve, falls der Flash fuer TLS/OCPP knapp wird; Nachteil: Originalwerte waeren bei Stromausfall waehrend des Verdichtens gefaehrdet |
| P4 Backup-SRAM | verworfen: keine Batterie in der Steuerung vorgesehen |

Zephyr-Bausteine passen nicht: NVS adressiert innerhalb eines Sektors nur 64 KB und verlangt
Sektoren als Vielfaches der Loescheinheit; ZMS verlangt mindestens zwei Loescheinheiten und
loescht selbststaendig beim Start und bei der Speicherbereinigung. Daher ein eigenes Log-Format
([nvm.h](../include/wallfree/nvm/nvm.h)): Eintraege mit CRC-8 im Kopf und CRC-32, Bereichskopf aus
Kennung (direkt nach dem Loeschen) und Abschluss (zuletzt), sodass ein unterbrochenes Verdichten
beim Start erkannt wird. Format und Ausfallverhalten: [Implementierung](implementierung.md#parameterspeicher).

**Noch zu pruefen (H0):** ob der Bootloader beim Update Sektor 7 loescht. Falls ja, ist der
Speicherort neu zu bewerten (z. B. P3).

## 4. Bootloader und Updatepfad

- Die Ersatzfirmware ist ein Rohabbild ab `0x08010000` mit eigener Vektortabelle. Original und
  Phoenix 1.27 haben denselben Startvertrag; 1.27 liess sich laut Community-Berichten ueber den
  Bootloader einspielen.
- Update-Anforderung wie Original (RTC-Backup-Register, Systemreset; SOLL-BOOT-30). Was der
  Bootloader danach erwartet und prueft, ist unbekannt; deshalb ist die Update-Uebergabe bis
  zum Auslesen des Bootloaders (H0) zurueckgestellt.
- Rueckweg zur Originalfirmware ueber denselben Bootloaderpfad; Sektor 3 bleibt erhalten.
- Start-SP: Zephyr setzt ihn auf das Ende seines Interrupt-Stacks, das Original auf das
  RAM-Ende `0x20020000`. Beides ist gueltig; nur falls der Bootloader den Wert genau prueft,
  waere das relevant (H0).

## 5. Speicherbudget

Groessentest vor der Entscheidung (`spike-size/`, nur gebaut): Zephyr mit IPv4, DHCP, TCP,
HTTP-Server, Telnet-Shell mit Netz-Kommandos, Settings, ADC, PWM, Watchdog: **207 KB Flash,
85 KB RAM**. Zum Vergleich: das Original belegt mit Weboberflaeche, OCPP, Modbus und lwIP
~318 KB.

Aktueller Stand der Anwendung und Anteile: [Implementierung](implementierung.md#ressourcen).
Gemessene Einsparmoeglichkeiten (jeweils einzeln gebaut, Basis 295 KB):

| Massnahme | Ersparnis | Bemerkung |
| --- | --- | --- |
| `CONFIG_NET_SHELL=n` | 43 KB | Netz-Kommandos der Shell |
| Assert-Texte weg (`ASSERT_VERBOSE=n`, `ASSERT_NO_*_INFO=y`) | 32 KB | Pruefungen bleiben; bei Ausloesung erscheint **gar nichts** im Log (ohne `ASSERT_VERBOSE` druckt Zephyr auch Datei und Zeile nicht) |
| **umgesetzt:** Ausdruck und Meldung weg, Datei und Zeile bleiben (`ASSERT_NO_COND_INFO=y`, `ASSERT_NO_MSG_INFO=y`) | 18,2 KB (gemessen 2026-10-21 auf 325,7 KB) | Pruefungen bleiben; bei Ausloesung `ASSERTION FAIL @ Datei:Zeile` |
| `CONFIG_LTO=y` | 24 KB | geprueft: Interrupttabelle, HTTP-Ressourcen und Fault-Handler korrekt; Fehlersuche schwerer |
| `CONFIG_SHELL_HELP=n` | 8 KB | Hilfetexte der Shell |
| LTO + Assert-Texte | 48 KB | |
| `CONFIG_ASSERT=n` | 40 KB | nicht empfohlen |
| `-Oz` statt `-Os` | 0 | GCC 14 erzeugt fuer Cortex-M3 denselben Code |

**Entscheidung (Nutzer 2026-10-21):** Assert-Ausdruck und -Meldung sind entfernt, Datei und
Zeile bleiben (siehe Tabelle). Die Netz-Shell (`net iface`, `ping`, `arp`, `conn`, `mem` ...)
bleibt bis nach H1: Damit wird beim ersten Probelauf das Netz angesehen (Link, PHY-Takt,
Adressen, Sockets, Netzpuffer); danach kann sie weg. Die Telnet-Shell selbst mit den
`evse`-Kommandos bleibt in jedem Fall. Weitere Reihenfolge, wenn es wieder knapp wird:
Netz-Shell (nach H1), LTO, Shell-Hilfe. TLS (mbedTLS) passt neben P1
voraussichtlich nicht (nicht gemessen); dann Speicherort und Budget neu bewerten.

**Zaehleranbindung Modbus RTU (F28), umgesetzt 2026-10-21:** UART4 mit RS-485-Freigabe PC12,
Zephyr-Modbus-Client, Zaehlermodul, API, Shell und Weboberflaeche zusammen **+15,2 KB Flash,
+3,9 KB RAM** (Aufteilung: [Implementierung](implementierung.md#ressourcen)). Der Groessentest
vorab (nur Abfrage, ohne Parameter, API und Oberflaeche) ergab +9,4 KB.

**Entscheidung (Nutzer 2026-10-21, F29):** Modbus-Client von Zephyr, hinter einer schmalen
Schnittstelle gekapselt (SOLL-MTR-04). Ein eigener RTU-Master lohnt nur, wenn er deutlich
kleiner und deutlich robuster waere; geschaetzt waere er etwa gleich gross. Zeigen die
Fehlerzaehler (SOLL-MTR-15) im Betrieb Probleme, wird die Implementierung getauscht. Bekannte Grenzen des Zephyr-Clients: Er erkennt das Telegrammende
nur an der Pause von 3,5 Zeichen (Luecken in einer Antwort ergeben CRC-Fehler), und eine
verspaetete Antwort kann der naechsten Anfrage zugeordnet werden, wenn Adresse, Funktion und
Laenge passen. Gegenmassnahmen in der Anwendung: Ruhepause vor jeder Anfrage und laengere Pause
nach einem Timeout, Wiederholungen, Plausibilitaetspruefung der Werte, Fehlerzaehler ueber die
API.

**MQTT-Client (F30), umgesetzt 2026-10-21:** Zephyr-MQTT 3.1.1 ueber TCP (Bibliothek gemessen
4,2 KB), Thread, Parameter, Status, Shell und Weboberflaeche zusammen **+9,1 KB Flash,
+5,1 KB RAM**.

## 6. Softwarestruktur

- **Ladelogik** als hardwareunabhaengiges Modul in reinem C (`lib/evse`): Eingaben Messwerte,
  Schalter, Parameter, Zeit; Ausgaben Tastgrad, Schuetz, Verriegelung, Ausgaenge, Status.
  Vollstaendig auf dem PC getestet.
- **Schutzpfad getrennt und ereignisgesteuert:** RCM-Flanke schaltet im Interrupt direkt ab;
  ein Schutzthread mit hoechster Prioritaet ueberwacht Pegel, Threads und IWDG. Zeitmodell
  und Prioritaeten: [Soll-Spezifikation Abschnitt 3](soll-spezifikation.md#3-zeitmodell-aufgabenaufteilung-watchdog-sicherer-zustand).
- **Hardwareschicht** (`app/src/hw_*.c`) direkt ueber Register fuer TIM1/ADC/DMA wie das
  Original; fuer `native_sim` ersetzt ein Hardwaremodell diese Schicht.
- **Schnittstellen** (HTTP, WebSocket, Shell) laufen ueber eine gemeinsame Zugriffsschicht mit
  Grenzpruefung (SOLL-API-20).

## 7. Hardware-Inbetriebnahme

Jede Stufe erfordert vorher eine ausdrueckliche Aenderung der Sicherheitsgrenzen in
[wallbe2/AGENTS.md](../../wallbe2/AGENTS.md).

Aufbau (Nutzer 2026-10-24): Die Steuerung bleibt in der Wallbox auf dem Tisch; sie wird nur ueber
230 V AC versorgt (keine Kleinspannungseinspeisung, Handbuch Steuerung S. 8). **Keine
handgefuehrten Pruefspitzen unter Netzspannung:** Messpunkte und Verbindungen werden spannungsfrei
angeschlossen, Abdeckung zu, Bedienung und Beobachtung nur ueber Netzwerk, Debugger und fest
angeklemmte Messleitungen. Signalformen (z. B. LD) zeichnet bevorzugt eine Pruef-Firmware auf.

| Stufe | Inhalt | Ausstattung |
| --- | --- | --- |
| H0 | Lesend: Debug-Anschluss, RDP-Stufe, Bootloader/Flash/OTP auslesen | ST-LINK V3SET, USB-Isolator (vorhanden) |
| H1 | Pruef-Firmware ohne Netzspannung am Lastkreis: CP-PWM mit festen Tastgraden, R1/R2-Impulse, Ausgaenge ER/LR/VR/CR, LEDs, Schuetz-Ansteuersignal, Eingaenge und ADC-Werte, IWDG, Reaktionszeit RCM -> PE2, Netzleistung | Oszilloskop (vorhanden), Labornetzteil |
| H2 | CP/PP mit EV-Simulator: Zustaende A..E, Diode, PP-Widerstaende | EV-Simulator |
| H3 | RCM-Selbsttest und Fehlerstrompfad mit Pruefstrom | sicherer Pruefaufbau |
| H4 | Ladelogik komplett mit Netzspannung, Fahrzeug oder Lastsimulator | Elektrofachkraft, FI in der Hausverteilung |

EV-Simulator: Fuer H2 genuegt ein selbstgebauter Pruefstecker (Diode, CP-Widerstaende
2,74 kOhm / 1,3 kOhm / 270 Ohm, PP-Widerstaende 1,5 kOhm / 680 / 220 / 100 Ohm, Schalter;
Werte gegen IEC 61851-1 Anhang A pruefen, Norm nicht lokal). Fuer H4 sind fertige
Typ-2-Pruefadapter mit Messbuchsen sicherer.

## 8. evcc und WARP

evcc spricht die WARP ueber `charger/warp-ws.go` (Vorlage `tinkerforge-warp-ws`; gelesen am
2026-10-19, Zweig master). Daraus folgt der Mindestumfang:

| Zugriff | Zweck | Umsetzung |
| --- | --- | --- |
| `GET /info/features` | Faehigkeiten | `[]`; mit Zaehler `["meters"]`, dann liest evcc `meters/0/value_ids` und `meters/0/values` (SOLL-MTR-30) |
| `GET /info/name` | Typ | `warp2`; bei `warp3`/`warp4` wuerde evcc zusaetzlich `evse/phase_auto_switch` schalten |
| WebSocket `/ws` | Ereignisse | Textrahmen `{"topic", "payload"}`; noetig `evse/state` (`iec61851_state` 0/1/2 = A/B/C, >= 3 = Fehler) und `evse/external_current` |
| `POST /evse/external_current` `{"current": mA}` | Ladestrom; 0 = Laden aus | Slot `external`, nur im RAM (kein Flashschreiben) |

Verhalten von evcc, gegen die Simulation geprueft ([Test und Simulation](test-und-simulation.md#evcc-gegen-die-simulation)):
evcc schreibt `external_current` nur, wenn sich sein eigenes Ziel aendert, und prueft vom
gelesenen Wert nur `> 0`. Ohne Zaehler schaetzt evcc die Ladeleistung aus seinem eigenen
Sollwert; mit Zaehler nimmt es Leistung, Zaehlerstand, Phasenstroeme und -spannungen aus
`meters/0/values` und liest `/info/features` nur beim Verbinden. Abhaengigkeit: Aenderungen im evcc-Treiber koennen Anpassungen erfordern.

WARP-Referenz: [API-Referenz EVSE](https://docs.warp-charger.com/en/docs/interfaces/mqtt_http/api_reference/evse).
Das Slot-Modell der WARP (wirksamer Strom = Minimum aller aktiven Grenzen) entspricht der
Stromkette des Originals und ist Vorbild fuer SOLL-CUR.

## 9. Entwicklungsumgebung

Zephyr **v4.4.2**, Zephyr-SDK **1.0.1** (`arm-zephyr-eabi-gcc` 14.3.0), west 1.5.0 in
`~/zephyrproject/.venv` (ausserhalb des Repos). Module per `manifest.project-filter` auf
cmsis, cmsis_6, hal_stm32, mbedtls und tf-psa-crypto begrenzt (tf-psa-crypto liefert SHA-1
fuer den WebSocket-Handshake). Twister-/Testabhaengigkeiten sind in der venv installiert.

```sh
cd ~/zephyrproject && . .venv/bin/activate && cd zephyr
west build -p always -b ev_cc_ac1 /pfad/zu/wallfree/app -d ~/zephyrproject/build-wallfree
west build -p always -b ev_cc_ac1 /pfad/zu/wallfree/spike-size -d ~/zephyrproject/build-spike
west build -d ~/zephyrproject/build-wallfree -t rom_report   # bzw. ram_report
```

## 10. Offene Punkte

- H0: Bootloader auslesen; danach Update-Uebergabe (SOLL-BOOT-30), Start-SP, loescht der
  Bootloader Sektor 7.
- H1: Hardwareannahmen pruefen ([Implementierung](implementierung.md#annahmen-der-hardwareschicht-in-h1-zu-pruefen)).
- TLS/OCPP: bei Bedarf Speicherort (P3) und Budget neu bewerten.
