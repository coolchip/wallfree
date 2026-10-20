# Implementierungsstand Ladelogik

Stand: 2026-10-19. Hardwareunabhaengige Ladelogik, Parameterspeicher und API-Abbildung als Zephyr-Modul (reines C99), getestet
auf dem PC (`native_sim/native/64`, ztest). Die ganze Anwendung laeuft zusaetzlich als
PC-Simulation mit Ende-zu-Ende-Tests (Abschnitt "Ende-zu-Ende-Test auf dem PC"). **Nicht** auf
Hardware ausgefuehrt; Tests belegen keine elektrische Sicherheit.

## Module und Anforderungen

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
| [persist.c](../lib/evse/persist.c) | Laden/Speichern der Konfiguration und des Verriegelungs-Endzustands (465), Loeschbedingung, Import aus Sektor 3 | SOLL-BOOT-04, SOLL-NVM-03/04/10, SOLL-PAR-03/40 | evse_persist (mit Simulationsmodell und simuliertem Flash) |
| [nvm.c](../lib/nvm/nvm.c) | Log-Format in einer Loescheinheit: Anhaengen, RAM-Puffer bei vollem Bereich, Verdichten, Formatieren, Erkennung unterbrochener Vorgaenge | SOLL-NVM-01..05 | `tests/nvm` (nvm: Stromausfall nach jedem Byte beim Anhaengen und Verdichten, Ausfall beim Loeschen, Pruefsummenfehler, voller Bereich) |
| [orig.c](../lib/nvm/orig.c) | Nur-Lese-Zugriff auf das Originalformat in Sektor 3 | SOLL-NVM-10 | nvm_orig (synthetisches Abbild) |
| [md5.c](../lib/api/md5.c), [digest.c](../lib/api/digest.c) | MD5 (RFC 1321), HTTP-Digest mit `qop=auth`: Challenge, Pruefung, zustandslose Nonces | SOLL-API-07 | `tests/api` (api_md5: RFC-1321-Vektoren; api_digest: RFC-2617-Beispiel, Rundlauf wie evcc, Fehlformen) |
| [json.c](../lib/api/json.c) | JSON-Ausgabe mit Maskierung, Lesen flacher Objekte | 12.1 | api_json |
| [evse_api.c](../lib/api/evse_api.c) | `evse/state` (WARP-Abbildung), `external_current`, alle Parameter lesen/schreiben, Parameterbeschreibung in Teilen | SOLL-API-03..05/30 | evse_api (mit Simulationsmodell) |
| [sysconf.c](../lib/api/sysconf.c) | Netz- und Systemparameter: Pruefung, JSON, Speicherkodierung (Kennungen 3010..3015), Import der Originalfelder 404/315..326 | 10.1, F20, F25, SOLL-NVM-10 | api_sysconf |
| [reset.c](../lib/api/reset.c) | Resetgrund aus Flags und Backup-Code | SOLL-RT-14 | api_reset |
| [ratelimit.c](../lib/api/ratelimit.c) | Bremse gegen Passwort-Durchprobieren je Absenderadresse | SOLL-API-07 | api_ratelimit |
| [events.c](../lib/api/events.c) | Ereignisse: Vergleich zweier Momentaufnahmen (Fehler, Schuetz, Verriegelungsergebnis, Zustand), Ringpuffer mit fortlaufender Nummer, JSON | SOLL-ERR-10 | evse_charger (api_events: Vergleich, Ueberlauf, JSON, Ladezyklus mit Verriegelung und Fehlerstrom im Simulationsmodell) |
| [param.c](../lib/evse/param.c) | Parametertabelle (Konfiguration, Laufzeit, Status) mit Name, Kennung, Herkunftsfeld, Einheit, Bereich, Default; Schreiben mit Grenz-/Zustandspruefung und Fehlercode; Wiederherstellen gespeicherter Werte | 10, SOLL-PAR-01..03/40, E14, SOLL-API-20/30 | evse_param (Konsistenz, Grenzen jedes Parameters, Sonderregeln) |

Aufruf: `scripts/run-tests.sh` (twister; `--simple` ohne twister, `--coverage` mit gcovr).
Stand 2026-10-19: **187 Tests in fuenf Suiten, alle bestanden**; Abdeckung `lib/` **96 % Zeilen,
88 % Verzweigungen** (gcovr; nicht abgedeckt sind vor allem Lesefehler des Flashs). Twister-/Testabhaengigkeiten wurden am 2026-10-19 mit
Freigabe des Nutzers in `~/zephyrproject/.venv` installiert (`requirements-run-test.txt`,
`requirements-build-test.txt`); `twister --coverage` bricht in dieser Version ab, daher ruft das
Skript gcovr direkt auf. Groesse fuer Cortex-M3 (`arm-zephyr-eabi-gcc` 14.3.0, `-Os`, ohne
Linker-GC): ca. **15,7 KB** Code und Konstanten (davon die Parametertabelle mit Namen und
Hilfetexten fuer Web/Shell), keine veraenderlichen statischen Daten.

## Bei der Implementierung festgelegte Details

Diese Punkte waren in der Spezifikation nicht eindeutig. **Alle Festlegungen dieser Tabelle hat
der Nutzer bestaetigt** (2026-10-19): die ersten sieben ("in Ordnung"), "Entriegeln klemmt"
einzeln, alle uebrigen (*neu*: Speicher, API, System, Web, Bedienung, HW) gesammelt;
Ereignisformat, Netzwerk-LED, WebSocket-Abbau und Log vor Neustart am 2026-10-20.
Neue Festlegungen werden weiter mit *neu* markiert, bis sie bestaetigt sind.

| Punkt | Umsetzung | Grund |
| --- | --- | --- |
| ERR-CONFIG bei D6 ohne LD-Eingang | nur, wenn **D4 und D6** an sind | ohne D4 wird LD nie ausgewertet; ein Fehler wuerde das Laden grundlos sperren |
| CP-Status-Entprellung | Erzwingung durch Fehler mit 20 ms entprellt; F (Diode, Schuetz, Verfuegbarkeit) und E (CP ungueltig) sofort | SOLL-CP-08 |
| RCM-Selbsttest | ERR_OUT fuehrt waehrend des **ganzen** Tests zum Fehlschlag (1.27 prueft nur bei 0/100 ms) | "jede Abweichung" (SOLL-PROT-10) |
| Code 42 "Suspended EV" | S5 mit Status B, laufender PWM und vorherigem Laden im selben Steckvorgang | Bedeutung im Original nur aus dem 1.27-Webtext |
| Code 38 Nachlauf | aktiv bei Code-19-Eingang und Status A/B oder < 1 s nach Schuetz-Aus | Ist-Spez. §12 |
| S9 bei CP A mit Schutzfehler | Entriegeln ist trotzdem erlaubt (Schuetz aus), bis 4 Versuche | sonst bliebe der Stecker bei z. B. RCM-Sensorfehler verriegelt |
| Ladeerlaubnis/Freigabe | Freigabe verlangt **keinen** aktiven Fehler (strenger als Original, das nur einzelne Fehler prueft) | SOLL-EN-02 |
| Entriegeln klemmt | nach 4 Fehlversuchen bleiben Zaehler und ERR-UNLOCK bis zum naechsten Fahrzeug bzw. Neustart; keine weiteren Impulse. Der misslungene Start-Entriegelungsversuch zaehlt mit | sonst Dauerversuche am blockierten Motor alle paar Sekunden (auch das Original versucht es endlos) |
| D9 ohne Verriegelungswunsch | S1 geht mit D9 nur bei ML = 1 nach S2 | vorher Pendeln S1<->S2 in jedem Zyklus |
| *(Speicher):* Loeschen nur in S1 | im Betrieb nur S1 mit CP A, Schuetz aus, ohne Verriegelungszyklus und RCM-Selbsttest; S0 nur vor dem Start der Ladelogik (Spezifikation: "S1 bzw. S0") | in S0 laufen Selbsttest und Start-Entriegelung, deren Zeitfolge der stehende Bus stoeren wuerde |
| *(Speicher):* Ablauf des Verdichtens | Ladelogik per Sperre angehalten, Thread-/Messueberwachung ausgesetzt, IWDG auf Maximum (~32 s); CPU steht bis zu 4 s. CP bleibt +12 V (TIM1 laeuft weiter), Schuetz aus; ein in dieser Zeit angestecktes Fahrzeug wird danach erkannt | Spezifikation SOLL-NVM-04, SOLL-RT-13 |
| *(Speicher):* Ausloeser des Verdichtens | ausstehende Werte, beschaedigter Rest oder weniger als 4 KB frei; Pruefung jede Sekunde im Speicher-Thread | 128 KB reichen fuer rund 10 000 Aenderungen; Verdichten selten |
| *(Speicher):* ungueltiger Bereich beim Start | sofort neu angelegt, Werkseinstellungen, Meldung im Log und in `evse nvm`; kein Fehlerbit der Ladelogik | SOLL-NVM-05 sagt nur "Meldung" |
| *(Speicher):* Import | 409/412 aus dem unteren Byte (im Original 1-Byte-Felder); jede Uebernahme ueber `evse_param_set`; Netzwerkfelder 315..326 werden nur angezeigt, weil Wallfree noch keine Netzparameter hat | SOLL-NVM-10 |
| *(Speicher):* Werksreset per Shell | `evse factory-reset confirm`, nur im sicheren Zustand, danach Neustart | einfachster Weg zu konsistenten Defaults |
| *(API):* Werkszugang | Benutzer `admin`, Passwort `wallfree`; bis zur Aenderung beantworten alle Ressourcen ausser `/credentials` mit 403 (auch evcc) | SOLL-API-07 nennt kein Passwort |
| *(API):* Nonces | gelten bis zum Neustart (Geheimnis je Start), keine Ablaufzeit, keine Pruefung von `nc`; Nonce eines frueheren Starts -> 401 mit `stale=true` | evcc erneuert die Challenge nur nach einem 401; eine Ablaufzeit liesse den WebSocket-Aufbau (kein 401 moeglich) dauerhaft scheitern. Folge: mitgeschnittene Anfragen sind bis zum Neustart wiederholbar (ohne TLS ohnehin mitlesbar) |
| *(API):* WebSocket-Anmeldung | Zephyr sendet 101 vor dem Anwendungs-Callback; ohne gueltige vorab gesendete Anmeldung wird die Verbindung ohne Daten geschlossen. Ein Browser muss vorher eine HTTP-Ressource abgerufen haben | Zephyr-Server bietet keinen Haken vor dem Upgrade |
| *(API):* `error_state` 1 | sonstige Fehler (z. B. ERR-LOCK, CP ungueltig) -> 1, bei WARP unbelegt; Vorrang RCM > Schuetz > Schalter/Konfiguration | WARP kennt keinen allgemeinen Fehlercode |
| *(API):* `contactor_state` | aus dem Schuetz-Soll (3/0), `contactor_error` 2 bzw. 6 bei ERR-CONTACTOR | keine Spannungsmessung vor/nach dem Schuetz |
| *(API):* `allowed_charging_current` | 0 ohne PWM, auch im Statuswert gleichen Namens (vorher Rechenwert auch ohne PWM) | Spezifikation 12.1 |
| *(API):* `POST /evse/params` | jeder Wert einzeln (nicht atomar); Syntaxfehler -> nichts geschrieben | Zustandsregeln stecken in den Schreibfunktionen |
| *(API):* Zugangsdaten speichern | ein Eintrag (HA1 + Benutzer, Kennung 3001), damit ein Stromausfall nie Benutzer und HA1 trennt | |
| *(System):* Wirksamkeit | Netz-/Systemparameter wirken erst nach Neustart; `POST /system/restart` nur in S1 ohne Fahrzeug (sonst 409) | Adresswechsel im Betrieb wuerde laufende Verbindungen (evcc) kappen |
| *(System):* Telnet aus | `shell_stop`: Port 23 bleibt offen, Kommandos werden nicht ausgefuehrt | Zephyr kann den Telnet-Port zur Laufzeit nicht schliessen |
| *(System):* widerspruechliche gespeicherte Adresse | Start mit DHCP und Warnung im Log | lieber erreichbar als gar kein Netz |
| *(System):* Import Netz | Felder 404 (DHCP), 315..326 (IP/Maske/Gateway) nach Original-Webseite; alles oder nichts; Geraetename 310 wird nicht uebernommen | Belegung aus `wallbe2/analysis/web/lan.html` |
| *(API):* Bremse | 5 freie Fehlversuche je Adresse, dann 10 s, verdoppelt bis 5 min, 429 mit `Retry-After`; nur falsches Passwort zaehlt (fehlende Anmeldung und veraltete Nonce nicht) | |
| *(API):* Absender beim WebSocket | aus Zephyr-Interna ermittelt (Kopfzeilenfeld des Client-Kontexts, Zephyr 4.4.2); bei Zephyr-Updates pruefen | der WebSocket-Callback liefert keinen TCP-Socket |
| *(Bedienung):* Neustart | nur geordnet: `evse_request_shutdown` (wirkt wie fehlende Verfuegbarkeit -> S7 mit 5-s-Frist -> S10), warten bis Schuetz aus, hoechstens 15 s, dann sicherer Zustand und Reset; gilt fuer Shell, HTTP, Werksreset. `kernel reboot` entfernt | SOLL-RT-22, SOLL-SH-20 |
| *(Bedienung):* Quittieren | `evse errors clear` / `POST /evse/errors/ack` quittiert nur ERR-CONTACTOR (F14); die Ausgabe nennt fuer alle anderen die Ruecksetzbedingung | SOLL-ERR-03 |
| *(Bedienung):* Selbsttest | `evse selftest` nur in S1, CP A, Schuetz aus | SOLL-SH-07 |
| *(Bedienung):* Fehlerhistorie | 32 Ereignisse im RAM (Zeit, neu, geloescht, Zustand), geht beim Neustart verloren; HTTP gibt so viele aus, wie in 1,5 KB passen (`truncated`) | SOLL-ERR-01 |
| *(HW):* IWDG | startet zu Beginn von `main` (250 ms); bis zum Schutzthread bedient ihn `main`, auch waehrend des Werksreset-Wartens | SOLL-BOOT-03, SOLL-RT-12 |
| *(HW):* Resetgrund | BKP19 = 0x5746xxxx vor Software-Resets; Grund `supervision_limit`, `fault`, `user_restart`, `factory_reset`, `interface_hang` bzw. aus RCC_CSR | SOLL-RT-14 |
| *(HW):* Schnittstellen-Ueberwachung | Speicher-, WebSocket-Thread, System-Workqueue (10 s); HTTP-Server/Telnet von Zephyr nicht ueberwachbar | SOLL-RT-11 |
| *(Web):* Anmeldung | Oberflaeche hinter Digest wie die API (Anmeldedialog des Browsers); im Werkszugang nur Zugangsdaten-Seite; nach der Aenderung fragt der Browser neu | SOLL-API-07 |
| *(Web):* Live-Daten | WebSocket `/ws`; schliesst der Server sofort (Browser schickt beim WebSocket-Aufbau evtl. keine Anmeldung, auf Hardware ungeprueft), Abfrage alle 2 s | |
| *(Web):* Sollstrom-Schieber | 6..32 A in 1-A-Schritten, dazu "Laden aus" (0) | evcc ueberschreibt den Wert, wenn verbunden |
| *(API):* Ereignisformat (SOLL-ERR-10) | WebSocket-Topic `evse/event`, je Ereignis ein Rahmen, Payload `{"seq", "t_ms", "type", ...}`: `state` (`from`/`to` = S0..S10), `error` (`set`/`cleared` als Namen wie `/evse/errors`), `lock` (`lock_state` 1/3/5, nur Endzustaende), `contactor` (`on`, Schuetz-Soll); `seq` fortlaufend ab 1 je Start, Luecke = verloren (Ringpuffer 16); kein Nachsenden beim Verbinden; jedes Ereignis zusaetzlich als Logzeile `Ereignis {...}` | Spezifikation nennt kein Format; evcc ignoriert unbekannte Topics |
| *(HW):* Netzwerk-LED | PC2 aktiv Low [HYP]; "verbunden" = Link (Carrier) und bevorzugte IPv4-Adresse; Takt aus der Systemworkqueue (haengt diese, steht die LED) | SOLL-LED-06 |
| *(API):* Erkennen geschlossener WebSockets | `websocket_recv_msg` mit 2 ms Timeout je Client und Zyklus statt `zsock_poll` auf dem WebSocket-Deskriptor | mit Timeout 0 bzw. Poll sahen die Host-Sockets der Simulation den Abbau nie; auf der Steuerung hoechstens 6 ms je 100 ms |
| *(Bedienung):* Log vor Neustart | geordneter Neustart: Meldung, dann 200 ms fuer den Log-Thread, dann Reset (bei Fristablauf Schuetz sofort per Register aus); kein `LOG_PANIC` in Thread-Neustartpfaden, Meldungen der Ueberwachung koennen verloren gehen (Grund steht in BKP19) | `LOG_PANIC` blockierte mit verbundenem Telnet-Client die Systemworkqueue (siehe unten) |
| Abbruch des RCM-Selbsttests nach dem Testimpuls | Zustand "Abbruch laeuft" bis 3,4 s nach Teststart: SWITCH_1/2 ausgeblendet, ERR_OUT wirksam; kein neuer Test, kein Weg zum Schuetz in dieser Zeit | der Sensor faehrt seinen Trip-Test zu Ende; dessen Ausloesungen wurden sonst als echter Fehlerstrom gewertet |

## Parameter: Festlegungen

- Kennungen fuer den Speicher: Feldnummer des Originals, wo vorhanden (z. B. 525), sonst
  1001.. (Konfiguration) bzw. 2000.. (Status). Gespeichert werden nur Konfigurationswerte.
- Konfigurationsaenderungen wirken sofort (`evse_apply_config()`); eine Aenderung, die die
  Konfiguration widerspruechlich macht (z. B. LD-Funktion entfernt bei D4+D6), setzt sofort
  ERR-CONFIG und beendet damit ein laufendes Laden.
- `lock_state` (465) loest beim Schreiben einen echten Zyklus aus und ist nur in S1 erlaubt;
  `dip_override` (802) nur in S1; `hardware_limit_override` (801) nur absenken, 0 hebt auf.

## Parameterspeicher: Format und Ausfallverhalten

Format in [nvm.h](../include/wallfree/nvm/nvm.h). Eintrag = Kopf (Kennung, Laenge, CRC-8) +
Daten + CRC-32; ein Wert mit 4 Byte belegt 12 Byte. Bereichskopf: Kennung direkt nach dem
Loeschen, Abschluss (Version) zuletzt. Getestetes Verhalten bei Stromausfall:

| Ausfall waehrend | Ergebnis beim naechsten Start |
| --- | --- |
| Anhaengen (jedes Byte) | alter oder neuer Wert, nie ein Mischwert; zerstoerter Kopf -> Rest gesperrt bis zum Verdichten, Werte davor bleiben |
| Loeschen beim Verdichten | Bereich ungueltig -> Werkseinstellungen, Meldung |
| Schreiben beim Verdichten | Bereich ungueltig (Kennung ohne Abschluss) -> Werkseinstellungen, Meldung; keine Teilergebnisse |
| genau zwischen Loeschende und erstem Byte | wie Erststart: Werkseinstellungen **ohne** Meldung (einziges unerkanntes Fenster) |
| erstes Schreiben auf leeren Bereich (Kopf ohne Abschluss) | "ungueltig" gemeldet, obwohl nichts verloren ist |

Gefundener Fehler im Entwurf: Ohne fruehe Kennung sah jedes unterbrochene Verdichten nach dem
Loeschen wie ein Erststart aus (Werkseinstellungen ohne Meldung). Behoben durch die Aufteilung des
Bereichskopfs.

## Durch Tests gefundene und behobene Fehler (2026-10-19)

1. S7 mit Ziel "nicht verfuegbar": Die Vorrangliste meldete jeden Zyklus "behandelt", das
   geordnete Ende lief nie weiter; bei nicht reagierendem Fahrzeug waere das Schuetz an
   geblieben. Regressionstest `test_availability_lost_vehicle_ignores`.
2. Abstecken waehrend des RCM-Selbsttests (S4) wurde erst nach Testende behandelt (Rang 4 jetzt
   S4..S7, Spezifikation angepasst).
3. Abbruch des Selbsttests erzeugte einen falschen, verriegelnden Fehlerstromfehler (siehe oben).

## Anwendung und Hardwareschicht (`app/`, nur gebaut)

Stand 2026-10-19. Baut fuer `ev_cc_ac1` ohne Warnung: **297 012 B Flash** (von 320 KB, 91 %),
**115 452 B RAM** (88 %); Ereignisse und Netzwerk-LED +1,6 KB Flash, +1,3 KB RAM; Korrekturen
aus dem Ende-zu-Ende-Test +0,2 KB Flash/RAM; Netz-/Systemparameter und Bremse +5,7 KB Flash, Weboberflaeche +12 KB,
Shell-Kommandos/Resetgrund/Fehlerhistorie +6 KB. Parameterspeicher: +8,2 KB Flash, +5,1 KB RAM. HTTP-/WebSocket-API:
+37,8 KB Flash (HTTP-Server 13,5 KB, WebSocket 2,6 KB, PSA-SHA-1 aus tf-psa-crypto 5,9 KB),
+22 KB RAM (vor allem 4 Client-Kontexte des HTTP-Servers, 9,7 KB). `tf-psa-crypto` wurde am
2026-10-19 mit Freigabe des Nutzers als west-Modul nachgeladen (`manifest.project-filter` um
`+tf-psa-crypto` ergaenzt); Zephyrs WebSocket-Handshake braucht SHA-1 ueber PSA. **Nicht auf Hardware ausgefuehrt** (Sicherheitsgrenzen).

```sh
cd ~/zephyrproject && . .venv/bin/activate && cd zephyr
west build -p always -b ev_cc_ac1 /pfad/zu/wallfree/app -d ~/zephyrproject/build-wallfree
```

| Datei | Inhalt | Anforderungen |
| --- | --- | --- |
| [hw_stm32.c](../app/src/hw_stm32.c) | TIM1 CH1/PE9 (PSC 49, center-aligned Modus 1, ARR 1000, 1 kHz), ADC1 10 Raenge in zwei 5er-Gruppen, Start im TIM1-Update-Interrupt bei CNT < 50 bzw. > 950, DMA2 Stream0 zirkular 80 Worte, Skalierung wie Original; MCO1 = HSE an PA8; PA15-Resetimpuls 11 ms; fruehe sichere Pinpegel; sicherer Zustand per Register | SOLL-RT-01..03, SOLL-IO-02, SOLL-BOOT-02/09, SOLL-RT-20 |
| [hw_gpio.c](../app/src/hw_gpio.c) | alle Ein-/Ausgaenge aus `zephyr,user`; RCM-Flankeninterrupt schaltet PE2 per Register ab und setzt einen Schutz-Latch, der Ein-Befehle sperrt; PE0-Flankenzeitstempel; PB2 erst nach der ersten Ausgabe | SOLL-PROT-01/05, SOLL-IO-01/21 |
| [evse_task.c](../app/src/evse_task.c) | Ereigniserkennung im Automatenthread (Momentaufnahme vor/nach `evse_step`, Ringpuffer mit Spinlock, Ausgabe im WebSocket-Thread); Schutzthread (Prio 0, 1 ms): RCM-Pegel zyklisch, Ueberwachung Automat (100 ms) und Messbloecke (50 ms), IWDG 250 ms, Wartungsmodus fuer Flash-Loeschen; Automatenthread (Prio 2, 10 ms und je Messblock), meldet Aenderungen des Verriegelungszustands an den Speicher-Thread; Fault-Handler: sicherer Zustand, Neustart | SOLL-RT-04/05/10..12/21 |
| [storage.c](../app/src/storage.c) | Flash-Adapter auf die Partitionen (Sektor 7 lesen/schreiben/loeschen, Sektor 3 nur lesen, Lage beim Bauen geprueft); Speicher-Thread (Prio 12): Verriegelungszustand speichern, Verdichten im sicheren Zustand; Werksreset, Import | SOLL-NVM-01..05/10, SOLL-RT-13, SOLL-BOOT-04/20/21 |
| [http_api.c](../app/src/http_api.c) | Ereignisse als Topic `evse/event` an alle WebSocket-Clients und ins Log (SOLL-ERR-10). HTTP-Server Port 80: `/info/name`, `/info/features`, `/evse/state`, `/evse/external_current`, `/evse/params`, `/evse/param_info`, `/credentials`, `/system/config`, `/system/config_info`, `/system/restart`, WebSocket `/ws` (bis 3 Clients, Push-Thread alle 100 ms bzw. sofort nach Schreibzugriffen, ein Topic je Rahmen); Digest-Pruefung je Anfrage | SOLL-API-01..07/30 |
| [netcfg.c](../app/src/netcfg.c) | Netz-/Systemparameter laden, Hostname, DHCP oder feste Adresse, Telnet; Aendern (atomar), Import; Netzwerk-LED PC2 (Systemworkqueue, 500 ms): an bei Link und IPv4-Adresse, sonst 1 Hz | 10.1, F20, F25, SOLL-LED-06 |
| [auth.c](../app/src/auth.c) | Zugangsdaten (HA1), Werkszugang, Passwortaenderung, Nonce-Geheimnis je Start | SOLL-API-07 |
| [web/](../web/) | Weboberflaeche: Werkszugang -> Zugangsdaten festlegen; "Einfach": Zustand, Fahrzeug, erlaubter Strom, Fehler, Sollstrom setzen; "Experte": alle Parameter mit Bereich, Default, Feld, Hilfetext und Schreiben, Netz/System mit Neustart, Zugangsdaten | SOLL-API-31 |
| [main.c](../app/src/main.c) | Startablauf: GPIO, Parameterspeicher (ungueltig -> neu anlegen), Reset-Taster 5 s -> Bereich loeschen, gespeicherte Werte laden, DIP/Drehschalter, Messung, Threads | SOLL-BOOT-01..10/20 |
| [shell_evse.c](../app/src/shell_evse.c) | `evse status`, `param list/get/set`, `current`, `enable`, `avail`, `pause`, `pwm5`, `lock`, `unlock`, `errors [clear]`, `selftest`, `reboot`, `ack`, `nvm`, `sys list/set`, `factory-reset confirm`, `import-original` (Telnet) | SOLL-SH-01..09/11, SOLL-SH-19/20 |
| [sysinfo.c](../app/src/sysinfo.c) | Resetgrund beim Start, Firmwarestand (`git describe` beim Bauen), Laufzeit; `GET /system/info` | SOLL-RT-14 |
| [hw_sim.c](../app/src/hw_sim.c), [sim_host.c](../app/src/sim_host.c) | nur `native_sim`: Hardwaremodell statt `hw_stm32.c`/`hw_gpio.c`, Neustart per `execv` (siehe unten) | – |

Pruefung am Build (2026-10-19): Systemtakt 100 MHz (`CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC`),
Ladeadresse `0x08010000`, Interrupttabelle: TIM1_UP -> `tim1_up_isr`, DMA2_Stream0 ->
`dma2_s0_isr`, EXTI0/3/4/9_5 -> Zephyr-GPIO-Treiber, ETH -> Ethernet-Treiber. RM0033 Abb. 98:
CCR = ARR haelt den Ausgang im Center-aligned-Modus dauerhaft auf 1 (CP +12 V DC).

Korrigiert: Die Board-Beschreibung stellte noch 120 MHz ein (aus dem Groessentest); jetzt 100 MHz
(PLLN 200) wie entschieden. TIM1/ADC1 sind aus der Board-Beschreibung entfernt, weil die
Anwendung sie direkt steuert; der Groessentest bringt sie per `spike-size/app.overlay` wieder mit.

### Speicherreserven (gemessen 2026-10-19)

Basis: 295 228 B Flash, 113 916 B RAM; gebaut mit `-Os` (`CONFIG_SIZE_OPTIMIZATIONS`) und `-g`.
Die Debug-Abschnitte (~6 MB in der ELF-Datei) liegen auf Adresse 0 und kommen nicht in den Flash.
Jede Variante einzeln gebaut (west build mit zusaetzlicher Kconfig-Option), alle ohne Warnung:

| Variante | Flash | Ersparnis | Bemerkung |
| --- | --- | --- | --- |
| `CONFIG_SIZE_OPTIMIZATIONS_AGGRESSIVE` (`-Oz`) | 295 228 | 0 | GCC 14 erzeugt fuer Cortex-M3 denselben Code wie `-Os` (Funktionsgroessen identisch) |
| `CONFIG_SHELL_HELP=n` | 287 216 | 8 KB | Hilfetexte der Shell entfallen |
| `CONFIG_LTO=y` (+ `ISR_TABLES_LOCAL_DECLARATION`) | 270 916 | 24 KB | geprueft: Interrupts TIM1_UP/DMA2_S0 richtig eingetragen, alle 9 HTTP-Ressourcen da, eigener Fault-Handler in `z_arm_fatal_error` eingebettet (ruft sicheren Zustand und Reset); Fehlersuche mit Debugger schwerer |
| Assert-Texte weg (`ASSERT_VERBOSE=n`, `ASSERT_NO_FILE_INFO/COND_INFO/MSG_INFO=y`) | 263 224 | 32 KB | Pruefungen bleiben, nur Datei/Bedingung/Meldung fehlen in der Ausgabe |
| `CONFIG_ASSERT=n` | 255 196 | 40 KB | Pruefungen entfallen, nicht empfohlen |
| `CONFIG_NET_SHELL=n` | 251 380 | 43 KB | mehr als die 17 KB aus dem Groessentest |
| LTO + Assert-Texte weg | 247 284 | 48 KB | |
| LTO + `ASSERT=n` | 240 340 | 55 KB | |

**Nutzerentscheidung (2026-10-19):** Vorerst nichts abschalten. Die Netz-Shell bleibt fuer den
ersten Probelauf (Netzleistung ansehen). Wird es knapp, in dieser Reihenfolge: Netz-Shell,
Assert-Texte, LTO, Shell-Hilfe (zusammen ~100 KB).

### Annahmen der Hardwareschicht (in H1 zu pruefen)

- Pinbelegung, aktive Pegel und ADC-Skalierung stammen aus dem Original (Firmwarebefunde,
  keine Schaltplanbestaetigung), insbesondere RCM "High = ausgeloest" (Inversion zum Bender-
  Ausgang) und PE2 High = Schuetz ein.
- Reaktionszeit RCM-Flanke -> PE2 Low ueber den Zephyr-GPIO-Callback: Ziel <= 1 ms
  (SOLL-PROT-05), am Oszilloskop zu messen.
- MCO1 an PA8 und Reset an PA15 fuer den PHY sind Hypothesen.
- IWDG-Umprogrammierung im Betrieb (direkt ueber die Register, weil der Zephyr-Treiber das Limit
  nach dem Start nicht aendern kann) und Loeschzeit Sektor 7 <= 4000 ms (Zephyr-Devicetree, nicht
  Datenblatt). Ob der Bootloader Sektor 7 loescht, klaert H0.

### Noch offen in der Hardwareschicht

- Update-Uebergabe an den Bootloader (SOLL-SH-10, SOLL-BOOT-30): zurueckgestellt bis H0
  (SOLL-BOOT-31: Bootloader zuerst auslesen).
- Zugriff auf RTC-Backup-Register ohne RTC-Takt (nur PWR-Takt und DBP) ist eine Annahme (H1).

## Ende-zu-Ende-Test auf dem PC (native_sim, 2026-10-19)

Dieselbe Anwendung (`app/`) baut auch fuer `native_sim/native/64` als Linux-Programm. Nur die
Hardwareschicht ist ersetzt; Threads, Ladelogik, Parameterspeicher, HTTP/Digest/WebSocket,
Weboberflaeche und Telnet-Shell sind der Code der Steuerung. Die Tests belegen nichts ueber die
reale Hardware (Pegel, Zeiten, Interrupts, elektrische Sicherheit).

```sh
scripts/run-e2e.sh                # bauen (build/sim) und alle Tests (Python, nur Standardbibliothek)
scripts/run-e2e.sh -k Faults      # Auswahl (unittest -k)
scripts/sim-run.sh [--build] [--dip=0x109] [--rotary=5] [--http-port=8081] [--reset-button=6000]
```

`sim-run.sh` startet die Simulation zum Ausprobieren: Web/API `http://127.0.0.1:8081/`
(Werkszugang `admin`/`wallfree`), Telnet-Shell `127.0.0.1:2323`, Flash in `build/sim-flash.bin`
(loeschen = fabrikneu). Ports unter 1024 braeuchten Root-Rechte, daher 8081 und 2323.

| Teil | Umsetzung |
| --- | --- |
| Messung | [hw_sim.c](../app/src/hw_sim.c): Zeitgeber 1 ms, Messblock alle 8 ms; Pegel wie das Szenario-Modell der Ladelogik-Tests (Fahrzeug A..E, Diode, PP-Kabel 13/20/32/63 A, Eingaenge, Versorgung) |
| RCM | Bender-Ablauf beim Testimpuls (SWITCH_2 330 ms, SWITCH_1 770 ms, Ende 1,9 s), Fehlerstrom, ERR_OUT, Sensor ohne Reaktion; Signalflanke setzt den Schutz-Latch sofort (wie der Interrupt) |
| Aktor, Schuetz | Fahrt 200 ms, LD gepulst/permanent/keine, Klemmen; Sense 50 Hz bei Schuetz ein oder verschweisst |
| IWDG | Software-Zeitgeber in simulierter Zeit (250 ms, beim Flash-Loeschen 32 s) |
| Reset | Prozess startet sich per `execv` neu ([sim_host.c](../app/src/sim_host.c), Host-Seite); Flags/BKP19 und der physische Modellzustand (Fahrzeug bleibt angesteckt, Aktorstellung) gehen ueber Umgebungsvariablen mit; neuer Prozess = Einschalten |
| Flash | Zephyr-Flash-Simulator mit Datei (`--flash=`), Partitionen an den Stellen von Sektor 3 und 7 ([Overlay](../app/boards/native_sim_native_64.overlay)) |
| Netz | Host-Sockets (`NET_NATIVE_OFFLOADED_SOCKETS`), keine Adress-/DHCP-Konfiguration; `bind` setzt dort `SO_REUSEADDR`, weil das Telnet-Backend es nicht tut und TIME_WAIT nach einem Neustart den Port blockiert |
| Bedienung | Shell-Kommando `sim` (`status`, `car`, `diode`, `cable`, `din`, `ld`, `rcm`, `actuator`, `weld`, `link`, `supply`, `hang`); DIP/Drehschalter/Reset-Taster per Kommandozeile |
| Logging | verzoegert mit Log-Thread wie auf der Steuerung (native_sim waehlt sonst den Sofortmodus); `NET_LOG` an; stdout zeilengepuffert (sonst erscheint das Log in einer Datei erst kilobyteweise) |
| Netz-LED | `sim link 0`/`1` steuert, ob das Netz als verbunden gilt |

Tests ([tests/e2e/test_e2e.py](../tests/e2e/test_e2e.py), Hilfen in [wfsim.py](../tests/e2e/wfsim.py)):

| Klasse | Inhalt | Anforderungen |
| --- | --- | --- |
| T1Api | 401 mit Challenge; Werkszugang nur `/credentials` und Oberflaeche (403 sonst); Passwortregeln; Aenderung; `/info/*`, `/system/*`, alle Webdateien gzip; Ruhezustand; Parameter lesen/Beschreibung/Grenzen/Syntaxfehler/Schreiben; Laden B -> PWM -> C -> Schuetz; Sollstrom 10 A (Tastgrad 166, 9960 mA), Ablehnung 3 A, 0 = aus, wieder ein; WebSocket Erstsynchronisation und Push < 1 s; 3 WebSockets gleichzeitig und Wiederverwendung der Plaetze, Abbruch ohne Close-Rahmen; WebSocket ohne bzw. mit falscher Anmeldung geschlossen; Abstecken; Neustart mit Persistenz (Zugangsdaten, Parameter, Hostname) und Resetgrund `user_restart`, Sollstrom wieder Startwert; Neustart waehrend des Ladens geordnet und danach wieder Laden; Bremse 5 x 401, dann 429 mit `Retry-After` | SOLL-API-01..07/30/31, SOLL-CUR-21, SOLL-NVM-03, SOLL-PAR-03, SOLL-RT-14/22 |
| T2Faults | Fehlerstrom beim Laden: Schuetz aus, `error_state` 3, RCM-Fehler aktiv; Messbloecke stehen -> Reset `supervision_limit`; Reset-Taster 6 s beim Start -> Werkszugang wieder aktiv | SOLL-PROT-01/05, SOLL-RT-10/14, SOLL-BOOT-20 |
| T1Api (Forts.) | Ereignisse beim Sollstrom 0 und wieder ein (Zustand aus S6, Schuetz aus/ein, `seq`/`t_ms` steigend, Logzeile); Netzwerk-LED an, bei `sim link 0` 1 Hz, danach wieder an | SOLL-ERR-10, SOLL-LED-06 |
| T2Faults (Forts.) | Ereignisse beim Fehlerstrom: `error` mit RCM, Schuetz aus, Zustand S9 | SOLL-ERR-10 |
| T3ZephyrTelnet | harter Telnet-Abbruch: HTTP, WebSocket, neue Telnet-Sitzung, geordneter Neustart und Logausgabe laufen weiter | SOLL-RT-11 |

Stand 2026-10-19: **23 Tests, alle bestanden** (zweimal hintereinander), Laufzeit ca. 70 s.

### Durch den Ende-zu-Ende-Test gefundene Fehler (behoben)

Alle vier betreffen auch die Steuerung; im Hardware-Build mitkorrigiert.

1. **HTTP-Server startete nie:** `CONFIG_ZVFS_EVENTFD_MAX` stand auf 1, den Platz belegt der
   Socket-Service; der HTTP-Server scheiterte an `eventfd` (-ENOMEM) und versuchte es jede
   Sekunde neu. Ohne `CONFIG_NET_LOG` erscheint diese Meldung **nicht** im Log. Jetzt 2.
2. **Nur ein WebSocket moeglich:** `CONFIG_WEBSOCKET_MAX_CONTEXTS` stand auf 1 (vorgesehen 3,
   SOLL-API-07 verlangt mindestens 2). Jetzt 3. Ein vierter Client wird von Zephyr vor dem
   Anwendungs-Callback abgewiesen (Log "HTTP request handling error (-2)").
3. **Geschlossene WebSockets blieben belegt** (in der Simulation sicher, auf der Steuerung
   unklar): Abbau wurde ueber `zsock_poll` auf dem WebSocket-Deskriptor gesucht; umgestellt
   (Festlegung oben).
4. **Geordneter Neustart konnte haengen:** `LOG_PANIC()` im Neustart (Systemworkqueue) schaltet
   das Telnet-Log-Backend synchron; dieses bricht seine Sende-Arbeit auf der Systemworkqueue mit
   `k_work_cancel_delayable_sync()` ab -> Selbstblockade, sobald ein Telnet-Client verbunden ist
   und Ausgabe ansteht. Erst die Schnittstellen-Ueberwachung bzw. der IWDG setzten zurueck, mit
   falschem Resetgrund. `LOG_PANIC` aus den Thread-Pfaden entfernt (Festlegung oben).

### Irrtum korrigiert: kein Log-Stillstand nach Telnet-Abbruch

Zwischenzeitlich sah es so aus, als bliebe nach einem harten Telnet-Abbruch die Logausgabe
dauerhaft stehen (Verdacht auf `shell_telnet.c` in Zephyr 4.4.2). Ursache war die
**Blockpufferung von stdout** in der Simulation (Log in eine Datei umgeleitet, native_sim leert
nicht): Meldungen erschienen erst Kilobytes spaeter. Mit zeilengepuffertem stdout laufen nach dem
Abbruch alle Meldungen weiter (Test T3, 3 von 3 Laeufen). Kein Befund fuer die Steuerung.
Fehler 4 oben (LOG_PANIC) bleibt ein echter Befund: die Selbstblockade folgt aus dem Quelltext
(`k_work_cancel_delayable_sync()` der Telnet-Sende-Arbeit aus der Systemworkqueue heraus) und trat
reproduzierbar auf (Resetgrund `fault` statt `user_restart`).

### Abweichungen der Simulation von der Steuerung

- Messwerte unabhaengig vom Tastgrad (wie das Testmodell); kein ADC-Rauschen, keine Zeitfehler.
- Thread-Prioritaeten wirken, aber native_sim laeuft nicht praeemptiv in Echtzeit: eine
  Endlosschleife ohne Warten haelt die simulierte Zeit an (Schutzthread und IWDG eingeschlossen).
- Netzparameter (feste Adresse, DHCP) werden nicht angewandt; `/info/name` liefert `uid` 000000
  (keine MAC).
- Sektor 3 ist leer (Import aus dem Original nicht pruefbar ohne Abbild).

## Noch nicht umgesetzt

- Startablauf auf der Hardware (SOLL-BOOT, H1).
- Zugriffsprotokoll (Nutzer 2026-10-20): Inhalt bleibt wie bisher (falsche Anmeldungen mit
  Absender, Ereignisse); **zusaetzlich abrufbar ueber HTTP** als Ringpuffer im RAM (z. B. letzte
  32 Eintraege) unter `GET /system/log`. Keine Schreibzugriffe, erfolgreichen Anmeldungen oder
  Shell-Kommandos. Noch umzusetzen; vorher in die Spezifikation aufnehmen.
- Ereignisse in der Weboberflaeche noch nicht angezeigt.
- RAM-Reserve ~14 KB: Heap (16 KB) und Client-Kontexte bei Bedarf verkleinern.
- Weboberflaeche im Browser gegen die Simulation noch nicht durchgeklickt (Dateien werden im
  Ende-zu-Ende-Test nur abgerufen und entpackt); evcc gegen die Simulation spaeter (Nutzer).

## Naechste Schritte (Stand 2026-10-19)

1. ~~Festlegungen bestaetigen~~ – erledigt, Nutzer 2026-10-19 (alle bestaetigt).
2. ~~Ende-zu-Ende-Test auf dem PC vorbereiten~~ – erledigt 2026-10-19 (`scripts/run-e2e.sh`,
   20 Tests; vier Fehler der Anwendung gefunden und behoben, siehe unten). Offen: Browser-
   Durchlauf der Oberflaeche, evcc (Nutzer: spaeter).
3. ~~Kleinere offene Anforderungen~~: SOLL-ERR-10 und SOLL-LED-06 umgesetzt (2026-10-19, vom
   Nutzer bestaetigt 2026-10-20); Zugriffsprotokoll entschieden (abrufbar ueber `GET /system/log`),
   Umsetzung offen.
4. **H0 (Freigabe noetig):** Bootloader auslesen; danach SOLL-SH-10/SOLL-BOOT-30 (Update-Uebergabe),
   Pruefung Start-SP, loescht der Bootloader Sektor 7.
5. **H1 (Freigabe noetig):** Pruefplan fuer den ersten Probelauf ohne Last: Pinpegel, LEDs, IWDG,
   Reaktionszeit RCM -> PE2, Backup-Register, Netzleistung (Netz-Shell bleibt dafuer im Build).
6. Speicherreserve: bei Bedarf in der beschlossenen Reihenfolge Netz-Shell, Assert-Texte, LTO,
   Shell-Hilfe abschalten (Nutzer 2026-10-19).
