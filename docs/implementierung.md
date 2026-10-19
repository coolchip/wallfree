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

Aufruf: `scripts/run-tests.sh` (twister; `--simple` ohne twister, `--coverage` mit gcovr).
Stand 2026-10-19: **107 Tests, alle bestanden**; Abdeckung `lib/evse` **95 % Zeilen, 88 %
Verzweigungen** (gcovr, alle drei Suiten). Twister-/Testabhaengigkeiten wurden am 2026-10-19 mit
Freigabe des Nutzers in `~/zephyrproject/.venv` installiert (`requirements-run-test.txt`,
`requirements-build-test.txt`); `twister --coverage` bricht in dieser Version ab, daher ruft das
Skript gcovr direkt auf. Groesse fuer Cortex-M3 (`arm-zephyr-eabi-gcc` 14.3.0, `-Os`, ohne
Linker-GC): ca. **8,8 KB** Code, keine statischen Daten.

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

## Durch Tests gefundene und behobene Fehler (2026-10-19)

1. S7 mit Ziel "nicht verfuegbar": Die Vorrangliste meldete jeden Zyklus "behandelt", das
   geordnete Ende lief nie weiter; bei nicht reagierendem Fahrzeug waere das Schuetz an
   geblieben. Regressionstest `test_availability_lost_vehicle_ignores`.
2. Abstecken waehrend des RCM-Selbsttests (S4) wurde erst nach Testende behandelt (Rang 4 jetzt
   S4..S7, Spezifikation angepasst).
3. Abbruch des Selbsttests erzeugte einen falschen, verriegelnden Fehlerstromfehler (siehe oben).

## Noch nicht umgesetzt

- Hardwareschicht (ADC/DMA, TIM1, GPIO, EXTI-Schutzpfad mit direktem PE2-Abschalten,
  IWDG und Thread-Watchdog, Fault-Handler) - SOLL-RT, SOLL-IO, SOLL-PROT-05.
- Parameterverwaltung mit Grenzpruefung und Schreibregeln (SOLL-PAR), Parameterspeicher
  (SOLL-NVM), Werksreset, Startablauf auf der Hardware (SOLL-BOOT).
- HTTP/WebSocket-API, Weboberflaeche, Zugangsschutz, Shell (Abschnitt 12).
- Fehlerhistorie mit Zeitstempeln (SOLL-ERR-01, bisher nur Sammelwert `errors_seen`).
