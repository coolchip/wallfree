# Implementierungsstand

Stand: 2026-10-21. **Nichts davon ist auf Hardware ausgefuehrt**; bestandene Tests belegen
keine elektrische Sicherheit. Rahmen und Begruendungen: [Architektur](architektur.md);
Tests: [Test und Simulation](test-und-simulation.md).

## Ueberblick

| Bereich | Stand |
| --- | --- |
| Ladelogik (`lib/evse`) | vollstaendig nach Soll-Spezifikation, PC-getestet |
| Parameterspeicher (`lib/nvm`) | fertig, PC-getestet inkl. Stromausfall nach jedem Byte |
| API-Bausteine (`lib/api`) | fertig, PC-getestet |
| Energiezaehler (`lib/meter`, Zaehlerthread, nur Anzeige) | fertig, PC-getestet, in der Simulation Ende-zu-Ende und gegen evcc geprueft; Modbus-Anbindung auf UART4 gebaut, ungeprueft (H1) |
| MQTT-Client (`lib/api/mqtt.c`, `mqtt_task.c`) | fertig, PC-getestet, in der Simulation Ende-zu-Ende gegen Mosquitto geprueft; auf der Steuerung gebaut, ungeprueft (H1) |
| Anwendung (`app/`) | baut fuer `ev_cc_ac1` ohne Warnung; als PC-Simulation Ende-zu-Ende getestet |
| Weboberflaeche (`web/`) | fertig (Bereiche, Verlauf), Browser-Durchlauf in der Simulation |
| evcc-Anbindung | gegen die Simulation geprueft (evcc 0.317.2) |
| Hardwareschicht (`hw_stm32.c`, `hw_gpio.c`) | gebaut, ungeprueft (H1) |
| Update-Uebergabe an den Bootloader (SOLL-SH-10, SOLL-BOOT-30) | zurueckgestellt bis H0 |

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
| [ctm.c](../lib/evse/ctm.c) | Schuetzueberwachung Modi 0..4 | SOLL-PROT-20..23 | evse_ctm |
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
| [alog.c](../lib/api/alog.c) | Zugriffsprotokoll: Ringpuffer (32), Zusammenfassen wiederholter Fehlversuche, JSON in Teilen | SOLL-ERR-11 | api_alog |
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
| [hw_stm32.c](../app/src/hw_stm32.c) | TIM1 CH1/PE9 (PSC 49, center-aligned Modus 1, ARR 1000, 1 kHz), ADC1 10 Raenge in zwei 5er-Gruppen, Start im TIM1-Update-Interrupt bei CNT < 50 bzw. > 950, DMA2 Stream0 zirkular 80 Worte, Skalierung wie Original; MCO1 = HSE an PA8; PA15-Resetimpuls 11 ms; fruehe sichere Pinpegel; sicherer Zustand per Register | SOLL-RT-01..03, SOLL-IO-02, SOLL-BOOT-02/09, SOLL-RT-20 |
| [hw_gpio.c](../app/src/hw_gpio.c) | alle Ein-/Ausgaenge aus `zephyr,user`; RCM-Flankeninterrupt schaltet PE2 per Register ab und setzt einen Schutz-Latch, der Ein-Befehle sperrt; PE0-Flankenzeitstempel; PB2 erst nach der ersten Ausgabe | SOLL-PROT-01/05, SOLL-IO-01/21 |
| [evse_task.c](../app/src/evse_task.c) | Schutzthread (Prio 0, 1 ms): RCM-Pegel, Ueberwachung Automat (100 ms) und Messbloecke (50 ms), IWDG 250 ms, Wartungsmodus fuer Flash-Loeschen; Automatenthread (Prio 2, 10 ms und je Messblock); Ereigniserkennung; Fault-Handler: sicherer Zustand, Neustart | SOLL-RT-04/05/10..12/21 |
| [storage.c](../app/src/storage.c) | Flash-Adapter (Sektor 7 lesen/schreiben/loeschen, Sektor 3 nur lesen, Lage beim Bauen geprueft); Speicher-Thread (Prio 12): Verriegelungszustand speichern, Verdichten im sicheren Zustand; Werksreset, Import | SOLL-NVM-01..05/10, SOLL-RT-13, SOLL-BOOT-04/20/21 |
| [http_api.c](../app/src/http_api.c) | HTTP-Server Port 80: `/info/name`, `/info/features`, `/evse/state`, `/evse/external_current`, `/evse/limits`, `/evse/params`, `/evse/param_info`, `/evse/errors`, `/evse/errors/ack`, `/credentials`, `/system/config`, `/system/config_info`, `/system/restart`, `/system/info`, `/system/log`, `/mqtt/state`, mit Zaehler `/meters/0/value_ids`, `/values`, `/state` (sonst 404); WebSocket `/ws` (bis 3 Clients, Push alle 100 ms bzw. sofort nach Schreibzugriffen, ein Topic je Rahmen, Topics `evse/state`, `evse/external_current`, `evse/limits`, `evse/event`, mit Zaehler `meters/0/*` nach jedem Zyklus); Digest-Pruefung je Anfrage | SOLL-API-01..07/30..32, SOLL-ERR-10/11, SOLL-MTR-30/31, SOLL-MQTT-30 |
| [auth.c](../app/src/auth.c) | Zugangsdaten (HA1), Werkszugang, Passwortaenderung, Nonce-Geheimnis je Start | SOLL-API-07 |
| [netcfg.c](../app/src/netcfg.c) | Netz-/Systemparameter, Hostname, DHCP oder feste Adresse, Telnet; Aendern (atomar), Import; `mqtt_*` sofort in die laufende Konfiguration und MQTT neu verbinden; Netzwerk-LED PC2 | 10.1, F20, F25, SOLL-LED-06, SOLL-MQTT-31 |
| [shell_evse.c](../app/src/shell_evse.c) | `evse status`, `param list/get/set`, `current`, `enable`, `avail`, `pause`, `pwm5`, `lock`, `unlock`, `errors [clear]`, `selftest`, `reboot`, `ack`, `nvm`, `meter`, `mqtt`, `sys list/set`, `factory-reset confirm`, `import-original` | SOLL-SH-01..09/11/19/20, SOLL-MTR-33, SOLL-MQTT-32 |
| [sysinfo.c](../app/src/sysinfo.c) | Resetgrund, Firmwarestand (`git describe`), Laufzeit; `GET /system/info` | SOLL-RT-14 |
| [meter_task.c](../app/src/meter_task.c) | Zaehlerthread (niedrigste Anwendungsprioritaet, Stack 1280 B, nicht ueberwacht), nur mit `meter_type` != 0; Momentaufnahme unter Sperre, Bus ohne Sperre; Ereignisse in den gemeinsamen Ringpuffer | SOLL-MTR-01/03/10..14 |
| [mqtt_task.c](../app/src/mqtt_task.c) | MQTT-Thread (niedrigste Anwendungsprioritaet, Stack 2048 B, nicht ueberwacht): Verbinden mit Last Will, Neuverbindung 1..60 s, Takt 100 ms fuer Zustaende und Ereignisse, Befehl `evse/external_current_update`, Status | SOLL-MQTT-01..04/10..12/20..23/30 |
| [meter_bus_modbus.c](../app/src/meter_bus_modbus.c) | nur Steuerung: Zephyr-Modbus-Client auf UART4 (`modbus0`, DE an PC12), Baudrate/Format aus den Parametern, Fehler auf Timeout/Rahmen/Ausnahme abgebildet | SOLL-MTR-02/04, F29 |
| [web/](../web/) | Werkszugang -> Zugangsdaten festlegen; Uebersicht: Zustand, Fahrzeug, Fehler, Zaehler (Leistung, Zaehlerstand), Ladestrom als Ring mit Vorgabe-Tasten, letzte Ereignisse; Insights: Verlauf, Ladestrom als Wertetabelle, Fehler mit Quittierung, Zaehlerwerte mit Fehlerzaehlern, Zugriffsprotokoll, Status der Ladelogik; Einstellungen: Laden (Parameter mit Bereich, Default, Herkunftsfeld, Hilfetext), Netzwerk, MQTT, Zaehler (je mit Zustand, Passwortfeld nur schreibbar), Zugang; System: Firmware, Laufzeit, Resetgrund, Neustart | SOLL-API-31/33, SOLL-ERR-10/11, SOLL-MTR-32, SOLL-MQTT-32 |
| [history.c](../lib/api/history.c), [history_task.c](../app/src/history_task.c) | Verlauf: Mittelwerte je 10 min im Ringpuffer (RAM), Ausgabe in Teilen; Abtastung je Sekunde auf der Systemworkqueue | SOLL-API-33 |
| [hw_sim.c](../app/src/hw_sim.c), [sim_host.c](../app/src/sim_host.c) | nur `native_sim`: Hardwaremodell mit simuliertem Zaehler hinter `meter_bus.h` und Neustart per `execv` ([Test und Simulation](test-und-simulation.md)) | – |

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
| Import aus Sektor 3 | 409/412 aus dem unteren Byte (im Original 1-Byte-Felder); jede Uebernahme ueber `evse_param_set`; Netz: 404 (DHCP), 315..326 (IP/Maske/Gateway), alles oder nichts; Geraetename 310 wird nicht uebernommen | SOLL-NVM-10; Belegung aus `wallbe2/analysis/web/lan.html` |
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
| Telnet aus | `shell_stop`: Port 23 bleibt offen, Kommandos werden nicht ausgefuehrt | Zephyr kann den Telnet-Port zur Laufzeit nicht schliessen |
| Telnet-Optionen | `CONFIG_SHELL_TELNET_SUPPORT_COMMAND` (in Zephyr als experimentell markiert): Echo und Zeichenmodus werden auf Anfrage des Clients ausgehandelt; mit `mode character` funktionieren Historie und Zeilenbearbeitung. Der Server bietet den Modus nicht von sich aus an. Kosten 0,3 KB Flash | Nutzerwunsch 2026-10-20 (Pfeil hoch fuer den letzten Befehl) |

### Ereignisse und Zugriffsprotokoll

| Punkt | Umsetzung | Grund |
| --- | --- | --- |
| Ereignisformat (SOLL-ERR-10) | WebSocket-Topic `evse/event`, je Ereignis ein Rahmen, Payload `{"seq", "t_ms", "type", ...}`: `state` (`from`/`to` = S0..S10), `error` (`set`/`cleared` als Namen wie `/evse/errors`), `lock` (`lock_state` 1/3/5, nur Endzustaende), `contactor` (`on`, Schuetz-Soll); `seq` fortlaufend ab 1 je Start, Luecke = verloren (Ringpuffer 16); kein Nachsenden beim Verbinden; jedes Ereignis zusaetzlich als Logzeile `Ereignis {...}` | Spezifikation nennt kein Format; evcc ignoriert unbekannte Topics |
| Protokolleintrag (SOLL-ERR-11) | JSON `{"seq", "t_ms", "kind", ...}`; `kind` = `auth_fail` mit `addr` (IPv4 als Text, `0.0.0.0` unbekannt) und `count`, oder `event` mit der Payload von `evse/event`; `t_ms` = ms seit Start; kein Freitext, Benutzername wird nicht aufgezeichnet | maschinenlesbar wie die uebrige API; die Steuerung hat keine gestellte Uhr |
| Fehlversuche | jede mitgesendete, aber abgelehnte Anmeldung (falsches Passwort, unbekannter Benutzer, fehlerhafte Kopfzeile), auch beim WebSocket-Aufbau; nicht: fehlende Anmeldung, veraltete Nonce, waehrend der Sperre abgewiesene Anfragen. Direkt aufeinanderfolgende Fehlversuche derselben Adresse zaehlen im juengsten Eintrag weiter (`count`, `t_ms` = letzter Versuch) | Durchprobieren soll die Ereignisse nicht aus dem Puffer draengen |
| Puffer | 32 Eintraege im RAM (1,8 KB), beim Neustart leer; Fehlversuche und Ereignisse teilen sich den Puffer; aeltester Eintrag faellt heraus, Luecke in `seq` zeigt Verlust | Nutzervorgabe "etwa 32" |
| Abruf | `GET /system/log` mit Digest, im Werkszugang 403; liefert alle vorhandenen Eintraege, aelteste zuerst, in Teilen (kein Abschneiden); der Abruf selbst wird nicht protokolliert | SOLL-API-07 |
| Absender beim WebSocket | aus Zephyr-Interna ermittelt (Kopfzeilenfeld des Client-Kontexts, Zephyr 4.4.2); bei Zephyr-Updates pruefen | der WebSocket-Callback liefert keinen TCP-Socket |

### Bedienung und System

| Punkt | Umsetzung | Grund |
| --- | --- | --- |
| Neustart | nur geordnet: `evse_request_shutdown` (wirkt wie fehlende Verfuegbarkeit -> S7 mit 5-s-Frist -> S10), warten bis Schuetz aus, hoechstens 15 s, dann sicherer Zustand und Reset; gilt fuer Shell, HTTP und Werksreset. `kernel reboot` ist entfernt | SOLL-RT-22, SOLL-SH-20 |
| Log vor Neustart | Meldung, dann 200 ms fuer den Log-Thread, dann Reset (bei Fristablauf Schuetz sofort per Register aus); kein `LOG_PANIC` in Thread-Neustartpfaden; Meldungen der Ueberwachung koennen verloren gehen (Grund steht in BKP19) | `LOG_PANIC` blockiert mit verbundenem Telnet-Client die Systemworkqueue (Fallstricke) |
| Quittieren | `evse errors clear` / `POST /evse/errors/ack` quittiert nur ERR-CONTACTOR (F14); die Ausgabe nennt fuer alle anderen die Ruecksetzbedingung | SOLL-ERR-03 |
| Selbsttest auf Anforderung | `evse selftest` nur in S1, CP A, Schuetz aus | SOLL-SH-07 |
| Fehlerhistorie | 32 Ereignisse im RAM (Zeit, neu, geloescht, Zustand), geht beim Neustart verloren; HTTP gibt so viele aus, wie in 1,5 KB passen (`truncated`) | SOLL-ERR-01 |
| IWDG | startet zu Beginn von `main` (250 ms); bis zum Schutzthread bedient ihn `main`, auch waehrend des Werksreset-Wartens | SOLL-BOOT-03, SOLL-RT-12 |
| Resetgrund | BKP19 = `0x5746xxxx` vor Software-Resets; Gruende `supervision_limit`, `fault`, `user_restart`, `factory_reset`, `interface_hang` bzw. aus RCC_CSR | SOLL-RT-14 |
| Schnittstellen-Ueberwachung | Speicher-, WebSocket-Thread, System-Workqueue (10 s); HTTP-Server und Telnet gehoeren zu Zephyr und sind nicht ueberwachbar | SOLL-RT-11 |
| Netzwerk-LED | PC2 aktiv Low [HYP]; "verbunden" = Link (Carrier) und bevorzugte IPv4-Adresse; Takt aus der Systemworkqueue (haengt diese, steht die LED) | SOLL-LED-06 |

### Weboberflaeche

| Punkt | Umsetzung | Grund |
| --- | --- | --- |
| Anmeldung | Oberflaeche hinter Digest wie die API (Anmeldedialog des Browsers); im Werkszugang nur die Zugangsdaten-Seite; nach der Aenderung fragt der Browser neu | SOLL-API-07 |
| Bezeichnungen | Parameter erscheinen mit ihrem Hilfetext, der interne Name (wie in API und Shell) nur als Tooltip; Auswahllisten zeigen den Hilfetext bis zum Doppelpunkt; ohne Verweise auf Entscheidungsnummern | Nutzer 2026-10-21: interne Namen und F-Nummern stoeren in der Oberflaeche |
| Zugangsdaten aendern | Benutzerfeld mit dem aktuellen Benutzer vorbelegt (`GET /credentials` liefert `user`); Hinweis: ohne HTTPS mitlesbar, bei der Anmeldung geht nur ein Pruefwert ueber das Netz, beim Aendern das neue Passwort lesbar | Nutzer 2026-10-21 |
| Live-Daten | WebSocket `/ws`; schliesst der Server sofort (Browser schickte keine Anmeldung), Abfrage alle 2 s und alle 30 s ein neuer WebSocket-Versuch; nach einer laengeren Verbindung neuer Versuch nach 3 s. Chrome sendet die Digest-Anmeldung beim WebSocket-Aufbau mit; andere Browser ungeprueft | |
| Verbindungsanzeige | "verbunden", sobald eine Nachricht oder Abfrage ankommt; 5 s ohne Daten "getrennt": `main.offline` grau, Klicks gesperrt; ein offener, aber stummer WebSocket (Kabel gezogen) wird dann verworfen. Grundlage ist das Lebenszeichen `evse/state` alle 2 s (SOLL-API-06). Abfragen mit 4 s Zeitlimit, nie zwei gleichzeitig. Periodische Nachlader (Parameter, Fehler) zeigen Verbindungsfehler nicht selbst an. Art der Verbindung (WebSocket/Abfrage) nur als Tooltip | Nutzer 2026-10-21: "Abfrage" war nicht greifbar und blieb bei getrennter Steuerung stehen |
| Favicon | Blitz weiss auf Tuerkis (#009670, abgerundetes Quadrat), SVG als `data:`-URL in `index.html` (etwa 200 Byte) | Nutzer 2026-10-22 (Variante 2 von 5); keine eigene Datei im Flash, keine Anfrage an die Steuerung, keine vergebliche Anfrage nach `/favicon.ico` |
| Kopf | Wortmarke des Nutzers als Text statt SVG, ohne Hintergrund (Farbe der Kopfzeile); "Wall" in der Textfarbe, "free" tuerkisgruen #009670 hell (Kontrast 3,8:1) bzw. #1fd1a0 dunkel; fett kursiv, eng; Link zur Uebersicht | Nutzer 2026-10-21: Farben ans Design angepasst; das Original-Gruen #00b88a war auf Weiss zu blass (2,6:1); Tuerkis bleibt vom Statusgruen unterscheidbar; Text skaliert mit dem Kopf und kostet keinen Flash |
| Gliederung | Leiste oben mit Uebersicht, Insights, Einstellungen, System; auf dem Handy volle Breite unter dem Titel. Einstellungen mit Unterseiten als Reiter. Adresse `#/<bereich>/<unterseite>`, unbekannt = Uebersicht bzw. Laden. Netzwerk: `net_*`, `telnet_enable`; MQTT: `mqtt_*`; Zaehler: `meter_*`. Nach dem Speichern von Werten, die erst nach einem Neustart wirken, steht der Hinweis mit "Jetzt neu starten" auf der Seite | Nutzer 2026-10-21 (F31) |
| Verlauf | je 10 min Mittelwert der Wirkleistung (W, nur gueltige Zaehlerwerte; Einspeisung = 0; ohne Zaehler `null`), Mittelwert des erlaubten Stroms (Tastgrad, 0,25 A) und Zustand als ODER (angesteckt, laedt, Fehler); 288 Zeilen = 48 h, 4 Byte je Zeile; Intervalle ab Start der Steuerung, Intervalle ohne Abtastung als leere Zeilen. `GET /evse/history` in Teilen; `age_ms` und das laufende Intervall stehen am Ende, damit sie zu den Zeilen passen. Anzeige: je Groesse ein Diagramm mit eigener Achse (Leistung nur mit Zaehler), Stufen je Intervall, Zustandsband darunter, Fenster mindestens 6 h, Werte nur beim Zeigen/Tippen als Hinweis an der Kurve (naechstgelegenes Intervall, damit auch schmale treffbar sind), keine feste Ablesezeile (Nutzer 2026-10-21: zu viel), neu geladen jede Minute | Nutzer 2026-10-21: Leistung, erlaubter Strom, Zustand, 10 min, 48 h; Darstellung mit zwei Diagrammen vorerst beibehalten (Alternative: mit Zaehler nur Leistung, ohne nur Strom; eine Linie "moegliche Leistung" scheidet aus, weil die Phasenzahl des Fahrzeugs unbekannt ist); Mittel statt Momentanwert, damit kurze Spitzen nicht verzerren; zwei Diagramme statt zweier y-Achsen |
| Ladestrom-Anzeige (Uebersicht) | Ring ueber 270 Grad von 0 bis zum Maximum (Drehschalter bzw. abgesenkt); blau "Erlaubt" (Strom des Tastgrads), gruener Innenring "Messwert" (Zaehler, hoechste Phase, nur wenn > 0), orange Marke fuer Kabel/Eingang, wenn kleiner als das Maximum; Kreis = Vorgabe (rot bei 0, am Ende, wenn ueber dem Maximum). Mitte: Messwert, ohne Zaehler der erlaubte Strom; darunter Leistung und "erlaubt". Darunter eine Zeile, was begrenzt ("begrenzt durch Kabel (20 A)", "Vorgabe: Laden aus", "Laden gesperrt", 5-%-Modus) | Nutzer 2026-10-21; Begriffe Erlaubt/Vorgabe/Messwert, weil "Freigabe" schon die Ladeerlaubnis (EN, Schnittstelle) bezeichnet |
| Vorgabe setzen | Tasten − / 6 / 10 / 16 / 32 A / + / "Laden aus", wirken sofort; −/+ in 1-A-Schritten zwischen 6 A und dem Maximum; Tasten ueber dem Maximum entfallen; nicht am Ring ziehbar. Hinweis: eine externe Vorgabe (z. B. Energiemanagement) kann den Wert ueberschreiben | Nutzer 2026-10-21: am Ring ziehen ist auf dem Handy fummelig |
| Ladestrom (Insights) | Tabelle Vorgabe, Maximum Drehschalter, Kabel, "Stromgrenze ueber Digitaleingang" (Grenze `input`, Funktionscodes 8..15; "Eingang" allein war unverstaendlich; in der Zeile "begrenzt durch ..." kurz "Digitaleingang"), Erlaubt, Messwert; wirkende Grenze orange mit "begrenzt"; keine Balken | Nutzer 2026-10-21 |
| Zaehleranzeige (**zu bestaetigen**) | Karte "Zaehler" in der Uebersicht immer, ohne Zaehler leer (Nutzer 2026-10-21: Anordnung bleibt gleich); mit Zaehler Leistung in kW, Zaehlerstand in kWh, Zustand; ungueltige Werte als "–"/"ungueltig"; Insights: Tabelle aller Messwerte, Fehlerzaehler; Einstellungen: `meter_type` und `meter_framing` als Auswahlliste | SOLL-MTR-32 |
| Ereignisse | Uebersicht: letzte 8 Ereignisse; Insights: ganzes Zugriffsprotokoll mit Fehlversuchen. Vorgeschichte aus `GET /system/log` beim Oeffnen, danach live ueber `evse/event` (ohne WebSocket alle 5 s neu); Uhrzeit = Startzeitpunkt + `t_ms` (Hinweis unter dem Protokoll: "Uhrzeiten auf wenige Sekunden genau"); Startzeitpunkt = Browserzeit zur Mitte der Anfrage - `uptime_ms` aus `/system/info`, bei jedem Abruf neu (mindestens jede Minute), damit ein lange offener Tab nicht mit dem Gang des Quarzes auseinanderlaeuft. Verlauf entsprechend: `age_ms` gilt zum Abrufzeitpunkt. Genauigkeit: Netzlaufzeit (ms) plus Quarzabweichung mal Alter des Eintrags (bei 20–50 ppm nach 48 h etwa 3–9 s) | keine Uhrzeit auf der Steuerung (F32) |

### Energiezaehler

| Punkt | Umsetzung | Grund |
| --- | --- | --- |
| Werte intern | ganze Zahlen in Tausendsteln der WARP-Einheit (mV, mA, mW, mHz, Wh), JSON mit 3 Nachkommastellen | keine Gleitkommazahlen ausserhalb der Dekodierung |
| Float32 der Eastron-Zaehler | ganzzahlig aus Mantisse und Exponent dekodiert, gerundet | `float` -> `int64` zieht auf dem Cortex-M3 die Double-Routinen nach (3,5 KB, Fallstricke) |
| Zyklus | 1 s von Beginn zu Beginn, im Zustand `error` 10 s; Ruhepause 50 ms vor jeder Anfrage, nach einem Timeout zusaetzlich 500 ms; je Block 3 Versuche, auch nach einer Ausnahmeantwort | SOLL-MTR-10/11 (Nutzer 2026-10-21) |
| Parameter | bei den Systemparametern (`/system/config`, `evse sys set`), Kennungen 3016..3019, wirken nach Neustart; Werkszustand `meter_type` = 0 | SOLL-MTR-01/02 |
| Ereignis | Typ `meter` mit `status` (`off`, `connecting`, `ok`, `error`), bei gesunkenem Zaehlerstand zusaetzlich `energy_decreased`: true | SOLL-MTR-13/14 |
| Ungueltige Werte an evcc (**zu bestaetigen**) | `null` wie WARP; evcc 0.317.2 macht daraus 0: bei ausgefallenem Zaehler zeigt evcc waehrend des Ladens 0 W und 0 A, der Zaehlerstand bleibt auf dem letzten Wert | WARP-konform; ein Weitersenden alter Werte wuerde einen Ausfall verbergen |
| UART4 ohne Zaehler | der UART-Treiber richtet UART4 beim Start ein; ohne Zaehler wird nie gesendet, PC12 bleibt Low | Zephyr initialisiert Devicetree-Geraete immer; ohne Sendefreigabe wirkt nichts auf den Bus |

### MQTT

| Punkt | Umsetzung | Grund |
| --- | --- | --- |
| Zeiten | Takt 100 ms; Keepalive 60 s; CONNACK-Frist 10 s; Neuverbindung nach 1 s, verdoppelt bis 60 s | wie der WebSocket-Takt; Keepalive ist der Zephyr-Default; 60 s Hoechstwartezeit begrenzt die Last bei dauerhaft fehlendem Broker |
| toter Broker | Zephyr wertet unbeantwortete PINGREQ nicht aus: Die Anwendung trennt, wenn ein PINGREQ eine Keepalive-Spanne lang offen ist. Zephyr pingt nur in Sendepausen; bei Dauerversand (Zaehlerwerte jede Sekunde) erkennt erst TCP den Ausfall (Wiederholungen bis zum Abbruch) | Erkennungszeit auf der Steuerung in H1 ansehen |
| Aenderungserkennung | FNV-1a-Pruefsumme der Nutzdaten statt einer Kopie je Topic | spart RAM; eine Kollision (2^-32 je Aenderung) verschiebt eine Meldung bis zur naechsten Aenderung |
| Neukonfiguration | nur wenn sich ein `mqtt_*`-Wert aendert: `online` = `false`, DISCONNECT, sofort neu verbinden | ein geordnetes DISCONNECT loest den Last Will nicht aus |
| Neustart der Steuerung | kein `online` = `false` vorab; der Broker meldet den Last Will nach 1,5 x Keepalive (bis 90 s) | der Neustart beendet die Verbindung ohne TCP-Abbau |
| Anmeldung am Broker | Client-ID = Hostname; Passwort nur zusammen mit einem Benutzer | MQTT 3.1.1 erlaubt kein Passwort ohne Benutzername. Zwei Steuerungen mit gleichem Hostname wuerden sich am Broker gegenseitig trennen |
| Praefix | leer = `wallfree/<MAC-Ende>` (in der Simulation ohne MAC `wallfree/000000`) | wie WARP `warp2/<uid>` |
| Retain bei Befehlen | eine mit Retain veroeffentlichte Nachricht kommt bei bestehendem Abo ohne Retain-Kennung an (MQTT-3.3.1-9) und wird angenommen; ignoriert wird die gespeicherte Kopie, die der Broker beim Abonnieren ausliefert | genau dieser Fall wuerde nach jedem Neustart einen alten Sollwert setzen (SOLL-MQTT-22) |
| Ereignisse | vor dem Verbinden entstandene werden verworfen | SOLL-MQTT-11 |
| Broker-Adresse | Pruefung wie Gateway, zusaetzlich 127.x erlaubt | PC-Simulation mit Broker auf demselben Rechner; auf der Steuerung schlaegt die Verbindung nur fehl |

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

Stand 2026-10-21, `-Os`, Assert-Texte ohne Ausdruck und Meldung, System-Heap 512 B: **313 920 B Flash** (95,8 % von 320 KB), **112 940 B RAM** (86,2 %).
Reserven und Einsparmoeglichkeiten: [Architektur](architektur.md#5-speicherbudget).

| Anteil (gemessen beim Hinzufuegen) | Flash | RAM |
| --- | --- | --- |
| HTTP-/WebSocket-API (HTTP-Server 13,5 KB, WebSocket 2,6 KB, PSA-SHA-1 5,9 KB) | 37,8 KB | 22 KB (davon 4 Client-Kontexte 9,7 KB) |
| Weboberflaeche (gzip) | 12 KB | – |
| Parameterspeicher | 8,2 KB | 5,1 KB |
| Shell-Kommandos, Resetgrund, Fehlerhistorie | 6 KB | – |
| Netz-/Systemparameter, Bremse | 5,7 KB | – |
| Ereignisse, Netzwerk-LED, Zugriffsprotokoll, `/evse/limits`, Ereignisanzeige | 5,1 KB | 3,9 KB |
| Ladelogik `lib/evse` (Code und Konstanten inkl. Parametertabelle mit Hilfetexten) | 15,7 KB | – |
| MQTT: Zephyr-Bibliothek (3.1.1, TCP) 4,2 KB, Thread und Anbindung, Parameter (8 Werte mit Hilfetext), `/mqtt/state`, Shell, Weboberflaeche | 9,1 KB | 5,1 KB (Stack 2 KB, Puffer 0,9 KB, Client, Status, Konfigurationskopien) |
| Verlauf (Ringpuffer 1,2 KB) und Gliederung der Weboberflaeche in Bereiche (gzip +2,9 KB) | 5,3 KB | 1,5 KB |
| Energiezaehler: UART-Treiber 2,1 KB, Zephyr-Modbus 2,7 KB, Interrupttabelle 1 KB, Zaehlermodul 1,4 KB, JSON/API/Shell/Parameter 2,9 KB, Weboberflaeche 1,4 KB, Rest (Treiberanteile) | 15,2 KB | 3,9 KB (Stack 1,3 KB, Modbus-Kontext 1 KB, Zaehlerzustand, WebSocket-Rahmen) |

Reserve rund **13,4 KB Flash und 17,7 KB RAM**. Der RAM ist bis auf die Stacks statisch belegt
(kein Wachstum zur Laufzeit); Stacks haben MPU-Schutz (Ueberlauf -> Fault -> sicherer Zustand),
ihre tatsaechliche Auslastung wird in H1 mit `kernel thread stacks` gemessen. Weitere
Einsparungen in der [Architektur](architektur.md#5-speicherbudget) (Netz-Shell erst nach H1).
Beim RAM (Heap umgesetzt, Nutzer 2026-10-21):

| Kandidat | Ersparnis | Befund |
| --- | --- | --- |
| **umgesetzt:** System-Heap (`CONFIG_HEAP_MEM_POOL_SIZE`) 16 KB -> 512 B | 15,9 KB | einziger Verweis auf `k_malloc` ist `websocket_send_msg` beim Maskieren, das nur die Client-Seite tut (Server: `is_client = 0`); Bau mit Heap 0 scheitert nur an diesem Verweis. Die 512 B halten ihn aufloesbar und fangen einen unerwarteten kleinen Nutzer ab. Kommt eine Zephyr-Funktion mit Heap-Bedarf hinzu (z. B. DNS, `HEAP_MEM_POOL_ADD_SIZE_*`), erneut pruefen |
| Thread-Stacks | je nach Messung | erst nach H1 (`kernel thread stacks`); die Simulation (64 Bit) ist nicht aussagekraeftig |
| `NET_MAX_CONN` 10 -> 8 | ~1,3 KB | 672 B je TCP-Verbindung; Bedarf: 4 HTTP, Telnet, MQTT, Listener, TIME_WAIT |
| Ethernet-DMA-Puffer (2 x 6 KB) | – | Anzahl fest im HAL-Treiber (4 + 4) |

## Annahmen der Hardwareschicht (in H1 zu pruefen)

- Pinbelegung, aktive Pegel und ADC-Skalierung stammen aus der Originalfirmware (keine
  Schaltplanbestaetigung), insbesondere RCM "High = ausgeloest" (Inversion zum Bender-Ausgang)
  und PE2 High = Schuetz ein.
- Reaktionszeit RCM-Flanke -> PE2 Low ueber den Zephyr-GPIO-Callback: Ziel <= 1 ms
  (SOLL-PROT-05), am Oszilloskop messen.
- MCO1 an PA8 als PHY-Takt und Reset an PA15 sind Hypothesen.
- IWDG-Umprogrammierung im Betrieb (direkt ueber Register, weil der Zephyr-Treiber das Limit
  nach dem Start nicht aendern kann) und Loeschzeit Sektor 7 <= 4000 ms (Zephyr-Devicetree,
  nicht Datenblatt).
- Zugriff auf RTC-Backup-Register ohne RTC-Takt (nur PWR-Takt und DBP).
- Zaehlerbus: RS-485 an Klemme A/B auf UART4 mit Freigabe PC12 (Firmwarebefund, wallbe2
  Belege §9): Sendepegel an A/B messen; danach je Modell einen echten Zaehler abfragen und die
  Fehlerzaehler bei offener und vertauschter Leitung ansehen. Zephyr-Modbus im nicht
  normkonformen Modus (8N1) ist nur gebaut.
- Ruhende HTTP-Verbindungen: Zephyr gibt Client-Plaetze nach 10 s Inaktivitaet per `shutdown`
  frei; in der Simulation fehlt `shutdown`. Auf der Steuerung mit offenem Browser und evcc
  gleichzeitig pruefen.
- MQTT: Betrieb gegen den Broker im LAN; Stackbedarf des MQTT-Threads (2048 B angenommen) mit
  dem Thread-Analysator messen; Erkennungszeit eines ausgeschalteten Brokers bei laufendem
  Zaehlerversand.

## Fallstricke

Gefunden beim Testen; gelten fuer Zephyr 4.4.2 und sind bei Updates erneut zu pruefen.

| Fallstrick | Folge | Massnahme |
| --- | --- | --- |
| `CONFIG_ZVFS_EVENTFD_MAX` = 1 (der Socket-Service belegt den einzigen Platz) | HTTP-Server startet nie; Meldung nur mit `CONFIG_NET_LOG` sichtbar | Wert 2 |
| `CONFIG_WEBSOCKET_MAX_CONTEXTS` zu klein | weitere WebSocket-Clients werden vor dem Anwendungs-Callback abgewiesen | 3 (SOLL-API-07 verlangt >= 2) |
| `LOG_PANIC()` auf der Systemworkqueue | schaltet das Telnet-Log-Backend synchron; dieses bricht seine Sende-Arbeit mit `k_work_cancel_delayable_sync()` auf derselben Workqueue ab -> Selbstblockade, sobald ein Telnet-Client verbunden ist; erst die Ueberwachung setzt zurueck (falscher Resetgrund) | kein `LOG_PANIC` in Thread-Pfaden |
| Abbau von WebSockets ueber `zsock_poll` auf dem WebSocket-Deskriptor | geschlossene Verbindungen blieben belegt (in der Simulation sicher) | `websocket_recv_msg` mit 2 ms Timeout je Client und Zyklus (auf der Steuerung hoechstens 6 ms je 100 ms) |
| Simulation: stdout blockgepuffert, wenn das Log in eine Datei geht | Meldungen erscheinen Kilobytes spaeter; sah wie ein Log-Stillstand nach Telnet-Abbruch aus (kein Befund fuer die Steuerung) | stdout zeilengepuffert |
| Simulation: Socket-Treiber NSOS ohne `shutdown` | ruhende HTTP-Verbindungen werden nie getrennt; ein offener Browser-Tab belegte alle 4 Plaetze | in der Simulation 8 Plaetze, 24 Deskriptoren |
| Simulation: Telnet-Backend setzt kein `SO_REUSEADDR` | nach Neustart blockiert TIME_WAIT den Port | `bind` setzt es in der Simulation |
| `twister --coverage` | bricht in dieser Version ab | `scripts/run-tests.sh --coverage` ruft gcovr direkt |
| Umrechnung `float` -> `int64_t` auf dem Cortex-M3 (`__aeabi_f2lz`) | zieht die Double-Softfloat-Routinen nach (+3,5 KB Flash), ohne dass der Quelltext `double` enthaelt | Float32 der Zaehler ganzzahlig dekodieren (`f32_scaled` in meter.c) |
| Zephyr-MQTT, Keepalive | `mqtt_live` sendet PINGREQ, wertet fehlende Antworten aber nicht aus; ein stumm verschwundener Broker bliebe "verbunden" | Anwendung prueft `unacked_ping` gegen `mqtt_keepalive_time_left` |
| Retain-Kennung beim Empfang | bei bestehendem Abo setzt der Broker sie nie (MQTT-3.3.1-9); ein Test mit `mosquitto_pub -r` zeigt das Ignorieren nicht | Test ueber eine Neuverbindung, die die gespeicherte Nachricht ausliefert |
| Zephyr-Modbus, Stoppbits | im normkonformen Modus erzwingt Zephyr bei "keine Paritaet" 2 Stoppbits; die Eastron-Werkseinstellung 8N1 ginge nicht | `CONFIG_MODBUS_NONCOMPLIANT_SERIAL_MODE`, Stoppbits je Format setzen |

## Offen

- H0 und H1 nur mit Freigabe ([Architektur](architektur.md#7-hardware-inbetriebnahme));
  danach Update-Uebergabe (SOLL-SH-10, SOLL-BOOT-30) und Startablauf auf der Hardware.
- Zaehleranzeige der Weboberflaeche bestaetigen lassen; ebenso die Weitergabe
  ungueltiger Zaehlerwerte als `null` (evcc zeigt dann 0 W).
- RAM: 97 % belegt; vor der naechsten groesseren Erweiterung Heapbedarf messen.
- Browser-Durchlauf um Zugangsdaten-Seite, Parameter schreiben und Netz/System erweitern
  (bisher nur ueber die API getestet).
