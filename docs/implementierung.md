# Implementierungsstand

Stand: 2026-10-27. **Auf Hardware nur ohne Last gelaufen** (Testgerät, Schuetz abgeklemmt, kein
Fahrzeug; Ergebnisse: [Pruefungen auf dem Testgerät](#pruefungen-auf-box-t)); bestandene Tests belegen
keine elektrische Sicherheit. Rahmen und Begruendungen: [Architektur](architektur.md);
Tests: [Test und Simulation](test-und-simulation.md).

## Ueberblick

| Bereich | Stand |
| --- | --- |
| Ladelogik (`lib/evse`) | vollstaendig nach Soll-Spezifikation, PC-getestet |
| Parameterspeicher (`lib/nvm`) | fertig, PC-getestet inkl. Stromausfall nach jedem Byte |
| API-Bausteine (`lib/api`) | fertig, PC-getestet |
| Energiezaehler (`lib/meter`, Zaehlerthread, nur Anzeige) | fertig, PC-getestet, in der Simulation Ende-zu-Ende und gegen evcc geprueft; Modbus-Anbindung auf UART4 gebaut, auf der Steuerung ungeprueft (kein Zaehler vorhanden) |
| MQTT-Client (`lib/api/mqtt.c`, `mqtt_task.c`) | fertig, PC-getestet, in der Simulation Ende-zu-Ende und auf dem Testgerät gegen Mosquitto geprueft |
| Anwendung (`app/`) | baut fuer `ev_cc_ac1` ohne Warnung; als PC-Simulation Ende-zu-Ende getestet; auf dem Testgerät unter Netzlast geprueft |
| Weboberflaeche (`web/`) | fertig (Bereiche, Verlauf, Auswahllisten, Begriffe), Browser-Durchlauf in der Simulation, auf dem Testgerät im Einsatz |
| evcc-Anbindung | gegen die Simulation geprueft (evcc 0.317.2), gegen das Testgerät ohne Fahrzeug |
| Hardwareschicht (`hw_stm32.c`, `hw_gpio.c`) | auf dem Testgerät gestartet: Takt, Bootloader-Start, Ethernet/PHY, DIP/Drehschalter, CP-Messung (Ruhe, A, F), RCM-Selbsttest, alle fuenf Eingaenge, Ausgaenge ER/LR/VR/CR (je 12 V), Verriegelung mit LD, Power-/Error-LED, Parameterspeicher, Reset-Taster/Werksreset; Schuetz, PWM, Connect-/Ready-LED (nur kurz bzw. erst mit Fahrzeug) noch ungeprueft |
| Update ueber den Bootloader (SOLL-SH-10, SOLL-BOOT-30..35) | umgesetzt und auf dem Testgerät geprueft (Weboberflaeche, 20 s, Pruefsumme des Abbilds); Simulation mit nachgebildetem Bootloader Ende-zu-Ende und im Browser |

## Module der Ladelogik und Bausteine

Reines C99, getestet auf `native_sim/native/64` mit ztest.

| Datei | Inhalt | Anforderungen | Tests |
| --- | --- | --- | --- |
| [cp.c](../lib/evse/cp.c) | CP-Bandtabelle, Hysterese, Entprellung, CP ungueltig, Diodenpruefung | SOLL-CP-01..07 | `tests/evse/meas` (evse_cp) |
| [pp.c](../lib/evse/pp.c) | PP-Klassen, 1-s-Uebernahme, Kabelabweisung | SOLL-PP-01..06 | evse_pp |
| [din.c](../lib/evse/din.c) | Digitaleingaenge, Hysterese, 2-Block-Entprellung, Funktionscodes 0..22 | SOLL-IO-10..12, 9.1, SOLL-FN-01..03 | evse_din |
| [current.c](../lib/evse/current.c) | Slot-Minimum, Stromsperre, 5-%-Modus, Tastgradregel | SOLL-CUR-01..22 | evse_current |
| [switches.c](../lib/evse/switches.c) | Drehschalter- und DIP-Dekodierung | 2.2, E11 | evse_current |
| [lock.c](../lib/evse/lock.c) | Verriegelungszyklus, LD permanent/gepulst, Option `unlock_check` | SOLL-LOCK-01..17, F27 | `tests/evse/prot` (evse_lock) |
| [rcm.c](../lib/evse/rcm.c) | Fehlerstrom-Latch, Selbsttest-Zeitfolge | SOLL-PROT-04..13 | evse_rcm |
| [ctm.c](../lib/evse/ctm.c) | Schuetzueberwachung Modi 0..4; Statuswert `ct_sense` (Spannung an CT, Schuetz Ist, in jedem Modus) | SOLL-PROT-20..23, 10.3 | evse_ctm |
| [charger.c](../lib/evse/charger.c) | Automat S0..S10, Vorrangliste, Weckfolge, Freigabe, Fehlermodell mit Historie (32 Ereignisse), LEDs, sicherer Zustand, Selbsttest auf Anforderung, geordnetes Ende vor Neustart (`evse_request_shutdown`) | 5, 9.2, 11, SOLL-PROT-01/41, SOLL-LOCK-20..41, SOLL-RT-20 | `tests/evse/charger` (evse_charger, evse_charger2: 45 Szenarien mit Simulationsmodell) |
| [outputs.c](../lib/evse/outputs.c) | Ausgangsfunktionen 0..42 | 9.3 | evse_outputs (Wahrheitstabelle aller Codes) |
| [param.c](../lib/evse/param.c) | Parametertabelle (Konfiguration, Laufzeit, Status) mit Name, Kennung, Herkunftsfeld, Einheit, Bereich, Default, Hilfetext; Schreiben mit Grenz-/Zustandspruefung und Fehlercode | 10, SOLL-PAR-01..03/40, E14, SOLL-API-20/30 | evse_param |
| [persist.c](../lib/evse/persist.c) | Laden/Speichern der Konfiguration und des Verriegelungs-Endzustands (465), Loeschbedingung, Import aus Sektor 3 | SOLL-BOOT-04, SOLL-NVM-03/04/10, SOLL-PAR-03/40 | evse_persist |
| [nvm.c](../lib/nvm/nvm.c) | Log-Format in einer Loescheinheit: Anhaengen, RAM-Puffer bei vollem Bereich, Verdichten, Formatieren, Erkennung unterbrochener Vorgaenge | SOLL-NVM-01..05 | `tests/nvm` |
| [orig.c](../lib/nvm/orig.c) | Nur-Lese-Zugriff auf das Originalformat in Sektor 3 | SOLL-NVM-10 | nvm_orig (synthetisches Abbild) |
| [md5.c](../lib/api/md5.c), [digest.c](../lib/api/digest.c) | MD5 (RFC 1321), HTTP-Digest mit `qop=auth`: Challenge, Pruefung, zustandslose Nonces | SOLL-API-07 | `tests/api` (RFC-Vektoren, Rundlauf wie evcc) |
| [json.c](../lib/api/json.c) | JSON-Ausgabe mit Maskierung, Lesen flacher Objekte | 12.1 | api_json |
| [evse_api.c](../lib/api/evse_api.c) | `evse/state` (WARP-Abbildung), `external_current`, `limits`, alle Parameter lesen/schreiben, Parameterbeschreibung in Teilen | SOLL-API-03..05/30/32 | evse_api |
| [sysconf.c](../lib/api/sysconf.c) | Netz-, Zaehler-, MQTT- und Systemparameter: Pruefung, JSON (Passwort `null`), Speicherkodierung (Kennungen 3010..3027), Korrektur widerspruechlicher Saetze beim Start, sofort wirksame Werte (`mqtt_*`), Import der Originalfelder 404/315..326 | 10.1, F20, F25, F28, F30, SOLL-NVM-10 | api_sysconf |
| [mqtt.c](../lib/api/mqtt.c) | MQTT ohne Netzwerk: Versandregel je Topic (Vollversand nach Verbinden, Aenderung per Pruefsumme, Mindestabstand mit Nachholen, Zyklus ohne Vergleich), Topicbildung, Befehlstabelle mit Retain-/Leer-/Laengenpruefung, Status-JSON | SOLL-MQTT-10/11/20/22/30 | api_mqtt |
| [reset.c](../lib/api/reset.c) | Resetgrund aus Flags und Backup-Code | SOLL-RT-14 | api_reset |
| [ratelimit.c](../lib/api/ratelimit.c) | Bremse gegen Passwort-Durchprobieren je Absenderadresse | SOLL-API-07 | api_ratelimit |
| [events.c](../lib/api/events.c) | Ereignisse aus dem Vergleich zweier Momentaufnahmen, Ringpuffer, JSON | SOLL-ERR-10 | api_events |
| [alog.c](../lib/api/alog.c) | Ereignisprotokoll: Ringpuffer (48, Backup-SRAM), Zusammenfassen wiederholter Fehlversuche, Loeschen mit Eintrag, JSON in Teilen | SOLL-ERR-11..13 | api_alog |
| [meter.c](../lib/meter/meter.c) | Zaehler: Modelltabellen (ABB B23, SDM630, SDM72D-M V2/V1), Dekodierung Float32/U16/U32/S32/U64 mit Ungueltig-Kennungen, Plausibilitaet, Abfragezyklus mit Ruhepause, Wiederholung und Zusatzpause nach Timeout, Satzbildung, Zustand, Fehlerzaehler, Ereignisse; Bus hinter `struct meter_bus` | SOLL-MTR-01/03..15 | `tests/meter` (meter) |
| [meter_sim.c](../lib/meter/meter_sim.c) | nur PC: Antworten eines Zaehlers je Modell (Tests, Simulation) | – | meter |
| [meter_json.c](../lib/api/meter_json.c) | `meters/0/value_ids`, `/values` (Tausendstel als Dezimalzahl, ungueltig `null`), `/state` mit Fehlerzaehlern | SOLL-MTR-30/31 | meter |

## Anwendung und Hardwareschicht

Board `boards/phoenix/ev_cc_ac1`: STM32F207VE, 100 MHz (PLLN 200), RMII mit KSZ8081 im
25-MHz-Modus, Partitionen nach [Architektur](architektur.md#3-speicheraufteilung-und-parameterspeicher).
TIM1 und ADC1 stehen nicht im Board-Devicetree, weil die Anwendung sie direkt steuert
(`spike-size/app.overlay` bringt sie fuer den Groessentest mit).

| Datei | Inhalt | Anforderungen |
| --- | --- | --- |
| [main.c](../app/src/main.c) | Startablauf: GPIO, Parameterspeicher (ungueltig -> neu anlegen), Reset-Taster 5 s -> Bereich loeschen, gespeicherte Werte laden, DIP/Drehschalter, Messung, Threads | SOLL-BOOT-01..10/20 |
| [hw_stm32.c](../app/src/hw_stm32.c) | TIM1 CH1/PE9 (PSC 49, center-aligned Modus 1, ARR 1000, 1 kHz), ADC1 10 Raenge in zwei 5er-Gruppen (Rang 10 je Block abwechselnd Temperatursensor und VREFINT, umgeschaltet in der DMA-Unterbrechung, beide 480 Takte, sonst 28; SOLL-API-35), Start im TIM1-Update-Interrupt bei CNT < 50 bzw. > 950, DMA2 Stream0 zirkular 80 Worte, Skalierung wie Original; MCO1 = HSE an PA8; PA15-Resetimpuls 11 ms; fruehe sichere Pinpegel; sicherer Zustand per Register | SOLL-RT-01..03, SOLL-IO-02, SOLL-BOOT-02/09, SOLL-RT-20 |
| [hw_gpio.c](../app/src/hw_gpio.c) | alle Ein-/Ausgaenge aus `zephyr,user`; RCM-Flankeninterrupt schaltet PE2 per Register ab und setzt einen Schutz-Latch, der Ein-Befehle sperrt; PE0-Flankenzeitstempel; PB2 erst nach der ersten Ausgabe | SOLL-PROT-01/05, SOLL-IO-01/21 |
| [evse_task.c](../app/src/evse_task.c) | Schutzthread (Prio 0, 1 ms): RCM-Pegel, Ueberwachung Automat (100 ms) und Messbloecke (50 ms), IWDG 250 ms, Wartungsmodus fuer Flash-Loeschen; Automatenthread (Prio 2, 10 ms und je Messblock); Ereigniserkennung; Fault-Handler: sicherer Zustand, Neustart | SOLL-RT-04/05/10..12/21 |
| [storage.c](../app/src/storage.c) | Flash-Adapter (Sektor 7 lesen/schreiben/loeschen, Sektor 3 nur lesen, Lage beim Bauen geprueft); Speicher-Thread (Prio 12): Verriegelungszustand speichern, Verdichten im sicheren Zustand; Werksreset, Import | SOLL-NVM-01..05/10, SOLL-RT-13, SOLL-BOOT-04/20/21 |
| [http_api.c](../app/src/http_api.c) | HTTP-Server Port 80: `/info/name`, `/info/features`, `/evse/state`, `/evse/external_current`, `/evse/limits`, `/evse/params`, `/evse/param_info`, `/evse/errors`, `/evse/errors/ack`, `/credentials`, `/system/config`, `/system/config_info`, `/system/restart`, `/system/update`, `/system/info`, `/system/log`, `/system/log/clear`, `/mqtt/state`, mit Zaehler `/meters/0/value_ids`, `/values`, `/state` (sonst 404); WebSocket `/ws` (bis 3 Clients, Push alle 100 ms bzw. sofort nach Schreibzugriffen, ein Topic je Rahmen, Topics `evse/state`, `evse/external_current`, `evse/limits`, `evse/event`, mit Zaehler `meters/0/*` nach jedem Zyklus); Digest-Pruefung je Anfrage | SOLL-API-01..07/30..32, SOLL-ERR-10/11, SOLL-MTR-30/31, SOLL-MQTT-30 |
| [auth.c](../app/src/auth.c) | Zugangsdaten (HA1), Werkszugang, Passwortaenderung, Nonce-Geheimnis je Start | SOLL-API-07 |
| [netcfg.c](../app/src/netcfg.c) | Geraete-MAC aus dem OTP (`0x1FFF7800`) vor DHCP, Netz-/Systemparameter, Hostname, DHCP oder feste Adresse, Telnet; Aendern (atomar), Import; `mqtt_*` sofort in die laufende Konfiguration und MQTT neu verbinden; Netzwerk-LED PC2 | 10.1, F20, F25, SOLL-LED-06, SOLL-MQTT-31 |
| [shell_evse.c](../app/src/shell_evse.c) | `evse status`, `param list/get/set`, `current`, `enable`, `avail`, `pause`, `pwm5`, `lock`, `unlock`, `errors [clear]`, `selftest`, `reboot`, `bootloader`, `boot` (Diagnose: RCC_CSR und BKP10..16 beim Start), `ack`, `nvm`, `meter`, `mqtt`, `sys list/set`, `factory-reset confirm`, `import-original` | SOLL-SH-01..11/19/20, SOLL-MTR-33, SOLL-MQTT-32 |
| [sysinfo.c](../app/src/sysinfo.c) | Prozessortemperatur (`mcu_temp_dc`, SOLL-API-35), Resetgrund, Firmwarestand (Version und Build, im Abbild als `WALLFREE-IMAGE:<version>+<bau>`), Laufzeit, Ergebnis eines Updates (BKP12), Pruefung des eigenen Abbilds (CRC32 gegen den Anhang, ungueltig -> ERR-INTERNAL); `GET /system/info` | SOLL-RT-14, SOLL-BOOT-32/33/35 |
| [meter_task.c](../app/src/meter_task.c) | Zaehlerthread (niedrigste Anwendungsprioritaet, Stack 1280 B, nicht ueberwacht), nur mit `meter_type` != 0; Momentaufnahme unter Sperre, Bus ohne Sperre; Ereignisse in den gemeinsamen Ringpuffer | SOLL-MTR-01/03/10..14 |
| CP-Aufnahme in [hw_stm32.c](../app/src/hw_stm32.c), `GET /evse/cp_scope` | ADC2 (CP+ IN0, CP- IN9, 28 Zyklen) per Software gestartet im TIM1-CC2-Interrupt (Prio wie TIM1-Update), DMA2 Stream2 Kanal 1, CCR2 = 60 + 8 i (t = 1000 - CCR2/2 us); Simulation aus dem Modell. Auf dem Testgerät geprueft (`6eb0e08`, 2026-10-27): Aufnahme gelingt, im Leerlauf Tastgrad 1000, CP+ 11,87..11,93 V; CC2 loest im zentrierten Modus 1 einmal je Periode aus | SOLL-API-34 |
| [timesync.c](../app/src/timesync.c), [ntp.c](../lib/api/ntp.c), [resolve.c](../app/src/resolve.c) | Uhrzeit per SNTP: Thread niedrigster Prioritaet (Stack 2048 B, nicht ueberwacht), Server nach SOLL-TIME-02, DNS asynchron ohne Heap (gemeinsam mit MQTT), Basis = Antwort minus Laufzeit, an das Protokoll weitergereicht | SOLL-TIME-01..04 |
| [mqtt_task.c](../app/src/mqtt_task.c) | MQTT-Thread (niedrigste Anwendungsprioritaet, Stack 2048 B, nicht ueberwacht): Verbinden mit Last Will, Neuverbindung 1..60 s, Takt 100 ms fuer Zustaende und Ereignisse, Befehl `evse/external_current_update`, Status | SOLL-MQTT-01..04/10..12/20..23/30 |
| [meter_bus_modbus.c](../app/src/meter_bus_modbus.c) | nur Steuerung: Zephyr-Modbus-Client auf UART4 (`modbus0`, DE an PC12), Baudrate/Format aus den Parametern, Fehler auf Timeout/Rahmen/Ausnahme abgebildet | SOLL-MTR-02/04, F29 |
| [web/](../web/) | Werkszugang -> Zugangsdaten festlegen; Uebersicht: Zustand, Fahrzeug, Fehler, Zaehler (Leistung, Zaehlerstand), Ladestrom als Ring mit Vorgabe-Tasten, letzte Ereignisse; Status: Verlauf, Ladestrom als Wertetabelle, Fehler mit Quittierung, Zaehlerwerte mit Fehlerzaehlern, Ereignisprotokoll, Ladelogik; Einstellungen: Laden (Parameter mit Bereich, Default, Herkunftsfeld, Hilfetext), Netzwerk, MQTT, Zaehler (je mit Zustand, Passwortfeld nur schreibbar), Zugang; System: Firmware, Laufzeit, Resetgrund, letztes Update, Neustart, Firmware-Update (Vorpruefung der Datei, Ablauf mit Fortschritt) | SOLL-API-31/33, SOLL-ERR-10/11, SOLL-MTR-32, SOLL-MQTT-32 |
| [history.c](../lib/api/history.c), [history_task.c](../app/src/history_task.c) | Verlauf: Mittelwerte je 10 min im Ringpuffer (RAM), Ausgabe in Teilen; Abtastung je Sekunde auf der Systemworkqueue | SOLL-API-33 |
| [hw_sim.c](../app/src/hw_sim.c), [sim_host.c](../app/src/sim_host.c) | nur `native_sim`: Hardwaremodell mit simuliertem Zaehler hinter `meter_bus.h` und Neustart per `execv` ([Test und Simulation](test-und-simulation.md)) | – |

Abbild: Der Build haengt an `zephyr.bin` 16 Byte an (Kennung, Laenge, CRC32, [image-trailer.py](../scripts/image-trailer.py),
SOLL-BOOT-35) und bricht ueber 320 KB ab. **Nur `zephyr.bin` flashen**; `.hex`/`.elf` haben den
Anhang nicht, Wallfree sperrt dann das Laden.

Am Build geprueft: Systemtakt 100 MHz, Ladeadresse `0x08010000`, Interrupttabelle (TIM1_UP ->
`tim1_up_isr`, DMA2_Stream0 -> `dma2_s0_isr`, EXTI0/3/4/9_5 -> GPIO-Treiber, ETH ->
Ethernet-Treiber, UART4 -> UART-Treiber). CCR = ARR haelt den Ausgang im Center-aligned-Modus dauerhaft auf 1 (CP
+12 V DC, RM0033 Abb. 98).

## Festlegungen

Punkte, die die Spezifikation offen liess. Alle vom Nutzer bestaetigt, ausser wo
"zu bestaetigen" steht.

### Ladelogik

| Punkt | Umsetzung | Grund |
| --- | --- | --- |
| ERR-CONFIG bei D6 ohne LD-Eingang | nur, wenn **D4 und D6** an sind | ohne D4 wird LD nie ausgewertet; ein Fehler wuerde das Laden grundlos sperren |
| CP-Status-Entprellung | Erzwingung durch Fehler mit 20 ms entprellt; F (Diode, Schuetz, Verfuegbarkeit) und E (CP ungueltig) sofort | SOLL-CP-08 |
| RCM-Selbsttest | ERR_OUT fuehrt waehrend des **ganzen** Tests zum Fehlschlag (1.27 prueft nur bei 0/100 ms) | "jede Abweichung" (SOLL-PROT-10) |
| Abbruch des RCM-Selbsttests nach dem Testimpuls | Zustand "Abbruch laeuft" bis 3,4 s nach Teststart: SWITCH_1/2 ausgeblendet, ERR_OUT wirksam; kein neuer Test, kein Weg zum Schuetz in dieser Zeit | der Sensor faehrt seinen Trip-Test zu Ende; dessen Ausloesungen wuerden sonst als echter Fehlerstrom gewertet |
| Code 42 "Suspended EV" | S5 mit Status B, laufender PWM und vorherigem Laden im selben Steckvorgang | Bedeutung im Original nur aus dem 1.27-Webtext |
| Code 38 Nachlauf | aktiv bei Code-19-Eingang und Status A/B oder < 1 s nach Schuetz-Aus | Ist-Spez. §12 |
| S9 bei CP A mit Schutzfehler | Entriegeln ist trotzdem erlaubt (Schuetz aus), bis 4 Versuche | sonst bliebe der Stecker z. B. bei RCM-Sensorfehler verriegelt |
| Freigabe | verlangt **keinen** aktiven Fehler (strenger als das Original, das nur einzelne Fehler prueft) | SOLL-EN-02 |
| Entriegeln klemmt | nach 4 Fehlversuchen bleiben Zaehler und ERR-UNLOCK bis zum naechsten Fahrzeug bzw. Neustart; keine weiteren Impulse; der misslungene Start-Entriegelungsversuch zaehlt mit | sonst Dauerversuche am blockierten Motor alle paar Sekunden (das Original versucht es endlos) |
| D9 ohne Verriegelungswunsch | S1 geht mit D9 nur bei ML = 1 nach S2 | sonst Pendeln S1 <-> S2 in jedem Zyklus |
| Ohne D4 bei CP A | S1 setzt ohne D4 und ohne D9 sofort UNLOCKED | SOLL-LOCK-01/23: sonst blieb `lock_state` nach dem Abstecken LOCKED (Connect-LED, evcc, Ausgangscodes falsch); Regression in `test_basic_charge_cycle` |

### Parameter

| Punkt | Umsetzung | Grund |
| --- | --- | --- |
| `external_current_default` | ab Werk 80000 = keine Einschraenkung (F12) | Ohne Zutun soll die Box laden. Ein Startwert 0 wurde erprobt und verworfen: Ohne evcc wuerde dann nie geladen. Folge: Nach einem Neustart waehrend des Ladens wirkt bis zur naechsten evcc-Aenderung das Maximum (evcc sendet seinen Wert nicht erneut). Wer evccs Wert streng einhalten will, setzt den Parameter auf 0; evcc setzt dann beim naechsten Statuswechsel sofort neu. Sollwert, Grenzen und wirksamer Strom sind getrennt sichtbar (SOLL-API-32) |
| Speicherkennungen | Feldnummer des Originals, wo vorhanden (z. B. 525), sonst 1001.. (Konfiguration) bzw. 2000.. (Status); gespeichert werden nur Konfigurationswerte | |
| Wirksamkeit | Konfigurationsaenderungen wirken sofort (`evse_apply_config()`); macht eine Aenderung die Konfiguration widerspruechlich (z. B. LD-Funktion entfernt bei D4+D6), gilt sofort ERR-CONFIG und ein laufendes Laden endet | |
| Sonderparameter | `lock_state` (465) loest beim Schreiben einen echten Zyklus aus, nur in S1; `dip_override` (802) nur in S1; `hardware_limit_override` (801) nur absenken, 0 hebt auf | F3 |

### Parameterspeicher

| Punkt | Umsetzung | Grund |
| --- | --- | --- |
| Loeschen | im Betrieb nur in S1 mit CP A, Schuetz aus, ohne Verriegelungszyklus und RCM-Selbsttest; S0 nur vor dem Start der Ladelogik | in S0 laufen Selbsttest und Start-Entriegelung, deren Zeitfolge der stehende Bus stoeren wuerde |
| Ablauf des Verdichtens | Ladelogik per Sperre angehalten, Thread-/Messueberwachung ausgesetzt, IWDG auf Maximum (~32 s); CPU steht bis zu 4 s. CP bleibt +12 V (TIM1 laeuft weiter), Schuetz aus; ein in dieser Zeit angestecktes Fahrzeug wird danach erkannt | SOLL-NVM-04, SOLL-RT-13 |
| Ausloeser des Verdichtens | ausstehende Werte, beschaedigter Rest oder weniger als 4 KB frei; Pruefung jede Sekunde im Speicher-Thread | 128 KB reichen fuer rund 10 000 Aenderungen |
| ungueltiger Bereich beim Start | sofort neu angelegt, Werkseinstellungen, Meldung im Log und in `evse nvm`; kein Fehlerbit der Ladelogik | SOLL-NVM-05 verlangt nur eine Meldung |
| Import aus Sektor 3 | 409/412 aus dem unteren Byte (im Original 1-Byte-Felder); jede Uebernahme ueber `evse_param_set`; ein fehlender Eintrag gilt als 0 (Shell: "im Original nicht gespeichert = 0"), auch bei 404 und einzelnen Oktetten; fehlen alle vier Oktette einer Adresse, bleibt der Wallfree-Wert; Netz: 404 (DHCP), 315..326 (IP/Maske/Gateway), alles oder nichts; Geraetename 310 wird nicht uebernommen (Nutzer 2026-10-26) | SOLL-NVM-10; Belegung aus `wallbe2/analysis/web/lan.html`; fehlend = 0: [wallbe2 Parameterspeicher](../../wallbe2/docs/parameterspeicher.md#firmware-081-box-t-schreiben-nur-bei-aenderung) |
| Werksreset per Shell | `evse factory-reset confirm`, nur im sicheren Zustand, danach Neustart | einfachster Weg zu konsistenten Defaults |
| Zugangsdaten | ein Eintrag (HA1 + Benutzer, Kennung 3001) | ein Stromausfall darf Benutzer und HA1 nie trennen |

### API und Anmeldung

| Punkt | Umsetzung | Grund |
| --- | --- | --- |
| Werkszugang | Benutzer `admin`, Passwort `wallfree`; bis zur Aenderung beantworten alle Ressourcen ausser `/credentials` und der Oberflaeche mit 403 (auch fuer evcc) | SOLL-API-07 nennt kein Passwort |
| Nonces | gelten bis zum Neustart (Geheimnis je Start), keine Ablaufzeit, keine Pruefung von `nc`; Nonce eines frueheren Starts -> 401 mit `stale=true` | evcc erneuert die Challenge nur nach einem 401; eine Ablaufzeit liesse den WebSocket-Aufbau (dort ist kein 401 moeglich) dauerhaft scheitern. Folge: mitgeschnittene Anfragen sind bis zum Neustart wiederholbar (ohne TLS ohnehin mitlesbar) |
| WebSocket-Anmeldung | Zephyr sendet 101 vor dem Anwendungs-Callback; ohne gueltige vorab gesendete Anmeldung wird die Verbindung ohne Daten geschlossen | Zephyr bietet keinen Haken vor dem Upgrade |
| Bremse | 5 freie Fehlversuche je Adresse, dann 10 s, verdoppelt bis 5 min, 429 mit `Retry-After`; nur falsches Passwort zaehlt (fehlende Anmeldung und veraltete Nonce nicht) | |
| `error_state` | 1 = sonstiger Fehler (z. B. ERR-LOCK, CP ungueltig), bei WARP unbelegt; Vorrang RCM > Schuetz > Schalter/Konfiguration | WARP kennt keinen allgemeinen Fehlercode |
| `contactor_state` | aus dem Schuetz-Soll (3/0); `contactor_error` 2 bzw. 6 bei ERR-CONTACTOR | keine Spannungsmessung vor/nach dem Schuetz |
| `allowed_charging_current` | 0 ohne PWM, auch im gleichnamigen Statuswert | Spezifikation 12.1 |
| `POST /evse/params` | jeder Wert einzeln (nicht atomar); Syntaxfehler -> nichts geschrieben | Zustandsregeln stecken in den Schreibfunktionen |
| Netz-/Systemparameter | wirken erst nach Neustart; `POST /system/restart` nur in S1 ohne Fahrzeug (sonst 409); widerspruechliche gespeicherte Adresse -> Start mit DHCP und Warnung | Adresswechsel im Betrieb wuerde laufende Verbindungen (evcc) kappen; lieber erreichbar als gar kein Netz |
| Telnet aus | `shell_stop`, danach den Socket-Dienst des Telnet-Servers mit `net_socket_service_close` abmelden und seine Sockets schliessen: Port 23 ist zu. Den Dienst erkennt netcfg an seinem Listen-Socket (lokaler Port = Telnet-Port, vorher steht im Kontext fd 0); bis der Shell-Thread den Port geoeffnet hat, alle 250 ms erneut, hoechstens 10 s. E2E `test_03_telnet_aus_port_zu` | Nutzer 2026-10-28: Port zu statt nur Kommandos verweigern. Zephyr startet Telnet vor `main()` ohne Schalter; der Weg ueber die oeffentliche Socket-Dienst-Schnittstelle kommt ohne Aenderung an Zephyr aus |
| Telnet-Optionen | `CONFIG_SHELL_TELNET_SUPPORT_COMMAND` (in Zephyr als experimentell markiert): Echo und Zeichenmodus werden auf Anfrage des Clients ausgehandelt; mit `mode character` funktionieren Historie und Zeilenbearbeitung. Der Server bietet den Modus nicht von sich aus an. Kosten 0,3 KB Flash | Nutzerwunsch 2026-10-20 (Pfeil hoch fuer den letzten Befehl) |

### Ereignisse und Ereignisprotokoll

| Punkt | Umsetzung | Grund |
| --- | --- | --- |
| Ereignisformat (SOLL-ERR-10) | WebSocket-Topic `evse/event`, je Ereignis ein Rahmen, Payload `{"seq", "t_ms", "type", ...}`: `state` (`from`/`to` = S0..S10), `error` (`set`/`cleared` als Namen wie `/evse/errors`), `lock` (`lock_state` 1/3/5, nur Endzustaende), `contactor` (`on`, Schuetz-Soll), `boot` (`reason`, `detail`, bei Ausnahme `pc`, `fault`; SOLL-ERR-12); `seq` fortlaufend ab 1 je Start, Luecke = verloren (Ringpuffer 16); kein Nachsenden beim Verbinden; jedes Ereignis zusaetzlich als Logzeile `Ereignis {...}` | Spezifikation nennt kein Format; evcc ignoriert unbekannte Topics |
| Protokolleintrag (SOLL-ERR-11) | JSON `{"seq", "t_ms", "boot", "wall_ms"?, "kind", ...}`, Antwort `{"boot": <laufender Start>, "entries": [...]}`; `kind` = `auth_fail` mit `addr` (IPv4 als Text, `0.0.0.0` unbekannt) und `count`, `event` mit der Payload von `evse/event` oder `cleared` mit `addr`; `t_ms` = ms seit dem Start `boot`, `wall_ms` nur mit bekannter Uhrzeit; beim ersten Zeitabgleich erhalten die Eintraege des laufenden Starts sie nachtraeglich (Nutzer 2026-10-27), Eintraege frueherer Starts ohne Uhrzeit zeigt die Oberflaeche als "unbekannte Zeit"; kein Freitext, Benutzername wird nicht aufgezeichnet | maschinenlesbar wie die uebrige API; ohne Nachtragen fehlte nach jedem Neustart die Uhrzeit der ersten Sekunden (Start, Entriegelung, S0 -> S1) |
| Fehlversuche | jede mitgesendete, aber abgelehnte Anmeldung (falsches Passwort, unbekannter Benutzer, fehlerhafte Kopfzeile), auch beim WebSocket-Aufbau; nicht: fehlende Anmeldung, veraltete Nonce, waehrend der Sperre abgewiesene Anfragen. Direkt aufeinanderfolgende Fehlversuche derselben Adresse zaehlen im juengsten Eintrag weiter (`count`, `t_ms` = letzter Versuch) | Durchprobieren soll die Ereignisse nicht aus dem Puffer draengen |
| Puffer | 48 Eintraege im Backup-SRAM (3,5 von 4 KB, entlastet den RAM um 1,8 KB); Fehlversuche und Ereignisse teilen sich den Puffer; aeltester Eintrag faellt heraus, Luecke in `seq` zeigt Verlust; `seq` und Startnummer laufen ueber Neustarts weiter. Beim Start uebernommen, wenn Kennung, Version und Nummern stimmen, sonst neu (Stromausfall). Fehlversuche verschiedener Starts werden nicht zusammengefasst. Simulation: Inhalt per Umgebungsvariable ueber den Neustart (execv), nicht als Datei, damit Beenden wie ein Stromausfall wirkt; bestaetigt (Nutzer 2026-10-27) | Nutzer 2026-10-26 (Ereignishistorie); Backup-SRAM nutzen weder Bootloader noch Originalfirmware (keine Zugriffe im Code) |
| Ausloeser des Resets | BKP18 = `"WD"` + Detail + Zusatz, BKP17 = PC bei Ausnahme; beim Start gelesen und geloescht; `reset_detail` in `/system/info`, Startereignis `boot` | SOLL-RT-14 |
| Abruf | `GET /system/log` mit Digest, im Werkszugang 403; liefert alle vorhandenen Eintraege, aelteste zuerst, in Teilen (kein Abschneiden); der Abruf selbst wird nicht protokolliert | SOLL-API-07 |
| Loeschen (SOLL-ERR-13) | `POST /system/log/clear`, im Werkszugang 403; erster neuer Eintrag `{"kind":"cleared","addr"}`, Nummern laufen weiter; Formatversion 2 (beim Update von Version 1 beginnt das Ereignisprotokoll einmal neu); die Weboberflaeche blendet live empfangene Ereignisse von vor dem Loeschen aus, bestaetigt (Nutzer 2026-10-27) | Loeschen soll nachvollziehbar bleiben: wer das Passwort kennt, kann seine Fehlversuche nicht spurlos entfernen |
| Absender beim WebSocket | aus Zephyr-Interna ermittelt (Kopfzeilenfeld des Client-Kontexts, Zephyr 4.4.2); bei Zephyr-Updates pruefen | der WebSocket-Callback liefert keinen TCP-Socket |

### Bedienung und System

| Punkt | Umsetzung | Grund |
| --- | --- | --- |
| Neustart | nur geordnet: `evse_request_shutdown` (wirkt wie fehlende Verfuegbarkeit -> S7 mit 5-s-Frist -> S10), warten bis Schuetz aus, hoechstens 15 s, dann sicherer Zustand und Reset; gilt fuer Shell, HTTP und Werksreset. `kernel reboot` ist entfernt | SOLL-RT-22, SOLL-SH-20 |
| Versionsnummer | Hauptversion.Nebenversion ohne Patch aus der Datei `VERSION`; `0.x`, bis das Laden auf der Hardware nachgewiesen ist, `1.0` nach Stufe C; Zwischenstaende `0.2-dev`. Veroeffentlichung = Git-Tag `v0.x` auf dem Commit, danach `VERSION` auf die naechste `-dev`. Build = Commit (`git describe --exclude=*`, bei Aenderungen `-dirty`). Kennung im Abbild `WALLFREE-IMAGE:0.1+<commit>`; `/system/info`: `firmware` = Version, `build` = Build; Log, Shell und Home Assistant zeigen `0.1+<commit>`. Weboberflaeche: "Wallfree 0.1", darunter klein "Build <commit>"; Vorpruefung des Updates "Wallfree 0.1 (Build <commit>)", aeltere Abbilder ohne Version "Wallfree (Build <commit>)". `scripts/build-image.sh`: `wallfree-0.1.bin` nur bei sauberem Stand mit Tag `v0.1` auf HEAD, sonst `wallfree-<version>-<commit>[-dirty].bin`; beim Aufraeumen bleiben die 3 neuesten, veroeffentlichte immer (aus dem Tag nachgebaut bitgleich, `v0.1` geprueft) | Nutzer 2026-10-27; ausgeschriebener Name statt "WF"; "Build" statt "Bau", weil im Deutschen gelaeufiger; ein Dateiname gehoert so immer zu genau einem Stand |
| Texte in JSON und MQTT | UTF-8 mit Umlauten (Hilfetexte, Fehlermeldungen, Ruecksetzbedingungen, Home-Assistant-Namen); nicht auf ASCII umgestellt (Nutzer 2026-10-27) | so getestet und fuer Endkunden lesbar; Umschreiben (ae/oe/ue) haette die gekuerzte Home-Assistant-Vorlage nicht verhindert (gleiche Bytezahl), die Ursache war die stille Kuerzung |
| Abbild ungueltig (SOLL-BOOT-35) | ERR-INTERNAL beim Start (Klasse P, kein Laden, loescht sich nur durch Neustart mit gueltigem Abbild), Logmeldung, `image.ok = false`; CRC mit `nvm_crc32` (bitweise, etwa 150 ms beim Start, vor dem IWDG von Wallfree) bestaetigt (Nutzer 2026-10-26) | SOLL-BOOT-35 |
| Update-Uebergabe | wie der Neustart geordnet (Ladeende, hoechstens 15 s), dann BKP10..15 und Reset statt BKP19-Reset allein; IP/Maske der aktuellen Adresse, ohne Adresse 0 (Bootloader: 192.168.0.8); eine zweite Anforderung waehrend Neustart/Update -> 409 bzw. Meldung, bestaetigt (Nutzer 2026-10-26) | SOLL-BOOT-30/31 |
| Update in der Weboberflaeche | Vorpruefung: hoechstens 320 KB (Sektor 4..6), Start-SP wie die Bootloader-Pruefung, Reset-Vektor ungerade in `0x08010000..0x0806FFFF`, Kennung `WALLFREE-IMAGE:` mit Version. Wer antwortet, zeigt `GET /system/info` ohne eigene Auswertung des Bootloader-Textes: JSON = Wallfree (auch 401), sonst HTML = Bootloader. Fristen: bis 60 s auf den Bootloader, Upload 30 s (wie der Bootloader), bis 180 s auf Wallfree; die Antwort des Bootloaders auf den Upload wird nicht ausgewertet (er startet 1 s danach neu). Erfolg = `update_result.ok`, `image.ok` und `image.crc` gleich dem Anhang der Datei; eine Datei mit Wallfree-Kennung ohne gueltigen Anhang wird abgelehnt. Ab der Uebergabe bis Wallfree zurueck stellt die Seite keine Anfragen ausser denen des Ablaufs (auch kein WebSocket). Fremdes Abbild: nach dem Upload Ende ohne Ergebnis; bestaetigt (Nutzer 2026-10-26) | SOLL-BOOT-32/35 |
| Log vor Neustart | Meldung, dann 200 ms fuer den Log-Thread, dann Reset (bei Fristablauf Schuetz sofort per Register aus); kein `LOG_PANIC` in Thread-Neustartpfaden; Meldungen der Ueberwachung koennen verloren gehen (Grund steht in BKP19) | `LOG_PANIC` blockiert mit verbundenem Telnet-Client die Systemworkqueue (Fallstricke) |
| Quittieren | `evse errors clear` / `POST /evse/errors/ack` quittiert nur ERR-CONTACTOR (F14); die Ausgabe nennt fuer alle anderen die Ruecksetzbedingung | SOLL-ERR-03 |
| Selbsttest auf Anforderung | `evse selftest` nur in S1, CP A, Schuetz aus | SOLL-SH-07 |
| Fehlerhistorie | 32 Ereignisse im RAM (Zeit, neu, geloescht, Zustand), geht beim Neustart verloren; HTTP gibt so viele aus, wie in 1,5 KB passen (`truncated`) | SOLL-ERR-01 |
| IWDG | startet zu Beginn von `main` (250 ms); bis zum Schutzthread bedient ihn `main`, auch waehrend des Werksreset-Wartens | SOLL-BOOT-03, SOLL-RT-12 |
| Resetgrund | BKP19 = `0x5746xxxx` vor Software-Resets; Gruende `supervision_limit`, `fault`, `user_restart`, `factory_reset`, `interface_hang` bzw. aus RCC_CSR | SOLL-RT-14 |
| Thread-Prioritaeten | Schutzthread 0, Automatenthread 2, Ethernet-RX 4, Netz-RX/TX, TCP-Arbeit und System-Workqueue 5, WebSocket 11, Speicher 12, HTTP-Server, net_mgmt, Shell, Log, Zaehler, MQTT 14; alle preemptiv (`CONFIG_NET_TC_THREAD_PREEMPTIVE`, Ethernet-RX dadurch preemptiv, in Zephyr als experimentell markiert), bestaetigt (Nutzer 2026-10-26) | SOLL-RT-07; mit Zephyrs Vorgabe (kooperativ) liess Netzlast den Automatenthread warten (Fallstricke) |
| Schnittstellen-Ueberwachung | Speicher-, WebSocket-Thread, System-Workqueue (10 s); HTTP-Server und Telnet gehoeren zu Zephyr und sind nicht ueberwachbar | SOLL-RT-11 |
| Netzwerk-LED | PC2 aktiv Low, treibt die gruene LED an der Netzwerkbuchse (Testgerät 2026-10-27: blinkt ohne Kabel mit 1 Hz, mit Verbindung Dauerlicht; die gelbe (PHY, Link) ist ohne Kabel aus); "verbunden" = Link (Carrier) und bevorzugte IPv4-Adresse; Takt aus der Systemworkqueue (haengt diese, steht die LED) | SOLL-LED-06 |

### Weboberflaeche

| Punkt | Umsetzung | Grund |
| --- | --- | --- |
| Anmeldung | Oberflaeche hinter Digest wie die API (Anmeldedialog des Browsers); im Werkszugang nur die Zugangsdaten-Seite; nach der Aenderung fragt der Browser neu | SOLL-API-07 |
| Bezeichnungen | Parameter erscheinen mit ihrem Hilfetext, der interne Name (wie in API und Shell) nur als Tooltip; Auswahllisten zeigen den Hilfetext bis zum Doppelpunkt; ohne Verweise auf Entscheidungsnummern | Nutzer 2026-10-21: interne Namen und F-Nummern stoeren in der Oberflaeche |
| Zugangsdaten aendern | Benutzerfeld mit dem aktuellen Benutzer vorbelegt (`GET /credentials` liefert `user`); Hinweis: ohne HTTPS mitlesbar, bei der Anmeldung geht nur ein Pruefwert ueber das Netz, beim Aendern das neue Passwort lesbar | Nutzer 2026-10-21 |
| Live-Daten | WebSocket `/ws`; schliesst der Server sofort (Browser schickte keine Anmeldung), Abfrage alle 2 s und alle 30 s ein neuer WebSocket-Versuch; nach einer laengeren Verbindung neuer Versuch nach 3 s. Chrome sendet die Digest-Anmeldung beim WebSocket-Aufbau mit; andere Browser ungeprueft | |
| Verbindungsanzeige | Punkt am Logo (gruen/rot/orange, Text beim Zeigen), bei "getrennt" und waehrend des Updates eine Hinweisleiste in der Kopfzeile (Nutzer 2026-10-27, Variante A von vier Entwuerfen); "verbunden", sobald eine Nachricht oder Abfrage ankommt; 5 s ohne Daten "getrennt": `main.offline` grau, Klicks gesperrt; ein offener, aber stummer WebSocket (Kabel gezogen) wird dann verworfen. Grundlage ist das Lebenszeichen `evse/state` alle 2 s (SOLL-API-06). Abfragen der Live-Daten mit 4 s Zeitlimit. Periodische Nachlader (Parameter, Fehler) zeigen Verbindungsfehler nicht selbst an. Art der Verbindung (WebSocket/Abfrage) nur als Tooltip | Nutzer 2026-10-21: "Abfrage" war nicht greifbar und blieb bei getrennter Steuerung stehen |
| Verbindungsplaetze | alle Abfragen eines Tabs nacheinander (eine Warteschlange, Zeitlimit 10 s je Anfrage), damit der Browser nur eine HTTP-Verbindung oeffnet; ein verborgener Tab (`document.hidden`) schliesst seinen WebSocket und fragt nichts ab, sichtbar sofort neu verbinden und nachladen; bestaetigt (Nutzer 2026-10-28). Auf dem Server schliesst Zephyr ruhende Verbindungen nach 5 s (`HTTP_SERVER_CLIENT_INACTIVITY_TIMEOUT`, Vorgabe 10 s), bestaetigt (Nutzer 2026-10-28) | Die Steuerung hat 4 HTTP-Plaetze (`HTTP_SERVER_MAX_CLIENTS`), jede Verbindung haelt ihren bis zum Ablauf der Frist. Die Karten von Status luden parallel, der Browser oeffnete dafuer 6 Verbindungen (Browser-Durchlauf); gedrosselte Hintergrund-Tabs wachen im Minutentakt zugleich auf. Mit 10 s wies das Testgerät neue Verbindungen trotzdem selten kurz ab (Pruefungen auf dem Testgerät), vermutlich wegen einer vorab geoeffneten, unbenutzten Verbindung von Chrome. 5 s ist ein gaengiger Wert, den andere Server ab Werk nutzen (Apache 2.4 `KeepAliveTimeout`, lighttpd, Node.js `keepAliveTimeout`); kuerzer bringt wenig, weil der Browser nach einer Pause ohnehin neu verbindet. Der WebSocket belegt keinen HTTP-Platz (Zephyr gibt ihn nach dem Upgrade frei) |
| Expertenansicht | ab Werk einfache Ansicht: Status nur Verlauf, Fehler, Ereignisprotokoll; Einstellungen ohne Reiter "Laden". Kaestchen "Expertenansicht" auf der Seite System schaltet Ladestrom-Tabelle, CP-Oszilloskop, Zaehlerrohwerte, Ladelogik und "Laden" zu. Gespeichert im `localStorage` des Browsers (`wallfree.expert`), ohne ihn einfach; nur Anzeige, API unveraendert | Nutzer 2026-10-27: fuer Menschen, die sich nicht mit den Parametern beschaeftigen wollen; im Browser, weil es eine Anzeigevorliebe ist (kein Flash-Schreibzugriff, kein Einfluss auf das Laden) |
| Lizenzhinweis | Karte "Lizenz" unter System: Apache-2.0, ohne Gewaehrleistung, Link auf `/lizenzen.txt` (`web/lizenzen.txt`, gzip etwa 1,8 KB): Liste der Fremdsoftware im Abbild mit Lizenz, Volltexte MIT (Preact) und BSD-3-Clause (ST); Apache- und picolibc-Text nur als Verweis (Flash). Urheberzeile "Copyright 2026 Sebastian Balling" (Nutzer 2026-10-27) | MIT und BSD verlangen den Hinweis bei Weitergabe des Binaers; Nutzer 2026-10-27 |
| Favicon | Blitz weiss auf Tuerkis (#009670, abgerundetes Quadrat), SVG als `data:`-URL in `index.html` (etwa 200 Byte) | Nutzer 2026-10-22 (Variante 2 von 5); keine eigene Datei im Flash, keine Anfrage an die Steuerung, keine vergebliche Anfrage nach `/favicon.ico` |
| Kopf | Blitz-Symbol (wie Favicon, Inline-SVG, Farbe `--logo-free`, etwa 200 Byte) vor der Wortmarke (Nutzer 2026-10-24); Wortmarke des Nutzers als Text statt SVG, ohne Hintergrund (Farbe der Kopfzeile); "Wall" in der Textfarbe, "free" tuerkisgruen #009670 hell (Kontrast 3,8:1) bzw. #1fd1a0 dunkel; fett kursiv, eng; Link zur Uebersicht | Nutzer 2026-10-21: Farben ans Design angepasst; das Original-Gruen #00b88a war auf Weiss zu blass (2,6:1); Tuerkis bleibt vom Statusgruen unterscheidbar; Text skaliert mit dem Kopf und kostet keinen Flash |
| Gliederung | Leiste oben mit Uebersicht, Status (bis 2026-10-27 "Auswertung", davor "Insights"), Einstellungen, System. Status: Verlauf, Verbindungen (Netzwerk, Uhrzeit, MQTT, Zaehler, Prozessortemperatur; nacheinander abgefragt, alle 10 s), Fehler, Ereignisprotokoll, in der Expertenansicht Ladestrom, CP-Oszilloskop, Zaehler, Ladelogik. Einstellungen nur mit Parametern, nach dem Speichern Verweis auf Status → Verbindungen; Uhrzeit nicht mehr unter System (Nutzer 2026-10-27: Zustand gehoert nicht in die Einstellungen, Verlauf ganz oben); auf dem Handy volle Breite unter dem Titel. Einstellungen mit Unterseiten als Reiter. Adresse `#/<bereich>/<unterseite>`, unbekannt = Uebersicht bzw. Laden. Netzwerk: `net_*`, `telnet_enable`; MQTT: `mqtt_*`; Zaehler: `meter_*`. Nach dem Speichern von Werten, die erst nach einem Neustart wirken, steht der Hinweis mit "Jetzt neu starten" auf der Seite | Nutzer 2026-10-21 (F31) |
| Verlauf | je 10 min Mittelwert der Wirkleistung (W, nur gueltige Zaehlerwerte; Einspeisung = 0; ohne Zaehler `null`), Mittelwert des erlaubten Stroms (Tastgrad, 0,25 A) und Zustand als ODER (angesteckt, laedt, Fehler); 288 Zeilen = 48 h, 4 Byte je Zeile; Intervalle ab Start der Steuerung, Intervalle ohne Abtastung als leere Zeilen. `GET /evse/history` in Teilen; `age_ms` und das laufende Intervall stehen am Ende, damit sie zu den Zeilen passen. Anzeige: je Groesse ein Diagramm mit eigener Achse (Leistung nur mit Zaehler), Stufen je Intervall, Zustandsband darunter, Fenster mindestens 6 h, Werte nur beim Zeigen/Tippen als Hinweis an der Kurve (naechstgelegenes Intervall, damit auch schmale treffbar sind), keine feste Ablesezeile (Nutzer 2026-10-21: zu viel), neu geladen jede Minute | Nutzer 2026-10-21: Leistung, erlaubter Strom, Zustand, 10 min, 48 h; Darstellung mit zwei Diagrammen vorerst beibehalten (Alternative: mit Zaehler nur Leistung, ohne nur Strom; eine Linie "moegliche Leistung" scheidet aus, weil die Phasenzahl des Fahrzeugs unbekannt ist); Mittel statt Momentanwert, damit kurze Spitzen nicht verzerren; zwei Diagramme statt zweier y-Achsen |
| Ladestrom-Anzeige (Uebersicht) | Ring ueber 270 Grad von 0 bis zum Maximum (Drehschalter bzw. abgesenkt); blau "Erlaubt" (Strom des Tastgrads), gruener Innenring "Messwert" (Zaehler, hoechste Phase, nur wenn > 0), orange Marke fuer Kabel/Eingang, wenn kleiner als das Maximum; Kreis = Vorgabe (rot bei 0, am Ende, wenn ueber dem Maximum). Mitte: Messwert, ohne Zaehler der erlaubte Strom; darunter Leistung und "erlaubt". Darunter eine Zeile, was begrenzt ("begrenzt durch Kabel (20 A)", "Vorgabe: Laden aus", "Laden gesperrt", 5-%-Modus) | Nutzer 2026-10-21; Begriffe Erlaubt/Vorgabe/Messwert, weil "Freigabe" schon die Ladeerlaubnis (EN, Schnittstelle) bezeichnet |
| Vorgabe setzen | Tasten − / 6 / 10 / 16 / 32 A / + / "Laden aus", wirken sofort; −/+ in 1-A-Schritten zwischen 6 A und dem Maximum; Tasten ueber dem Maximum entfallen; nicht am Ring ziehbar. Hinweis: eine externe Vorgabe (z. B. Energiemanagement) kann den Wert ueberschreiben | Nutzer 2026-10-21: am Ring ziehen ist auf dem Handy fummelig |
| Ladestrom (Status) | Tabelle Vorgabe, Maximum Drehschalter, Kabel, "Stromgrenze ueber Digitaleingang" (Grenze `input`, Funktionscodes 8..15; "Eingang" allein war unverstaendlich; in der Zeile "begrenzt durch ..." kurz "Digitaleingang"), Erlaubt, Messwert; wirkende Grenze orange mit "begrenzt"; keine Balken | Nutzer 2026-10-21 |
| Zaehleranzeige | Karte "Zaehler" in der Uebersicht immer, ohne Zaehler leer (Nutzer 2026-10-21: Anordnung bleibt gleich); mit Zaehler Leistung in kW, Zaehlerstand in kWh, Zustand; ungueltige Werte als "–"/"ungueltig"; Status: Tabelle aller Messwerte, Fehlerzaehler; Einstellungen: `meter_type` und `meter_framing` als Auswahlliste | SOLL-MTR-32; Nutzer 2026-10-23 bestaetigt |
| Ereignisse | Uebersicht: letzte 8 Ereignisse; Status: ganzes Ereignisprotokoll mit Fehlversuchen und Taste "Protokoll loeschen". Vorgeschichte aus `GET /system/log` beim Oeffnen, danach live ueber `evse/event` (ohne WebSocket alle 5 s neu); Uhrzeit = Startzeitpunkt + `t_ms` (Hinweis unter dem Protokoll: "Uhrzeiten auf wenige Sekunden genau"); Startzeitpunkt = Browserzeit zur Mitte der Anfrage - `uptime_ms` aus `/system/info`, bei jedem Abruf neu (mindestens jede Minute), damit ein lange offener Tab nicht mit dem Gang des Quarzes auseinanderlaeuft. Verlauf entsprechend: `age_ms` gilt zum Abrufzeitpunkt. Genauigkeit: Netzlaufzeit (ms) plus Quarzabweichung mal Alter des Eintrags (bei 20–50 ppm nach 48 h etwa 3–9 s) | keine Uhrzeit auf der Steuerung (F32) |

### Energiezaehler

| Punkt | Umsetzung | Grund |
| --- | --- | --- |
| Werte intern | ganze Zahlen in Tausendsteln der WARP-Einheit (mV, mA, mW, mHz, Wh), JSON mit 3 Nachkommastellen | keine Gleitkommazahlen ausserhalb der Dekodierung |
| Float32 der Eastron-Zaehler | ganzzahlig aus Mantisse und Exponent dekodiert, gerundet | `float` -> `int64` zieht auf dem Cortex-M3 die Double-Routinen nach (3,5 KB, Fallstricke) |
| Zyklus | 1 s von Beginn zu Beginn, im Zustand `error` 10 s; Ruhepause 50 ms vor jeder Anfrage, nach einem Timeout zusaetzlich 500 ms; je Block 3 Versuche, auch nach einer Ausnahmeantwort | SOLL-MTR-10/11 (Nutzer 2026-10-21) |
| Parameter | bei den Systemparametern (`/system/config`, `evse sys set`), Kennungen 3016..3019, wirken nach Neustart; Werkszustand `meter_type` = 0 | SOLL-MTR-01/02 |
| Ereignis | Typ `meter` mit `status` (`off`, `connecting`, `ok`, `error`), bei gesunkenem Zaehlerstand zusaetzlich `energy_decreased`: true | SOLL-MTR-13/14 |
| Ungueltige Werte an evcc | `null` wie WARP; evcc 0.317.2 macht daraus 0: bei ausgefallenem Zaehler zeigt evcc waehrend des Ladens 0 W und 0 A, der Zaehlerstand bleibt auf dem letzten Wert | Nutzer 2026-10-23 bestaetigt: WARP-konform; die 0 laesst evcc bei der Ueberschussregelung eher herunterregeln (Fehler zur sicheren Seite), waehrend ein Weitersenden alter Werte den Ausfall verbergen und zu hohen Ueberschuss vortaeuschen koennte; ein Abmelden des Zaehlers wirkt nicht, weil evcc `/info/features` nur beim Verbinden liest |
| UART4 ohne Zaehler | der UART-Treiber richtet UART4 beim Start ein; ohne Zaehler wird nie gesendet, PC12 bleibt Low | Zephyr initialisiert Devicetree-Geraete immer; ohne Sendefreigabe wirkt nichts auf den Bus |

### Uhrzeit

| Punkt | Umsetzung | Grund |
| --- | --- | --- |
| Parameter | `ntp_enable` (3028, Werk an), `ntp_server` (3029, Werk leer), beide sofort wirksam | Nutzer 2026-10-26 |
| Fehlschlag | Logzeile (Warnung) je Versuch, kein Ereignis, keine Fehlerklasse; Uhrzeit bleibt beim letzten Erfolg (nach 6 h neu), bestaetigt (Nutzer 2026-10-27) | SOLL-TIME-01 |
| Aufloesung | `dns_get_addr_info` statt `getaddrinfo` (das braucht Heap, `k_calloc`); IPv4-Literal ohne DNS; feste Adresse: Gateway als DNS-Server | System-Heap nur 512 B |
| Kosten | +11,3 KB Flash, +6,4 KB RAM (Stack 2 KB, DNS-Kontext, Sockets; `ZVFS_OPEN_MAX` 18) | |
| Simulation | `pool.ntp.org` nicht aufloesbar (kein DNS-Server); Test gegen eigenen SNTP-Server auf 127.0.0.1 (T7Zeit) | |

### MQTT

| Punkt | Umsetzung | Grund |
| --- | --- | --- |
| Zeiten | Takt 100 ms; Keepalive 60 s; CONNACK-Frist 10 s; Neuverbindung nach 1 s, verdoppelt bis 60 s | wie der WebSocket-Takt; Keepalive ist der Zephyr-Default; 60 s Hoechstwartezeit begrenzt die Last bei dauerhaft fehlendem Broker |
| toter Broker | Zephyr wertet unbeantwortete PINGREQ nicht aus: Die Anwendung trennt, wenn ein PINGREQ eine Keepalive-Spanne lang offen ist; Erkennung also 60..120 s nach der letzten Nachricht (Testgerät: 121 s). Zephyr pingt nur in Sendepausen; bei Dauerversand (Zaehlerwerte jede Sekunde) erkennt erst TCP den Ausfall (Wiederholungen bis zum Abbruch) | Testgerät ohne Zaehler bestaetigt; Dauerversand mit Zaehler noch zu messen |
| Aenderungserkennung | FNV-1a-Pruefsumme der Nutzdaten statt einer Kopie je Topic | spart RAM; eine Kollision (2^-32 je Aenderung) verschiebt eine Meldung bis zur naechsten Aenderung |
| Neukonfiguration | nur wenn sich ein `mqtt_*`-Wert aendert: `online` = `false`, DISCONNECT, sofort neu verbinden | ein geordnetes DISCONNECT loest den Last Will nicht aus |
| Neustart der Steuerung | kein `online` = `false` vorab; der Broker meldet den Last Will nach 1,5 x Keepalive (bis 90 s) | der Neustart beendet die Verbindung ohne TCP-Abbau |
| Anmeldung am Broker | Client-ID = Hostname; Passwort nur zusammen mit einem Benutzer | MQTT 3.1.1 erlaubt kein Passwort ohne Benutzername. Zwei Steuerungen mit gleichem Hostname wuerden sich am Broker gegenseitig trennen |
| Praefix | leer = `wallfree/<MAC-Ende>` (in der Simulation ohne MAC `wallfree/000000`) | wie WARP `warp2/<uid>` |
| Retain bei Befehlen | eine mit Retain veroeffentlichte Nachricht kommt bei bestehendem Abo ohne Retain-Kennung an (MQTT-3.3.1-9) und wird angenommen; ignoriert wird die gespeicherte Kopie, die der Broker beim Abonnieren ausliefert | genau dieser Fall wuerde nach jedem Neustart einen alten Sollwert setzen (SOLL-MQTT-22) |
| Ereignisse | vor dem Verbinden entstandene werden verworfen | SOLL-MQTT-11 |
| Broker-Adresse | Pruefung wie Gateway, zusaetzlich 127.x erlaubt | PC-Simulation mit Broker auf demselben Rechner; auf der Steuerung schlaegt die Verbindung nur fehl |
| Home-Assistant-Erkennung | Konfigurationen aus [ha.c](../lib/api/ha.c) beim Verbinden (nach `online`), Abkuerzungen (`stat_t`, `val_tpl`, `~` = Praefix), Knoten `wallfree_<MAC-Ende>` (Simulation ohne MAC: `000000`); Nutzdatenpuffer 640 B (groesste Konfiguration mit 64 Zeichen Praefix passt); Ladestrom-Zahlenfeld 0 bis Maximum des Drehschalters (ungueltige Stellung: 80 A), 1..5 A lehnt die Steuerung ab (Meldung im MQTT-Status); wie WARP (dort fest 0..32 A), bestaetigt (Nutzer 2026-10-27) | SOLL-MQTT-40..42; +2,3 KB Flash |
| Broker per Name | `mqtt_broker` Text (Kennung 3031) statt IPv4 (3021); alter Wert wird beim Laden uebernommen, solange 3031 fehlt; Aufloesung ueber [resolve.c](../app/src/resolve.c) wie der Zeitserver; Simulation ohne DNS nur Adressen | Nutzer 2026-10-27 (DNS jetzt vorhanden) |

## Parameterspeicher

Format in [nvm.h](../include/wallfree/nvm/nvm.h). Eintrag = Kopf (Kennung, Laenge, CRC-8) +
Daten + CRC-32; ein Wert mit 4 Byte belegt 12 Byte. Bereichskopf: Kennung direkt nach dem
Loeschen, Abschluss (Version) zuletzt; ohne die fruehe Kennung saehe jedes nach dem Loeschen
unterbrochene Verdichten wie ein Erststart aus. Getestetes Verhalten bei Stromausfall:

| Ausfall waehrend | Ergebnis beim naechsten Start |
| --- | --- |
| Anhaengen (jedes Byte) | alter oder neuer Wert, nie ein Mischwert; zerstoerter Kopf -> Rest gesperrt bis zum Verdichten, Werte davor bleiben |
| Loeschen beim Verdichten | Bereich ungueltig -> Werkseinstellungen, Meldung |
| Schreiben beim Verdichten | Bereich ungueltig (Kennung ohne Abschluss) -> Werkseinstellungen, Meldung; keine Teilergebnisse |
| genau zwischen Loeschende und erstem Byte | wie Erststart: Werkseinstellungen **ohne** Meldung (einziges unerkanntes Fenster) |
| erstes Schreiben auf leeren Bereich (Kopf ohne Abschluss) | "ungueltig" gemeldet, obwohl nichts verloren ist |

## Ressourcen

Stand 2026-10-27 (`v0.1`), `-Os`, Assert-Texte ohne Ausdruck und Meldung, System-Heap 512 B, ohne Netz-Shell, 12 Netz-Kontexte: **298 008 B Flash** (90,9 % von 320 KB), **118 780 B RAM** (90,6 %).
Reserven und Einsparmoeglichkeiten: [Architektur](architektur.md#5-speicherbudget).

| Anteil (gemessen beim Hinzufuegen) | Flash | RAM |
| --- | --- | --- |
| HTTP-/WebSocket-API (HTTP-Server 13,5 KB, WebSocket 2,6 KB, PSA-SHA-1 5,9 KB) | 37,8 KB | 22 KB (davon 4 Client-Kontexte 9,7 KB) |
| Weboberflaeche (gzip) | 12 KB | – |
| Parameterspeicher | 8,2 KB | 5,1 KB |
| Shell-Kommandos, Resetgrund, Fehlerhistorie | 6 KB | – |
| Netz-/Systemparameter, Bremse | 5,7 KB | – |
| Ereignisse, Netzwerk-LED, Ereignisprotokoll, `/evse/limits`, Ereignisanzeige | 5,1 KB | 3,9 KB |
| Ladelogik `lib/evse` (Code und Konstanten inkl. Parametertabelle mit Hilfetexten) | 15,7 KB | – |
| MQTT: Zephyr-Bibliothek (3.1.1, TCP) 4,2 KB, Thread und Anbindung, Parameter (8 Werte mit Hilfetext), `/mqtt/state`, Shell, Weboberflaeche | 9,1 KB | 5,1 KB (Stack 2 KB, Puffer 0,9 KB, Client, Status, Konfigurationskopien) |
| Verlauf (Ringpuffer 1,2 KB) und Gliederung der Weboberflaeche in Bereiche (gzip +2,9 KB) | 5,3 KB | 1,5 KB |
| Energiezaehler: UART-Treiber 2,1 KB, Zephyr-Modbus 2,7 KB, Interrupttabelle 1 KB, Zaehlermodul 1,4 KB, JSON/API/Shell/Parameter 2,9 KB, Weboberflaeche 1,4 KB, Rest (Treiberanteile) | 15,2 KB | 3,9 KB (Stack 1,3 KB, Modbus-Kontext 1 KB, Zaehlerzustand, WebSocket-Rahmen) |

Reserve rund **29 KB Flash und 12 KB RAM** (Uhrzeit, Home-Assistant-Erkennung und CP-Aufnahme seit der Aufstellung oben hinzugekommen). Der RAM ist bis auf die Stacks statisch belegt
(kein Wachstum zur Laufzeit); Stacks haben MPU-Schutz (Ueberlauf -> Fault -> sicherer Zustand),
ihre Auslastung auf dem Testgerät steht in der Tabelle unten (`kernel thread stacks`). Weitere
Einsparungen in der [Architektur](architektur.md#5-speicherbudget).

**Grenze 320 KB:** Die Anwendung darf hoechstens Sektor 4..6 belegen, mehr loescht der
Bootloader nicht. Sektor 7 (128 KB, davon wenige hundert Byte Parameter) kaeme nur fuer Daten in
Frage, die Wallfree selbst schreibt (z. B. die Weboberflaeche), mit eigenem Aktualisierungsweg
und Verdichten; Sektor 3 (16 KB) nur bei Verzicht auf die Originalparameter (Rueckweg, Import).
Weitere Hebel: `app.js` minifizieren (gzip 15,8 KB, geschaetzt 4..5 KB weniger), allgemeine Shell
20 KB nur ohne Telnet; Netzstack und HTTP (~120 KB) sind noetig.
Beim RAM (Heap umgesetzt, Nutzer 2026-10-21):

| Kandidat | Ersparnis | Befund |
| --- | --- | --- |
| **umgesetzt:** System-Heap (`CONFIG_HEAP_MEM_POOL_SIZE`) 16 KB -> 512 B | 15,9 KB | einziger Verweis auf `k_malloc` ist `websocket_send_msg` beim Maskieren, das nur die Client-Seite tut (Server: `is_client = 0`); Bau mit Heap 0 scheitert nur an diesem Verweis. Die 512 B halten ihn aufloesbar und fangen einen unerwarteten kleinen Nutzer ab. Kommt eine Zephyr-Funktion mit Heap-Bedarf hinzu (z. B. DNS, `HEAP_MEM_POOL_ADD_SIZE_*`), erneut pruefen |
| Thread-Stacks | je nach Messung | Testgerät (`87b9bef`, 2 h, mit Parameter speichern, Verriegeln, API, ohne Browser/evcc/MQTT): knapp `shell_telnet` 1720/2048, `sysworkq` 800/1024; reichlich `logging` 208/2048, `mqtt` 584/2048 (MQTT aus), `IRQ` 320/2048, `evse_logic` 648/2048, `evse_prot` 192/1024. Unter Last (`a8311ae`, 30 min Browser-Last, 2 WebSockets, MQTT an): `http_server_tid` 2120/4096, `mqtt` 1120/2048, `shell_telnet` 1688/2048, `sysworkq` 800/1024, `ws_push` 1096/2048, `rx_q` 912/1536, die uebrigen unveraendert; kein Stack ueber 82 %. Die Simulation (64 Bit) ist nicht aussagekraeftig |
| `NET_MAX_CONN`/`NET_MAX_CONTEXTS` | – | **erhoeht 10 -> 12** (Nutzer 2026-10-26): im Lasttest 8..9 von 10 belegt (Browser, 2 WebSockets, Abfrager, Telnet, 2 Listener), dazu MQTT und TIME_WAIT; 672 B je TCP-Verbindung |
| Ethernet-DMA-Puffer (2 x 6 KB) | – | Anzahl fest im HAL-Treiber (4 + 4) |

## Annahmen der Hardwareschicht

Bestaetigt auf dem Testgerät: Pinbelegung und Pegel der Eingaenge, Ausgaenge, LEDs, DIP/Drehschalter,
Reset-Taster und CP-Messung; PHY-Takt und -Reset (Netz laeuft); IWDG-Umprogrammierung und
Loeschen von Sektor 7 im Betrieb (Verdichten gelaufen); RTC-Backup-Register ohne RTC-Takt
(Resetgrund, Update-Ergebnis); Freigabe ruhender HTTP-Verbindungen nach 5 s (Lasttest).
Noch offen:

- PE2 High = Schuetz ein und RCM "High = ausgeloest" (Inversion zum Bender-Ausgang) stammen aus
  der Originalfirmware; der RCM-Selbsttest besteht, das Schuetz ist ungeprueft (Stufe C).
- Reaktionszeit RCM-Flanke -> PE2 Low ueber den Zephyr-GPIO-Callback: Ziel <= 1 ms
  (SOLL-PROT-05), am Oszilloskop messen.
- Zaehlerbus: RS-485 an Klemme A/B auf UART4 mit Freigabe PC12 (Firmwarebefund, wallbe2
  Belege §9): Sendepegel an A/B messen; danach je Modell einen echten Zaehler abfragen und die
  Fehlerzaehler bei offener und vertauschter Leitung ansehen. Zephyr-Modbus im nicht
  normkonformen Modus (8N1) ist nur gebaut.
- MQTT: Erkennungszeit eines ausgeschalteten Brokers bei laufendem Zaehlerversand.

## Fallstricke

Gefunden beim Testen; gelten fuer Zephyr 4.4.2 und sind bei Updates erneut zu pruefen.

| Fallstrick | Folge | Massnahme |
| --- | --- | --- |
| ADC-Referenz der Steuerung ist nicht 3,3 V, sondern 2,495 V (Faktor der Originalfirmware) | MCU-Temperatur mit 3,3 V gerechnet: 132 statt etwa 32 Grad (Testgerät) | Spannungen aus ADC-Werten mit 2,495 V rechnen; die uebrigen Kanaele nutzen ohnehin die Skalierung des Originals |
| `CONFIG_ZVFS_EVENTFD_MAX` = 1 (der Socket-Service belegt den einzigen Platz) | HTTP-Server startet nie; Meldung nur mit `CONFIG_NET_LOG` sichtbar | Wert 2 |
| `CONFIG_WEBSOCKET_MAX_CONTEXTS` zu klein | weitere WebSocket-Clients werden vor dem Anwendungs-Callback abgewiesen | 3 (SOLL-API-07 verlangt >= 2) |
| Netz-Threads in Zephyr standardmaessig kooperativ (`NET_TC_THREAD_COOPERATIVE`): RX/TX, TCP-Arbeit, Ethernet-RX, HTTP-Server, net_mgmt; die System-Workqueue ebenfalls (-1) | laufen vor jedem preemptiven Thread und werden nicht unterbrochen; auf dem Testgerät (`477bb1b`, 4 parallele HTTP-Abfrager) wartete der Automatenthread > 50 ms -> `supervision_limit`, mehrere Neustarts in 2 min, je Start ein Lauf des Verriegelungsmotors | alle preemptiv unter dem Automatenthread (Festlegung Thread-Prioritaeten); Lasttest `scripts/net-load.py` |
| `LOG_PANIC()` auf der Systemworkqueue | schaltet das Telnet-Log-Backend synchron; dieses bricht seine Sende-Arbeit mit `k_work_cancel_delayable_sync()` auf derselben Workqueue ab -> Selbstblockade, sobald ein Telnet-Client verbunden ist; erst die Ueberwachung setzt zurueck (falscher Resetgrund) | kein `LOG_PANIC` in Thread-Pfaden |
| Abbau von WebSockets ueber `zsock_poll` auf dem WebSocket-Deskriptor | geschlossene Verbindungen blieben belegt (in der Simulation sicher) | `websocket_recv_msg` mit 2 ms Timeout je Client und Zyklus (auf der Steuerung hoechstens 6 ms je 100 ms) |
| Erster Start auf dem Testgerät (2026-10-24) | Wallfree lief an und brach vor `main` mit einem fatalen Fehler ab (BKP19 = Grund `fault`, RCC-Flags ungeloescht), Neustartschleife ueber den Bootloader. Vermutete Ursache: vom Bootloader eingeschaltete Interrupts (SysTick, USART2, RTC). Abhilfe `CONFIG_INIT_ARCH_HW_AT_BOOT=y` (NVIC-Freigaben und anstehende Interrupts beim Start loeschen); zur Diagnose schreibt der Fehlerbehandler Programmzaehler und Grund nach BKP17/BKP18 (per SWD lesbar) | danach lief Wallfree (Power-LED 1 Hz, keine erneute Fehlermeldung in BKP17..19); die Interrupt-Hypothese ist damit gestuetzt, nicht bewiesen |
| Erster Lauf mit Netz auf dem Testgerät (2026-10-24) | Drehschalter richtig gelesen (Stellung 4, 16 A). DIP nach dem Einschalten mit F1 als 239 gelesen (D8 an), nach Software-Neustart zweimal 111 (wie Originalfirmware; Pinpegel per SWD bestaetigen 111: PA11 High). Mit D8 fehlte die Verfuegbarkeit (XR nicht beschaltet) -> S10. Vermutung: PA11 (nur interner Pull-up) beim Einschalten kurz Low. Die gruene LED an der Buchse ist PC2 (Festlegung Netzwerk-LED) | Nutzer bestaetigt D8 aus. Abhilfe: Schalter erst 20 ms nach der Pin-Einrichtung lesen, dann alle 10 ms bis 3 gleiche Lesungen (hoechstens 30), IWDG dabei bedient; vermutlich laedt der interne Pull-up eine Filterkapazitaet (Hypothese) |
| Kaltstart das Testgerät mit `2b29b8b` (2026-10-24) | nach F1-Einschalten DIP 111, Drehschalter 4, S1 bereit, CP A mit +11,9 V (cp_pos 11918 mV), RCM-Selbsttest bestanden, keine Fehler; Ladeerlaubnis 0 (D7 an, EN nicht beschaltet). Schuetz und Aktoren abgeklemmt | Ruhebetrieb auf der Steuerung bestaetigt |
| `FLASH_SR`-Fehlerbits beim Start (Testgerät 2026-10-24) | Beim Start steht in `FLASH_SR` `0xc0`: PGSERR und PGPERR. Laut PM0059 (2.8.4, 2.6) ist der Resetwert 0; PGSERR setzt die Hardware bei einem Schreibzugriff auf das Flash, ohne dass PG gesetzt ist, PGPERR, wenn die Zugriffsbreite nicht zu PSIZE passt (Voreinstellung x8). `0xc0` bedeutet also: Nach dem Reset hat Code mit einem Halbwort- oder Wortzugriff auf eine Flash-Adresse geschrieben, auch ueber den Spiegel ab `0x00000000` (z. B. Nullzeiger, RM0033 2.4). Die Bits haben sonst keine Wirkung. Der Zephyr-STM32-Treiber prueft sie vor jedem Schreiben, meldet ein schon gesetztes Bit einmal als `-EIO` und loescht es dabei: Ohne Gegenmassnahme scheiterte die erste Speicherung nach jedem Start (`NVM_E_IO`), der Bereich wurde als beschaedigt markiert und unnoetig verdichtet (Sektor 7 geloescht). Herkunft (Debugger, Testgerät 2026-10-26): Bootloader. Nach dem Reset ist `FLASH_SR` 0, am Sprung des Bootloaders in die Anwendung (`0x08008258`) schon `0xc0` bei gesperrtem Flash (`FLASH_CR` `0x80000000`), am Reset-Handler von Wallfree unveraendert ([wallbe2 Bootloader](../../wallbe2/docs/bootloader.md#startablauf-bootloader-box-t-firmware-081)). Die Stelle im Bootloader ist nicht weiter eingegrenzt (ohne Wirkung, siehe Massnahme) | `storage_init` loescht die Bits vor dem Einhaengen (`hw_flash_take_errors`); `evse nvm` zeigt sie (beim Start und vor den Treibern). Seit `87b9bef` gelingt die erste Speicherung sofort |
| Zephyr-Treiber KSZ8081, Reg. 0x1F Bit 7 | Treiber setzt Bit 7 nur bei `interface-type = "rmii"` (Logik des RNA); beim RND waehlt Bit 7 = 1 den 25-MHz-Modus mit REF_CLK-Ausgang. Mit `"rmii-25MHz"` lieferte der PHY keinen REF_CLK: MAC nicht gestartet (MACCR im Resetwert, DMAOMR FTF haengt), Netz-LED blinkt, MDIO funktionierte (BMSR `0x7849`) | fuer den RND `"rmii"` eintragen (Testgerät 2026-10-24, SWD-Lesebefund) |
| Start ueber den Original-Bootloader (Testgerät, [wallbe2/docs/bootloader.md](../../wallbe2/docs/bootloader.md#startablauf-bootloader-box-t-firmware-081)) | springt nach `0x08010000`, wenn das erste Wort ein SRAM-Stack-Pointer ist, sonst keine Pruefung; IWDG laeuft dann schon (etwa 33 s, LSI-abhaengig 22..62 s), PRIMASK = 1, PLL eingestellt; zaehlt IWDG-Resets in BKP16 und geht nach dem 5. in den Update-Betrieb | Wallfree bei `0x08010000` mit Bootloader moeglich; Zephyr gibt PRIMASK beim Start des Hauptthreads frei und schaltet vor der PLL-Konfiguration auf HSI; Wallfree uebernimmt den IWDG frueh (SOLL-BOOT-03) und loescht die Reset-Flags; BKP10..16 bleiben unberuehrt |
| Simulation: stdout blockgepuffert, wenn das Log in eine Datei geht | Meldungen erscheinen Kilobytes spaeter; sah wie ein Log-Stillstand nach Telnet-Abbruch aus (kein Befund fuer die Steuerung) | stdout zeilengepuffert |
| Simulation: Socket-Treiber NSOS ohne `shutdown` | ruhende HTTP-Verbindungen werden nie getrennt (auch nicht mit kuerzerer `HTTP_SERVER_CLIENT_INACTIVITY_TIMEOUT`); ein offener Browser-Tab belegte alle 4 Plaetze | in der Simulation 8 Plaetze, 24 Deskriptoren; Fristen der HTTP-Plaetze nur auf der Steuerung messen |
| Parallele `fetch`-Aufrufe im Browser | Chrome oeffnet je gleichzeitiger Anfrage eine eigene Verbindung (bis 6 je Host), die Steuerung hat 4 Plaetze | Warteschlange in `api()` (Festlegung Verbindungsplaetze) |
| Telnet-Ausgabe bei gleichzeitigem HTTP-Verkehr (Testgerät, schon mit `477bb1b`) | `kernel thread stacks` bricht ab, die Telnet-Ausgabe haengt etwa 9 s, HTTP-Abfragen und WebSocket-Takt stehen dabei 3..5 s | Diagnose per Telnet nur ohne Last aussagekraeftig; Telnet ist Entwicklungszugang (F20). Ursache nicht untersucht |
| Simulation: Socket-Ersatz NSOS stuerzt selten ab (SIGSEGV in `nsos_isr`, `sys_dlist_is_empty`), vor allem beim Broker-Neustart in T5Mqtt; etwa 1..2 von 12 Laeufen, schon vor SNTP und Discovery | ein Ende-zu-Ende-Lauf scheitert gelegentlich mit "Connection refused" | nicht in Wallfree; Lauf wiederholen. DNS-Resolver in der Simulation aus (ohne DNS-Server nutzlos) |
| Simulation: Telnet-Backend setzt kein `SO_REUSEADDR` | nach Neustart blockiert TIME_WAIT den Port | `bind` setzt es in der Simulation |
| `twister --coverage` | bricht in dieser Version ab | `scripts/run-tests.sh --coverage` ruft gcovr direkt |
| Umrechnung `float` -> `int64_t` auf dem Cortex-M3 (`__aeabi_f2lz`) | zieht die Double-Softfloat-Routinen nach (+3,5 KB Flash), ohne dass der Quelltext `double` enthaelt | Float32 der Zaehler ganzzahlig dekodieren (`f32_scaled` in meter.c) |
| Zephyr-MQTT, Keepalive | `mqtt_live` sendet PINGREQ, wertet fehlende Antworten aber nicht aus; ein stumm verschwundener Broker bliebe "verbunden" | Anwendung prueft `unacked_ping` gegen `mqtt_keepalive_time_left` |
| Retain-Kennung beim Empfang | bei bestehendem Abo setzt der Broker sie nie (MQTT-3.3.1-9); ein Test mit `mosquitto_pub -r` zeigt das Ignorieren nicht | Test ueber eine Neuverbindung, die die gespeicherte Nachricht ausliefert |
| Zephyr-Modbus, Stoppbits | im normkonformen Modus erzwingt Zephyr bei "keine Paritaet" 2 Stoppbits; die Eastron-Werkseinstellung 8N1 ginge nicht | `CONFIG_MODBUS_NONCOMPLIANT_SERIAL_MODE`, Stoppbits je Format setzen |

## Pruefungen auf dem Testgerät

Das Testgerät (Firmware 0.81 ab Werk, Bootloader und Sektor 3 unveraendert), Freigaben in
[wallbe2/AGENTS.md](../../wallbe2/AGENTS.md); Schuetz abgeklemmt, kein Fahrzeug. Steuerung ueber
Shell/API von Wallfree, der Nutzer beobachtet und misst (unter Spannung nur an den Klemmen, nie
auf der Platine). Einzelheiten zur Hardware: [wallbe2 Hardware](../../wallbe2/docs/hardware.md).

| Bereich | Ergebnis |
| --- | --- |
| Start, Netz | Start ueber den Original-Bootloader, Takt, Ethernet/PHY, DHCP; Geraete-MAC aus dem OTP (`xx:xx:xx:a1:b2:c3`, Hostname `wallfree-a1b2c3`) |
| Eingaenge (Stufe A) | EN (Schluesselschalter, 0/12 V), ML, XR, IN je 12 V -> `inputs` Bit 1..4; XR mit D8: ab -> S10, an -> S1; IN Code 8 -> 6 A, Code 11 -> 16 A |
| Ausgaenge, LEDs (Stufe A) | ER/LR/VR/CR je 12 V an der Klemme (Register-Ausgaenge 30..33); Front-LED ER rot, VR blau, CR gruen; Power- und Error-LED; mit `dip_override` D8 -> S10, CP -12 V, Error-LED an (`dip_override` in S10 nicht schreibbar, SOLL-PAR-40) |
| Verriegelung (Stufe B) | LD ist ein Impuls von grob 60..100 ms waehrend der Fahrt: Code 22 noetig, mit Code 3 schlaegt Verriegeln fehl; mit D9 aus entriegelt der Automat in S1 bei CP A sofort wieder (SOLL-LOCK-23). Das Testgerät: `in_function_ld` 22, `out_function_cr` 40 (gruen im Leerlauf wie das zweite Gerät, Nutzer 2026-10-24) |
| Reset-Taster | wirkt auf NRST und PD2; der Prozessor bleibt nach dem Druecken etwa 5 s im Reset, auch wenn der Taster gehalten wird (vermutlich RC-Glied an NRST), und startet dann. Kurz oder unter etwa 5 s -> nur Neustart (`reset_pin`); gehalten -> nach etwa 5 s blinkt die Power-LED 1 Hz, nach weiteren 5 s Werksreset, also insgesamt etwa 10 s (Testgerät 2026-10-27: nach dem Druecken etwa 5 s dunkel (Prozessor im Reset), dann blinkt die Power-LED 1 Hz (Wallfree wartet); Loslassen vor Ablauf -> nur Neustart, Startereignis um die Wartezeit spaeter) |
| Parameterspeicher | `FLASH_SR` beim Start `0xc0` (aus dem Bootloader, Fallstricke); erste Speicherung danach fehlerfrei |
| Import aus Sektor 3 (`c5fce83`, 2026-10-28) | `evse import-original` las die Werte der Originalfirmware 0.81: 16 Werte der Ladelogik uebernommen (u. a. LR 17, CR 9, LD 3; `overcurrent_enable` fehlte im Original und gilt als 0) und 4 Netzwerkwerte (feste Adresse). Danach ueber die API zurueckgestellt (0,3 s, ohne Neustart): LR 29, CR 40, LD 22, DHCP; `restart_required` blieb falsch, kein Fehler. Sicherungen und Ausgabe: `wallbe2/analysis/box-t/import-2026-10-28/` |
| Update | ueber die Weboberflaeche in etwa 20 s, Erfolg nur bei passender Pruefsumme; Rueckweg zur Originalfirmware 0.81 und zurueck geprueft (Sektor 3 und 7 erhalten). Fruehere Abbrueche: jede Abfrage waehrend des Uploads bricht ihn im Bootloader ab (Seite fragt deshalb nichts ab); wechselnde MAC von Bootloader und Wallfree verzoegerte per ARP-Cache (behoben mit Geraete-MAC) |
| Browser-Tabs und Verbindungsplaetze (`cfb6def`, 2026-10-28, `scripts/web-tabs-load.mjs`, daneben alle 0,5 s eine Anfrage mit neuer Verbindung) | vorher (`b5ce020`) mit offenen Handy-Tabs jede Minute 8..10 s keine Antwort (Ursache Festlegung Verbindungsplaetze). Frist: 6 stumme Verbindungen, nach 5,1 s wieder erreichbar (`net-load.py idle`). 4 Tabs auf Status, 2 verborgen, 5 min: verborgene 0 Anfragen, sichtbare je 2 Verbindungen nacheinander, 592 von 592 Anfragen beantwortet. Mit der Frist 10 s (`7cfd6fa`) im gleichen Aufbau einmal 2,5 s Abweisung in 7,5 min. Ohne Tabs 6..8 ms |
| Netzlast (`scripts/net-load.py`) | Mit kooperativen Netz-Threads startete das Testgerät unter 4 parallelen Abfragern neu (Fallstricke); seit `eca1ad4` kein Neustart: 83 Anfragen/s, hoechstens 123 ms; 3 WebSockets, Takt 2,0 s; 2300 Verbindungsabbrueche in 60 s ohne Rest; 30 min Dauerlast mit 2 WebSockets und MQTT ohne Neustart, WebSocket-Luecke hoechstens 2,1 s |
| MQTT (Mosquitto 2.1 im LAN) | Verbindung sofort nach `mqtt_enable`; Zustaende retained; Befehl als Zahl und JSON, 3 A abgelehnt, ohne `mqtt_commands` wirkungslos, retained Befehl verworfen; nach Broker-Neustart in 4 s verbunden; Last Will bei Uebernahme der Sitzung nach Neustart |
| evcc 0.317.2 | Anmeldung und WebSocket ohne Fehler, ohne Fahrzeug "car disconnected", `external_current` = 0 |
| Version (`v0.1`, 2026-10-27) | Update auf 0.1 erfolgreich, `/system/info` mit `firmware` 0.1 und `build` d45de9d, Pruefsumme passt zur Datei, Protokoll laeuft ueber den Update-Neustart weiter. Kopfzeile: Netzkabel gezogen -> nach wenigen Sekunden "getrennt" mit Hinweisleiste, eingesteckt -> wenige Sekunden bis "verbunden", ohne Neustart |
| MQTT per Name, Home-Assistant-Erkennung (`v0.1`, 2026-10-27) | Broker `broker.lan` (Mosquitto, ohne Anmeldung) aufgeloest und sofort verbunden; nach `online` 6 Konfigurationen retained, Ladestrom/Leistung/Energie als leere Nachricht entfernt (ohne `mqtt_commands`, ohne Zaehler); Geraet mit `sw` `0.1+d45de9d`. Gefunden: Wertvorlage "Zustand" auf 95 Byte gekuerzt (ohne `}}`), behoben in 0.2-dev. Mit `590465a`: `mqtt_commands` an -> Ladestrom-Feld 0..16 A (Drehschalter), aus -> wieder entfernt; kein Home Assistant vorhanden, Pruefung nur mit `mosquitto_sub` |
| Toter Broker (`b5ce020`, 2026-10-27) | Test-Broker auf dem PC (Mosquitto, Port 18830), per SIGSTOP eingefroren (TCP bleibt offen, keine Antworten): letzte Nachricht 21:11:07, PINGREQ nach 60 s ohne Antwort, Trennung mit -116 (ETIMEDOUT) nach 121 s (106 s nach dem Einfrieren); nach dem Auftauen sofort wieder verbunden, `online` retained. Ohne Zaehler (kein Dauerversand); der Fall mit Zaehler ist offen |
| Protokoll, Uhrzeit, CP-Aufnahme (`6eb0e08`, 2026-10-27) | Ereignisprotokoll uebersteht den Neustart; Uhrzeit von `pool.ntp.org` (Router meldet per DHCP keinen Zeitserver, DNS ueber DHCP funktioniert); CP-Aufnahme im Leerlauf +11,9 V |
| Stacks | siehe [Ressourcen](#ressourcen); keiner ueber 82 % |

Bekannte Grenzen: Keep-Alive-Verbindungen belegen je einen der 4 HTTP-Plaetze bis 5 s;
weitere werden abgewiesen. Stumme Verbindungen, die staendig nachkommen, blockieren die
Weboberflaeche (Laden und Schutz unberuehrt). Telnet-Ausgabe unter Last siehe Fallstricke.

## Offen

- **Stufe C** mit Pruefstecker ([Pruefstecker](pruefstecker.md), Freigabe in wallbe2/AGENTS.md
  noch offen): CP-PWM, Schuetz, Modul-LEDs Connect/Ready, Laden mit evcc.
- Reaktionszeit RCM -> PE2 am Oszilloskop (SOLL-PROT-05).
- Energiezaehler an RS-485: Eastron SDM72D-M-2 des Nutzers anschliessen (spaeter, Nutzer 2026-10-27);
  Sendepegel an A/B, Abfrage, Fehlerzaehler bei offener und vertauschter Leitung.
- Statuswert `ct_sense` (Spannung an CT) auf dem Testgerät pruefen: ohne Schuetz 0 (geht ohne Pruefstecker),
  1 bei eingeschaltetem Schuetz erst in Stufe C (Nutzer 2026-10-28: spaeter).
- Ziel von PE1 und Ansteuerung des Relais C3/C4 (F15, Nutzer 2026-10-28: spaeter suchen; erst dann
  in der Statusanzeige): Testgerät 2026-10-27 (Wallfree `c54fa08` mit einem Diagnosebefehl, danach wieder entfernt): PE1 geht 1 s auf High (GPIOE ODR und IDR Bit 1 per SWD gelesen, ohne Anhalten, `wallbe2/analysis/box-t/pe1-gpioe.log`), das Relais an C3/C4 klickt nicht, keine LED reagiert. PE1 schaltet das Relais C3/C4 also nicht direkt; Ziel weiter offen. Spaeter untersuchen, was an PE1
  haengt und wie das Relais angesteuert wird (Nutzer 2026-10-27). Naechster Schritt (Nutzer 2026-10-28: wenn er zu
  Hause ist): Kandidat PB3 (Pin 89, zugleich SWO an der Debug-Leiste) per SWD 1 s High setzen und auf
  das Relais hoeren ([wallbe2 hardware.md](../../wallbe2/docs/hardware.md#4-offene-fragen)).
- Prozessortemperatur (SOLL-API-35) auf dem Testgerät pruefen: Wert plausibel (kalte Box nach laengerer Pause
  etwa Raumtemperatur plus wenige Grad), steigt nach dem Einschalten langsam an. `8198151` rechnete
  mit VREF+ 3,3 V und zeigte 132 Grad; mit 2,495 V (Faktor der Originalfirmware) ergibt derselbe
  Rohwert etwa 32 Grad. Ob die Referenz wirklich 2,495 V hat, zeigt `adc_vref_mv` in `/system/info` (aus VREFINT, +-2,5 %). `8d466ea`: 31,8 Grad knapp 3 min nach dem Start.
  Bestaetigt (Nutzer 2026-10-28): Anzeige "Prozessortemperatur" in Status -> Verbindungen, ganze Grad,
  Hinweis "kann um einige Grad abweichen". Abgleichparameter vorgesehen (Nutzer
  2026-10-28, spaeter): Verschiebung in 0,1 Grad, einmal an der kalten Box gegen ein Thermometer
  eingestellt; die Steigung streut kaum (RM0033: +-1,5 Grad nach Abgleich), nur die Verschiebung
  (bis 45 Grad zwischen Chips).
- Toter MQTT-Broker bei Dauerversand (Zaehlerwerte jede Sekunde): Erkennung erst ueber TCP, Zeit
  offen; mit dem Zaehler messen.

## Ideen fuer spaeter

Nicht beauftragt; gesammelt mit dem Nutzer (2026-10-26). Beauftragte Punkte stehen unter Offen.

| Idee | Nutzen | Aufwand, Bedenken |
| --- | --- | --- |
| Inbetriebnahme (Nutzer 2026-10-27, zurueckgestellt): im Werkszustand Laden gesperrt, bis auf einer Seite die Hinweise (freie Software ohne Gewaehrleistung, Laden noch nicht auf Hardware geprueft, Installation durch Fachkraft) bestaetigt und Drehschalter, DIP-Schalter, Verriegelungsart (LD 3/22) und Schuetzueberwachung angesehen sind; Eintrag im Protokoll, API `POST /system/commissioning`, Werksreset setzt zurueck | zwingt vor dem ersten Laden zum Blick auf die Einstellungen (Falle LD 22 bei wallbe) | etwa 1..2 KB Flash bei 92 % Belegung; rechtlich wenig Schutz. Alternative: Hinweise und Pruefliste nur im Repo (README, [umruesten.md](umruesten.md)) |
| Shell in der Weboberflaeche (ueber den angemeldeten WebSocket), danach Telnet entfernen (Nutzer ueberlegt) | gleiche Anmeldung und Bremse wie die API, kein offener Port 23, etwa 20 KB und ein Thread weniger, Fallstrick "Telnet bremst HTTP" entfaellt | Shell weg, wenn der HTTP-Server haengt (dann Debugger und Ueberwachung). Verworfen (Nutzer 2026-10-26): eigene Anmeldung fuer Telnet |
| S0-Zaehler an einem Digitaleingang | Energie und Leistung mit einem schon vorhandenen Zaehler ohne RS-485 | Anschluss: S0+ an 12 V der Steuerung, S0- an einen freien Eingang (IN oder ML), neue Eingangsfunktion "S0" (ausserhalb der Originalcodes); ob der Eingang genug Laststrom zieht (S0 meist >= 1..10 mA) und unter 3 V faellt, erst messen. Keine Phasenwerte; Ueberstrom und Schuetzueberwachung ueber Zaehler gehen damit nicht; Eingaenge alle 8 ms gemessen, 2 Bloecke entprellt (S0-Impulse 30..90 ms, bei 22 kW und 1000 Imp./kWh etwa 6 Imp./s: passt). Neukauf lohnt kaum: der unterstuetzte Eastron SDM72D-M (Modbus) kostet etwa gleich viel und liefert mehr |
| Ladesitzungen (Beginn, Dauer, Energie, Ende-Grund) | Abrechnung, Ueberblick | sinnvoll mit Zaehler; nur am Sitzungsende in Sektor 7 speichern |
| OCPP 1.6J (zurueckgestellt, Nutzer 2026-10-26) | Abrechnung ueber Backends (Dienstwagen) | grob 25..40 KB Flash, 6..10 KB RAM, rund 25 Konfigurationsschluessel; die meisten Backends verlangen TLS (passt kaum); wenige Privatanwender |
| Lizenzhinweis in der Weboberflaeche | Wallfree Apache-2.0, enthaelt Zephyr (Apache-2.0), Preact (MIT), htm (Apache-2.0); Link aufs Projekt; Vermerk in `web/vendor/preact-htm.js` (MIT verlangt ihn) | ~150 Byte; Link erst mit oeffentlicher Adresse |
| Lastmanagement mehrerer Boxen | – | kann evcc; nicht in der Firmware |
| Phasenumschaltung 1p/3p ueber C3/C4 (Nutzer 2026-10-27, spaeter) | Laden ab etwa 1,4 kW statt 4,1 kW (PV-Ueberschuss); evcc kann die Umschaltung ueber die WARP-Schnittstelle steuern | setzt eine Ansteuerung des Relais C3/C4 voraus; PE1 ist es nicht (Testgerät 2026-10-27, unter Offen). C3/C4 schaltet nur die Spule eines zweiten Schuetzes (wie C1/C2, Handbuch: Last nie direkt); ueblich: Schuetz 1 L1+N, Schuetz 2 L2+L3; Umschalten nur ohne Last (Laden unterbrechen, Pause, neu starten); Rueckmeldung des zweiten Schuetzes ueber einen Hilfskontakt an einem Eingang; zweites Schuetz nie ohne das erste. WARP: WARP2 mit Energy Manager und externem Schuetz (230-V-Schaltausgang, Hilfskontakt-Rueckmeldung, Laden wird vor dem Umschalten unterbrochen), WARP3 mit zwei eingebauten zweipoligen 63-A-Hutschienenschuetzen (Ersatzteil `WARP-CON-2P-63A`, Hersteller nicht genannt, auf dem Produktfoto "EKMF6320" laut Nutzer 2026-10-27; 63 A gilt je Pol, Reserve fuer Dauerlast und Waerme; fuer 11 kW genuegt vermutlich ein kleineres, der Nutzer wuerde ein anderes waehlen; das zweite schaltet L2/L3 und bleibt bei einphasigem Anschluss immer aus; WARP3-Betriebsanleitung 1.2, Abschnitt 5 und 13.7). Installation durch eine Elektrofachkraft |
| Zweite Sprache (Englisch) | Nutzer ausserhalb des deutschen Sprachraums (GitHub) | etwa 8..12 KB Flash; Hilfetexte der Parameter kommen aus der Firmware und muessten doppelt vorliegen; Anleitung uebersetzen. Vorerst nur Deutsch (Nutzer 2026-10-27) |
| Kartenleser (RFID), zukuenftige Erweiterung (Nutzer 2026-10-28; Nutzer prueft, ob er einen Leser bekommt) | Freigabe per Karte statt Schluesselschalter; Freigabeliste und Anlernen ueber die zuletzt gelesene Karte in der Weboberflaeche | Leser als Modbus-RTU-Teilnehmer am vorhandenen RS-485-Bus A/B neben dem Zaehler (eigene Adresse, gleiche Baudrate und gleiches Rahmenformat fuer alle Teilnehmer; der Leser im Original stand auf 115200 Bd, Zaehler meist 9600); Abfrage im Zaehler-Thread abwechselnd; Freigabe wirkt wie EN. Befunde zum Leser des Originals (Typen `DE_950`, `MW_R7`, TeraTron, Register, 20 Karten): [wallbe2 hardware.md](../../wallbe2/docs/hardware.md#4-offene-fragen). Flash bei 92 % knapp. Soll-Spezifikation erst bei Umsetzung |
| ESP32 als WLAN-Schnittstelle (Nutzer 2026-10-28) | Box ohne Netzwerkkabel | Anschluss an die Bestueckungsflaeche des Modems: USART2 (PD5 TX, PD6 RX, PD3 CTS, PD4 RTS, 115200 Bd in der Originalfirmware), PD0 Einschalten, PD1 Reset, PB10 Status ([Belege §9](../../wallbe2/docs/ladelogik-belege.md#9-nicht-zugeordnete-pins)); welche Pads dort ankommen, erst mit Fotos und Durchgangsmessung (Steuerung muss weiter zerlegt werden, Nutzer spaeter). ESP32 als Netzwerkbruecke (z. B. PPP oder SLIP ueber die UART) waere ein zweiter Netzweg in Zephyr; Flash bei 92 % knapp. Versorgung 3,3 V mit Stromspitzen bis etwa 0,5 A klaeren |
| Weitere Umbauten der Steuerung (2026-10-28 besprochen) | Batterie oder Goldcap an VBAT (Uhr und Backup-SRAM ueber Stromausfall), Temperaturfuehler DS18B20 an Steckdose oder Schuetz (Strom bei Hitze senken), Display an I2C1 (PB8/PB9 laut Originalfirmware frei), zweiter RS-485-Kanal USART3 fuer den Kartenleser | alle ungeprueft: ob die Pins auf Pads enden, ist offen; feine MCU-Pins (0,5 mm) |
| Bewusst nicht: TLS fuer die Weboberflaeche, signierte Updates; gestufter Werksreset (Passwort vergessen nur ueber den kompletten Werksreset, Nutzer 2026-10-26) | – | Platz/Zertifikate; Bootloader prueft nichts; keine Hardware |

### Veroeffentlichung (Ideen des Nutzers, 2026-10-27)

Fuer die spaetere Veroeffentlichung der Firmware vorbereiten:

- Liste der Funktionen (Ladelogik, evcc/WARP-API, MQTT mit Home-Assistant-Erkennung, Zaehler,
  Uhrzeit, Protokoll ueber Neustarts, CP-Oszilloskop, Update ueber die Weboberflaeche).
- Rueckweg zur Originalfirmware (auf dem Testgerät geprueft): Anleitung [umruesten.md](umruesten.md), nur
  ueber Weboberflaeche und Bootloader; keine Sicherung per Debugger beschrieben, weil kaum jemand
  die Ausruestung hat (Nutzer 2026-10-27).
- Vorher-Nachher-Vergleich der Weboberflaeche: [vergleich-weboberflaeche.md](vergleich-weboberflaeche.md);
  Originalseiten nur als Text, keine Bildschirmfotos (Logo, Texte und Geraetedaten des Herstellers;
  Nutzer 2026-10-27).
- Technik und Lizenzen: [technik-und-lizenzen.md](technik-und-lizenzen.md) (Nutzer 2026-10-27: Tech-Stack
  aufschreiben); Lizenzhinweis auf der Seite System. Zu jeder Veroeffentlichung `LICENSE` und
  `COPYING.picolibc` (Zephyr SDK) beilegen.
- Lizenzen dateigenau pruefen: `west spdx` (SPDX-Stueckliste der ins Abbild gelinkten Dateien). Ablauf:
  `west spdx --init -d <dir>`, bauen mit `-DCONFIG_BUILD_OUTPUT_META=y` (sonst bricht `west spdx` ab),
  `west spdx -d <dir>`. Ergebnis 2026-10-28: zusaetzlich zur Liste HTTP-Parser (MIT) und `fnmatch.c`,
  `getopt.c` (BSD-3-Clause, eigener Wortlaut von Klausel 3), in `web/lizenzen.txt` und
  technik-und-lizenzen.md ergaenzt. STM32Cube-Dateien ohne SPDX-Kennung (Lizenz in `LICENSE.md`).
  Nicht erfasst: picolibc, libgcc, Weboberflaeche (von Hand in der Liste). Bei neuen Zephyr-Versionen
  oder Kconfig-Aenderungen wiederholen.
- Kompletter Simulator auf dem PC (native_sim mit Hardwaremodell, Ersatz-Bootloader, evcc).
- Vor der Veroeffentlichung: Zeiten der Git-Commits pruefen (Nutzer 2026-10-27; Claude erinnert
  daran, der Nutzer weiss, was gemeint ist).
