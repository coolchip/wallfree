# Architektur und Rahmenentscheidungen

Stand: 2026-10-26. Rahmen der Ersatzfirmware mit Begründungen. Das Soll-Verhalten steht in
der [Soll-Spezifikation](soll-spezifikation.md), der Umsetzungsstand in
[Implementierung](implementierung.md), Tests und Simulation in
[Test und Simulation](test-und-simulation.md). Belege zur Originalfirmware liegen in
[wallbe2](../../wallbe2/docs/analysestand.md).

## 1. Vorgaben des Nutzers

- **Schmal bleiben**, damit Platz für Erweiterungen bleibt.
- **Rückkehr zur Originalfirmware ist sehr wichtig:** Bootloader und Originalparameter
  bleiben unangetastet; die Ersatzfirmware hat einen eigenen Parameterbereich.
- **evcc-Anbindung ist sehr wichtig**, über die vorhandene evcc-Vorlage
  `tinkerforge-warp-ws`, ohne neuen Wallbox-Typ in evcc. Die WARP-Schnittstelle wird
  schrittweise nach Bedarf nachgebaut, nicht vollständig übernommen.
- **Alles über HTTP:** alle Messwerte, Statuswerte und Parameter lesbar, schreibbare Werte
  schreibbar. Zielgruppe sind versierte Anwender; die Weboberfläche hat zusätzlich eine
  einfache Ansicht.
- Shell nur über die Konsole der angemeldeten Weboberfläche, kein Telnet auf der Steuerung (Nutzer 2026-10-28):
  kein offener Port ohne Anmeldung; als Notweg zum Aufspielen dient der Reset-Taster (SOLL-BOOT-22).
- **OCPP perspektivisch** als allgemeine Implementierung, nicht aus der wallbe-Firmware
  abgeleitet. **TLS vorerst nicht. Modbus TCP entfällt**; die evcc-Vorlage für die
  Phoenix-Steuerung (Modbus TCP) wird wegen früherer Probleme bewusst nicht genutzt.
- **Energiezähler über Modbus RTU nur zur Anzeige** (Nutzer 2026-10-21, F28): Messwerte für
  Weboberfläche und evcc; die Steuerung arbeitet ohne Zähler vollständig.
- **MQTT-Client** (Nutzer 2026-10-21, F30): Zustände als WARP-JSON an einen Broker, Ladestrom
  als erster Befehl, weitere Befehle später ergänzbar; ohne TLS, ohne Home-Assistant-Erkennung.
- Hardwaretests erst nach ausdrücklicher Freigabe, in Stufen (Abschnitt 7).

## 2. Entscheidungen

| Thema | Entscheidung | Begründung |
| --- | --- | --- |
| Rahmenwerk | **Zephyr v4.4.2** | Vorerfahrung des Nutzers; Treiber für STM32F2, STM32-Ethernet und KSZ8081 vorhanden; IP-Stack, HTTP-Server mit WebSocket, Shell mit Telnet, Watchdog/ADC/Flash-APIs; per Kconfig schmal konfigurierbar; `native_sim` erlaubt PC-Tests der ganzen Anwendung. Der Größentest (Abschnitt 5) zeigte, dass es passt. Verworfen: FreeRTOS + STM32Cube-HAL + lwIP (wie Original, aber mehr Eigenbau bei HTTP/Settings/Treiberintegration) und Bare-Metal (am meisten Eigenarbeit im Netzwerkteil) |
| IP-Stack | Zephyr-eigener Stack | lwIP wäre nur ohne Zephyr sinnvoll |
| Systemtakt | **100 MHz** wie Original (HSE 25 MHz, PLL M 25/N 200/P 2) | 120 MHz wären ohne zusätzliche Wartezyklen möglich (RM0033 Tab. 3: 3 WS für 90..120 MHz bei 2,7..3,6 V), bringen der Ladelogik aber nichts und weichen von der vom Hersteller erprobten Konfiguration (EMV) ab. Spätere Option, falls TLS/OCPP Rechenzeit brauchen |
| Ethernet | RMII, Autonegotiation (10/100 Mbit/s) | wie Original; der Zephyr-Treiber verhandelt ebenfalls, an einem 100-Mbit-Switch also 100 Mbit/s Vollduplex |
| Ladeadresse | Rohabbild ab `0x08010000` (Sektoren 4..6, max. 320 KB) | Platz und Startvertrag der Originalanwendung; Bootloader bleibt nutzbar (Abschnitt 4) |
| Parameterspeicher | **P1: Sektor 7 mit eigenem Log-Format**, Sektor 3 nur lesen | siehe Abschnitt 3 |
| HTTP-Schnittstelle | an WARP angelehnt, Ströme in **mA**, Typ `warp2`, erster Umfang = evcc-Mindestumfang, danach alle Parameter über eigene Ressourcen | evcc-Treiber `warp-ws.go` bestimmt Mindestumfang und Verhalten; mA wie WARP (16000 = 16 A) |
| Zugriffsschutz | HTTP-Digest (MD5, `qop=auth`) für Web und API, Werkszugang mit Änderungszwang | evcc spricht WARP mit Digest (`charger/warp/connection.go`); Passwort wird nie übertragen. Ohne TLS bleibt Mitlesen im LAN möglich |
| Weboberfläche | **Preact + htm** mit eigenem kleinem CSS, gzip im Flash | Komponenten wie React ohne Build-Schritt, ~5 KB; WARP nutzt ebenfalls Preact. Verglichen mit Vanilla JS (kleiner, mehr Handarbeit), petite-vue (~7 KB), Alpine.js (~15 KB). Gemessen: 11,7 KB gzip für die ganze Oberfläche |
| Projekt | Name **Wallfree**, eigenes veröffentlichbares Repository, **Apache-2.0** | wallbe2 enthält Originalfirmware und Handbücher und bleibt privat; Befunde werden hier nur in eigenen Worten mit Verweis genutzt |

## 3. Speicheraufteilung und Parameterspeicher

| Bereich | Sektoren | Inhalt | Beleg |
| --- | --- | --- | --- |
| `0x08000000..0x0800BFFF` | 0..2 (je 16 KB) | Bootloader (nicht im Abbild, Hypothese) | [Bootloader](../../wallbe2/docs/bootloader.md) |
| `0x0800C000..0x0800FFFF` | 3 (16 KB) | Originalparameter, **nur lesen** | [Parameterspeicher](../../wallbe2/docs/parameterspeicher.md) |
| `0x08010000..0x0805FFFF` | 4 (64 KB), 5..6 (je 128 KB) | Anwendung, max. 320 KB (Original belegt bis `0x0805F677`) | [Ladeadresse](../../wallbe2/docs/firmware-ladeadresse.md) |
| `0x08060000..0x0807FFFF` | 7 (128 KB) | **Wallfree-Parameter** (vom Original nicht belegt) | |

MCU STM32F207VET6: 512 KB Flash, 128 KB SRAM (RM0033 Tab. 2). Der Flash hat nur eine Bank:
Während eines Löschvorgangs steht der Bus (Sektor 7 laut Zephyr-Devicetree bis 4 s, nicht am
Datenblatt geprüft). Deshalb wird nur im sicheren Zustand gelöscht (SOLL-NVM-04), mit vorher
verlängertem IWDG (SOLL-RT-13). Das Original hat dieselbe Eigenschaft bei Sektor 3.

**Warum P1:** Betrachtet wurden vier Varianten.

| Variante | Ergebnis |
| --- | --- |
| **P1** Sektor 7, eigenes Format; Sektor 3 nur lesen | **gewählt:** Original völlig unangetastet, Sektor 7 vom Original ungenutzt; Anwendung behält 320 KB wie das Original. Nachteil: nur eine Löscheinheit, beim Verdichten bleibt ein kurzes Fenster ohne Zweitkopie (wie im Original) |
| P2 Sektoren 6+7 als Zwei-Seiten-Speicher | verworfen: Anwendung nur 192 KB |
| P3 Sektor 3 mitbenutzen | Reserve, falls der Flash für TLS/OCPP knapp wird; Nachteil: Originalwerte wären bei Stromausfall während des Verdichtens gefährdet |
| P4 Backup-SRAM | verworfen: keine Batterie in der Steuerung vorgesehen |

Zephyr-Bausteine passen nicht: NVS adressiert innerhalb eines Sektors nur 64 KB und verlangt
Sektoren als Vielfaches der Löscheinheit; ZMS verlangt mindestens zwei Löscheinheiten und
löscht selbstständig beim Start und bei der Speicherbereinigung. Daher ein eigenes Log-Format
([nvm.h](../include/wallfree/nvm/nvm.h)): Einträge mit CRC-8 im Kopf und CRC-32, Bereichskopf aus
Kennung (direkt nach dem Löschen) und Abschluss (zuletzt), sodass ein unterbrochenes Verdichten
beim Start erkannt wird. Format und Ausfallverhalten: [Implementierung](implementierung.md#parameterspeicher).

**Noch zu prüfen (H0):** ob der Bootloader beim Update Sektor 7 löscht. Falls ja, ist der
Speicherort neu zu bewerten (z. B. P3).

## 4. Bootloader und Updatepfad

- Die Ersatzfirmware ist ein Rohabbild ab `0x08010000` mit eigener Vektortabelle. Original und
  Phoenix 1.27 haben denselben Startvertrag; 1.27 ließ sich laut Community-Berichten über den
  Bootloader einspielen.
- Update-Anforderung wie Original (RTC-Backup-Register BKP10..15, Systemreset; SOLL-BOOT-30).
  Der Bootloader (aus der H0-Sicherung analysiert, [wallbe2 Bootloader](../../wallbe2/docs/bootloader.md))
  nimmt das Rohabbild per Web-Formular an, löscht nur Sektor 4..6 und prüft vor dem Start nur
  den Start-SP (im SRAM); Wallfree hängt deshalb eine eigene Prüfsumme an und prüft sie beim
  Start (SOLL-BOOT-35). Update über die Weboberfläche auf dem Testgerät geprüft.
- Rückweg zur Originalfirmware über denselben Bootloaderpfad; Sektor 3 bleibt erhalten (auf
  Das Testgerät geprüft).
- Start-SP: Zephyr setzt ihn auf das Ende seines Interrupt-Stacks, das Original auf das
  RAM-Ende `0x20020000`; der Bootloader verlangt nur einen Wert im SRAM, beides passt.

## 5. Speicherbudget

Größentest vor der Entscheidung (`spike-size/`, nur gebaut): Zephyr mit IPv4, DHCP, TCP,
HTTP-Server, Telnet-Shell mit Netz-Kommandos, Settings, ADC, PWM, Watchdog: **207 KB Flash,
85 KB RAM**. Zum Vergleich: das Original belegt mit Weboberfläche, OCPP, Modbus und lwIP
~318 KB.

Aktueller Stand der Anwendung und Anteile: [Implementierung](implementierung.md#ressourcen).
Gemessene Einsparmöglichkeiten (jeweils einzeln gebaut, Basis 295 KB):

| Maßnahme | Ersparnis | Bemerkung |
| --- | --- | --- |
| **umgesetzt:** `CONFIG_NET_SHELL=n` | 43 KB (gemessen 2026-10-26: 44 KB) | Netz-Kommandos der Shell |
| Assert-Texte weg (`ASSERT_VERBOSE=n`, `ASSERT_NO_*_INFO=y`) | 32 KB | Prüfungen bleiben; bei Auslösung erscheint **gar nichts** im Log (ohne `ASSERT_VERBOSE` druckt Zephyr auch Datei und Zeile nicht) |
| **umgesetzt:** Ausdruck und Meldung weg, Datei und Zeile bleiben (`ASSERT_NO_COND_INFO=y`, `ASSERT_NO_MSG_INFO=y`) | 18,2 KB (gemessen 2026-10-21 auf 325,7 KB) | Prüfungen bleiben; bei Auslösung `ASSERTION FAIL @ Datei:Zeile` |
| `CONFIG_LTO=y` | 24 KB | geprüft: Interrupttabelle, HTTP-Ressourcen und Fault-Handler korrekt; Fehlersuche schwerer |
| `CONFIG_SHELL_HELP=n` | 8 KB | Hilfetexte der Shell |
| LTO + Assert-Texte | 48 KB | |
| `CONFIG_ASSERT=n` | 40 KB | nicht empfohlen |
| `-Oz` statt `-Os` | 0 | GCC 14 erzeugt für Cortex-M3 denselben Code |

**Entscheidung (Nutzer 2026-10-21):** Assert-Ausdruck und -Meldung sind entfernt, Datei und
Zeile bleiben (siehe Tabelle). Die Netz-Shell (`net iface`, `ping`, `arp`, `conn`, `mem` ...)
ist entfernt (Nutzer 2026-10-26), nachdem sie bei der Inbetriebnahme und im Netz-Lasttest auf
Das Testgerät gebraucht wurde. Seit 2026-10-28 läuft die Shell nur noch über die Konsole der
Weboberfläche (ohne Telnet, interaktive Funktionen und Zephyrs Zusatzkommandos, 24 KB weniger).
Weitere Reihenfolge, wenn es wieder knapp wird: LTO, Shell-Hilfe. TLS (mbedTLS) passt neben P1
voraussichtlich nicht (nicht gemessen); dann Speicherort und Budget neu bewerten.

**Zähleranbindung Modbus RTU (F28), umgesetzt 2026-10-21:** UART4 mit RS-485-Freigabe PC12,
Zephyr-Modbus-Client, Zählermodul, API, Shell und Weboberfläche zusammen **+15,2 KB Flash,
+3,9 KB RAM** (Aufteilung: [Implementierung](implementierung.md#ressourcen)). Der Größentest
vorab (nur Abfrage, ohne Parameter, API und Oberfläche) ergab +9,4 KB.

**Entscheidung (Nutzer 2026-10-21, F29):** Modbus-Client von Zephyr, hinter einer schmalen
Schnittstelle gekapselt (SOLL-MTR-04). Ein eigener RTU-Master lohnt nur, wenn er deutlich
kleiner und deutlich robuster wäre; geschätzt wäre er etwa gleich groß. Zeigen die
Fehlerzähler (SOLL-MTR-15) im Betrieb Probleme, wird die Implementierung getauscht. Bekannte Grenzen des Zephyr-Clients: Er erkennt das Telegrammende
nur an der Pause von 3,5 Zeichen (Lücken in einer Antwort ergeben CRC-Fehler), und eine
verspätete Antwort kann der nächsten Anfrage zugeordnet werden, wenn Adresse, Funktion und
Länge passen. Gegenmaßnahmen in der Anwendung: Ruhepause vor jeder Anfrage und längere Pause
nach einem Timeout, Wiederholungen, Plausibilitätsprüfung der Werte, Fehlerzähler über die
API.

**MQTT-Client (F30), umgesetzt 2026-10-21:** Zephyr-MQTT 3.1.1 über TCP (Bibliothek gemessen
4,2 KB), Thread, Parameter, Status, Shell und Weboberfläche zusammen **+9,1 KB Flash,
+5,1 KB RAM**.

## 6. Softwarestruktur

- **Ladelogik** als hardwareunabhängiges Modul in reinem C (`lib/evse`): Eingaben Messwerte,
  Schalter, Parameter, Zeit; Ausgaben Tastgrad, Schütz, Verriegelung, Ausgänge, Status.
  Vollständig auf dem PC getestet.
- **Schutzpfad getrennt und ereignisgesteuert:** RCM-Flanke schaltet im Interrupt direkt ab;
  ein Schutzthread mit höchster Priorität überwacht Pegel, Threads und IWDG. Zeitmodell
  und Prioritäten: [Soll-Spezifikation Abschnitt 3](soll-spezifikation.md#3-zeitmodell-aufgabenaufteilung-watchdog-sicherer-zustand).
- **Hardwareschicht** (`app/src/hw_*.c`) direkt über Register für TIM1/ADC/DMA wie das
  Original; für `native_sim` ersetzt ein Hardwaremodell diese Schicht.
- **Schnittstellen** (HTTP, WebSocket, Shell) laufen über eine gemeinsame Zugriffsschicht mit
  Grenzprüfung (SOLL-API-20).

## 7. Hardware-Inbetriebnahme

Jede Stufe erfordert vorher eine ausdrückliche Änderung der Sicherheitsgrenzen in
[wallbe2/AGENTS.md](../../wallbe2/AGENTS.md).

Aufbau (Nutzer 2026-10-24): Die Steuerung bleibt in der Wallbox auf dem Tisch; sie wird nur über
230 V AC versorgt (keine Kleinspannungseinspeisung, Handbuch Steuerung S. 8). **Unter Spannung
nie direkt auf der Platine messen;** an den großen Klemmen misst der Nutzer mit
Schutzhandschuhen (Nutzer 2026-10-24). Bedienung über Netzwerk und Debugger. Statt einer
eigenen Prüf-Firmware wurde Wallfree selbst verwendet (Shell/API, Register-Ausgänge).

| Stufe | Inhalt | Ausstattung | Stand (Testgerät) |
| --- | --- | --- | --- |
| H0 | Lesend: Debug-Anschluss, RDP-Stufe, Bootloader/Flash/OTP auslesen | ST-LINK V3SET | erledigt 2026-10-24 (RDP 0, Sicherung) |
| H1 | Ohne Last: Ausgänge ER/LR/VR/CR, LEDs, Eingänge und ADC-Werte, IWDG, Verriegelung, Netz; Reaktionszeit RCM -> PE2 | Oszilloskop | mit Wallfree als Stufen A/B erledigt; RCM-Zeit in Stufe C4 ([Implementierung](implementierung.md#prüfungen-auf-box-t)) |
| H2 | CP/PP mit EV-Simulator: Zustände A..E, Diode, PP-Widerstände; CP-PWM, Schütz-Ansteuerung | Prüfstecker | als Stufe C erledigt 2026-11-07 ([Prüfstecker](pruefstecker.md)) |
| H3 | RCM-Selbsttest und Fehlerstrompfad mit Prüfstrom | sicherer Prüfaufbau | Selbsttest und DC-Prüfstrom (9 mA) in Stufe C4 erledigt, Reaktionszeit 2,5..3 ms; AC-Zweig (30 mA) offen |
| H4 | Ladelogik komplett mit Netzspannung, Fahrzeug oder Lastsimulator | Elektrofachkraft, FI in der Hausverteilung | geplant als Stufe D am zweiten Gerät mit Nissan Leaf ([Prüfstecker](pruefstecker.md#8-stufe-d-laden-mit-fahrzeug-box-g)) |

EV-Simulator: Für H2 genügt ein selbstgebauter Prüfstecker (Diode, CP-Widerstände
2,74 kOhm / 1,3 kOhm / 270 Ohm, PP-Widerstände 1,5 kOhm / 680 / 220 / 100 Ohm, Schalter;
Werte gegen IEC 61851-1 Anhang A prüfen, Norm nicht lokal). Für H4 sind fertige
Typ-2-Prüfadapter mit Messbuchsen sicherer.

## 8. evcc und WARP

evcc spricht die WARP über `charger/warp-ws.go` (Vorlage `tinkerforge-warp-ws`; gelesen am
2026-10-19, Zweig master). Daraus folgt der Mindestumfang:

| Zugriff | Zweck | Umsetzung |
| --- | --- | --- |
| `GET /info/features` | Fähigkeiten | `[]`; mit Zähler `["meters"]`, dann liest evcc `meters/0/value_ids` und `meters/0/values` (SOLL-MTR-30) |
| `GET /info/name` | Typ | `warp2`; bei `warp3`/`warp4` würde evcc zusätzlich `evse/phase_auto_switch` schalten |
| WebSocket `/ws` | Ereignisse | Textrahmen `{"topic", "payload"}`; nötig `evse/state` (`iec61851_state` 0/1/2 = A/B/C, >= 3 = Fehler) und `evse/external_current` |
| `POST /evse/external_current` `{"current": mA}` | Ladestrom; 0 = Laden aus | Slot `external`, nur im RAM (kein Flashschreiben) |

Verhalten von evcc, gegen die Simulation geprüft ([Test und Simulation](test-und-simulation.md#evcc-gegen-die-simulation)):
evcc schreibt `external_current` nur, wenn sich sein eigenes Ziel ändert, und prüft vom
gelesenen Wert nur `> 0`. Ohne Zähler schätzt evcc die Ladeleistung aus seinem eigenen
Sollwert; mit Zähler nimmt es Leistung, Zählerstand, Phasenströme und -spannungen aus
`meters/0/values` und liest `/info/features` nur beim Verbinden. Abhängigkeit: Änderungen im evcc-Treiber können Anpassungen erfordern.

WARP-Referenz: [API-Referenz EVSE](https://docs.warp-charger.com/en/docs/interfaces/mqtt_http/api_reference/evse).
Das Slot-Modell der WARP (wirksamer Strom = Minimum aller aktiven Grenzen) entspricht der
Stromkette des Originals und ist Vorbild für SOLL-CUR.

## 9. Entwicklungsumgebung

Zephyr **v4.4.2**, Zephyr-SDK **1.0.1** (`arm-zephyr-eabi-gcc` 14.3.0), west 1.5.0 in
`~/zephyrproject/.venv` (außerhalb des Repos). Module per `manifest.project-filter` auf
cmsis, cmsis_6, hal_stm32, mbedtls und tf-psa-crypto begrenzt (tf-psa-crypto liefert SHA-1
für den WebSocket-Handshake). Twister-/Testabhaengigkeiten sind in der venv installiert.

```sh
cd ~/zephyrproject && . .venv/bin/activate && cd zephyr
west build -p always -b ev_cc_ac1 /pfad/zu/wallfree/app -d /pfad/zu/wallfree/build/ev_cc_ac1
west build -p always -b ev_cc_ac1 /pfad/zu/wallfree/spike-size -d ~/zephyrproject/build-spike
west build -d /pfad/zu/wallfree/build/ev_cc_ac1 -t rom_report   # bzw. ram_report
```

## 10. Offene Punkte

- H2..H4 (Stufe C ff.) und die restlichen Hardwareannahmen ([Implementierung](implementierung.md#annahmen-der-hardwareschicht)).
- TLS/OCPP: bei Bedarf Speicherort (P3) und Budget neu bewerten.
