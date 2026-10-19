# Implementierungsstand Ladelogik

Stand: 2026-10-19. Hardwareunabhaengige Ladelogik, Parameterspeicher und API-Abbildung als Zephyr-Modul (reines C99), getestet
auf dem PC (`native_sim/native/64`, ztest). **Nicht** auf Hardware ausgefuehrt; Tests belegen
keine elektrische Sicherheit.

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
| [charger.c](../lib/evse/charger.c) | Automat S0..S10, Vorrangliste, Weckfolge, Freigabe, Fehlermodell, LEDs, sicherer Zustand | 5, 9.2, 11, SOLL-PROT-01/41, SOLL-LOCK-20..41, SOLL-RT-20 | `tests/evse/charger` (evse_charger, evse_charger2: 45 Szenarien mit Simulationsmodell) |
| [outputs.c](../lib/evse/outputs.c) | Ausgangsfunktionen 0..42 | 9.3 | evse_outputs (Wahrheitstabelle aller Codes) |
| [persist.c](../lib/evse/persist.c) | Laden/Speichern der Konfiguration und des Verriegelungs-Endzustands (465), Loeschbedingung, Import aus Sektor 3 | SOLL-BOOT-04, SOLL-NVM-03/04/10, SOLL-PAR-03/40 | evse_persist (mit Simulationsmodell und simuliertem Flash) |
| [nvm.c](../lib/nvm/nvm.c) | Log-Format in einer Loescheinheit: Anhaengen, RAM-Puffer bei vollem Bereich, Verdichten, Formatieren, Erkennung unterbrochener Vorgaenge | SOLL-NVM-01..05 | `tests/nvm` (nvm: Stromausfall nach jedem Byte beim Anhaengen und Verdichten, Ausfall beim Loeschen, Pruefsummenfehler, voller Bereich) |
| [orig.c](../lib/nvm/orig.c) | Nur-Lese-Zugriff auf das Originalformat in Sektor 3 | SOLL-NVM-10 | nvm_orig (synthetisches Abbild) |
| [md5.c](../lib/api/md5.c), [digest.c](../lib/api/digest.c) | MD5 (RFC 1321), HTTP-Digest mit `qop=auth`: Challenge, Pruefung, zustandslose Nonces | SOLL-API-07 | `tests/api` (api_md5: RFC-1321-Vektoren; api_digest: RFC-2617-Beispiel, Rundlauf wie evcc, Fehlformen) |
| [json.c](../lib/api/json.c) | JSON-Ausgabe mit Maskierung, Lesen flacher Objekte | 12.1 | api_json |
| [evse_api.c](../lib/api/evse_api.c) | `evse/state` (WARP-Abbildung), `external_current`, alle Parameter lesen/schreiben, Parameterbeschreibung in Teilen | SOLL-API-03..05/30 | evse_api (mit Simulationsmodell) |
| [param.c](../lib/evse/param.c) | Parametertabelle (Konfiguration, Laufzeit, Status) mit Name, Kennung, Herkunftsfeld, Einheit, Bereich, Default; Schreiben mit Grenz-/Zustandspruefung und Fehlercode; Wiederherstellen gespeicherter Werte | 10, SOLL-PAR-01..03/40, E14, SOLL-API-20/30 | evse_param (Konsistenz, Grenzen jedes Parameters, Sonderregeln) |

Aufruf: `scripts/run-tests.sh` (twister; `--simple` ohne twister, `--coverage` mit gcovr).
Stand 2026-10-19: **163 Tests in fuenf Suiten, alle bestanden**; Abdeckung `lib/` **95 % Zeilen,
88 % Verzweigungen** (gcovr; nicht abgedeckt sind vor allem Lesefehler des Flashs). Twister-/Testabhaengigkeiten wurden am 2026-10-19 mit
Freigabe des Nutzers in `~/zephyrproject/.venv` installiert (`requirements-run-test.txt`,
`requirements-build-test.txt`); `twister --coverage` bricht in dieser Version ab, daher ruft das
Skript gcovr direkt auf. Groesse fuer Cortex-M3 (`arm-zephyr-eabi-gcc` 14.3.0, `-Os`, ohne
Linker-GC): ca. **15,7 KB** Code und Konstanten (davon die Parametertabelle mit Namen und
Hilfetexten fuer Web/Shell), keine veraenderlichen statischen Daten.

## Bei der Implementierung festgelegte Details

Diese Punkte waren in der Spezifikation nicht eindeutig. Die ersten sieben hat der Nutzer am
2026-10-19 bestaetigt ("in Ordnung"); die mit *neu* markierten sind danach hinzugekommen.
Die Festlegung "Entriegeln klemmt" hat der Nutzer am 2026-10-19 ebenfalls bestaetigt.

| Punkt | Umsetzung | Grund |
| --- | --- | --- |
| ERR-CONFIG bei D6 ohne LD-Eingang | nur, wenn **D4 und D6** an sind | ohne D4 wird LD nie ausgewertet; ein Fehler wuerde das Laden grundlos sperren |
| CP-Status-Entprellung | Erzwingung durch Fehler mit 20 ms entprellt; F (Diode, Schuetz, Verfuegbarkeit) und E (CP ungueltig) sofort | SOLL-CP-08 |
| RCM-Selbsttest | ERR_OUT fuehrt waehrend des **ganzen** Tests zum Fehlschlag (1.27 prueft nur bei 0/100 ms) | "jede Abweichung" (SOLL-PROT-10) |
| Code 42 "Suspended EV" | S5 mit Status B, laufender PWM und vorherigem Laden im selben Steckvorgang | Bedeutung im Original nur aus dem 1.27-Webtext |
| Code 38 Nachlauf | aktiv bei Code-19-Eingang und Status A/B oder < 1 s nach Schuetz-Aus | Ist-Spez. §12 |
| S9 bei CP A mit Schutzfehler | Entriegeln ist trotzdem erlaubt (Schuetz aus), bis 4 Versuche | sonst bliebe der Stecker bei z. B. RCM-Sensorfehler verriegelt |
| Ladeerlaubnis/Freigabe | Freigabe verlangt **keinen** aktiven Fehler (strenger als Original, das nur einzelne Fehler prueft) | SOLL-EN-02 |
| *neu, bestaetigt:* Entriegeln klemmt | nach 4 Fehlversuchen bleiben Zaehler und ERR-UNLOCK bis zum naechsten Fahrzeug bzw. Neustart; keine weiteren Impulse. Der misslungene Start-Entriegelungsversuch zaehlt mit | sonst Dauerversuche am blockierten Motor alle paar Sekunden (auch das Original versucht es endlos) |
| *neu:* D9 ohne Verriegelungswunsch | S1 geht mit D9 nur bei ML = 1 nach S2 | vorher Pendeln S1<->S2 in jedem Zyklus |
| *neu (Speicher):* Loeschen nur in S1 | im Betrieb nur S1 mit CP A, Schuetz aus, ohne Verriegelungszyklus und RCM-Selbsttest; S0 nur vor dem Start der Ladelogik (Spezifikation: "S1 bzw. S0") | in S0 laufen Selbsttest und Start-Entriegelung, deren Zeitfolge der stehende Bus stoeren wuerde |
| *neu (Speicher):* Ablauf des Verdichtens | Ladelogik per Sperre angehalten, Thread-/Messueberwachung ausgesetzt, IWDG auf Maximum (~32 s); CPU steht bis zu 4 s. CP bleibt +12 V (TIM1 laeuft weiter), Schuetz aus; ein in dieser Zeit angestecktes Fahrzeug wird danach erkannt | Spezifikation SOLL-NVM-04, SOLL-RT-13 |
| *neu (Speicher):* Ausloeser des Verdichtens | ausstehende Werte, beschaedigter Rest oder weniger als 4 KB frei; Pruefung jede Sekunde im Speicher-Thread | 128 KB reichen fuer rund 10 000 Aenderungen; Verdichten selten |
| *neu (Speicher):* ungueltiger Bereich beim Start | sofort neu angelegt, Werkseinstellungen, Meldung im Log und in `evse nvm`; kein Fehlerbit der Ladelogik | SOLL-NVM-05 sagt nur "Meldung" |
| *neu (Speicher):* Import | 409/412 aus dem unteren Byte (im Original 1-Byte-Felder); jede Uebernahme ueber `evse_param_set`; Netzwerkfelder 315..326 werden nur angezeigt, weil Wallfree noch keine Netzparameter hat | SOLL-NVM-10 |
| *neu (Speicher):* Werksreset per Shell | `evse factory-reset confirm`, nur im sicheren Zustand, danach Neustart | einfachster Weg zu konsistenten Defaults |
| *neu (API):* Werkszugang | Benutzer `admin`, Passwort `wallfree`; bis zur Aenderung beantworten alle Ressourcen ausser `/credentials` mit 403 (auch evcc) | SOLL-API-07 nennt kein Passwort |
| *neu (API):* Nonces | gelten bis zum Neustart (Geheimnis je Start), keine Ablaufzeit, keine Pruefung von `nc`; Nonce eines frueheren Starts -> 401 mit `stale=true` | evcc erneuert die Challenge nur nach einem 401; eine Ablaufzeit liesse den WebSocket-Aufbau (kein 401 moeglich) dauerhaft scheitern. Folge: mitgeschnittene Anfragen sind bis zum Neustart wiederholbar (ohne TLS ohnehin mitlesbar) |
| *neu (API):* WebSocket-Anmeldung | Zephyr sendet 101 vor dem Anwendungs-Callback; ohne gueltige vorab gesendete Anmeldung wird die Verbindung ohne Daten geschlossen. Ein Browser muss vorher eine HTTP-Ressource abgerufen haben | Zephyr-Server bietet keinen Haken vor dem Upgrade |
| *neu (API):* `error_state` 1 | sonstige Fehler (z. B. ERR-LOCK, CP ungueltig) -> 1, bei WARP unbelegt; Vorrang RCM > Schuetz > Schalter/Konfiguration | WARP kennt keinen allgemeinen Fehlercode |
| *neu (API):* `contactor_state` | aus dem Schuetz-Soll (3/0), `contactor_error` 2 bzw. 6 bei ERR-CONTACTOR | keine Spannungsmessung vor/nach dem Schuetz |
| *neu (API):* `allowed_charging_current` | 0 ohne PWM, auch im Statuswert gleichen Namens (vorher Rechenwert auch ohne PWM) | Spezifikation 12.1 |
| *neu (API):* `POST /evse/params` | jeder Wert einzeln (nicht atomar); Syntaxfehler -> nichts geschrieben | Zustandsregeln stecken in den Schreibfunktionen |
| *neu (API):* Zugangsdaten speichern | ein Eintrag (HA1 + Benutzer, Kennung 3001), damit ein Stromausfall nie Benutzer und HA1 trennt | |
| *neu:* Abbruch des RCM-Selbsttests nach dem Testimpuls | Zustand "Abbruch laeuft" bis 3,4 s nach Teststart: SWITCH_1/2 ausgeblendet, ERR_OUT wirksam; kein neuer Test, kein Weg zum Schuetz in dieser Zeit | der Sensor faehrt seinen Trip-Test zu Ende; dessen Ausloesungen wurden sonst als echter Fehlerstrom gewertet |

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

Stand 2026-10-19. Baut fuer `ev_cc_ac1` ohne Warnung: **271 504 B Flash** (von 320 KB, 83 %),
**110 396 B RAM** (84 %). Parameterspeicher: +8,2 KB Flash, +5,1 KB RAM. HTTP-/WebSocket-API:
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
| [evse_task.c](../app/src/evse_task.c) | Schutzthread (Prio 0, 1 ms): RCM-Pegel zyklisch, Ueberwachung Automat (100 ms) und Messbloecke (50 ms), IWDG 250 ms, Wartungsmodus fuer Flash-Loeschen; Automatenthread (Prio 2, 10 ms und je Messblock), meldet Aenderungen des Verriegelungszustands an den Speicher-Thread; Fault-Handler: sicherer Zustand, Neustart | SOLL-RT-04/05/10..12/21 |
| [storage.c](../app/src/storage.c) | Flash-Adapter auf die Partitionen (Sektor 7 lesen/schreiben/loeschen, Sektor 3 nur lesen, Lage beim Bauen geprueft); Speicher-Thread (Prio 12): Verriegelungszustand speichern, Verdichten im sicheren Zustand; Werksreset, Import | SOLL-NVM-01..05/10, SOLL-RT-13, SOLL-BOOT-04/20/21 |
| [http_api.c](../app/src/http_api.c) | HTTP-Server Port 80: `/info/name`, `/info/features`, `/evse/state`, `/evse/external_current`, `/evse/params`, `/evse/param_info`, `/credentials`, WebSocket `/ws` (bis 3 Clients, Push-Thread alle 100 ms bzw. sofort nach Schreibzugriffen, ein Topic je Rahmen); Digest-Pruefung je Anfrage | SOLL-API-01..07/30 |
| [auth.c](../app/src/auth.c) | Zugangsdaten (HA1), Werkszugang, Passwortaenderung, Nonce-Geheimnis je Start | SOLL-API-07 |
| [main.c](../app/src/main.c) | Startablauf: GPIO, Parameterspeicher (ungueltig -> neu anlegen), Reset-Taster 5 s -> Bereich loeschen, gespeicherte Werte laden, DIP/Drehschalter, Messung, Threads | SOLL-BOOT-01..10/20 |
| [shell_evse.c](../app/src/shell_evse.c) | `evse status`, `evse param list/get/set` (persistente Werte werden gespeichert), `evse ack`, `evse nvm`, `evse factory-reset confirm`, `evse import-original` (Telnet) | SOLL-SH-01/02/06/08/09, SOLL-SH-19/20 |

Pruefung am Build (2026-10-19): Systemtakt 100 MHz (`CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC`),
Ladeadresse `0x08010000`, Interrupttabelle: TIM1_UP -> `tim1_up_isr`, DMA2_Stream0 ->
`dma2_s0_isr`, EXTI0/3/4/9_5 -> Zephyr-GPIO-Treiber, ETH -> Ethernet-Treiber. RM0033 Abb. 98:
CCR = ARR haelt den Ausgang im Center-aligned-Modus dauerhaft auf 1 (CP +12 V DC).

Korrigiert: Die Board-Beschreibung stellte noch 120 MHz ein (aus dem Groessentest); jetzt 100 MHz
(PLLN 200) wie entschieden. TIM1/ADC1 sind aus der Board-Beschreibung entfernt, weil die
Anwendung sie direkt steuert; der Groessentest bringt sie per `spike-size/app.overlay` wieder mit.

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

- Resetgrund ueber den Neustart sichern (SOLL-RT-14), Neustart nur ohne Ladung bei haengenden
  Netz-Threads (SOLL-RT-11).
- IWDG schon vor dem Werksreset-Warten starten (SOLL-BOOT-03); bisher startet der Schutzthread
  ihn erst nach dem Laden der Parameter.
- Power-LED 5 Hz nach dem Werksreset bis zum Loslassen (SOLL-BOOT-20).

## Noch nicht umgesetzt

- Startablauf auf der Hardware (SOLL-BOOT, H1).
- System- und Netzparameter (Hostname, IP, Zugangsdaten, Telnet) gehoeren nicht zur Ladelogik
  und kommen in eine eigene Tabelle der Anwendung (Speicherkennungen ab 3000); danach Import
  der Netzwerkfelder 315..326.
- Weboberflaeche (SOLL-API-31), restliche Shell-Kommandos (12.3).
- Test der API mit evcc gegen einen `native_sim`-Build mit Netz (Spezifikation 12: nur nach
  Absprache); bisher sind Digest und JSON nur als Bausteine getestet, der HTTP-Ablauf nur gebaut.
- Keine Bremse gegen Passwort-Durchprobieren; Zugriffsprotokoll fehlt.
- RAM-Reserve ~17 KB: Heap (16 KB) und Client-Kontexte bei Bedarf verkleinern.
- Fehlerhistorie mit Zeitstempeln (SOLL-ERR-01, bisher nur Sammelwert `errors_seen`).
