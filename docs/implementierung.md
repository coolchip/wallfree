# Implementierungsstand Ladelogik

Stand: 2026-10-19. Hardwareunabhaengige Ladelogik als Zephyr-Modul (reines C99), getestet
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
| [param.c](../lib/evse/param.c) | Parametertabelle (Konfiguration, Laufzeit, Status) mit Name, Kennung, Herkunftsfeld, Einheit, Bereich, Default; Schreiben mit Grenz-/Zustandspruefung und Fehlercode; Wiederherstellen gespeicherter Werte | 10, SOLL-PAR-01..03/40, E14, SOLL-API-20/30 | evse_param (Konsistenz, Grenzen jedes Parameters, Sonderregeln) |

Aufruf: `scripts/run-tests.sh` (twister; `--simple` ohne twister, `--coverage` mit gcovr).
Stand 2026-10-19: **120 Tests, alle bestanden**; Abdeckung `lib/evse` **96 % Zeilen, 90 %
Verzweigungen** (gcovr, alle drei Suiten). Twister-/Testabhaengigkeiten wurden am 2026-10-19 mit
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
| *neu:* Abbruch des RCM-Selbsttests nach dem Testimpuls | Zustand "Abbruch laeuft" bis 3,4 s nach Teststart: SWITCH_1/2 ausgeblendet, ERR_OUT wirksam; kein neuer Test, kein Weg zum Schuetz in dieser Zeit | der Sensor faehrt seinen Trip-Test zu Ende; dessen Ausloesungen wurden sonst als echter Fehlerstrom gewertet |

## Parameter: Festlegungen

- Kennungen fuer den Speicher: Feldnummer des Originals, wo vorhanden (z. B. 525), sonst
  1001.. (Konfiguration) bzw. 2000.. (Status). Gespeichert werden nur Konfigurationswerte.
- Konfigurationsaenderungen wirken sofort (`evse_apply_config()`); eine Aenderung, die die
  Konfiguration widerspruechlich macht (z. B. LD-Funktion entfernt bei D4+D6), setzt sofort
  ERR-CONFIG und beendet damit ein laufendes Laden.
- `lock_state` (465) loest beim Schreiben einen echten Zyklus aus und ist nur in S1 erlaubt;
  `dip_override` (802) nur in S1; `hardware_limit_override` (801) nur absenken, 0 hebt auf.

## Durch Tests gefundene und behobene Fehler (2026-10-19)

1. S7 mit Ziel "nicht verfuegbar": Die Vorrangliste meldete jeden Zyklus "behandelt", das
   geordnete Ende lief nie weiter; bei nicht reagierendem Fahrzeug waere das Schuetz an
   geblieben. Regressionstest `test_availability_lost_vehicle_ignores`.
2. Abstecken waehrend des RCM-Selbsttests (S4) wurde erst nach Testende behandelt (Rang 4 jetzt
   S4..S7, Spezifikation angepasst).
3. Abbruch des Selbsttests erzeugte einen falschen, verriegelnden Fehlerstromfehler (siehe oben).

## Anwendung und Hardwareschicht (`app/`, nur gebaut)

Stand 2026-10-19. Baut fuer `ev_cc_ac1` ohne Warnung: **225 508 B Flash** (von 320 KB),
**83 324 B RAM**, noch ohne HTTP-Server. **Nicht auf Hardware ausgefuehrt** (Sicherheitsgrenzen).

```sh
cd ~/zephyrproject && . .venv/bin/activate && cd zephyr
west build -p always -b ev_cc_ac1 /pfad/zu/wallfree/app -d ~/zephyrproject/build-wallfree
```

| Datei | Inhalt | Anforderungen |
| --- | --- | --- |
| [hw_stm32.c](../app/src/hw_stm32.c) | TIM1 CH1/PE9 (PSC 49, center-aligned Modus 1, ARR 1000, 1 kHz), ADC1 10 Raenge in zwei 5er-Gruppen, Start im TIM1-Update-Interrupt bei CNT < 50 bzw. > 950, DMA2 Stream0 zirkular 80 Worte, Skalierung wie Original; MCO1 = HSE an PA8; PA15-Resetimpuls 11 ms; fruehe sichere Pinpegel; sicherer Zustand per Register | SOLL-RT-01..03, SOLL-IO-02, SOLL-BOOT-02/09, SOLL-RT-20 |
| [hw_gpio.c](../app/src/hw_gpio.c) | alle Ein-/Ausgaenge aus `zephyr,user`; RCM-Flankeninterrupt schaltet PE2 per Register ab und setzt einen Schutz-Latch, der Ein-Befehle sperrt; PE0-Flankenzeitstempel; PB2 erst nach der ersten Ausgabe | SOLL-PROT-01/05, SOLL-IO-01/21 |
| [evse_task.c](../app/src/evse_task.c) | Schutzthread (Prio 0, 1 ms): RCM-Pegel zyklisch, Ueberwachung Automat (100 ms) und Messbloecke (50 ms), IWDG 250 ms; Automatenthread (Prio 2, 10 ms und je Messblock); Fault-Handler: sicherer Zustand, Neustart | SOLL-RT-04/05/10..12/21 |
| [main.c](../app/src/main.c) | Startablauf: GPIO, Reset-Taster 5 s, DIP/Drehschalter, Messung, Threads | SOLL-BOOT-01..10/20 |
| [shell_evse.c](../app/src/shell_evse.c) | `evse status`, `evse param list/get/set`, `evse ack` ueber die Parametertabelle (Telnet) | SOLL-SH-01/02/06, SOLL-SH-19/20 |

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

### Noch offen in der Hardwareschicht

- Resetgrund ueber den Neustart sichern (SOLL-RT-14), Neustart nur ohne Ladung bei haengenden
  Netz-Threads (SOLL-RT-11), IWDG-Verlaengerung fuer Flash-Loeschen (SOLL-RT-13).
- Werksreset und Verriegelungszustand persistent (mit dem Parameterspeicher).

## Noch nicht umgesetzt

- Parameterspeicher (SOLL-NVM; Kennungen und `evse_param_restore()` sind vorbereitet),
  Werksreset, Startablauf auf der Hardware (SOLL-BOOT).
- System- und Netzparameter (Hostname, IP, Zugangsdaten, Telnet) gehoeren nicht zur Ladelogik
  und kommen in eine eigene Tabelle der Anwendung.
- HTTP/WebSocket-API, Weboberflaeche, Zugangsschutz, Shell (Abschnitt 12).
- Fehlerhistorie mit Zeitstempeln (SOLL-ERR-01, bisher nur Sammelwert `errors_seen`).
