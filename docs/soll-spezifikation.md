# Soll-Spezifikation der Ersatzfirmware "Wallfree"

Stand: 2026-10-26. Beschreibt, **was die Ersatzfirmware tun soll**, bezogen auf
die Klemmen der Steuerung **EV-CC-AC1-M3-CBC-RCM-ETH** (Phoenix Contact). Das
Originalverhalten steht in der [Ladeautomat-Spezifikation](../../wallbe2/docs/ladeautomat-spezifikation.md)
(im Folgenden "Ist-Spez."). Diese Datei ist Grundlage für Implementierung und Tests.
Sie ist **kein Sicherheitsnachweis**. Weder bestandene Tests noch diese Spezifikation
belegen elektrische Sicherheit oder Eignung für den Ladebetrieb.

Die Fragen F1..F32 sind entschieden (F32 durch SOLL-TIME ersetzt) ([Abschnitt 15](#15-entscheidungen-und-offene-punkte)).
Umsetzungsstand und Festlegungen im Detail: [Implementierung](implementierung.md).

## Lesehilfe

- **Kennungen:** `SOLL-<Bereich>-<Nr>`. Bereiche: GEN (allgemein), IO (Klemmen),
  RT (Zeitmodell, Watchdog), CP, PP, SM (Ladeautomat), CUR (Strom), PROT (Schutzpfad),
  LOCK (Verriegelung), FN (Ein-/Ausgangsfunktionen), EN (Freigabe), PAR (Parameter),
  ERR (Fehler/Status), LED, API, SH (Shell), NVM (Parameterspeicher), BOOT (Start),
  MTR (Energiezähler), MQTT, TIME (Uhrzeit).
- **Quelle** je Anforderung: `E<n>` = Entscheidung n der
  [Ist-Spez.](../../wallbe2/docs/ladeautomat-spezifikation.md#entscheidungen),
  `Ist §n` = Abschnitt n der Ist-Spez., `Belege §n` = Abschnitt n der
  [Ladelogik-Belege](../../wallbe2/docs/ladelogik-belege.md), `HB S. n` = Steuerungshandbuch
  [108191_de_04](../../wallbe2/docs/um_de_ev_cc_ac1_m3_cbc_rcm_eth_108191_de_04.pdf) PDF-Seite n,
  `Arch §n` = [Architektur](architektur.md), `1.27 §n` =
  [Firmwarevergleich](../../wallbe2/docs/firmware-vergleich-1.27.md), `AG` = [AGENTS.md](../../wallbe2/AGENTS.md),
  `Norm` = IEC 61851-1 (Normtext liegt **nicht** im Projekt; Normbezüge stammen aus dem
  Handbuch oder sind als ungeprüft markiert).
- **Abweichung** vom Original (SL-01.04.24) ist mit **[ABW]** markiert, eine Annahme
  ohne Beleg mit **[ANN]**, eine Hardwarezuordnung ohne Leiterbahnbeleg mit **[HYP]**.
  Entscheidungen des Nutzers zu Fragen dieser Spezifikation tragen die Kennung `F<n>`
  ([Abschnitt 15](#15-entscheidungen-und-offene-punkte)).
- **Prüfung:** Am Ende jedes Abschnitts steht, wie die Anforderungen geprüft werden:
  `PC` = hardwareunabhängiger Test der Ladelogik auf dem PC (Zephyr `native_sim` oder
  Host-Unit-Test), `H1..H4` = Hardwarestufen aus der
  [Architektur](architektur.md#7-hardware-inbetriebnahme) (jede Stufe erst nach
  ausdrücklicher Freigabe in AGENTS.md).
- Feldnummern (z. B. "Feld 520") bezeichnen nur die **Herkunft** im Original. Die
  Ersatzfirmware hat eigene Parameternamen ([Abschnitt 10](#10-parameter)); die
  Feldnummern-Schnittstelle des Originals ist kein Ziel (E21).

## 1. Zweck, Geltungsbereich, Begriffe, Normbezug, Abgrenzung

### 1.1 Zweck und Geltungsbereich

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-GEN-01 | Die Ersatzfirmware steuert einen Ladepunkt Mode 3, Ladefall B und C, mit einer Fahrzeugschnittstelle, über die Klemmen der Steuerung EV-CC-AC1-M3-CBC-RCM-ETH. | HB S. 9 |
| SOLL-GEN-02 | Alle Festlegungen beziehen sich auf die **Klemmen der Steuerung** (Tab. 3-1). Eigenschaften einer bestimmten Wallbox (wallbe eco 2.0, Steckdose, Motor, LEDs, Schlüsselschalter) dürfen keine feste Programmierung auslösen; Unterschiede werden über Parameter abgebildet. | AG Grundsatz |
| SOLL-GEN-03 | Die Ersatzfirmware ist mindestens so flexibel wie das Original: **alle** Betriebsarten, die DIP D1..D10, der Drehschalter und die konfigurierbaren Ein-/Ausgangsfunktionen erlauben, werden unterstützt; ausgenommen sind Funktionen außerhalb des Umfangs (1.4). | AG Anforderungen, Ist §10 |
| SOLL-GEN-04 | Verhalten nach IEC 61851-1; Abweichungen nur für dokumentierte Maßnahmen zur Fahrzeugkompatibilität (z. B. Weckfolge). Jede Abweichung ist hier genannt. | AG Grundsatz |
| SOLL-GEN-05 | Die Ladelogik ist ein **hardwareunabhängiges Modul** mit definierten Ein-/Ausgaengen (Messwerte, Schalterstellungen, Parameter, Zeit -> CP-Tastgrad, Schütz, Verriegelung, Ausgangsfunktionen, Status), das vollständig auf dem PC getestet werden kann. | Arch §6 |
| SOLL-GEN-06 | Der Schutzpfad (Fehlerstrom, Schützfehler, Versorgung, Watchdog) ist vom Ladeautomaten getrennt und wirkt ereignisgesteuert direkt auf das Schütz. | E1, E2 |

Beispielkonfiguration der vorhandenen Wallbox: [Hardware, Ist-Einstellungen](../../wallbe2/docs/hardware.md#3-ist-einstellungen-laut-weboberflaeche-box-g)
(keine Vorgabe).

### 1.2 Plattform (entschieden, nicht Gegenstand dieser Spezifikation)

- MCU STM32F207VET6, HSE-Quarz 25 MHz, SYSCLK 100 MHz (Nutzer 2026-10-19; wie Original, [Taktbaum](../../wallbe2/docs/takt-adc-gpio.md#taktbaum)).
- Zephyr v4.4.2, vorläufiges Board `boards/phoenix/ev_cc_ac1`.
- Anwendung als Rohabbild ab `0x08010000` (Flash-Sektoren 4..6, max. 320 KB), Bootloader in
  Sektoren 0..2 bleibt, Sektor 3 (Originalparameter) wird nur gelesen, eigener
  Parameterbereich in Sektor 7 (P1), siehe [Abschnitt 13](#13-parameterspeicher-startablauf-werksreset).
- Netz: Zephyr-IP-Stack, KSZ8081RND über RMII, PHY-Adresse 0 ([Ethernet-PHY](../../wallbe2/docs/ethernet-phy.md)).

### 1.3 Begriffe

| Begriff | Bedeutung |
| --- | --- |
| CP-Zustand | Buchstabe A..F nach IEC 61851-1 (HB S. 37 Tab. 6-1), gemessen am CP-Plateau |
| CP-Rohzustand | nur aus dem Pegel entprellter Zustand, ohne Erzwingung durch Fehler (Original `c29`) |
| CP-Status | entprellter, ggf. durch Fehler erzwungener Zustand (Original `c28`) |
| PWM an | CP mit 1-kHz-Rechteck und Tastgrad 3..97 % (B2/C2/D2) |
| CP +12 V DC | Tastgrad 100 %, "PWM aus" (B1/C1), Original CCR 1000 |
| CP -12 V DC | Tastgrad 0 %, Zustand F ("nicht verfügbar"), Original CCR 0 |
| Stromsperre | Laden ist freigegeben, aber der wirksame Strom ist 0 bzw. < 6 A: CP +12 V DC, Schütz aus |
| Freigabe | logische Ladeerlaubnis aus EN/Schnittstelle je nach DIP D7/D10 (Abschnitt 9) |
| Verfügbarkeit | XR (D8) bzw. Schnittstelle (D10); fehlt sie -> CP-Status F |
| Steckvorgang | Zeitraum von Verlassen des CP-Zustands A bis zur Rückkehr nach A |
| Schutzfehler | Fehler, die sofort und unabhängig vom Automaten das Schütz öffnen (Abschnitt 7) |
| sicherer Zustand | Schütz aus, CP gemäß SOLL-RT-20, Verriegelungsaktor unbestromt |
| Slot | eine einzelne Strombegrenzung; wirksamer Strom = Minimum aller aktiven Slots (Abschnitt 6) |
| mA | Einheit aller Ströme an Schnittstellen (E21) |

### 1.4 Abgrenzung (nicht im Umfang)

Kartenleser/RFID (zukünftige Erweiterung, Nutzer 2026-10-28; Ideen in implementierung.md), OCPP,
Mobilfunk, Master-Slave/Lastverteilung, Modbus TCP und die
Modbus-Register-Schnittstelle des Originals, TLS, Nachbau der Originalweboberfläche.
**Im Umfang, aber nur zur Anzeige:** Energiezähler über Modbus RTU (F28, Abschnitt
[12.5](#125-energiezähler-modbus-rtu-nur-anzeige)). Für OCPP wird später eine
allgemeine Implementierung vorgesehen; die Architektur lässt Platz (eigener Slot in der
Stromkette, Freigabequelle, Statusereignisse). Details: [Abschnitt 14](#14-nicht-umgesetzt--später).

**Folge für abhängige Funktionen** (siehe F1, F28): Überstromerkennung (Feld 409),
Schützüberwachung Modus 4 "Via Energy Meter" und Phasensymmetrie (Feld 433) brauchen
Messwerte eines Zählers als **Messquelle der Ladelogik**. Die Zähleranbindung nach 12.5 ist
keine solche Messquelle (nur Anzeige); die Funktionen bleiben daher **nicht verfügbar**. Die
Logik ist gegen eine abstrakte Messquelle spezifiziert (SOLL-PROT-30 ff.), damit eine
verlässliche Quelle später angeschlossen werden kann.

Prüfung Abschnitt 1: Review dieser Spezifikation gegen AGENTS.md; Rückverfolgbarkeit
Kennung -> Test in der Testliste der Implementierung.

## 2. Klemmen und Signale

Belegarten: **FW** = Firmwarebefund im Original (Pin aus Code, keine Leiterbahn),
**HB** = Handbuch, **N** = Nutzerangabe, **[HYP]** = Hypothese. Pinbelegungen sind
**keine Schaltplanbestätigung** ([Taktbaum/ADC/GPIO](../../wallbe2/docs/takt-adc-gpio.md)). Spannungsangaben
an Klemmen setzen voraus, dass die ADC-Skalierung des Originals ("mV") die Klemmenspannung
wiedergibt [ANN]; Bestätigung in H1/H2.

### 2.1 Klemmen nach Tabelle 3-1 (HB S. 13/14)

| Nr. | Klemme | Funktion (HB) | MCU | Richtung, Pegel | Abtastung / Ansteuerung (Soll) | Beleg |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | C1/C2 | Relaiskontakt Lastschütz, 230 V AC, 16 A | **PE2** | Ausgang, High = Relais an | nur über Schütz-Schnittstelle (SOLL-PROT-01) | FW (alle Schreiber in `0x0803ba2c`), Relais-Treiber [HYP] |
| 1 | CT | Contactor Test: Spannung am Schützausgang nach Ladeende | **PE0** | Eingang, EXTI steigende Flanke | Flankenzeitstempel; "Via Sense" (SOLL-PROT-20) | FW, Verbindung CT -> PE0 [HYP] |
| 2 | L/N/PE | Versorgung 230 V AC | (PC0) | – | interne Versorgungsüberwachung über ADC IN10/PC0 | FW, Bedeutung (12-V-Schiene) [HYP] |
| 3 | Ethernet | RJ45, 100Base-TX | RMII: PA1, PA2, PA7, PB11, PB12, PB13, PC1, PC4, PC5; MCO1 **PA8** (über 470 Ohm an PHY XI) und Reset **PA15** (direkt an PHY RST#), per Durchgang belegt (Nutzer 2026-10-24) | – | Zephyr-Treiber; PA8 = 25 MHz für PHY-XI [HYP]; PA15 >= 10 ms Low beim Start | FW, [Ethernet-PHY](../../wallbe2/docs/ethernet-phy.md) |
| 4 | 12 V | Ausgang 12 V DC, max. 200 mA | – | – | keine Ansteuerung bekannt | HB |
| 4 | LD | Digitaleingang (Default Verriegelungsrückmeldung) | **PA3** (ADC IN3) | analog, < 3 V aus / > 9 V ein, Eingangswiderstand 3 kOhm | ADC-Block (SOLL-IO-10) | FW, HB S. 10 |
| 4 | EN | Digitaleingang (Default Freigabe) | **PA4** (IN4) | wie LD | wie LD | FW |
| 4 | ML | Digitaleingang (Default manuelle Verriegelung) | **PA5** (IN5) | wie LD | wie LD | FW |
| 4 | XR | Digitaleingang (Default Verfügbarkeit) | **PA6** (IN6) | wie LD | wie LD | FW |
| 4 | IN | Digitaleingang (Default Ladestrom 16 A) | **PC3** (IN13) | wie LD | wie LD | FW |
| 4 | A/B | RS-485 (Modbus RTU) | **UART4** PC10 TX / PC11 RX, Treiberfreigabe **PC12** [HYP]; zweiter Kanal USART3 PD8/PD9, Freigabe PD7 | halbduplex, Freigabe High = Senden | Energiezähler (12.5); ohne Zähler UART aus und PC12 Low; PD7 dauerhaft Low | FW [Belege §9](../../wallbe2/docs/ladelogik-belege.md#9-nicht-zugeordnete-pins) |
| 4 | GND | Systemerde | – | – | – | HB |
| 4 | ER | Digitalausgang (Default Fehler) | **PB4** | Ausgang, aktiv High, 8..30 V über 12a, max. 600 mA | Funktionscode (Abschnitt 9) | FW, Treiber DRV8844 [HYP] |
| 4 | LR | Digitalausgang (Default Lock Request) | **PB5** | wie ER | wie ER | FW |
| 4 | VR | Digitalausgang (Default Fahrzeug bereit) | **PB6** | wie ER | wie ER | FW |
| 4 | CR | Digitalausgang (Default PWM an) | **PB7** | wie ER | wie ER | FW |
| 4 | – | Freigabe Ausgangstreiber | **PB2** | Ausgang, High = frei | nach Initialisierung der Ausgänge High (SOLL-IO-21) | FW, DRV8844 nSLEEP [HYP] |
| 4 | 12a | Speisung der Ausgänge 8..30 V DC | – | – | – | HB |
| 5 | RCM | Fehlerstromsensor (Bender CCE4400/D0569) | **PE3** SWITCH_1, **PE4** SWITCH_2, **PE6** ERR_OUT, **PE5** TEST_IN | Eingänge ohne Pull, High = ausgelöst (Inversion zum Bender-Pegel [HYP]); PE5 Open-Drain, Low = Test | EXTI + zyklische Kontrolle (SOLL-PROT-02) | FW, [RCM/Bender](../../wallbe2/docs/rcm-bender.md) |
| 6 | SIM | Mobilfunk | PD0, PD1, PB10 | – | nicht genutzt; Pins in sicherem Ruhezustand wie Original-Init (PD1 Low) | FW |
| 7 | R1/R2 | Steuerspannung Verriegelungsaktor, 12 V, max. 1 A, Impuls 500 ms (Default) bis 3 s | **PA9** (Verriegeln), **PA10** (Entriegeln) | Ausgänge, Ruhe High, Impuls = Low | Abschnitt 8 | FW, HB S. 9, A4953-Ansteuerung [HYP] |
| 7 | CP | Control Pilot | **PE9** (TIM1_CH1, PWM), **PA0** (IN0, CP+), **PB1** (IN9, CP-) | PWM 1 kHz, +-12 V über Treiber [HYP] | Abschnitt 4 | FW |
| 7 | PX | Proximity | **PB0** (IN8) | analog | Abschnitt 4 | FW |
| 8 | C3/C4 | Relaisausgang, "reserviert" | PE2 (mit C1/C2) | Ausgang | schließt und öffnet zusammen mit C1/C2 (Testgerät 2026-11-07, Stufe C); auf dem dritten Gerät hängen beide Relaisstufen an PE2 und je an einem Ausgang des Fehlerstrommoduls | Testgerät, drittes Gerät |
| 9 | Antenne | Mobilfunk | – | – | nicht genutzt | HB |

### 2.2 Bedienelemente (HB S. 15/16)

| Element | MCU | Pegel | Soll-Auswertung | Beleg |
| --- | --- | --- | --- | --- |
| DIP D1..D10 | PD15, PD14, PD13, PD12, PD11, PD10, PA12, PA11, PB15, PB14 | Pull-up, Low = ON | nur in der Initialisierung gelesen (SOLL-IO-30) | FW, HB S. 15 |
| Drehschalter Bit 0..3 | PC9, PC8, PC6, PC7 | Pull-up, Low = 1 | nur in der Initialisierung gelesen; Position 0..15 | FW |
| LED Power (grün) | PE15 | aktiv Low | Abschnitt 11 | FW, HB S. 16 |
| LED Error (rot) | PE14 | aktiv Low | Abschnitt 11 | FW |
| LED Connect (gelb) | PE12 | aktiv Low | Abschnitt 11 | FW |
| LED Ready (grün) | PE13 | aktiv Low | Abschnitt 11 | FW |
| LED Status Modem | PE10/PE11 | aktiv Low | aus (Mobilfunk nicht im Umfang) | FW |
| (Netzwerkstatus) | PC2 | Ausgang | Netzwerkanzeige (SOLL-LED-06), aktiv Low, grüne LED an der Netzwerkbuchse | FW, Testgerät 2026-10-27 |
| Reset-Taster | **PD2** | Low = gedrückt | Werksreset beim Start (SOLL-BOOT-20) | FW (`main`), HB S. 16, Testgerät |

Hinweis: Der Taster wirkt zusätzlich auf NRST. Auf dem Testgerät bleibt der Prozessor nach dem Drücken
etwa 5 s im Reset, auch wenn der Taster gehalten wird, und startet dann; erst ab da laufen die
5 s nach SOLL-BOOT-20. Für einen Werksreset hält man also etwa 10 s (Testgerät 2026-10-24/27;
vermutlich RC-Glied an NRST, Beschaltung nicht verfolgt).

### 2.3 Anforderungen an Ein-/Ausgangssignale

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-IO-01 | Unbenutzte bzw. außer Umfang liegende Pins (PB3, PB8..PB10, PC12, PC13, PD0, PD1, PD7, PE7, PE8, PE10, PE11) werden wie in der Original-GPIO-Initialisierung in einen definierten Ruhezustand gesetzt; PC12/PD7 (RS-485-DE) Low. PC10..PC12 übernimmt die Zählerabfrage erst, wenn ein Zähler konfiguriert ist (SOLL-MTR-01). | FW [Taktbaum](../../wallbe2/docs/takt-adc-gpio.md#gpio-initialisierung-0x08040ce0-pinuebersicht) |
| SOLL-IO-02 | Erste Maßnahme nach dem Reset, vor Takt- und Netzinitialisierung: PE2 (Schütz) Low, PA9/PA10 High (Verriegelungsaktor in Ruhe), PE1 Low, PB2 Low, PB4..PB7 Low, PE5 High (kein RCM-Test). | Ist §14 (Startpegel), E3 |
| SOLL-IO-10 | Die fünf Digitaleingänge werden im ADC-Block (SOLL-RT-03) gemessen. Ein: Pegel > 9,0 V; Aus: < 3,0 V; dazwischen bleibt der letzte Wert (Hysterese). | HB S. 10, FW `0x0803c7d4` |
| SOLL-IO-11 | Der Rohpegel jedes Eingangs (ein/aus) ist unabhängig von seiner Funktion als Status lesbar (Original Felder 200..203, 208). | Ist §2, HB Tab. 9-3 |
| SOLL-IO-12 | Ein Eingangswechsel gilt erst als übernommen, wenn er in zwei aufeinanderfolgenden ADC-Blöcken besteht (Entprellung ca. 16 ms). **[ABW]** Original übernimmt ohne zusätzliche Entprellung (nur Mittelung über 8 Perioden). | F2 entschieden |
| SOLL-IO-20 | Die Ausgänge ER/LR/VR/CR werden aktiv High angesteuert, Zustand jeweils als Status lesbar (Original 204..207). | Ist §12 |
| SOLL-IO-21 | PB2 wird erst High gesetzt, nachdem alle Ausgänge ihren berechneten Wert haben (kein Impuls beim Start). | FW `0x08039f8c` (Original: Taskstart), [ANN] |
| SOLL-IO-30 | DIP-Schalter und Drehschalter werden **nur** in der Initialisierung gelesen; Änderungen wirken erst nach Neustart. Ausnahme: Schreiben über die Schnittstelle (SOLL-PAR-40, F3). | HB S. 15, Ist §10 |
| SOLL-IO-31 | Ein Wert über die Schnittstelle für DIP/Drehschalter-Überschreibung ist als Status getrennt von der tatsächlichen Schalterstellung lesbar. | [ANN] |

Prüfung Abschnitt 2: PC – Eingangshysterese, Entprellung, DIP-/Drehschalterdekodierung
mit simulierten Rohwerten. H1 – Pegel an jeder Klemme messen (Ausgänge, R1/R2-Impulse,
CP-PWM, PB2-Startverhalten), Eingangsschwellen mit Labornetzteil, Zuordnung LED/Pin,
DIP/Drehschalter-Bitfolge. H2 – CP/PX-Skalierung mit EV-Simulator.

## 3. Zeitmodell, Aufgabenaufteilung, Watchdog, sicherer Zustand

### 3.1 Ablaufeinheiten

| Einheit | Kontext / Priorität | Takt | Aufgabe |
| --- | --- | --- | --- |
| TIM1 | Hardware | 1 kHz, center-aligned | CP-PWM auf PE9; Update löst ADC-Gruppen aus |
| ADC/DMA-ISR | Interrupt | je Messblock (8 PWM-Perioden, ~8 ms) | Mittelung, CP/PP/Eingaenge/Versorgung bereitstellen, Ereignis an Automat |
| Schutz-ISR | Interrupt höchster Anwendungspriorität (EXTI3/EXTI4/EXTI9_5 für PE3/PE4/PE6, EXTI0 für PE0) | Ereignis | Fehlerstrom -> Schütz sofort aus; Sense-Zeitstempel |
| Schutzthread | Thread, höchste Thread-Priorität | 1 ms | zyklische Kontrolle der RCM-Pegel, Schützüberwachung, Versorgung, Überwachung der anderen Threads, IWDG |
| Automatenthread | Thread unter dem Schutzthread | 10 ms und bei Messblock-Ereignis | CP-/PP-Auswertung, Ladeautomat, Strom/PWM, Verriegelung, Ausgänge, LEDs |
| Netz-/Shell-Threads | niedrige Priorität | ereignisgesteuert | HTTP (mit Konsole), WebSocket, Parameterspeicher-Schreiben |
| Zählerthread | niedrigste Priorität, nur mit konfiguriertem Zähler | 1 s | Zählerabfrage (SOLL-MTR-10); nicht in der Thread-Überwachung (SOLL-MTR-03) |

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-RT-01 | CP-PWM: TIM1 CH1 auf PE9, 1,000 kHz, Auflösung mindestens 1 Promille (Original: PSC 49, ARR 1000, center-aligned). Die PWM läuft in Hardware unabhängig von der Software weiter. | FW [Taktbaum](../../wallbe2/docs/takt-adc-gpio.md#tim1--cp-pwm) |
| SOLL-RT-02 | ADC-Abtastung synchron zum CP-Plateau: CP+ im aktiven (positiven) Abschnitt, CP- im inaktiven Abschnitt (Original: CNT < 50 bzw. > 950); Mittelung über 8 Perioden. | FW |
| SOLL-RT-03 | Messblock: CP+, CP-, PP, LD/EN/ML/XR/IN, Versorgung je ~8 ms. Bei Tastgrad 0 % bzw. 100 % gibt es nur ein Plateau; die Auswertung muss das berücksichtigen (SOLL-CP-04). | FW, [ANN] |
| SOLL-RT-04 | Der Automatenthread **blockiert nie**: keine Wartezeit > 1 ms in einem Zyklus. Alle Fristen (Verriegelungsimpuls, Weckfolge, Selbsttest, Schütz-Frist) sind Zeitgeber-Teilzustände. **[ABW]** Original wartet blockierend bis ~4,25 s (Weckfolge), ~3 s bzw. 13,5 s (Lock), ~2,1 s (Selbsttest). | E2, E10, Ist §8 |
| SOLL-RT-05 | Zykluszeit des Automaten 10 ms (Jitter < 2 ms). **[ABW]** Original 25 ms + Laufzeit. | [ANN] |
| SOLL-RT-06 | Zeitbasis: monotone Millisekunden (64 Bit oder überlaufsicher); alle Fristen werden als "seit Zeitpunkt t vergangen >= Dauer" geprüft. | [ANN] |
| SOLL-RT-07 | Prioritäten: Schutz-ISR > ADC-ISR > Netz-ISR; Schutzthread > Automatenthread > alle übrigen Threads. Kein Netz- oder Shellkommando darf Schutz- oder Automatenthread blockieren (keine gemeinsamen Sperren mit unbegrenzter Haltezeit). | E2 |

### 3.2 Watchdog

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-RT-10 | Jeder überwachte Thread meldet sich zyklisch beim Schutzthread. Limits: Automatenthread **100 ms**, Messblock-Ereignis (ADC) **50 ms**, Schutzthread selbst über den IWDG. Überschreitung -> sofort sicherer Zustand (SOLL-RT-20), Ursache sichern, danach Reset. | E3, F4 (Startwerte, nach H1 nachschärfen) |
| SOLL-RT-11 | Netz- und Shell-Threads: Limit 10 s. Überschreitung -> Fehlermeldung im Protokoll; Neustart nur, wenn das Schütz aus ist und kein Ladevorgang läuft (kein Abschalten unter Last nur wegen der Schnittstelle). Umsetzung: überwacht werden Speicher-Thread, WebSocket-Thread und System-Workqueue (zyklische Meldung) sowie der HTTP-Server: Er gehört zu Zephyr und meldet sich nicht zyklisch; überwacht wird, dass jeder Aufruf der Anwendung (Anfrage, Datei, Konsole, WebSocket-Aufbau) nach höchstens 10 s zurückkehrt. Ein Hänger im Zephyr-Code außerhalb dieser Aufrufe (z. B. blockierendes Senden) wird nicht erkannt. Neustart nur in S1/S9/S10 mit Schütz aus. | F4; Umsetzung für den HTTP-Server bestätigt (Nutzer 2026-10-29) |
| SOLL-RT-12 | IWDG wird nur vom Schutzthread nachgeladen, wenn alle Limits eingehalten sind. Zeitlimit **250 ms** im Betrieb. **[ABW]** Original: Task-Limit 60 s + IWDG ~3 s. | E3, F4 |
| SOLL-RT-13 | Für Flash-Löschvorgänge (blockieren den Bus für Sekunden, Arch §3) wird das IWDG-Limit **vorher** auf den Maximalwert verlängert und danach zurückgesetzt; Löschen nur im sicheren Zustand (SOLL-NVM-05). Länge der Löschzeit und Umprogrammierbarkeit des IWDG im Betrieb sind gegen RM0033/DS6329 zu prüfen. | Arch §3, [ANN] |
| SOLL-RT-14 | Grund des letzten Resets (Watchdog, Fault, Update, Werksreset, Spannungseinbruch) wird über den Neustart erhalten (RTC-Backup-Register, nicht BKP10..16, die der Bootloader nutzt, siehe SOLL-BOOT-30) und ist als Status lesbar. Umsetzung: Code "WF"+Grund in **BKP19** vor jedem Software-Reset, beim Start mit den Flags aus RCC_CSR verknüpft (Watchdog vor Software, NRST-Pin zuletzt, weil jede interne Quelle den Pin pulst, RM0033 RCC); lesbar über `GET /system/info` und `status`. Zusätzlich steht bei Überwachung und Prozessorausnahme der genaue Auslöser (Automatenthread, Messblöcke, Speicher-/WebSocket-Thread, System-Workqueue, Ausnahme mit Grund und PC) in BKP18/BKP17 und erscheint als `reset_detail` und im Startereignis (SOLL-ERR-12). | Original BKP19, [ANN]; Detail: Nutzer 2026-10-26 (Lasttest, Logmeldung ging mit dem Reset verloren) |

### 3.3 Sicherer Zustand und Ausnahmen

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-RT-20 | **Sicherer Zustand:** (1) PE2 Low (Schütz aus) als erste Aktion, (2) CP auf **-12 V DC** (Zustand F, Tastgrad 0 %, F5 entschieden), (3) PA9/PA10 High (kein Aktorimpuls), (4) ER gemäß Funktion, LED Error an. Der Verriegelungszustand wird nicht verändert. | E3 |
| SOLL-RT-21 | HardFault, MemManage, BusFault, UsageFault, NMI mit Fehler, Assertion und Stack-Überlauf: sicherer Zustand direkt über Registerzugriffe (ohne Treiberaufrufe), Ursache sichern, Systemreset. **[ABW]** Original kreist ohne Abschaltung bis IWDG (~3 s). | E3, [Belege §8](../../wallbe2/docs/ladelogik-belege.md#8-fehler--und-ausnahmepfade) |
| SOLL-RT-22 | Neustart- und Updateanforderungen werden erst ausgeführt, wenn das Schütz aus ist (geordnetes Ladeende mit Frist SOLL-SM-41); Höchstwartezeit 15 s wie Original, danach sicherer Zustand und Reset. | [Belege §8](../../wallbe2/docs/ladelogik-belege.md#8-fehler--und-ausnahmepfade), [Bootloader](../../wallbe2/docs/bootloader.md) |
| SOLL-RT-23 | Nach jedem Reset gilt bis zum Ende der Initialisierung der Zustand aus SOLL-IO-02. | E3 |

Prüfung Abschnitt 3: PC – Automat mit simulierter Zeit (keine blockierenden Aufrufe,
Fristen), Watchdog-Supervisor mit künstlich hängendem Thread. H1 – Zykluszeiten und
ISR-Latenz mit Testpins am Oszilloskop; Watchdog-Auslösung (hängender Thread) ->
Schützsignal PE2 Low und CP-Zustand messen; Fault-Injektion (absichtlicher HardFault in
Prüf-Firmware) -> sicherer Zustand vor Reset.

## 4. CP- und PP-Auswertung

Grundlage: [Belege §1/§2](../../wallbe2/docs/ladelogik-belege.md#1-cp-klassifikator-0x08039fe0),
Ist §2a, HB S. 37 (Tab. 6-1), S. 41 (Tab. 6-4). Spannungen gelten am CP-Plateau
(Klemme CP gegen PE). Die Umrechnung ADC -> Volt übernimmt zunächst die
Original-Skalierung (`x*39/100 + 300` bzw. `14050 - x*397/1000` je Summe über 8 Werte)
[ANN]; Kalibrierung in H2.

### 4.1 CP-Pegel und Zustände

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-CP-01 | Bandtabelle für den Pegel `v` des positiven Plateaus (Grenzwert gehört zum zuerst genannten Band): `v` >= 11,0 V -> A; 10,0..10,999 -> A, wenn Rohzustand bisher A, sonst B; 8,0..9,999 -> B; 7,0..7,999 -> B, wenn bisher <= B, sonst C; 5,0..6,999 -> C; 4,0..4,999 -> C, wenn bisher <= C, sonst D; 2,0..3,999 -> D; 1,0..1,999 -> D, wenn bisher <= D, sonst E; `v` < 1,0 V -> Kandidat "CP ungültig". `v` > 13 V gilt als A ohne Fehler. | Ist §2a |
| SOLL-CP-02 | Die Hysterese in den Zwischenbändern bezieht sich auf den **CP-Rohzustand** (nur Pegel). **[ABW]** Original vergleicht mit dem ggf. durch Fehler erzwungenen Status `c28`; dadurch hängt die Pegelauswertung im Original von Fehlerbits ab. | F6 entschieden |
| SOLL-CP-03 | Entprellung: Der Rohzustand übernimmt einen neuen Kandidaten, wenn dieser **ununterbrochen > 20 ms** vom bisherigen Rohzustand abweicht; übernommen wird der dann aktuelle Kandidat. | Ist §2a |
| SOLL-CP-04 | Ausgewertet wird nur ein Plateau, das tatsächlich ausgegeben wird: bei CP -12 V DC (Tastgrad 0 %) gibt es kein positives Plateau; dann bleibt der Rohzustand unverändert und "CP ungültig" wird nicht gemeldet (Original: Sonderfall Weckfolge `c2a` = 5). Bei CP +12 V DC gibt es kein negatives Plateau; die Diodenprüfung entfällt. | Ist §2a, [ANN] |
| SOLL-CP-05 | Nach jeder Änderung der CP-Ausgabe (Tastgrad, DC-Pegel) werden die Messwerte der folgenden 2 Messblöcke (~16 ms) verworfen (Einschwingen von Treiber und Fahrzeugkapazität). | [ANN] |
| SOLL-CP-06 | **CP ungültig** (Kurzschluss/0 V, Zustand E nach Tab. 6-1): `v` < 1,0 V und seit dem ersten Block mit `v` < 1,0 V mehr als 20 ms vergangen -> Fehler ERR-CP-INVALID, Status **sofort** E (ohne weitere Entprellung). Der Startzeitpunkt wird erst bei `v` >= 1,2 V verworfen; zwischen 1,0 und 1,2 V bleibt er erhalten, der Fehler gilt dort aber nicht (wie Original). Während `v` < 1,0 V bleibt der Rohzustand unverändert. **[ABW]** Original setzt den Rohzustand dann auf A, was als "Fahrzeug getrennt" missdeutet werden kann. | Ist §2a, Belege §1 Schritt 4 |
| SOLL-CP-07 | **Diodenprüfung:** nur bei laufender PWM (Tastgrad 3..97 %) seit > 100 ms und ohne Stromsperre: Ist das negative Plateau weniger negativ als **-10,0 V** (Betrag <= 9,999 V) ununterbrochen > 20 ms -> Fehler ERR-DIODE, Status **sofort** F. Nur solange das positive Plateau mindestens 2,0 V hat (B..D): Bei einem Kurzschluss CP-PE fallen beide Plateaus, das ist CP ungültig (SOLL-CP-06), keine fehlende Diode; ohne diese Bedingung gewann auf dem Testgerät die Diodenprüfung das gleichzeitige Rennen, und der Fehler hielt bis zum Abstecken (Nutzer 2026-11-07). Ausgabe (F7): **3 s -12 V DC** (eindeutiges F für das Fahrzeug), danach **+12 V DC**, damit das Abstecken (A) erkannt wird; Fehler bleibt bis CP A. **[ABW]** Original bleibt bei -12 V. Normtext zur Diodenprüfung nicht im Projekt (Prüfung offen); Dauer 3 s [ANN]. | Ist §2a, HB S. 37, F7 |
| SOLL-CP-08 | Der **CP-Status** (nach außen gemeldet, Ausgangsfunktionen) entsteht aus dem Rohzustand durch Erzwingung (Reihenfolge, erste gilt): (1) Verfügbarkeit fehlt (SOLL-EN-10) oder ERR-DIODE oder ERR-CONTACTOR -> **F**; (2) ERR-CP-INVALID -> **E** sofort; (3) jeder andere aktive Fehler der Klassen P und V (Abschnitt 11) -> **E**, sofern Rohzustand != A oder Fehler gilt "immer" (Tabelle 11.1); (4) sonst Rohzustand. Der Status wird wie in SOLL-CP-03 entprellt, außer bei den als "sofort" bezeichneten Fällen. | Ist §2a, Belege §1 Schritt 6 |
| SOLL-CP-09 | Fehlerstrom bei Rohzustand A führt **nicht** zu E, sondern startet den RCM-Selbsttest zur Rücksetzung (SOLL-PROT-11). | Ist §2a (d) |

### 4.2 CP-Ausgabe je Lage

| Lage | CP-Ausgabe | Tastgrad (Promille) |
| --- | --- | --- |
| Initialisierung, kein Fahrzeug, verriegeln, warten auf Freigabe/Strom, Stromsperre, Ladeende, Entriegeln, Fehler (E) | +12 V DC | 1000 |
| PWM an (B2/C2/D2) | 1 kHz | nach SOLL-CUR-20, 5 % im 5-%-Modus |
| Verfügbarkeit fehlt (F), Schützfehler, Weckfolge Phase 1 | -12 V DC | 0 |
| Weckfolge Phase 2 | +12 V DC | 1000 |
| sicherer Zustand (SOLL-RT-20) | -12 V DC (F5) | 0 |
| Diode fehlt | 3 s -12 V DC, danach +12 V DC (F7) | 0, dann 1000 |

### 4.3 PP (Proximity, Klemme PX)

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-PP-01 | PP wird nur mit **D1 an** (Ladefall B, Steckdose) ausgewertet. Ohne D1 (Ladefall C, festes Kabel) gilt als Kabelstrom der Wert des Drehschalters; keine PP-Fehler, keine Kabelabweisung. | HB S. 15, Ist §2a |
| SOLL-PP-02 | Widerstandsklassen nach HB Tab. 6-4: < 75 Ohm -> ungültig; 75..150 -> 63 A; 150..330 -> 32 A; 330..1000 -> 20 A; 1000..2200 -> 13 A; > 2200 Ohm -> kein Kabel (0 A). Implementierung zunächst mit den Original-Schwellen in der Original-Skalierung (821/1521/2821/5331/7401) [ANN], Prüfung der Zuordnung Pegel -> Widerstand in H2. Der Wert 63/70 A wird als **63 A** gemeldet. | HB S. 41, Belege §2 |
| SOLL-PP-03 | Übernahme eines neuen Kabelwerts erst, wenn derselbe Kandidat **> 1000 ms** ununterbrochen anliegt; danach erneute Bewertung etwa jede Sekunde. Während der Frist gilt der alte Wert. | Belege §2 |
| SOLL-PP-04 | Fehler **ERR-PP-INVALID**: Kandidat "ungültig" übernommen, oder Kabelwert 0 übernommen, während der CP-Rohzustand nicht A und nicht F ist (Fahrzeug erkannt, aber kein Kabel). Rücksetzung, sobald die Bedingung bei einer Bewertung nicht mehr gilt. | Belege §2 |
| SOLL-PP-05 | **Kabelabweisung** nur mit D1 und D2: Kabelwert 13 A -> ERR-CABLE-REJECT; zusätzlich bei D3 aus: Kabelwert 20 A -> ERR-CABLE-REJECT. Rücksetzung bei anderem Kabelwert. | HB S. 15, Belege §2 |
| SOLL-PP-06 | Kabelwert 0 übernommen (Stecker gezogen) löscht ERR-LOCK (Verriegelungsfehler). | Belege §2 |

Prüfung Abschnitt 4: PC – Bandtabelle mit Grenzwerten (jeder Grenzwert und +-1 mV),
Hysterese je Richtung, 20-ms-Entprellung mit Blöcken von 8 ms (Übernahme nach dem 3.
abweichenden Block), "CP ungültig" mit 1,0/1,2-V-Hysterese, Diodenprüfung nur bei PWM,
Verwerfen nach Ausgabewechsel; PP-Klassen, 1-s-Frist, Kabelabweisung für alle
Kombinationen D1/D2/D3. H2 – EV-Simulator: Zustände A..E, Diode entfernt, Kurzschluss
CP-PE, PP-Widerstände 100/220/680/1500 Ohm sowie < 75 Ohm und offen; gemessene
Klemmenspannungen gegen die Bandgrenzen.

## 5. Ladeautomat

### 5.1 Zustände

Die Nummerierung ist neu; die Spalte "Original" nennt den entsprechenden Zustand der
Ist-Spez. (Ist §4).

| Zustand | Name | Original | CP-Ausgabe | Schütz | Bedeutung |
| --- | --- | --- | --- | --- | --- |
| S0 | INIT | Anlauf | +12 V DC | aus | Anlaufverzögerung, Selbsttest, Entriegeln beim Start |
| S1 | IDLE | Z0 | +12 V DC | aus | kein Fahrzeug (A) bzw. Ruhe |
| S2 | LOCKING | Z1 | +12 V DC | aus | Fahrzeug erkannt bzw. manuelle Verriegelung, Verriegelung läuft |
| S3 | WAIT | Z3 | +12 V DC (B1) | aus | verriegelt, wartet auf Freigabe, Strom >= 6 A, Verfügbarkeit |
| S4 | SELFTEST | Z4 | +12 V DC | aus | RCM-Selbsttest vor dem ersten PWM-Start je Steckvorgang |
| S5 | PWM_ON | Z5/Z6 | PWM | aus | B2: wartet auf C/D; enthält Teilzustand Weckfolge; im 5-%-Modus Dauerzustand |
| S6 | CHARGING | Z7 | PWM | **ein** | C2/D2: Laden |
| S7 | STOPPING | (in Z7/Z2 implizit) | +12 V DC | ein, bis Fahrzeug reagiert oder Frist | geordnetes Ladeende |
| S8 | UNLOCKING | Z2 | +12 V DC | aus | Entriegeln |
| S9 | ERROR | Z8 | +12 V DC | aus | Fehler (Status E) |
| S10 | UNAVAILABLE | Z9 | -12 V DC | aus | nicht verfügbar (Status F) |

**[ABW]** S7 ist ein eigener Zustand (im Original verteilt auf Aus(5000)-Wartezeiten in
Z7/Z3/Z2). Zustand 10 des Originals (toter Code) entfällt.

### 5.2 Vorrangreihenfolge (E7)

Je Zyklus wird zuerst die folgende Liste geprüft; die **erste** zutreffende Zeile
bestimmt die Reaktion. **[ABW]** Im Original überschreiben spätere Zuweisungen frühere
(Ist §4, Z5/Z7), wodurch z. B. ein Entriegelungswunsch bei CP C ignoriert wird.

| Rang | Bedingung | Reaktion | Quelle |
| --- | --- | --- | --- |
| 1 | Schutzfehler (Klasse P, Abschnitt 7/11) aktiv | Schütz ist bereits durch den Schutzpfad aus; -> S9 bzw. S10 (Schützfehler) | E1, E2, E7a |
| 2 | Konfigurationsfehler (Drehschalter A..F, SOLL-LOCK-03) | -> S9, kein Laden bis Neustart | E11 |
| 3 | CP-Rohzustand E oder F, ERR-CP-INVALID, ERR-DIODE | Schütz **sofort** aus -> S9 bzw. S10 | Ist §6 (Aus(0) bei CP-Roh A/B/F) |
| 4 | CP-Rohzustand A in S4..S7 | Schütz **sofort** aus (Selbsttest abbrechen) -> S8 (bzw. S1 ohne Verriegelung) | Ist Z7, SOLL-SM-42 |
| 5 | Fehler der Klasse V mit Sofortabschaltung (Tabelle 11.1) | Schütz sofort aus -> S9 | F8 |
| 6 | Verfügbarkeit entzogen | geordnetes Ende (S7) -> S10 | E7b |
| 7 | übrige Fehler der Klasse V | geordnetes Ende (S7) -> S9 | F8 |
| 8 | Freigabe entzogen oder Stromsperre | geordnetes Ende (S7) -> S3 | E7b |
| 9 | Entriegelungswunsch (D9 mit ML = 0, Entriegeln-Anforderung) | geordnetes Ende (S7) -> S8 | E7c |
| 10 | regulärer Zustandsablauf (5.3) | | |
| 11 | Stromänderung | neuer Tastgrad im selben Zyklus | E7d |

### 5.3 Übergänge

"CP X" bezeichnet den entprellten Rohzustand, sofern nicht "Status" genannt ist.
"verriegelt" heißt Verriegelungszustand LOCKED oder D4 aus (SOLL-LOCK-01).

| Kennung | Von | Bedingung | Aktion / Nach | Quelle |
| --- | --- | --- | --- | --- |
| SOLL-SM-01 | S0 | ab Ende der Initialisierung **5 s** vergangen und Start-Selbsttest (SOLL-PROT-10) sowie Start-Entriegelung (SOLL-LOCK-20) beendet | -> S1 | E9 **[ABW]** Original 20 s |
| SOLL-SM-02 | S0 | während der Anlaufzeit | CP +12 V DC, Messung läuft, Ausgangsfunktion 40 blinkt, kein Verriegeln, kein Laden | E9, Ist §12 Code 40 |
| SOLL-SM-10 | S1 (Eintritt und je Zyklus) | – | Löschen der steckvorgangsbezogenen Fehler (Tabelle 11.1, Rücksetzen "A"), Zähler Verriegelungsversuche, Merker Weckfolge und Selbsttest des Steckvorgangs, Entriegeln-Anforderung; Ist-Strom 0 | Ist Z0 |
| SOLL-SM-11 | S1 | verriegelt (LOCKED), D4 an, D9 aus, CP A | Entriegeln -> S8 | Ist Z0 |
| SOLL-SM-12 | S1 | D9 an und Verriegelungswunsch (ML) = 1 | -> S2 | Ist Z0/Z1 |
| SOLL-SM-13 | S1 | CP B, C oder D | -> S2 | Ist Z0 |
| SOLL-SM-20 | S2 | D1 an und Kabelwert 0 | bleibt (wartet auf Stecker), kein Verriegeln | Ist Z1 |
| SOLL-SM-21 | S2 | Entriegeln-Anforderung gesetzt | kein Verriegeln, bleibt bis CP A -> S1 | Ist Z1 (`c8b`), [ANN] |
| SOLL-SM-22 | S2 | D9 an und ML = 0 | nicht verriegelt: -> S1; verriegelt: -> S8 | Ist Z1 |
| SOLL-SM-23 | S2 | CP A und D9 aus | -> S1 (laufenden Verriegelungszyklus geordnet beenden, dann S8 falls verriegelt) | Ist Z1 |
| SOLL-SM-24 | S2 | sonst | Verriegelungszyklus (Abschnitt 8); Erfolg und CP B/C/D -> S3; Erfolg und CP A (nur D9) -> bleibt verriegelt in S2 | Ist Z1 |
| SOLL-SM-30 | S3 | verriegelt, Freigabe (Abschnitt 9), Verfügbarkeit, keine Stromsperre (Abschnitt 6), kein Fehler, CP B/C/D | Selbsttest dieses Steckvorgangs noch nicht bestanden: -> S4; sonst -> S5 | Ist Z3, E18 |
| SOLL-SM-31 | S3 | CP A | D9 aus: -> S8; D9 an: -> S1 (bleibt verriegelt) | Ist Z3 |
| SOLL-SM-40 | S4 | Selbsttest bestanden | Merker "Selbsttest bestanden" für diesen Steckvorgang; -> S5 | Ist Z4, E16, E18 |
| SOLL-SM-41 | S4 | Selbsttest fehlgeschlagen | Schutzfehler ERR-RCM-SELFTEST -> S9 | E16 |
| SOLL-SM-42 | S4 | Rang 1..9 tritt während des Tests ein | Test abbrechen (TEST_IN freigeben), Reaktion nach 5.2; Test gilt als nicht bestanden, kein Fehler | [ANN] |
| SOLL-SM-50 | S5 | Eintritt | PWM mit Tastgrad nach Abschnitt 6; Zeitstempel "PWM an" | Ist Z5 |
| SOLL-SM-51 | S5 | CP C (bzw. D bei "D zulassen") seit Entprellung, PWM seit >= 100 ms aktiv, Bedingungen SOLL-SM-60 erfüllt, kein 5-%-Modus | -> S6 | Ist Z5, Ist §6 (`c2c`) |
| SOLL-SM-52 | S5 | CP D und "D abweisen" an (Default) | Fehler ERR-STATE-D -> S9 (über Rang 7) | Ist Z5, Feld 412 |
| SOLL-SM-53 | S5 | 5-%-Modus und CP C/D | bleibt S5, Schütz bleibt aus, Status "digitale Kommunikation nicht verfügbar" (SOLL-ERR-30) | E6 |
| SOLL-SM-54 | S5 | CP B ununterbrochen seit Wartezeit `wakeup_delay` (Default 30 s), Weckfolge nach `wakeup_mode` zulässig, kein 5-%-Modus | Weckfolge (5.4) | E8, E17 |
| SOLL-SM-55 | S5 | CP B ohne laufende PWM (z. B. nach Weckfolge) | **bleibt S5**, kein Rückfall nach S3 und kein neuer Selbsttest | E18 **[ABW]** Original Z5/Z6 -> Z3 |
| SOLL-SM-60 | S5 -> S6 | Schütz-Einschaltbedingungen (alle): Status C oder D (D nur ohne "D abweisen"), PWM läuft >= 100 ms mit Tastgrad nach SOLL-CUR-20, Freigabe, Verfügbarkeit, keine Stromsperre, kein aktiver Fehler, verriegelt, Selbsttest dieses Steckvorgangs bestanden, keine Schützfehler-Erkennung, seit dem letzten Schütz-Aus >= `contactor_min_off` (Feld 519) | Schütz ein (SOLL-PROT-01) | Ist §6, [ANN] Deutung 519 |
| SOLL-SM-61 | S6 | je Zyklus | Ladedauer fortschreiben; Tastgrad nachführen; bei CP D Ausgangsfunktion "Belüftung" (Code 28) | Ist Z7 |
| SOLL-SM-62 | S6 | CP B (Fahrzeug beendet) | Schütz **sofort** aus, -> S5 (PWM bleibt an, Fahrzeug darf erneut laden) | Ist Z7, E18, Norm (Abschaltung nach C->B; Zeit ungeprüft) |
| SOLL-SM-63 | S6 | LD fällt weg (D4, D6, LD permanent) > 100 ms | siehe SOLL-LOCK-40 | Ist Z7, F9 |
| SOLL-SM-70 | S7 | Eintritt | CP +12 V DC (PWM aus), Frist `stop_timeout` = **5 s** starten | E7b, HB S. 38 |
| SOLL-SM-71 | S7 | CP-Rohzustand B, A, E oder F, oder Frist abgelaufen | Schütz aus; danach Ziel aus 5.2 (S3, S8, S9, S10) | E7b, HB S. 38 ("Wenn 5 s nach Abschaltung des PWM-Signals S2 nicht geöffnet wird, wird der Ladevorgang unabhängig vom Fahrzeugstatus beendet") |
| SOLL-SM-72 | S7 | Ursache entfällt vor Schütz-Aus (z. B. Strom wieder >= 6 A) | Ladeende trotzdem abschließen (Schütz aus), dann regulär über S3 neu starten | [ANN] |
| SOLL-SM-80 | S8 | Schütz aus (immer, da Eintritt nur über S7 oder aus Zuständen ohne Schütz) | Entriegelungszyklus (Abschnitt 8); Erfolg: CP E -> S9, CP F -> S10, sonst -> S1 | Ist Z2, E10 |
| SOLL-SM-81 | S8 | D9 an, ML = 1, CP nicht E/F, kein Entriegelungs-Fehler | Abbruch -> S2 (erneut verriegeln) | Ist Z2 |
| SOLL-SM-90 | S9 | CP A | Fehler mit Rücksetzen "A" löschen -> S1 | Ist Z8 |
| SOLL-SM-91 | S9 | CP B/C/D und kein Fehler mehr aktiv | -> S1 | Ist Z8 |
| SOLL-SM-92 | S9 | Status F (Verfügbarkeit fehlt, Schützfehler) | -> S10 | Ist Z8 |
| SOLL-SM-100 | S10 | Ursache des F entfallen | -> S1 (Neubewertung nach SOLL-CP-05) | Ist Z9 |
| SOLL-SM-101 | S10 | Ursache entfallen, aber anderer Fehler aktiv | -> S9 | Ist Z9 |

Hinweis zu SOLL-SM-62: Das Original fällt in diesem Fall nach Z3 zurück und löst über
Z4 einen neuen Selbsttest aus; E18 schließt den erneuten Test aus, daher direkt S5.

### 5.4 Weckfolge (E8, E17)

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-SM-110 | Ablauf: Phase 1 CP **-12 V DC für 3000 ms**, Phase 2 CP **+12 V DC für 750 ms**, danach PWM wie vor der Weckfolge. Schütz in allen Phasen aus. Nicht blockierend. | Ist Z5, E8 |
| SOLL-SM-111 | Auslösung: CP B in S5 für `wakeup_delay` (Default **30000 ms**, Bereich 0 = aus, 5000..300000 ms) seit PWM-Start. | E17, HB S. 40 |
| SOLL-SM-112 | Wiederholung `wakeup_mode`: 0 = einmal je Steckvorgang (**Default**, wie 1.27); 1 = zusätzlich erneut nach jeder Stromsperre bzw. Unterbrechung durch die Ladestation (wallbe-Verhalten). Das wallbe-Verhalten ergibt sich mit `wakeup_delay` = 5000 und `wakeup_mode` = 1. | E17, 1.27 §3.2 |
| SOLL-SM-113 | Während Phase 1 gibt es kein positives Plateau (SOLL-CP-04); "CP ungültig" und Zustandswechsel werden nicht ausgewertet. In Phase 2 wird gemessen; meldet das Fahrzeug C, wird die Weckfolge regulär bis zum PWM-Start fortgesetzt (kein Schütz ohne PWM). | Ist §2a, [ANN] |
| SOLL-SM-114 | Rang 1..9 der Vorrangliste brechen die Weckfolge sofort ab. | E7 |

Prüfung Abschnitt 5: PC – Zustandsübergangstabelle vollständig (jede Zeile ein Test),
Vorrangreihenfolge mit gleichzeitig eintretenden Ereignissen (z. B. Entriegelungswunsch +
CP C, Fehler + Stromänderung), Fristen 5 s/100 ms/Weckfolge mit simulierter Zeit,
Weckfolge in beiden Modi und mit Abbruch, Selbsttest einmal je Steckvorgang,
Anlauf 5 s. Abgleich gegen Ist-Spez. als Regressionsliste (jede **[ABW]** als bewusst
abweichender Test). H2 – Ablauf A->B->C->B->A und Weckfolge am EV-Simulator
(Oszilloskop: CP-Verlauf, Schütz-Ansteuersignal ohne Netzspannung). H4 – Ladevorgang
mit Fahrzeug.

## 6. Stromvorgabe

### 6.1 Slot-/Minimum-Modell

Alle Ströme in **mA** (E21). Der wirksame Strom `I` ist das Minimum aller aktiven Slots.
Das entspricht der Stromkette des Originals (Ist §6) und dem Slot-Modell von WARP
(Arch §8).

| Kennung | Slot | Wert | aktiv | Quelle |
| --- | --- | --- | --- | --- |
| SOLL-CUR-01 | `hardware` | Drehschalter: Pos. 1..9 = 6000, 10000, 13000, 16000, 20000, 32000, 63000, 70000, 80000 mA; über die Schnittstelle absenkbar (Original Feld 801, F3) | immer | HB S. 16, Ist §10 |
| SOLL-CUR-02 | `cable` | Kabelwert aus PP (13000/20000/32000/63000) bzw. ohne D1 = `hardware` | mit D1 und gültigem Kabelwert | Ist §6 Schritt 4 |
| SOLL-CUR-03 | `input` | kleinster Wert aller aktiven Eingänge mit Code 8..15 (6, 10, 13, 16, 20, 32, 63, 70 A) | Eingang aktiv | Ist §6 Schritt 7 |
| SOLL-CUR-04 | `external` | Sollstrom der Schnittstelle (WARP `evse/external_current`, Original Feld 528) | immer, außer Eingang Code 19 aktiv (F10 entschieden: wie HB) | E21, Arch §8, HB S. 50 |
| SOLL-CUR-05 | (Platzhalter) | später OCPP, Lastmanagement | nicht implementiert | AG, Arch §6 |

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-CUR-10 | `I` = min(aktive Slots). | Ist §6 |
| SOLL-CUR-11 | **Stromsperre**, wenn `I` < 6000 mA, oder Eingang Code 20 (Pause) aktiv, oder Pause-Anforderung der Schnittstelle (Original Feld 468) gesetzt. Stromsperre -> CP +12 V DC, kein Schütz (geordnetes Ende, Rang 8). 5100..5999 mA werden **nicht** als Tastgrad < 10 % ausgegeben. | E19, Ist §6 Schritt 9/10 |
| SOLL-CUR-12 | **5-%-Modus** (Drehschalter 0, Schnittstellen-Anforderung Original Feld 401, oder Eingang Code 7): ist keine Stromsperre aktiv, wird Tastgrad **50 Promille** ausgegeben; das Schütz schließt in diesem Modus nie; Status "digitale Kommunikation nicht verfügbar". Die Stromslots sind wirkungslos. **[ABW]** Original: unerreichbar, CCR 1000. | E6, HB S. 39 |
| SOLL-CUR-13 | Plausibilität: Ein berechnetes `I` > `hardware` oder > 80000 mA ist ein interner Fehler (Klasse P, ERR-INTERNAL) mit Sofortabschaltung. Konstruktionsbedingt darf das nie auftreten. | E5 |
| SOLL-CUR-14 | Schreibzugriffe auf Strom-Slots: zulässig 0 oder 6000..80000 mA; andere Werte werden **mit Fehlermeldung abgelehnt**. Ein Wert über dem aktuellen `hardware`-Grenzwert wird angenommen und wirkt über das Minimum (kein Fehlerzustand). | E14, F11 entschieden |
| SOLL-CUR-15 | Startwert von `external` nach jedem Start (Erststart und Neustart): Parameter `external_current_default`, Default 80000 mA = keine Einschränkung (Original: 528 = Drehschalterwert; wirksam ist in beiden Fällen das Drehschaltermaximum). Änderungen von `external` werden **nicht** gespeichert. Grund: Ohne Zutun soll die Box laden; ein Startwert 0 würde ohne evcc nie laden. Folge: Nach einem Neustart während des Ladens sendet evcc seinen Wert erst bei der nächsten Änderung bzw. beim nächsten Anstecken erneut, bis dahin wirkt das Maximum ([Test und Simulation](test-und-simulation.md#evcc-gegen-die-simulation)). Wer das nicht will, setzt den Parameter auf 0 (evcc setzt seinen Strom dann beim nächsten Statuswechsel neu). Sollwert, Grenzen und wirksamer Strom sind getrennt sichtbar (SOLL-API-32). | Arch §8 (kein Flashschreiben), F12 |
| SOLL-CUR-16 | Änderungen von `I` während PWM an wirken im selben Automatenzyklus auf den Tastgrad; Wechsel ueber/unter 6 A führt über Stromsperre/Neustart der PWM (S7 -> S3 -> S5). | E7d |

### 6.2 Tastgrad

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-CUR-20 | Ausgegeben wird der **größte** Tastgrad (Promille, ganzzahlig), dessen Strom nach HB Tab. 6-3 `I` nicht übersteigt: 6000 <= `I` <= 51000: `d` = floor(`I` / 60); 51000 < `I` < 52500: `d` = 850; 52500 <= `I` <= 80000: `d` = floor(`I` / 250) + 640. Beispiele: 6000 -> 100; 16000 -> 266 (15,96 A); 32000 -> 533; 80000 -> 960. | HB S. 39, Ist §6 Schritt 11 |
| SOLL-CUR-21 | Gemeldet werden `I` (wirksamer Sollstrom, mA) und der dem Tastgrad entsprechende Strom `allowed_charging_current` (mA). | Arch §8 |
| SOLL-CUR-22 | **[ABW]** Das Original rundet bei 51,1..52,4 A auf einen kleineren Tastgrad (z. B. 51,1 A -> 844); die Soll-Regel liefert in diesem Bereich 850 (= 51,0 A). Beide bleiben unter dem Sollstrom. | Ist §6 |

Prüfung Abschnitt 6: PC – Minimum aller Slot-Kombinationen, Code-Reihenfolge der
Eingänge, 5-%-Modus aus allen drei Quellen mit/ohne Stromsperre, Grenzen 5999/6000,
51000/51001/52499/52500, 80000, Ablehnung ungültiger Schreibwerte, Plausibilitätsfehler
durch Fehlerinjektion. H1 – Tastgrad am Oszilloskop für 6/10/16/32/63/80 A und 5 %.

## 7. Schutzpfad

Grundlage: [RCM/Bender](../../wallbe2/docs/rcm-bender.md), Ist §7, 1.27 §3.4, E1, E2, E4, E12, E16, E22.
**Sicherheitshinweis:** Die Soll-Reaktionszeiten betreffen nur die Firmware; die
Abschaltzeit des Gesamtsystems (Relais, Schütz) und die Frage, ob SWITCH_1/2 zusätzlich
direkt auf den Schütztreiber wirken, sind Hardwarefragen ([Hardwarefragen](../../wallbe2/docs/hardware.md#4-offene-fragen)).

### 7.1 Schütz-Schnittstelle

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-PROT-01 | Das Schütz (PE2) wird über genau eine Schnittstelle geschaltet: **Aus** darf jede Stelle (Schutz-ISR, Schutzthread, Automat, Ausnahmebehandlung) jederzeit; **Ein** nur der Automat in S6 und nur, wenn kein Schutzfehler verriegelt ist. Ein Schutzfehler setzt eine Sperre, die Ein-Befehle bis zur Rücksetzung (Abschnitt 11) wirkungslos macht. | E2 |
| SOLL-PROT-02 | Mit jedem Schütz-Aus durch den Schutzpfad wird zugleich die CP-Ausgabe auf +12 V DC gesetzt (PWM aus), Ausnahme Schützfehler (-12 V DC). | Ist §7, E7a |
| SOLL-PROT-03 | Der Schützzustand (Soll und Ansteuerpegel) ist als Status lesbar. | Ist §12 Code 27 |

### 7.2 Fehlerstrom (RCM)

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-PROT-04 | Die drei Sensorausgänge **SWITCH_1 (PE3), SWITCH_2 (PE4), ERR_OUT (PE6)** werden in **jedem** Zustand ausgewertet, auch in S0 und S1. Jeder "ausgelöst"-Pegel schaltet sofort ab (Schütz aus nach SOLL-PROT-01/02). | E1 **[ABW]** Original: SWITCH_1 nur bei CP A/B und Sensorprofil 0 |
| SOLL-PROT-05 | Erfassung: Flankeninterrupt (EXTI) auf jeden Wechsel nach "ausgelöst" **und** zyklische Pegelkontrolle im Schutzthread (1 ms). Software-Reaktionszeit vom Signalwechsel bis PE2 Low **<= 1 ms** (ISR-Pfad), Rückfallebene zyklisch <= 2 ms. | E2, F4 |
| SOLL-PROT-06 | Aktive Pegel ("ausgelöst" = High an der MCU, Inversion zum Bender-Ausgang) sind eine Eigenschaft der Steuerung und werden in der Board-Beschreibung festgelegt, nicht als Laufzeitparameter. Bestätigung in H3. | [HYP] RCM/Bender |
| SOLL-PROT-07 | Fehlerbits: ERR_OUT -> ERR-RCM-SENSOR; SWITCH_2 -> ERR-RCM-DC6 (Fehlerstrom >= 6 mA DC bzw. 30 mA AC im IEC-Modus); SWITCH_1 -> ERR-RCM-AC30 (30 mA rms). Gemeinsamer Sammelzustand "Fehlerstrom" (Original R107 Bit 15). Bei gleichzeitigen Signalen werden alle zutreffenden Bits gesetzt **[ABW]** (Original: nur ein Zweig je Aufruf). | Ist §7, rcm-bender |
| SOLL-PROT-08 | Fehlerstromfehler sind **verriegelnd**: Sie bleiben gesetzt, auch wenn der Sensor sich selbst wieder einschaltet (Auto-Reclose). Rücksetzung nur durch einen bestandenen Selbsttest im CP-Rohzustand A (SOLL-PROT-11). | rcm-bender Vorschlag 3, Ist §2a (d) |

### 7.3 RCM-Selbsttest (E16)

Zeitfolge nach Phoenix 1.27 (`0x0803bbb8`, Decompilat `analysis/ghidra-1.27/export/decomp.c`,
am 2026-10-19 gelesen), Zeitpunkte ab Teststart:

| t | Schritt | Fehlschlag -> |
| --- | --- | --- |
| 0 ms | Vorbedingung: ERR_OUT, SWITCH_1, SWITCH_2 nicht ausgelöst, Schütz aus | Test nicht starten (Fehler bleibt bestehen) |
| 100 ms | ERR_OUT erneut prüfen | ERR-RCM-SENSOR + ERR-RCM-SELFTEST |
| 200..300 ms | TEST_IN (PE5) Low für **100 ms** | – |
| 1000 ms | SWITCH_2 muss ausgelöst sein (Bender M1) | ERR-RCM-SELFTEST + ERR-RCM-DC6 |
| 1700 ms | SWITCH_1 muss ausgelöst sein (M2) | ERR-RCM-SELFTEST + ERR-RCM-AC30 |
| 2400 ms | SWITCH_1 und SWITCH_2 dürfen nicht beide noch ausgelöst sein (M3) | ERR-RCM-SELFTEST |
| bis 3400 ms | **[ABW]** beide wieder nicht ausgelöst (vollständige Rückkehr vor Ladefreigabe) | ERR-RCM-SELFTEST |

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-PROT-10 | Der Selbsttest läuft nach der Zeitfolge oben, nicht blockierend, nur bei Schütz aus. Erwartete Auslösungen während des Tests sind keine Fehlerstromfehler; jede Abweichung setzt **sofort** ERR-RCM-SELFTEST (Klasse P) und sperrt die Freigabe. | E16 |
| SOLL-PROT-11 | Anlässe: (1) beim Start (S0); (2) vor dem ersten PWM-Start je Steckvorgang (S4); (3) zur Rücksetzung eines Fehlerstrom-/Selbsttestfehlers im CP-Rohzustand A, frühestens `rcm_retest_interval` = **30 s** nach dem letzten Versuch (F13). Nicht bei Rückkehr der PWM innerhalb eines Steckvorgangs. | E16, E18, F13 |
| SOLL-PROT-12 | Ein bestandener Test löscht alle RCM-Fehlerbits. | 1.27 `0x0803bbb8` |
| SOLL-PROT-13 | Das zweite Antwortprofil des Originals (`cf3` = 1, Reaktion 390..710 ms, SWITCH_1 ignoriert) wird **nicht** übernommen. **[ABW]** wie 1.27. | E16, rcm-bender |

### 7.4 Schützüberwachung (Feld 390)

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-PROT-20 | Modus `contactor_monitor`: 0 aus; 1 **Via Sense** (Klemme CT, PE0); 2 Rückmeldekontakt NO (Eingang Code 5); 3 Rückmeldekontakt NC (Eingang Code 6); 4 Via Energy Meter. Default **1** (E20). | Ist §7, takt-adc-gpio |
| SOLL-PROT-21 | Prüfung nur bei ausgeschaltetem Schütz, beginnend `contactor_check_delay` (Vorschlag 200 ms) nach dem Aus-Befehl. Fehler ERR-CONTACTOR, wenn ununterbrochen >= 250 ms gilt: Modus 1 – letzte steigende Flanke an PE0 jünger als 100 ms; Modus 2 – Eingang Code 5 aktiv; Modus 3 – Eingang Code 6 inaktiv. (Original: Zähler > 9 bei 25-ms-Zyklus, also ~250 ms.) | Ist §7, [ANN] Verzögerung |
| SOLL-PROT-22 | Modus 4: jede Phase einzeln, Fehler wenn **irgendeine** Phase > 50 V. **Nur mit Messquelle** (F1); ohne Messquelle wird das Setzen von Modus 4 abgelehnt. **[ABW]** Original wertet nur alle drei Phasen gemeinsam. | E4 |
| SOLL-PROT-23 | Wirkung ERR-CONTACTOR: Klasse P, CP-Status F, kein erneutes Einschalten. Rücksetzung **nur durch Neustart oder ausdrückliche Quittierung** (Web/Shell), und nur wenn der Fehler nicht mehr erkannt wird (F14). **[ABW]** Original löscht das Bit, sobald die Erkennung entfällt. | Ist §7, F14 |
| SOLL-PROT-24 | **D5:** Ist D5 an und ERR-CONTACTOR gesetzt, wird PE1 **200 ms** später auf Low gesetzt. Befund: Im Original wird PE1 nur Low geschrieben (GPIO-Init und dieser Pfad; kein High-Schreiber in SL und 1.27 gefunden); die Funktion ist damit im Original ohne sichtbare Wirkung. Entschieden (F15): **wie Original** (PE1 bleibt Low, D5 ohne sichtbare Wirkung); nach Klärung des PE1-Ziels an der Hardware (H1, Kandidat Relais C3/C4) neu entscheiden. | E12, Decompilat SL Zeile 38338, 1.27 Zeile 39145 |

### 7.5 Überstrom (Feld 409), nur mit Messquelle

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-PROT-30 | Die Ladelogik bietet einen Eingang für Phasenströme (mA, je Phase, mit Zeitstempel/Gueltigkeit). Ohne Messquelle ist die Überstromerkennung **nicht verfügbar**; Parameter `overcurrent_enable` ist dann nur 0 schreibbar. | F1 |
| SOLL-PROT-31 | Mit Messquelle: Fehler ERR-OVERCURRENT, wenn ein Phasenstrom >= 1,25 x Tastgrad-Strom für 5 s oder >= 1,1 x für 95 s überschreitet. Rücksetzung bei CP A. | E22, Ist §7 |
| SOLL-PROT-32 | Ausfall der Messquelle bei aktivierter Überstromerkennung -> ERR-METER-COMM. | Ist §7 (c7b.1) |

### 7.6 Versorgung und interne Prüfungen

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-PROT-40 | Versorgungswert (ADC IN10/PC0, Original-Skalierung, Bedeutung [HYP] 12-V-Schiene): > 10,0 = gut, < 9,0 = zu niedrig, dazwischen Hysterese. | Ist §3, takt-adc-gpio |
| SOLL-PROT-41 | Versorgung zu niedrig für > 100 ms -> **Warnung** WARN-SUPPLY (keine Abschaltung, kein Fehlerzustand, wie Original). Die RCM-Überwachung bleibt **immer** aktiv (E1). Rücksetzung nach 2500 ms ununterbrochen "gut"; **kein** Sprung in den Grundzustand. **[ABW]** Original setzt die RCM-Überwachung bei Unterspannung aus (außer in Z7) und kehrt nach 2500 ms nach Z0 zurück. | Ist §3, F16 |
| SOLL-PROT-42 | Der Schutzpfad selbst (EXTI-Konfiguration, Pegel PE2) wird zyklisch auf Plausibilität geprüft (z. B. Ausgangsregister PE2 entspricht Sollzustand); Abweichung -> ERR-INTERNAL. | E3, [ANN] |

Prüfung Abschnitt 7: PC – Schütz-Schnittstelle (Ein-Sperre), Selbsttest-Zeitfolge mit
simulierten Pegeln (alle Fehlschlagpunkte, Abbruch), Verriegelung der RCM-Fehler,
Schützüberwachung Modi 1..3 mit Zeitreihen, Modus-4-Ablehnung, Überstrom mit
simulierter Messquelle, Versorgungshysterese. H1 – Latenz EXTI -> PE2 mit Testsignal am
Oszilloskop (Signalgenerator statt Sensor, ohne Netzspannung); D5/PE1-Pegel. H3 –
Selbsttest mit echtem Sensor, Prüfstrom 6 mA DC und 30 mA AC im sicheren Prüfaufbau;
Abschaltzeit Gesamtsystem messen.

## 8. Verriegelung

Grundlage: Ist §9, E10, E13, E15, HB S. 9 (Ansteuerung 12 V, max. 1 A, 500 ms Default bis
3 s; Rückmeldung "potenzialfreier Kontakt, geschlossen bei erfolgreicher Verriegelung";
"automatische Entriegelung bei Stromausfall"), [Firmwarevergleich 1.27 §3.5](../../wallbe2/docs/firmware-vergleich-1.27.md#35-verriegelung-und-dip-schalter).

### 8.1 Verriegelungszustand

Zustände: INIT, UNLOCKED, LOCKING, LOCKED, UNLOCKING, ERROR (entspricht WARP
`lock_state` 0..5). Persistent gespeichert wird der zuletzt erreichte Endzustand
(Original Feld 465), damit nach einem Neustart bekannt ist, ob entriegelt werden muss.

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-LOCK-01 | **D4 aus:** keine Aktorimpulse; ein Verriegelungs-/Entriegelungswunsch setzt den Zustand sofort auf LOCKED bzw. UNLOCKED. | Ist §9 |
| SOLL-LOCK-02 | **D4 an:** Verriegelungszyklus nach 8.2. D4 und D6 wirken immer; es gibt kein Feld 485. | E13, E15 |
| SOLL-LOCK-03 | **D6 an:** Die LD-Funktion liefert der erste der Eingänge LD, EN, ML, XR, IN (in dieser Reihenfolge) mit Funktionscode **22** (gepulst) bzw., falls keiner, **3** (permanent). Die Auswahl erfolgt über Feld 520 bzw. die Funktionsparameter der Eingänge. Ist D6 an und kein Eingang mit Code 3 oder 22 konfiguriert -> Konfigurationsfehler ERR-CONFIG (kein Laden, F17 entschieden). **[ABW]** Original: Verriegelung schlägt dann still fehl. | E15, Ist §9, F17 |

### 8.2 Verriegelungszyklus (nicht blockierend, E10)

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-LOCK-10 | Zwischen dem Ende eines Zyklus und dem Beginn des nächsten liegen mindestens `lock_cycle_gap` ms (Feld 527, 500..10000, Default 2000). | Ist §9 Schritt 3 |
| SOLL-LOCK-11 | Verriegeln: PA9 Low für `lock_pulse` ms (Feld 525, 100..3000, Default 500), dann High. Entriegeln: PA10 Low für `unlock_pulse` ms (Feld 526, 100..3000, Default 500), dann High. PA9 und PA10 sind **nie gleichzeitig** Low; zwischen zwei Impulsen mindestens 10 ms beide High. | Ist §9 Schritt 4, HB S. 9, [ANN] Gegenseitigkeit |
| SOLL-LOCK-12 | Ohne D6: Nach dem Impuls gilt der Zielzustand als erreicht. | Ist §9 Schritt 6 |
| SOLL-LOCK-13 | D6 mit **LD permanent (Code 3):** 500 ms nach Impulsende wird LD gelesen: Verriegeln erfolgreich, wenn LD aktiv; Entriegeln erfolgreich, wenn LD inaktiv. | Ist §9 Schritt 6, E15 |
| SOLL-LOCK-14 | D6 mit **LD gepulst (Code 22):** Während des Verriegelungsimpulses wird der LD-Rohpegel in jedem Messblock abgetastet. Erfolg, wenn LD mindestens **10 ms** (mindestens zwei aufeinanderfolgende Messblöcke) aktiv war und danach innerhalb des Impulsfensters wieder inaktiv wird. Beim Entriegeln gilt LD nach dem Impuls als inaktiv (keine Auswertung). **Keine** Flankenauswertung außerhalb der Impulse **[ABW]** gegenüber 1.27 (Flankentoggle), wie SL. | E15, Ist §9 Schritt 4, 1.27 §3.5 |
| SOLL-LOCK-14a | Hintergrund (Fremdquelle, Hypothese): Beim Küster-Aktor (02S/03S, Phoenix EV-T2M3S-E-LOCK12V) liegt die Rückmeldung in beiden Endlagen auf 0 V und während der Fahrt auf +12 V. Code 22 erkennt damit "Motor ist von einer Endlage in die andere gefahren". Das gleiche Kriterium ist beim **Entriegeln** als Option `unlock_check` auswertbar (F27: Default **aus** = wie Original; LD-Impuls beim Entriegeln am Testgerät gemessen, Option bleibt vorerst aus, Nutzer 2026-10-24). | [Hardwarefragen](../../wallbe2/docs/hardware.md#5-verriegelung-ld-rueckmeldung) |
| SOLL-LOCK-15 | Abtastrate für LD gepulst: Ist die Impulsdauer des Rückmeldekontakts kürzer als zwei Messblöcke, wird LD während eines Verriegelungsimpulses mit 1 ms abgetastet (eigene ADC-Abfrage). Gemessen am Testgerät (Küster 03S): Impuls grob 60..100 ms, also länger als zwei Messblöcke; die 1-ms-Abtastung entfällt (Nutzer 2026-10-24). | [ANN], [Hardware §5](../../wallbe2/docs/hardware.md#5-verriegelung-ld-rueckmeldung) |
| SOLL-LOCK-16 | Ein Zyklus endet nach dem Ergebnis plus 10 ms; der Zeitpunkt des Endes ist Bezug für SOLL-LOCK-10. Während eines Zyklus laufen Schutzpfad, CP-Auswertung und Automat unverändert weiter. | E10, Ist §9 Schritt 5 |
| SOLL-LOCK-17 | Ein laufender Impuls wird nur durch den sicheren Zustand abgebrochen (beide Ausgänge High); der Zyklus gilt dann als fehlgeschlagen. | [ANN] |

### 8.3 Ablauf, Fehlversuche, Entriegeln

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-LOCK-20 | Beim Start (S0) mit D4: ein Entriegelungszyklus, unabhängig vom gespeicherten Zustand (wie Original-Lade-Init `FUN_08039cd8`: `Lock(0)` nach dem Selbsttest). Ist das Ergebnis "verriegelt", gilt ERR-UNLOCK. | Decompilat SL `0x08039cd8` (2026-10-19 gelesen) |
| SOLL-LOCK-21 | Verriegeln in S2: bis zu **5** Versuche; nach jedem Fehlschlag ERR-LOCK setzen und einen Entriegelungszyklus ausführen, dann erneut versuchen; nach dem 5. Fehlschlag -> S9 mit ERR-LOCK (bis CP A bzw. Kabelwert 0). Erfolg löscht ERR-LOCK. ERR-LOCK erzwingt E erst ab dem 3. Fehlschlag (bzw. sofort mit D9). | Ist Z1, Belege §1 Schritt 6 |
| SOLL-LOCK-22 | Entriegeln (S8) **nur bei ausgeschaltetem Schütz und CP +12 V DC** (keine PWM). Bis zu **4** Versuche; nach jedem Fehlschlag ERR-UNLOCK setzen und erneut verriegeln; nach dem 4. Fehlschlag -> S9 mit ERR-UNLOCK, Zustand bleibt LOCKED. | E10, Ist Z2 |
| SOLL-LOCK-23 | **D9 aus** (automatisch): Verriegeln bei Fahrzeug erkannt (CP B/C/D) und gültigem Kabelwert; Entriegeln bei CP A. | HB S. 15, Ist Z0/Z1 |
| SOLL-LOCK-24 | **D9 an** (manuell): Verriegelungswunsch = Funktion ML (Eingang Code 4 Pegel bzw. Code 17 Umschalten bei steigender Flanke, Mindestabstand 200 ms) oder Schnittstellenwunsch (Original Feld 403). Wunsch 1 -> verriegeln (auch ohne Fahrzeug); Wunsch 0 -> geordnetes Ladeende, dann entriegeln. | HB S. 15, Ist §10, E7c |
| SOLL-LOCK-25 | **Entriegeln-Anforderung** (Original Feld 440, unabhängig von D9): beendet einen Ladevorgang geordnet, entriegelt und verhindert ein erneutes Verriegeln bis CP A. | Ist Z1/Z3/Z5/Z7, E7c |
| SOLL-LOCK-26 | Ein Verriegelungszyklus während S6 ist nur als **Wiederverriegeln** zulässig (SOLL-LOCK-40); ein Entriegelungsimpuls bei eingeschaltetem Schütz ist unzulässig und wird von der Aktorschnittstelle abgewiesen (ERR-INTERNAL). | E10 |

### 8.4 LD-Überwachung während des Ladens

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-LOCK-40 | Mit D4, D6 und **LD permanent**: LD inaktiv > 100 ms in S6 -> ERR-LD-LOST. Entschieden (F9): Schütz **sofort** aus (Verriegelung nicht gesichert), PWM aus, ein Wiederverriegelungszyklus; Erfolg -> Fehler löschen und über S3 neu starten (ohne neuen Selbsttest); Misserfolg -> S9 bis CP A. **[ABW]** Original: blockierendes Wiederverriegeln bei möglicherweise eingeschaltetem Schütz, Ladeende erst über erzwungenes E. | Ist Z7, E10, F9 |
| SOLL-LOCK-41 | Mit LD gepulst entfällt die Überwachung während des Ladens (der Rückmeldewert ändert sich nur im Verriegelungszyklus). | Ist §9, 1.27 §3.5 |

Prüfung Abschnitt 8: PC – Zyklus mit simulierter Zeit für D4/D6/Code 3/Code 22,
Mindestabstand 527, Impulslängen 100/500/3000 ms, nie gleichzeitig Low, Fehlversuche
5/4, Start-Entriegelung, D9 mit Code 4/17/Schnittstelle, Entriegeln-Anforderung,
LD-Verlust im Laden; nachweisen, dass Schutzreaktionen während eines Zyklus nicht
verzögert werden. H1 – Impulse an R1/R2 (Oszilloskop, Aktor angeschlossen oder
Ersatzlast), Signalform des LD-Kontakts der vorhandenen Steckdose (gepulst/permanent,
Impulse beim Entriegeln, Dauer). H2 – Ablauf mit EV-Simulator und Stecker.

## 9. Ein-/Ausgangsfunktionen und Freigabelogik

### 9.1 Eingangsfunktionen (LD, EN, ML, XR, IN)

Jeder der fünf Eingänge hat einen Funktionscode (Parameter `in_function_<klemme>`,
Original Felder 520..524). Codes und Texte nach `input_list` (`status.html`) und HB
Tab. 8-2 (S. 49/50); Wirkung nach [Taktbaum §Digitale Eingänge](../../wallbe2/docs/takt-adc-gpio.md#digitale-eingaenge-0x0803c7d4).

| Code | Funktion | Soll-Wirkung |
| --- | --- | --- |
| 0 | keine | – |
| 1 | EN permanent | Freigabe aktiv, solange Eingang ein |
| 16 | EN gepulst | jede steigende Flanke schaltet die EN-Freigabe um, Mindestabstand 200 ms |
| 18 | EN mit Portal-Logging | wie 1 (Portal nicht im Umfang; F18 entschieden) |
| 2 | XR | Verfügbarkeit (mit D8) |
| 3 | LD permanent | Verriegelungsrückmeldung als Pegel (Abschnitt 8) |
| 22 | LD gepulst | Verriegelungsrückmeldung als Impuls während des Verriegelungsimpulses (Abschnitt 8); sonst ohne Wirkung |
| 4 | ML permanent | steigende Flanke: Verriegelungswunsch 1, fallende Flanke: 0 (mit D9) |
| 17 | ML gepulst | steigende Flanke schaltet den Verriegelungswunsch um, Mindestabstand 200 ms |
| 5 / 6 | Schützrückmeldung NO / NC | Eingang für Schützüberwachung Modus 2/3 |
| 7 | 5 % | 5-%-Modus (SOLL-CUR-12) |
| 8..15 | Maximal Current 6/10/13/16/20/32/63/70 A | Slot `input` (SOLL-CUR-03) |
| 19 | Maximal Current | Slot `external` wird ignoriert (HB S. 50: "Begrenzungen über Modbus TCP werden ignoriert"), F10 |
| 20 | Pause (Suspended EVSE) | Stromsperre (SOLL-CUR-11) |
| 21 | Error Input | Fehler ERR-INPUT solange aktiv (Original R155 Bit 2) |

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-FN-01 | Codes außerhalb {0..22} werden beim Schreiben abgelehnt. | E14, Belege §5 |
| SOLL-FN-02 | Haben mehrere Eingänge dieselbe Funktion, gilt für Pegelfunktionen ODER (aktiv, wenn irgendeiner aktiv ist), für Flankenfunktionen jede Flanke an jedem dieser Eingänge. **[ABW]** Original: zuletzt ausgewerteter Kanal gewinnt. | F26 entschieden |
| SOLL-FN-03 | Verriegelungswunsch (ML) kann außerdem über die Schnittstelle gesetzt werden (Original Feld 403); das jüngste Ereignis (Flanke oder Schreibzugriff) gilt. | Ist §2 |

### 9.2 Freigabe und Verfügbarkeit

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-EN-01 | **Ladeerlaubnis** liegt vor, wenn mindestens eine Bedingung gilt: (a) D7 an und EN-Funktion aktiv; (b) D10 an und Schnittstellen-Freigabe (`enable_register`, Original Feld 400) = 1; (c) weder D7 noch D10 an. **[ABW]** Die RFID-Whitelist (`c87`) und OCPP-Autorisierung (`c88`) entfallen (nicht im Umfang). | Ist §5, HB S. 15 |
| SOLL-EN-02 | **Freigabe** = Ladeerlaubnis und keiner der Sperrgründe: ERR-CABLE-REJECT, nicht verriegelt (D4 an und Zustand != LOCKED), Fehlerstrom-/RCM-Fehler, ERR-CONTACTOR, sowie jeder weitere aktive Fehler der Klassen P/V/K. | Ist §5 (erweitert) |
| SOLL-EN-03 | Startwert `enable_register` nach Neustart = Parameter `enable_register_default` (Default 0 wie Original, `.data` `c86` = 0; F19 entschieden). | Datei `0x4cdf6` = 0 |
| SOLL-EN-10 | **Verfügbarkeit** fehlt, wenn (D8 an und XR-Funktion inaktiv) oder (D10 an und `available_register` (Original Feld 402) != 1). Folge: CP-Status F, -12 V DC (nach geordnetem Ende, Rang 6). | Ist §2a, Ist §6, HB S. 15 |
| SOLL-EN-11 | Startwert `available_register` nach Neustart: 1 (Original: `.data` `c89` = 1, Lade-Init setzt 1). | Datei `0x4cdf9` = 1, Decompilat `0x08039cd8` |
| SOLL-EN-12 | Die evcc-Anbindung (Slot `external`) ist von der Freigabe unabhängig: `external` = 0 bewirkt Stromsperre, nicht Entzug der Freigabe. Mit D10 an ist zusätzlich `enable_register` = 1 nötig. | Arch §8, [ANN] |

### 9.3 Ausgangsfunktionen (ER, LR, VR, CR)

Parameter `out_function_<klemme>` (Original Felder 327..330), Auswertung in jedem
Automatenzyklus. "ohne Fehler" = kein aktiver Fehler; "Status" = CP-Status (SOLL-CP-08).
Codes nach Ist §12 und HB Tab. 8-3 (S. 50).

| Code | Bedingung für High |
| --- | --- |
| 0 | nie |
| 1 / 2 | Status A / B ohne Fehler |
| 3 / 4 | Status B und PWM an / aus, ohne Fehler |
| 5 / 6 / 8 | Status C / D / F |
| 7 | Status E oder (Fehler aktiv und Status nicht F) |
| 9 | A oder B ohne Fehler |
| 10 / 11 | A, oder B mit PWM an / aus, ohne Fehler |
| 12 / 13 / 14 | (A oder B ohne Fehler) oder C / D / C oder D |
| 15 | E oder F oder Fehler aktiv |
| 16 | C oder D |
| 17 | PWM an |
| 18 / 19 | gültiger / kein gültiger Kabelwert |
| 20..23 | Kabelwert 13 / 20 / 32 / 63 A |
| 24 / 25 | Kabelwert in {13, 20} / {13, 20, 32} |
| 26 | Kabel abgewiesen |
| 27 | Schütz ein |
| 28 | Belüftung (S6 mit Status D) |
| 29 | Verriegelungsanforderung (letzter Verriegelungswunsch = verriegeln) |
| 30..33 | Schnittstellen-Ausgang 1..4 (Original Coils 405..408), Startwert 0 |
| 34 / 35 / 36 | ERR-OVERCURRENT / ERR-CONTACTOR / ERR-STATE-D |
| 37 | (B ohne Fehler) oder C oder D |
| 38 | A/B mit aktivem Eingangscode 19; Nachlauf 1 s nach Schütz aus |
| 39 | Freigabe erteilt und (C oder D, oder B nach vorheriger Stromsperre) **[ABW]** ohne RFID-Blinken (Feld 430) |
| 40 | während S0 1-Hz-Blinken, danach wie 9 |
| 41 | A oder B ohne Fehler und PWM aus **[ABW]** ohne RFID-Blinken |
| 42 | S6 mit Status B bei laufender PWM ("Suspended EV", 1.27) |
| sonst | Low; Schreiben abgelehnt (gültig 0..42) |

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-FN-10 | Alle Codes 0..42 werden unterstützt (Tabelle). Defaults: ER 15, LR 29, VR 16, CR 17. | E20, Ist §12 |
| SOLL-FN-11 | Im sicheren Zustand behalten die Ausgänge ihre Funktion; ER (Default 15) zeigt den Fehler. | [ANN] |

Prüfung Abschnitt 9: PC – Wahrheitstabelle jedes Ausgangscodes über alle Status- und
Fehlerkombinationen, jeder Eingangscode (Pegel, Flanke, 200-ms-Sperre), Freigabematrix
D7/D10/EN/400, Verfügbarkeit D8/D10/XR/402. H1 – Pegel an ER/LR/VR/CR, Eingänge mit
Labornetzteil (3/9 V), Schlüsselschalter an EN (wallbe, Beispielkonfiguration).

## 10. Parameter

Spalten: **P** = persistent (Parameterspeicher, Abschnitt 13), **B** = hängt von der
äußeren Beschaltung ab (Beispielwerte in [Hardware, Ist-Einstellungen](../../wallbe2/docs/hardware.md#3-ist-einstellungen-laut-weboberflaeche-box-g)).
Alle Schreibzugriffe mit Grenzprüfung; ungültige Werte werden mit Fehlermeldung
abgelehnt (E14). Namen sind Vorschläge (WARP-angelehnt, englisch).

### 10.1 Konfiguration

| Name | Einheit | Bereich | Default | P | Feld (Original) | B | Quelle |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `out_function_er` / `_lr` / `_vr` / `_cr` | Code | 0..42 | 15 / 29 / 16 / 17 | ja | 327 / 328 / 329 / 330 | ja | E20 |
| `in_function_ld` / `_en` / `_ml` / `_xr` / `_in` | Code | 0..22 | 3 / 1 / 4 / 2 / 11 | ja | 520 / 521 / 522 / 523 / 524 | ja | E15, E20 |
| `lock_pulse` | ms | 100..3000 | 500 | ja | 525 | ja | Ist §9, HB S. 9 |
| `unlock_pulse` | ms | 100..3000 | 500 | ja | 526 | ja | Ist §9 |
| `lock_cycle_gap` | ms | 500..10000 | 2000 | ja | 527 | ja | Ist §9 |
| `contactor_min_off` | ms | 0..10000 | 5000 | ja | 519 (nicht übernommen) | ja | Ist §8, F33 **[ABW]** |
| `contactor_monitor` | Code | 0..4 (4 nur mit Messquelle) | 1 | ja | 390 | ja | E20, SOLL-PROT-22 |
| `reject_state_d` | 0/1 | 0..1 | 1 | ja | 412 | nein | Ist §13 |
| `overcurrent_enable` | 0/1 | 0..1 (1 nur mit Messquelle) | 0 | ja | 409 | nein | SOLL-PROT-30 |
| `wakeup_delay` | ms | 0 (aus), 5000..300000 | 30000 | ja | – | nein | E17 |
| `wakeup_mode` | Code | 0 einmal je Steckvorgang, 1 auch nach Stromsperre | 0 | ja | – | nein | E17 |
| `external_current_default` | mA | 0, 6000..80000 | 80000 | ja | – | nein | F12 |
| `enable_register_default` | 0/1 | 0..1 | 0 | ja | – (neu, F19) | nein | F19 |
| `net_dhcp`, `net_ip`, `net_netmask`, `net_gateway`, `net_hostname` | – | IPv4 / Text | DHCP an (F25), statisch 192.168.0.8/24, 192.168.0.1, `wallfree-<MAC-Ende>` | ja | 404 (DHCP), 315..318 / 319..322 / 323..326 (IP / Maske / Gateway), laut Original-Webseite `lan.html` | nein | F25 |
| `web_user`, `web_password` | Text | Benutzer 1..32 Zeichen, Passwort 8..64 Zeichen (gespeichert als Hash) | `admin` / Default-Passwort, **Änderung beim ersten Zugriff erzwungen** | ja | – | nein | F20 |
| `unlock_check` | 0/1 | 0..1 | 0 | ja | – | ja | F27 |
| `meter_type` | Code | 0 kein Zähler, 1 ABB B23, 2 Eastron SDM630, 3 Eastron SDM72D-M V2, 4 Eastron SDM72D-M (V1) | 0 | ja | – | ja | F28, SOLL-MTR-01 |
| `meter_address` | – | 1..247 | 1 | ja | – | ja | SOLL-MTR-02 |
| `meter_baud` | Bd | 2400, 4800, 9600, 19200, 38400, 57600, 115200 | 9600 | ja | (386) | ja | SOLL-MTR-02 |
| `meter_framing` | Code | 0 = 8N1, 1 = 8E1, 2 = 8O1, 3 = 8N2 | 0 | ja | – | ja | SOLL-MTR-02 |
| `mqtt_enable` | 0/1 | 0..1 (1 nur mit `mqtt_broker`) | 0 | ja | – | nein | F30, SOLL-MQTT-01 |
| `mqtt_broker` | Text | Name (per DNS, bei jedem Verbindungsversuch neu aufgelöst) oder IPv4-Einzeladresse (auch 127.x für die PC-Simulation), ohne Port; leer = keiner (bis 2026-10-27 nur IPv4, alter Wert wird beim Start übernommen) | leer | ja | – | nein | SOLL-MQTT-02 |
| `mqtt_port` | – | 1..65535 | 1883 | ja | – | nein | SOLL-MQTT-02 |
| `mqtt_user`, `mqtt_password` | Text | 0..32 / 0..64 druckbare ASCII-Zeichen, leer = ohne Anmeldung; Passwort nicht lesbar | leer | ja | – | nein | SOLL-MQTT-02 |
| `mqtt_prefix` | Text | 0..64 Zeichen, leer = `wallfree/<MAC-Ende>` | leer | ja | – | nein | SOLL-MQTT-12 |
| `mqtt_interval` | s | 0..3600 (0 = ohne Bremse) | 1 | ja | – | nein | SOLL-MQTT-11 |
| `mqtt_commands` | 0/1 | 0..1 | 0 | ja | – | nein | SOLL-MQTT-20 |

Netz- und Systemparameter (`net_*`) wirken **nach einem Neustart**
(`POST /system/restart` oder Shell); bis dahin läuft die beim Start angewandte Konfiguration.
Prüfung: Einzeladresse (1..223, nicht 127), Netzmaske zusammenhängend /8../30, Gateway im Netz
der Adresse oder 0.0.0.0, Hostname 1..32 Zeichen aus Buchstaben, Ziffern, '-' (leer =
automatisch). Ist die gespeicherte feste Adresse widersprüchlich, startet das Netz mit DHCP.
**MAC-Adresse (Nutzer 2026-10-26):** Wallfree verwendet die Geräte-MAC, die der Hersteller im
OTP-Bereich ab `0x1FFF7800` (6 Byte) hinterlegt hat und die auch Bootloader und Originalfirmware
nutzen (Testgerät: `xx:xx:xx:a1:b2:c3`); sie wird vor dem Start von DHCP gesetzt. Fehlt sie (alles
`0xFF` oder 0) oder ist sie eine Gruppenadresse, bleibt die aus der Chip-ID erzeugte Adresse. Grund:
gleiche Adresse für Router/DHCP wie im Original und keine ARP-Verzögerung beim Wechsel zwischen
Bootloader und Wallfree ([Bootloader](../../wallbe2/docs/bootloader.md#update-betrieb-bootloader-box-t-firmware-081)).
Die Zählerparameter (`meter_*`) wirken ebenfalls nach einem Neustart (evcc liest die
Fähigkeiten nur beim Verbinden, SOLL-MTR-30). Die MQTT-Parameter (`mqtt_*`) wirken **sofort**: Die
Verbindung zum Broker wird mit den neuen Werten neu aufgebaut (SOLL-MQTT-31).

Feste Konstanten (keine Parameter, Änderung nur nach Absprache): Anlauf 5 s (E9),
Frist geordnetes Ende 5 s (E7), Überstrom 1,25 x/5 s und 1,1 x/95 s (E22), Werksreset
5 s (E22), Weckfolge 3000/750 ms (E8), RCM-Testfolge (E16), CP-/PP-Fristen 20 ms/1000 ms,
Diodenschwelle -10 V, Eingangsschwellen 3/9 V, `rcm_retest_interval` 30 s (F13),
`contactor_check_delay` 200 ms, Diodenfehler-Ausgabe -12 V 3 s (F7).

### 10.1a Schreibregeln

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-PAR-01 | Jeder Parameter und Laufzeitwert ist über HTTP und Shell lesbar; schreibbare Werte sind dort schreibbar (SOLL-API-30). | E14, Nutzer 2026-10-19 |
| SOLL-PAR-02 | Jeder Schreibzugriff wird gegen Bereich und Zustandsbedingung geprüft; Ablehnung mit Fehlercode und Klartext an den Absender, nie stilles Verwerfen. | E14 |
| SOLL-PAR-03 | Persistente Parameter wirken sofort und werden ohne Löschvorgang angehängt (SOLL-NVM-03). | [ANN] |
| SOLL-PAR-40 | **801/802/465 (F3 entschieden):** `hardware_limit_override` nur **absenken** (6000 mA .. Drehschalterwert); `dip_override` und `lock_state` nur in S1 (kein Fahrzeug) schreibbar, nicht persistent (`lock_state` bleibt als Ergebnis persistent); ein Schreiben von `lock_state` löst einen echten Ver-/Entriegelungszyklus aus statt nur den Merker zu setzen. **[ABW]** Original: ohne Grenze, jederzeit, 465 nur Merker. | E14, F3 |

### 10.2 Laufzeitwerte (schreibbar, nicht persistent)

| Name | Einheit | Bereich | Startwert | Feld (Original) | Bemerkung |
| --- | --- | --- | --- | --- | --- |
| `external_current` | mA | 0, 6000..80000 | `external_current_default` | 528 | Slot `external`; evcc `POST /evse/external_current` |
| `enable_register` | 0/1 | 0..1 | `enable_register_default` | 400 | **[ABW]** Original deutet jeden Wert != 0 als 1; Soll lehnt andere Werte ab (V11) |
| `available_register` | 0/1 | 0..1 | 1 | 402 | |
| `pwm5_request` | 0/1 | 0..1 | 0 | 401 | 5-%-Modus |
| `pause_request` | 0/1 | 0..1 | 0 | 468 | Stromsperre; nicht persistent (F21 entschieden) |
| `lock_request` | 0/1 | 0..1 | 0 | 403 | Verriegelungswunsch (mit D9) |
| `unlock_request` | 0/1 | 0..1 | 0 | 440 | Entriegeln-Anforderung, gelöscht bei CP A |
| `register_output_1..4` | 0/1 | 0..1 | 0 | 405..408 | für Ausgangscodes 30..33 |
| `hardware_limit_override` | mA | 6000..Drehschalterwert (nur absenken) | Drehschalterwert | 801 | SOLL-PAR-40 |
| `dip_override` | Bitmaske D1..D10 | 0..0x3FF, nur in S1 | Schalterstellung | 802 (Original nur D1..D8) | SOLL-PAR-40 |
| `lock_state` | Zustand | 0/1, nur in S1, löst Zyklus aus | gespeicherter Wert | 465 | persistent wie Original; SOLL-PAR-40 |

### 10.3 Nur lesbar (Status)

CP-Status und -Rohzustand, Ausgabe (Tastgrad/DC), CP+/CP-/PP/Versorgung als Messwert,
Kabelwert, wirksamer Strom `I`, `allowed_charging_current`, aktive Slots, Automatenzustand
S0..S10, Freigabe, Verfügbarkeit, Ladeerlaubnisquelle, Eingangs-Rohpegel und
-Funktionszustände, Ausgangszustände, Verriegelungszustand und -zähler, Schütz Soll (Klemme
C1/C2, `contactor`) und Ist (Spannung an Klemme CT, `ct_sense`, Nutzer 2026-10-28),
Fehler (aktiv, Historie), Ladedauer, Laufzeit, Resetgrund, Firmwareversion, DIP- und
Drehschalterstellung (tatsächlich und wirksam).

Prüfung Abschnitt 10: PC – Grenzwerte jedes Parameters (min-1, min, max, max+1),
Ablehnung mit Fehlermeldung, Defaults nach Werksreset gegen diese Tabelle (automatisch aus
einer Tabelle im Quelltext erzeugt). H1 – Persistenz über Neustart.

## 11. Fehler- und Statusmodell, LED-Verhalten

### 11.1 Fehler

Klassen: **P** = Schutzfehler (Schutzpfad, sofort Schütz aus, Ein-Sperre), **V** =
Fahrzeug-/Ablauffehler (Automat; "sofort" = Schütz sofort aus, "geordnet" = über S7),
**K** = Konfigurationsfehler (kein Laden bis Neustart bzw. Korrektur). Bitpositionen nach
HB Tab. 9-2 (S. 72/74) zur Wiedererkennung, 0-basiert.

| Kennung | Bedeutung | Original | Klasse / Reaktion | E-/F-Wirkung | Rücksetzen |
| --- | --- | --- | --- | --- | --- |
| ERR-CABLE-REJECT | Kabel abgewiesen (13 A bzw. 13/20 A) | R107.0/.1 | V geordnet | E immer | anderer Kabelwert |
| ERR-PP-INVALID | PP ungültig bzw. kein Kabel bei Fahrzeug | R107.2 | V **sofort** | E (bei -1 immer, sonst Roh != A) | Bedingung entfällt |
| ERR-CP-INVALID | CP < 1 V (Kurzschluss) | R107.3 | V **sofort** | E sofort | Pegel >= 1,2 V |
| (Status) | F wegen fehlender Verfügbarkeit | R107.4 | – | F | Verfügbarkeit |
| ERR-LOCK | Verriegelung fehlgeschlagen | R107.5 | V | E ab 3. Versuch (mit D9 immer), Roh != A | Erfolg, CP A, Kabelwert 0 |
| ERR-UNLOCK | Entriegelung fehlgeschlagen | R107.6 | V | E immer | Erfolg, CP A |
| ERR-LD-LOST | LD im Laden weggefallen | R107.7 | V **sofort** (F9) | E, Roh != A | Wiederverriegeln erfolgreich, CP A |
| ERR-OVERCURRENT | Überstrom (nur mit Messquelle) | R107.8 | V **sofort** | E, Roh != A | CP A |
| ERR-METER-COMM | Messquelle ausgefallen | R107.9 | V geordnet | E immer | Messquelle wieder gültig |
| ERR-STATE-D | Status D abgewiesen | R107.10 | V **sofort** | E, Roh != A | CP A |
| ERR-CONTACTOR | Schützfehler | R107.11 | **P** | F sofort | F14 |
| ERR-DIODE | Diode fehlt | R107.12 | V **sofort** | F sofort (Ausgabe 3 s -12 V, dann +12 V) | CP A |
| (Sammel) | Fehlerstrom detektiert | R107.14 | P | E, Roh != A | Selbsttest bestanden |
| ERR-INPUT | Error Input (Code 21) | R155.1 | V **sofort** | E immer | Eingang inaktiv |
| ERR-RCM-SELFTEST | RCM-Selbsttest fehlgeschlagen | R155.3 | **P** | E | Selbsttest bestanden |
| ERR-RCM-SENSOR | Sensorfehler (ERR_OUT) | R155.4 | **P** | E | Selbsttest bestanden |
| ERR-RCM-DC6 | SWITCH_2 (6 mA DC / 30 mA AC) | R155.5 | **P** | E | Selbsttest bestanden |
| ERR-RCM-AC30 | SWITCH_1 (30 mA rms) | R155.6 | **P** | E | Selbsttest bestanden |
| ERR-SWITCH | Drehschalter A..F | neu (E11) | K | E | Neustart |
| ERR-CONFIG | Konfiguration widersprüchlich (z. B. D6 ohne LD-Eingang) | neu | K | E | Korrektur und Neustart bzw. Parameterkorrektur |
| WARN-SUPPLY | Versorgung zu niedrig (nur Warnung) | neu (F16) | Warnung, keine Abschaltung | keine | 2500 ms gut |
| ERR-INTERNAL | Plausibilität, Watchdog-Vorwarnung, unzulässiger Aktorbefehl | neu (E3, E5) | **P** | E | Neustart |

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-ERR-01 | Jeder Fehler ist einzeln als aktiv/inaktiv lesbar; zusätzlich eine **Historie** (letzte 32 Ereignisse mit Zeit, Fehler, Zustand, RAM) und ein Sammelwert "seit Start aufgetreten" (Original Fehlerverlauf `0x200138d0`). | Belege §7, [ANN] |
| SOLL-ERR-02 | "Rücksetzen bei CP A" bedeutet: Rohzustand A entprellt (Fahrzeug getrennt). | Ist Z0 |
| SOLL-ERR-03 | Eine Quittierung über Shell/Schnittstelle löscht nur Fehler, deren Ursache nicht mehr besteht; Klasse-P-Fehler nur in S1/S9 ohne Fahrzeug. | E14, [ANN] |
| SOLL-ERR-10 | Fehler, Zustandswechsel, Verriegelungsergebnisse und Schützschaltungen werden als Ereignisse an die Schnittstellen gemeldet (WebSocket, Protokoll). | Arch §8 |
| SOLL-ERR-11 | **Ereignisprotokoll:** Fehlgeschlagene Anmeldungen (falsches Passwort) mit Absenderadresse und alle Ereignisse nach SOLL-ERR-10 werden protokolliert: als Logzeile und zusätzlich in einem Ringpuffer (letzte 48 Einträge, übersteht Neustarts nach SOLL-ERR-12), abrufbar über `GET /system/log` mit derselben Anmeldung wie die übrige API. Nicht protokolliert werden Schreibzugriffe, erfolgreiche Anmeldungen und Shell-Kommandos. | Nutzer 2026-10-20 |
| SOLL-ERR-12 | **Ereignisprotokoll über Neustarts:** Der Ringpuffer nach SOLL-ERR-11 liegt im Backup-SRAM und übersteht jeden Reset (Software, Watchdog, Pin, Update, Werksreset), nicht aber einen Stromausfall ohne Batterie an VBAT; ein nach dem Einschalten unstimmiger Inhalt wird verworfen (keine Meldung). Jeder Start erzeugt das Ereignis `boot` mit Resetgrund und Auslöser (SOLL-RT-14). Jeder Eintrag trägt die Startnummer und, sobald die Uhrzeit bekannt ist, die Uhrzeit. | Nutzer 2026-10-26 |
| SOLL-ERR-13 | **Ereignisprotokoll löschen:** `POST /system/log/clear` mit derselben Anmeldung wie die übrige API (Weboberfläche: Taste mit Rückfrage) verwirft alle Einträge. Erster neuer Eintrag ist `cleared` mit der Absenderadresse, damit das Löschen selbst nachvollziehbar bleibt; die Nummern laufen weiter. | Nutzer 2026-10-27 |
| SOLL-ERR-30 | Status "digitale Kommunikation nicht verfügbar": im 5-%-Modus bei CP C/D gesetzt, kein Fehler, LED Error aus, Meldung an der Schnittstelle. | E6 |

### 11.2 LEDs (HB Tab. 3-3, S. 16)

Blinkformen: 1 Hz (500 ms an/500 ms aus), 2 Hz (250/250 ms), 5 Hz (100/100 ms).

| Kennung | LED | Soll | Quelle |
| --- | --- | --- | --- |
| SOLL-LED-01 | Power (PE15) | blinkt 1 Hz, sobald die Firmware läuft (auch in S0); Reset-Taster beim Start: 1 Hz in den ersten 5 s, 5 Hz von 5 bis 10 s (Loslassen = Notweg zum Bootloader, SOLL-BOOT-22), nach dem Werksreset dauerhaft an bis zum Loslassen | Ist §11, Ist §14 |
| SOLL-LED-02 | Error (PE14) | an bei jedem aktiven Fehler sowie in S9/S10 | Ist §11 |
| SOLL-LED-03 | Connect (PE12) | an bei LOCKED; sonst blinkt 1 Hz bei gültigem Kabelwert und D1; sonst aus | Ist §11 |
| SOLL-LED-04 | Ready (PE13) | Schütz ein: an; sonst PWM an: 2 Hz; sonst Status B/C/D: 1 Hz; sonst aus | Ist §11, HB S. 16 |
| SOLL-LED-05 | Status Modem (PE10/PE11) | aus | Umfang |
| SOLL-LED-06 | PC2 (Testgerät 2026-10-27: grüne LED an der Netzwerkbuchse) | an: Netz verbunden (Link und IP-Adresse); 1 Hz: kein Link oder keine Adresse | [ANN] |

Prüfung Abschnitt 11: PC – Fehlertabelle als Testmatrix (Setzen, Wirkung auf Status und
Schütz, Rücksetzen je Zeile), LED-Ausgabe je Zustand. H1 – LED-Zuordnung auf der
Platine. H2/H3 – Fehler durch EV-Simulator bzw. Prüfstrom auslösen.

## 12. Schnittstellen

### 12.1 HTTP und WebSocket (erster Umfang: evcc über `tinkerforge-warp-ws`)

Grundlage: Arch §8; evcc `charger/warp-ws.go` (master, gelesen 2026-10-19): `Status()`
bildet `iec61851_state` 0/1/2 auf A/B/C ab, Werte >= 3 ergeben einen Fehler;
`Enabled()` = `external_current.current` > 0; `Enable()` sendet den zuletzt gesetzten
Strom bzw. 0; `MaxCurrentMillis()` sendet mA; `/info/features` bestimmt die
Zusatzfunktionen, `/info/name` den Typ (bei `warp3`/`warp4` schaltet evcc
`evse/phase_auto_switch` ab).

| Kennung | Zugriff | Inhalt (Soll) |
| --- | --- | --- |
| SOLL-API-01 | `GET /info/name` | `{"name": <net_hostname>, "type": "warp2", "display_type": "Wallfree", "uid": <aus MAC>}` – Typ `warp2` (F22 entschieden) |
| SOLL-API-02 | `GET /info/features` | `[]` ohne Zähler, `["meters"]` mit konfiguriertem Zähler (SOLL-MTR-30); kein NFC, keine Phasenumschaltung |
| SOLL-API-03 | `GET /evse/state` | `{"iec61851_state": 0..4, "charger_state": 0..4, "contactor_state": .., "contactor_error": .., "allowed_charging_current": mA, "error_state": .., "lock_state": 0..5}` |
| SOLL-API-04 | `GET /evse/external_current` | `{"current": mA}` |
| SOLL-API-05 | `POST /evse/external_current` mit `{"current": mA}` (auch WARP-Kurzform: nur die Zahl) | setzt Slot `external` (nur RAM); 0 = Laden aus (Stromsperre); erlaubt 0 und 6000..80000, sonst HTTP 400 mit Fehlertext (E14) |
| SOLL-API-06 | WebSocket `GET /ws` | Textrahmen mit `{"topic": <name>, "payload": <wie GET>}`; beim Verbinden alle Topics einmal, danach bei jeder Änderung, `evse/state` zusätzlich mindestens alle 2 s als Lebenszeichen (ein stiller WebSocket ist sonst nicht von einer abgerissenen Verbindung zu unterscheiden); Topics `evse/state`, `evse/external_current`, `evse/limits` (SOLL-API-32), `evse/event` (SOLL-ERR-10), mit Zähler zusätzlich `meters/0/value_ids`, `meters/0/values`, `meters/0/state` (SOLL-MTR-30/31) |
| SOLL-API-07 | alle | **Zugriffsschutz von Anfang an (F20):** Weboberfläche und HTTP-/WebSocket-API nur mit Benutzername/Passwort (`web_user`/`web_password`, konfigurierbar). Werkszustand: Default-Zugangsdaten, beim ersten Zugriff muss das Passwort geändert werden; bis dahin beantwortet die API nur die Passwortänderung (sonst HTTP 403). **Verfahren (geprüft 2026-10-19): HTTP-Digest, MD5, `qop=auth`** – evcc `charger/warp/connection.go` nutzt `transport.Digest` (`icholy/digest`, Challenge je Host zwischengespeichert, danach Anmeldung vorab, auch beim WebSocket-Aufbau). Gespeichert wird nur HA1. **Bremse:** je Absenderadresse nach 5 Fehlversuchen Sperre 10 s, je weiterer Fehlversuch verdoppelt bis 5 min, Antwort 429 mit `Retry-After`; ein Erfolg setzt zurück; gesperrt wird auch mit richtigem Passwort, aber nur diese Adresse. Passwortänderung über `POST /credentials` `{"user", "password"}`; `GET /credentials` meldet `must_change` und den aktuellen Benutzer (`user`). Ohne TLS kann Mitlesen im LAN Anfragen wiederholbar machen (Hinweis in der Doku). Mindestens 2 gleichzeitige WebSocket-Clients. |
| SOLL-API-30 | `GET /evse/params`, `POST /evse/params`, `GET /evse/param_info`, `GET/POST /system/config`, `GET /system/config_info`, `POST /system/restart`, `GET /system/info`, `GET /evse/errors`, `GET /system/log` (SOLL-ERR-11) | **Nutzervorgabe 2026-10-19:** Alle Parameter (10.1, 10.2) und alle Mess- und Statuswerte (10.3) sind über HTTP lesbar, schreibbare Werte über HTTP schreibbar, mit denselben Grenzprüfungen und Fehlermeldungen wie die Shell (E14). Umsetzung: `GET /evse/params` = `{"name": Wert, ...}`; `POST /evse/params` mit `{"name": Wert, ...}` prüft und schreibt jeden Wert einzeln (abgelehnte mit Klartext in `{"errors": {...}}`, HTTP 400; bei Syntaxfehler wird nichts geschrieben); persistente Werte werden gespeichert; `GET /evse/param_info` = Name, Art, Einheit, Bereich, Default, Feld des Originals, Hilfetext |
| SOLL-API-31 | Weboberfläche `/`, `/app.js`, `/style.css`, `/vendor.js` | statische, gzip-komprimierte Dateien, die nur die API nutzen; gegliedert in die Bereiche **Übersicht** (Zustand, Ladestrom mit Vorgabe, Zähler, letzte Ereignisse), **Status** (Verlauf SOLL-API-33, Zustand der Verbindungen Netzwerk/Uhrzeit/MQTT/Zaehler, Fehler- und Ereignisprotokoll, wirkende Grenzen, alle Zählerwerte), **Einstellungen** (Unterseiten Laden, Netzwerk, MQTT, Zähler, Zugang; nur Parameter, der Zustand steht unter Status, Nutzer 2026-10-27) und **System** (Firmware, Laufzeit, Resetgrund, Neustart); Navigation als Leiste oben, Bereich in der Adresse (`#/einstellungen/mqtt`, Zurück-Taste und Lesezeichen) (F31). Verbindungsanzeige im Kopf: "verbunden" oder "getrennt" (5 s ohne Daten); getrennt wird der Inhalt grau und ist nicht bedienbar, ohne Zeitangabe zum letzten Stand (Nutzer 2026-10-21). Rahmenwerk Preact + htm mit eigenem CSS (Nutzer 2026-10-19, [Architektur](architektur.md#2-entscheidungen)). Umsetzung: hinter derselben Digest-Anmeldung wie die API (auch im Werkszugang erreichbar, damit die Passwortänderung möglich ist); live über `/ws`, sonst Abfrage alle 2 s und alle 30 s ein neuer WebSocket-Versuch |
| SOLL-API-32 | `GET /evse/limits`, WebSocket-Topic `evse/limits` (eigene Erweiterung, nicht WARP) | Getrennt: Sollwert der Schnittstelle (`external`, darf das Maximum überschreiten, F11), Grenzen (`hardware` = Drehschalter bzw. abgesenkt, `cable`, `input`, 0 = nicht aktiv), wirksamer Strom (`effective`), Strom des Tastgrads (`allowed`) und welche Grenze wirkt (`limited_by`: `external`, `hardware`, `cable`, `input`, `blocked`, `pwm5`). Die Weboberfläche zeigt Vorgabe, Grenzen und erlaubten Strom in einer Ringanzeige und nennt die wirkende Grenze; "Status" zeigt dieselben Werte als Zahlen (Nutzer 2026-10-21, ersetzt die Balkenanzeige). Ein Istwert (gemessener Strom) existiert nur mit Zähler und ist dann reine Anzeige (SOLL-MTR-32). |
| SOLL-API-34 | CP-Aufnahme (eigene Erweiterung, nicht WARP, Nutzer 2026-10-26) | `GET /evse/cp_scope`: Verlauf des CP-Signals über eine PWM-Periode zur Diagnose, auf Anforderung. Äquivalente Abtastung mit einem zweiten ADC (ADC2) synchron zu TIM1: je Periode ein Punkt beim Abwärtszählen, Phase je Periode verschoben, 111 Punkte im Raster 4 us (`t0_us` 970, `dt_us` -4), Kanäle CP+ und CP- wie die Messblöcke skaliert (`pos_mv`, `neg_mv`), dazu `duty`. Die Plateaumitten, in denen ADC1 misst, bleiben ausgespart (zwei Wandler dürfen denselben Pin nicht gleichzeitig abtasten); die Messung der Ladelogik bleibt unverändert. Dauer etwa 120 ms; eine zweite gleichzeitige Anfrage -> 409. Die zentrierte PWM ist symmetrisch, die Weboberfläche spiegelt die gemessene Hälfte. | [ANN], RM0033 ADC |
| SOLL-API-35 | Chiptemperatur der MCU (eigene Erweiterung, Nutzer 2026-10-28) | `GET /system/info` meldet `mcu_temp_dc` (0,1 Grad C) oder `null` vor dem ersten Messblock; Status -> Verbindungen zeigt "Prozessortemperatur" in ganzen Grad mit dem Hinweis "kann um einige Grad abweichen" (Nutzer 2026-10-28). Abgleich (Nutzer 2026-10-28): Systemparameter `mcu_temp_offset` in 0,1 Grad (-450..450, Werk 0, sofort wirksam) wird zu `mcu_temp_dc` addiert; einmal an der kalten Box gegen ein Thermometer eingestellt. Nur die Verschiebung, weil die Steigung kaum streut (RM0033: +-1,5 Grad nach Abgleich bei einem Punkt). Mit Abgleich lautet der Hinweis "abgeglichen um x °C". Zur Kontrolle der Referenz meldet `GET /system/info` `adc_vref_mv`: VREF+ berechnet aus der internen Referenz VREFINT (ADC1_IN17, typisch 1,21 V, +-2,5 %). Rang 10 wechselt dazu je Messblock (8 ms) zwischen IN16 und IN17. Quelle der Temperatur: interner Sensor ADC1_IN16 im ohnehin laufenden Messblock (Rang 10, Abtastzeit 480 Takte = 19,2 us, Datenblatt verlangt >= 10 us), Umrechnung mit typischen Kennwerten (V25 0,76 V, 2,5 mV/K) und VREF+ 2,495 V (Faktor der Originalfirmware für Rang 10, 6,0913e-4 = 2,495 V / 4096; Bauteil der Referenz nicht geprüft, 2,495 V passt zu einem TL431; auf dem Testgerät über VREFINT bestätigt: 2493..2494 mV). RM0033 10.10: Streuung zwischen Chips bis 45 Grad, daher Trend der Temperatur im Gehäuse, kein Messwert für Abschaltungen. | Nutzer 2026-10-28 |
| SOLL-API-33 | Verlauf (eigene Erweiterung, nicht WARP) | `GET /evse/history`: Ringpuffer im RAM mit Mittelwerten je 10 min von Ladeleistung (nur mit Zähler), erlaubtem Strom und Zustand (angesteckt, lädt, Fehler) (Nutzer 2026-10-21), über mehrere Stunden; in "Status" als Kurve; nichts im Flash, nach einem Neustart leer; keine Ladesitzungen (F31). Dauer und Format: [Festlegung](implementierung.md#weboberfläche) |

Abbildungen für `evse/state` (WARP-Semantik nach der
[WARP-API-Referenz](https://docs.warp-charger.com/en/docs/interfaces/mqtt_http/api_reference/evse), gelesen 2026-10-19):

| Feld | Soll-Abbildung |
| --- | --- |
| `iec61851_state` | CP-Status A = 0, B = 1, C = 2, D = 3, E/F = 4 (F23 entschieden; evcc meldet bei 3/4 einen Fehler) |
| `charger_state` | 0 kein Fahrzeug (S0, S1; S8 ohne Fahrzeug); 1 wartet auf Freigabe/Strom (S2, S3; S8 mit Fahrzeug); 2 bereit, PWM an (S4, S5); 3 lädt (S6, S7); 4 Fehler (S9, S10) |
| `allowed_charging_current` | Strom des ausgegebenen Tastgrads (SOLL-CUR-21), 0 ohne PWM (CP +12 V oder -12 V); gilt ebenso für den Statuswert gleichen Namens |
| `lock_state` | 0 INIT, 1 UNLOCKED, 2 LOCKING, 3 LOCKED, 4 UNLOCKING, 5 ERROR |
| `error_state` | WARP: 0 OK, 2 Schalterfehler, 3 DC-Fehlerstromüberwachung, 4 Schützfehler, 5 Kommunikationsfehler. Abbildung: 0 kein Fehler; 3 RCM (Vorrang); 4 ERR-CONTACTOR; 2 ERR-SWITCH/ERR-CONFIG; **1 = sonstiger Fehler** (bei WARP unbelegt, eigene Erweiterung) |
| `contactor_state` | WARP meint Spannung vor/nach dem Schütz; die Steuerung misst das nicht, daher aus dem Schütz-Soll: 3 ein, 0 aus |
| `contactor_error` | 0 ohne ERR-CONTACTOR; sonst 2 (sollte geschlossen sein) bei Soll ein, 6 (sollte offen sein, Spannung danach) bei Soll aus |

Rahmenformat (geprüft 2026-10-19): evcc liest je Rahmen beliebig viele JSON-Objekte
nacheinander; gesendet wird ein Objekt je Rahmen (auch für Browser einfach).

### 12.2 Interne Schnittstelle der Ladelogik

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-API-20 | Alle Zugriffe von außen (HTTP, Shell, später OCPP) laufen über eine gemeinsame Schicht mit Funktionen: Slot schreiben, Freigabe/Verfuegbarkeit/Pause/5 %/Verriegelungs-/Entriegelungswunsch setzen, Parameter lesen/schreiben (mit Grenzprüfung und Fehlercode), Status lesen, Fehler quittieren, Ereignisse abonnieren. | AG, Belege §7 (Zugriffsschicht des Originals als Vorbild) |
| SOLL-API-21 | Die Schicht ist nicht blockierend für die Ladelogik (Nachrichten oder kurze, begrenzte Sperren). | SOLL-RT-07 |

### 12.3 Shell (Weboberfläche; in der Simulation zusätzlich Telnet)

Die Kommandos stehen direkt auf der obersten Ebene, ohne gemeinsamen Vorsatz; `help` listet sie (Nutzer 2026-10-29: der frühere Vorsatz `evse` stammte aus der Zephyr-Shell mit ihren Zusatzkommandos, die es auf der Steuerung nicht mehr gibt, SOLL-SH-21, und musste in der Konsole jedes Mal mitgetippt werden). Neue Kommandos dürfen nicht mit Zephyr-Kommandonamen (`kernel`, `device`, `net`, `log` der Log-Shell u. a.) kollidieren, falls diese in der Simulation zugeschaltet werden.

| Kennung | Kommando | Wirkung |
| --- | --- | --- |
| SOLL-SH-01 | `status` | Zustand, CP, PP, Strom, Fehler, Verriegelung, Schütz |
| SOLL-SH-02 | `param list [Anfang]` / `get <name>` / `set <name> <wert>` | Parameter; `set` mit Grenzprüfung, Fehlermeldung bei Ablehnung |
| SOLL-SH-03 | `current <mA>` | Slot `external` |
| SOLL-SH-04 | `enable 0|1`, `avail 0|1`, `pause 0|1`, `pwm5 0|1` | Laufzeitwerte (10.2) |
| SOLL-SH-05 | `lock 0|1`, `unlock` | Verriegelungswunsch, Entriegeln-Anforderung |
| SOLL-SH-06 | `errors` / `errors clear` | aktive Fehler mit Rücksetzbedingung, Historie, Quittierung (SOLL-ERR-03; quittierbar ist nur ERR-CONTACTOR, alle anderen löschen sich nach Tabelle 11.1 selbst). HTTP: `GET /evse/errors`, `POST /evse/errors/ack` |
| SOLL-SH-07 | `selftest` | RCM-Selbsttest, nur in S1 bei CP A |
| SOLL-SH-08 | `factory-reset` | Werkseinstellungen, nur im sicheren Zustand, Bestätigung erforderlich |
| SOLL-SH-09 | `import-original` | einmalige Übernahme aus Sektor 3 (SOLL-NVM-10), nur im sicheren Zustand |
| SOLL-SH-10 | `bootloader` | Update-Übergabe (SOLL-BOOT-30) |
| SOLL-SH-11 | `reboot` | Neustart nach SOLL-RT-22 (HTTP: `POST /system/restart`). Zephyrs `kernel reboot` ist entfernt (`CONFIG_REBOOT=n`), weil es die Wartezeit umgehen würde (SOLL-SH-20) |
| SOLL-SH-19 | Konsole in der Weboberfläche: `POST /system/shell`, Körper = eine Kommandozeile (Text, höchstens 255 Zeichen), Anmeldung und Bremse wie die übrige API (SOLL-API-07), im Werkszugang 403. Antwort `text/plain` mit der Ausgabe des Kommandos, höchstens 2 KB; längere Ausgabe wird gekürzt und endet mit einem Hinweis. Ein Kommando je Anfrage, kein Live-Log, keine Vervollständigung. Die Weboberfläche zeigt die Konsole unter System nur in der Expertenansicht. **Kein Telnet auf der Steuerung** (Nutzer 2026-10-28): kein offener Port ohne Anmeldung; in der PC-Simulation bleibt Telnet als Testzugang (Kommando `sim`). Der frühere Parameter `telnet_enable` (Kennung 3015) entfällt, ein gespeicherter Wert wird ignoriert. |
| SOLL-SH-20 | Shell-Kommandos sind gegenüber dem Schutzpfad nicht privilegiert: kein Kommando kann Schutzfunktionen abschalten, das Schütz direkt einschalten oder Aktorimpulse außerhalb der Ablaufregeln auslösen. | AG Sicherheitsgrenzen |
| SOLL-SH-21 | Von Zephyrs Zusatzkommandos bleibt keines: `kernel` enthält `thread suspend/resume/kill` (könnte Schutz- und Automatenthread anhalten, SOLL-SH-20) und ist nicht einzeln abschaltbar, `devmem` schreibt beliebige Register (SOLL-SH-20), `device` und `date` werden nicht gebraucht. Ersatz für die Diagnose: `threads` (Threads mit Priorität und Stack-Auslastung, nur lesend) und `uptime`. | AG Sicherheitsgrenzen, Nutzer 2026-10-28 |
| SOLL-SH-22 | `log [clear]`: die jüngsten Logmeldungen seit dem Start (Ring im RAM, etwa 2 KB, geht beim Neustart verloren), `clear` leert ihn. Ersetzt das Live-Log der Telnet-Shell; der Ring ist zugleich das einzige Log-Backend der Steuerung (Zephyr verlangt mindestens eines). | Nutzer 2026-10-28 |
| SOLL-SH-23 | Vervollständigung in der Konsole: `GET /system/shell_commands` liefert den Kommandobaum mit Kurzhilfe (`[["status",...],["param get",...],...]`, Anmeldung wie die API). Die Weboberfläche ergänzt mit Tab das letzte Wort (eindeutig ganz, sonst bis zum gemeinsamen Anfang) und zeigt die passenden Vorschläge zum Antippen; nach `param get/set/list` und `sys set` die Parameternamen aus den Parameterbeschreibungen. | Nutzer 2026-10-29 (wie die CLI von Betaflight) |
| SOLL-SH-24 | `flashread <adresse> <länge>`: liest Flash, OTP oder Optionsbytes **nur lesend** und gibt die Bytes als Base64 aus (Zeilen zu 76 Zeichen), höchstens 1024 Byte je Aufruf. Erlaubt: Sektor 0..6 (`0x08000000`..`0x0805FFFF`: Bootloader, Parameter der Originalfirmware, Firmware), OTP mit Sperrbytes (`0x1FFF7800`..`0x1FFF7A0F`), Optionsbytes (`0x1FFFC000`..`0x1FFFC00F`). Sektor 7 (Parameter von Wallfree mit den Zugangsdaten) und alles andere wird abgewiesen. `scripts/flash-read.py` setzt größere Bereiche über `POST /system/shell` zusammen. Zweck: Bootloader und Parameter anderer Boxen ohne SWD untersuchen (Bootloader 1.03 des zweiten Geräts). Abzüge mit Herstellercode gehören nach wallbe2, nicht ins Repository. | Nutzer 2026-11-08 |

### 12.4 Update über den Bootloader

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-BOOT-30 | Update-Anforderung wie Original: nur im sicheren Zustand (Schütz aus, kein Ladevorgang; Höchstwartezeit 15 s nach SOLL-RT-22), dann RTC-Backup-Register entsperren, **BKP10 = 2, BKP11 = 3, BKP15 = 0, BKP13 = aktuelle IP-Adresse, BKP14 = aktuelle Netzmaske**, Interrupts sperren, Systemreset (AIRCR `0x05FA0004`). | [Bootloader](../../wallbe2/docs/bootloader.md), 1.27 §3.9 |
| SOLL-BOOT-31 | Bedeutung (Bootloader das Testgerät ausgelesen und analysiert): BKP10 = 2 wählt den Web-Update-Betrieb, BKP11 = Zahl der Versuche (je höchstens 120 s Warten auf den Upload, dann 30 s für die Übertragung), BKP13/BKP14 = IP und Maske in lwIP-Byteordnung (erstes Oktett im niederwertigsten Byte); sind sie 0, gilt 192.168.0.8/24. Der Bootloader nimmt ein Rohabbild per Web-Formular an, prüft den Inhalt nicht, löscht nur Sektor 4..6 (Parameter in Sektor 3 und 7 bleiben) und meldet das Ergebnis in BKP12. Wallfree schreibt BKP13 = 0, wenn es keine IPv4-Adresse hat. | [Bootloader](../../wallbe2/docs/bootloader.md#update-betrieb-bootloader-box-t-firmware-081) |
| SOLL-BOOT-32 | Auslösung über die Shell (`bootloader`, SOLL-SH-10) und über die Weboberfläche, dort über die API (`POST /system/update`). **Weboberfläche (Nutzer 2026-10-24):** Im Bereich System wählt der Nutzer zuerst die Datei; die Seite prüft sie vorab (höchstens 320 KB = Sektor 4..6, so groß ist auch die Sicherung der Originalfirmware; Start-SP im SRAM, Reset-Vektor in der Anwendung; Wallfree-Abbild -> Version anzeigen, fremdes Abbild wie die Originalfirmware nur nach ausdrücklicher Rückfrage). Während des Updates stellt die Seite keine anderen Anfragen (der Bootloader bricht den Upload bei jeder anderen Anfrage ab) und weist vorher darauf hin, andere Abfragen (evcc) anzuhalten. Nach Bestätigung läuft alles selbsttätig: Übergabe, Warten auf den Bootloader unter derselben Adresse, Upload der Datei an ihn, Warten auf den Neustart, Anzeige des Ergebnisses (SOLL-BOOT-33) und der neuen Version; dazu eine Fortschrittsanzeige. Die Shell löst nur die Übergabe aus; den Upload macht dann der Nutzer auf der Seite des Bootloaders oder mit `scripts/update.sh` (Notweg). | Nutzer 2026-10-24, Original `config.html` |
| SOLL-BOOT-33 | Beim Start liest Wallfree BKP12 (Ergebnis eines Updates: Modus im oberen Byte, Bit 0 = Erfolg), meldet es in `GET /system/info` (`update_result`) und im Ereignisprotokoll und setzt BKP12 auf 0. | Nutzer 2026-10-24 |
| SOLL-BOOT-34 | Das Abbild darf höchstens 320 KB groß sein (Sektor 4..6; mehr löscht der Bootloader nicht und schriebe in den ungelöschten Sektor 7 mit den Parametern); erfüllt durch die Anwendungspartition von 320 KB (Linker). | Bootloader |
| SOLL-BOOT-35 | **Prüfung des eigenen Abbilds (Nutzer 2026-10-26):** Der Build hängt an das Abbild Länge und CRC32 (IEEE) an. Beim Start rechnet Wallfree die CRC über das Abbild nach. Fehlt der Anhang oder passt die CRC nicht (z. B. abgeschnittenes Abbild nach einem Update), gilt ERR-INTERNAL (Klasse P, kein Laden) bis zum nächsten Start mit gültigem Abbild; Netz und Update bleiben möglich. `GET /system/info` meldet CRC und Ergebnis (`image`). Die Weboberfläche prüft den Anhang der Datei vor dem Update und wertet ein Update nur als erfolgreich, wenn die CRC der laufenden Firmware der Datei entspricht. Grund: Weder Bootloader noch Originalfirmware prüfen das Abbild, und der Bootloader kann ein abgeschnittenes Abbild als Erfolg melden. | [Bootloader](../../wallbe2/docs/bootloader.md#update-betrieb-bootloader-box-t-firmware-081) |

Prüfung Abschnitt 12: PC/native_sim – HTTP- und WebSocket-Antworten gegen Beispiel-JSON,
Ablehnung ungültiger Werte, Shell-Kommandos inkl. Ablehnungen; evcc gegen die Simulation
(erledigt, [Test und Simulation](test-und-simulation.md#evcc-gegen-die-simulation)). H1 – Netzbetrieb
auf der Steuerung (ohne Last); Update-Übergabe auf dem Testgerät mit Rückweg über denselben Weg.

### 12.5 Energiezähler (Modbus RTU, nur Anzeige)

Grundlage: F28, F29; Registerkarten der Hersteller (ABB "EQ-Energiezähler B-Serie",
Kapitel Modbus; Eastron "SDM630 Modbus Protocol" V1.8 und "SDM72D-M Modbus Protocol" V1.1),
gegengeprüft mit evcc (Vorlagen `abb-ab`, `eastron-sdm72v2_630`, `eastron-sdm72`) und mbmd
(`meters/rs485/abb.go`), gelesen 2026-10-21. Registeradressen sind die Adressen auf dem Bus
(Eastron-Handbücher nennen sie zusätzlich als 3xxxx-Nummern, um 30001 verschoben).

**Grundsatz und Abgrenzung**

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-MTR-01 | Der Zähler ist optional. `meter_type` = 0 ("kein Zähler") ist der Werkszustand; dann wird nichts gesendet (PC12 bleibt Low, keine Abfrage), und die Steuerung verhält sich in allem wie ohne Zähleranbindung. | Nutzer 2026-10-21 |
| SOLL-MTR-02 | Schnittstelle: UART4 (PC10/PC11) mit Treiberfreigabe PC12 (auf dem Testgerät bestätigt), Modbus-RTU-Client. Adresse, Baudrate und Rahmenformat sind Parameter (10.1); die Defaults entsprechen den Werkseinstellungen der Eastron-Zähler (Adresse 1, 9600 Bd, 8N1). 8N1 ist nach der Modbus-Norm nicht konform und muss trotzdem möglich sein. | Herstellerunterlagen, wallbe2 Belege §9 |
| SOLL-MTR-03 | **Nur Anzeige:** Zählerwerte fließen nicht in Ladeautomat, Stromvorgabe, Schutzpfad, Fehlermodell oder LEDs ein. Ein Zählerausfall ist kein Fehler der Ladelogik (keine Fehlerklasse, keine Fehler-LED, kein Einfluss auf das Laden); er erscheint nur im Zählerstatus und als Ereignis. Der Zählerthread ist nicht Teil der Thread-Überwachung (SOLL-RT), damit ein Hängen keinen Neustart auslöst. Die Messquelle der Ladelogik (SOLL-PROT-30) bleibt unbelegt; `contactor_monitor` = 4 und `overcurrent_enable` = 1 bleiben abgelehnt. | F28 |
| SOLL-MTR-04 | Der Zugriff auf den Bus ist hinter einer schmalen Schnittstelle gekapselt (Register lesen: Adresse, Funktion 03/04, Startregister, Anzahl -> Daten oder Fehlerart), damit die Modbus-Implementierung ohne Änderung der Zählerlogik ausgetauscht werden kann. Erste Umsetzung: Modbus-Client von Zephyr (F29). | Nutzer 2026-10-21 |

**Modelle und Register**

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-MTR-05 | Ein Modell ist eine Tabelle aus Registerblöcken (Funktion, Startregister, Anzahl) und Werten (Messgröße, Block, Position, Datentyp, Zehnerpotenz). Datentypen: Float32, U16, U32, S32, U64, jeweils höchstwertiges Wort zuerst. Ein neues Modell erfordert nur einen neuen Tabelleneintrag; Code nur für einen neuen Datentyp. Nicht gelieferte Messgrößen sind "nicht vorhanden". | Erweiterbarkeit (Nutzer) |
| SOLL-MTR-06 | Unterstützte Modelle und ihre Blöcke siehe Tabelle unten. Je Abfragezyklus werden alle Blöcke eines Modells gelesen. Höchstzahl je Anfrage nach Hersteller: ABB 125 Register, SDM630 40 Werte (80 Register), SDM72D-M V1 30 Werte; die Blöcke bleiben darunter. | Herstellerunterlagen |

| Messgröße | ABB B23 (Funktion 03) | Eastron SDM630 / SDM72D-M V2 (Funktion 04, Float32) | SDM72D-M V1 (04, Float32) |
| --- | --- | --- | --- |
| Blöcke | `0x5B00` x 28, `0x5000` x 8, `0x5B2C` x 1 | `0x0000` x 18, `0x0034` x 24 | `0x0034` x 24 |
| Spannung L1/L2/L3-N | `0x5B00/02/04`, U32, 0,1 V | `0x0000/02/04`, V | – |
| Strom L1/L2/L3 | `0x5B0C/0E/10`, U32, 0,01 A | `0x0006/08/0A`, A | – |
| Wirkleistung L1/L2/L3 | `0x5B16/18/1A`, S32, 0,01 W | `0x000C/0E/10`, W | – |
| Wirkleistung gesamt | `0x5B14`, S32, 0,01 W | `0x0034`, W | `0x0034`, W |
| Frequenz | `0x5B2C`, U16, 0,01 Hz | `0x0046`, Hz | – |
| Energie Bezug | `0x5000`, U64, 0,01 kWh | `0x0048`, kWh | `0x0048`, kWh |
| Energie Einspeisung | `0x5004`, U64, 0,01 kWh | `0x004A`, kWh | `0x004A`, kWh |
| Ungültig-Kennung | alle Bytes `FF` (vorzeichenlos) bzw. Höchstwert `7F..FF` (vorzeichenbehaftet) | NaN | NaN |

Das SDM72D-M gibt es unter gleichem Namen in zwei Versionen; nur V2 liefert Werte je Phase.
Ein V1-Zähler mit `meter_type` = 3 antwortet auf den Block `0x0000` mit einer Ausnahme oder
Nullen; der Zählerstatus zeigt das (Hinweis in der Hilfe zu `meter_type`).

**Abfrage und Robustheit**

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-MTR-10 | Abfragezyklus 1 s (Beginn zu Beginn). Vor jeder Anfrage eine Ruhepause von mindestens 50 ms auf dem Bus; Antwortfrist je Anfrage 500 ms. | Nutzer 2026-10-21 |
| SOLL-MTR-11 | Fehlschläge werden nach Art unterschieden: keine Antwort (Timeout), gestörter Rahmen (CRC, Länge, Adresse/Funktion passt nicht), Ausnahmeantwort (mit Code), unplausibler Wert (SOLL-MTR-13). Ein Block wird bis zu 3-mal versucht. Nach einem Timeout wird vor der nächsten Anfrage zusätzlich die Antwortfrist abgewartet, damit eine verspätete Antwort nicht der nächsten Anfrage zugeordnet wird. | F29 (Grenzen des Zephyr-Clients, [Architektur](architektur.md#5-speicherbudget)) |
| SOLL-MTR-12 | Ein Messwertsatz gilt nur, wenn alle Blöcke desselben Zyklus erfolgreich gelesen und plausibel sind; sonst bleibt der letzte gültige Satz mit seinem Alter stehen. Liegt der letzte gültige Satz mehr als 10 s zurück, sind alle Werte ungültig (`null`). | [ANN] |
| SOLL-MTR-13 | Plausibilität je Wert: Ungültig-Kennung (Tabelle), NaN/unendlich, Spannung 0..300 V, Strom 0..100 A, Leistung je Phase +-25 kW und gesamt +-75 kW, Frequenz 45..65 Hz, Energie 0..10^8 kWh (Nutzer 2026-10-21). Sinkt der Energiezählerstand gegenüber dem letzten gültigen Satz, wird der Wert übernommen und ein Ereignis gemeldet (Zählertausch oder Fehler sichtbar, keine Blockade). | [ANN] |
| SOLL-MTR-14 | Zustand des Zählers: `off` (kein Zähler), `connecting` (seit Start noch kein gültiger Satz), `ok`, `error` (3 Zyklen in Folge ohne gültigen Satz). Im Zustand `error` wird nur noch alle 10 s abgefragt, bis wieder ein Satz gelingt. Jeder Zustandswechsel ist ein Ereignis (SOLL-ERR-10). | [ANN] |
| SOLL-MTR-15 | **Fehlerzähler** seit dem Start, über HTTP, WebSocket und Shell lesbar: Anfragen, erfolgreiche Antworten, Timeouts, gestörte Rahmen, Ausnahmeantworten (dazu der letzte Ausnahmecode), unplausible Werte, gültige Sätze; dazu Alter des letzten gültigen Satzes. Sie dienen der Fehlersuche am Bus und der Entscheidung, ob die Modbus-Implementierung getauscht werden muss (SOLL-MTR-04). | Nutzer 2026-10-21 |

**Schnittstellen**

| Kennung | Zugriff | Inhalt (Soll) |
| --- | --- | --- |
| SOLL-MTR-30 | `GET /meters/0/value_ids`, `GET /meters/0/values`, WebSocket-Topics gleichen Namens (WARP-API `meters`) | Nur mit Zähler vorhanden (sonst HTTP 404), `/info/features` enthält dann `meters` (SOLL-API-02). `value_ids` listet die Kennungen der WARP-Messwertliste für die Messgrößen des Modells: 1/2/3 Spannung L1..L3-N, 13/17/21 Strom L1..L3, 39/48/57 Wirkleistung L1..L3, 74 Wirkleistung gesamt, 364 Frequenz, 209 Energie Bezug, 211 Energie Einspeisung. `values` enthält die Werte in derselben Reihenfolge in V, A, W, Hz, kWh; ungültige Werte als `null`. Leistung: Bezug positiv. `values` wird über den WebSocket nach jedem Zyklus gesendet, auch wenn sich nichts ändert. evcc (`warp-ws.go`) nimmt daraus Ladeleistung (74), Zählerstand (209), Phasenströme und -spannungen und liest die Fähigkeiten nur beim Verbinden; nach Änderung von `meter_type` evcc neu starten. |
| SOLL-MTR-31 | `GET /meters/0/state`, WebSocket-Topic gleichen Namens (an WARP angelehnt, Felder ab `type` eigene Erweiterung) | `{"state": 0 kein Zaehler / 1 nicht verbunden (connecting, error) / 2 verbunden (ok), "type": meter_type, "status": "off"|"connecting"|"ok"|"error", "age_ms", ` Fehlerzähler nach SOLL-MTR-15 `}` |
| SOLL-MTR-32 | Weboberfläche | Mit Zähler: "Übersicht" zeigt Ladeleistung, Zählerstand und Zustand; "Status" alle Messwerte und Fehlerzähler, "Einstellungen" die Zählerparameter, "Status → Verbindungen" den Zustand. Der gemessene Strom (höchste Phase) erscheint in der Ladestrom-Anzeige (SOLL-API-32) als Messwert neben Vorgabe und Grenzen. Ungültige oder veraltete Werte werden als solche angezeigt, nicht als 0. Ohne Zähler bleibt nur die Kachel "Zähler" der Übersicht leer stehen (Nutzer 2026-10-21), sonst keine Zählerelemente außer der Auswahl `meter_type`. |
| SOLL-MTR-33 | Shell `meter` | Zustand, Messwerte, Fehlerzähler |

Prüfung 12.5: PC – Dekodierung jedes Datentyps und jeder Ungültig-Kennung, Modelltabellen
gegen Beispielantworten aus den Herstellerunterlagen, Plausibilitätsgrenzen, Satzbildung und
Zustandsfolge mit simulierter Registerschnittstelle (Timeout, gestörter Rahmen, Ausnahme,
verspätete Antwort, Teilerfolg im Zyklus), Ausnahme `meter_type` = 0 (keine Abfrage, keine
Topics, `features` leer). native_sim – Zählermodell hinter derselben Schnittstelle, API und
evcc gegen die Simulation. H1 – Abfrage eines echten
Zählers, Fehlerzähler bei offener und vertauschter Leitung.

### 12.6 MQTT

Grundlage: F30; MQTT-Modul der WARP (Tinkerforge `esp32-firmware`, `modules/mqtt`, gelesen
2026-10-21) und [WARP-API-Referenz](https://docs.warp-charger.com/en/docs/interfaces/mqtt_http/api_reference/evse).
Die WARP behandelt MQTT als weitere Schnittstelle der API: Zustände unter
`<prefix>/<pfad>` mit demselben JSON wie HTTP, Befehle auf Befehlstopics. Daran hält sich
Wallfree, im Umfang wie der WebSocket (SOLL-API-06) und mit einem ersten Befehl.

**Grundsatz und Verbindung**

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-MQTT-01 | MQTT ist optional, Werkszustand aus (`mqtt_enable` = 0): kein Socket, keine Verbindung. Ein fehlender oder gestörter Broker ist kein Fehler der Ladelogik (keine Fehlerklasse, keine Fehler-LED, kein Einfluss auf das Laden); er erscheint nur im MQTT-Status (SOLL-MQTT-30). Der MQTT-Thread ist nicht Teil der Thread-Überwachung (SOLL-RT-11), damit ein hängender Broker keinen Neustart auslöst; er läuft mit niedrigster Priorität und hält die Sperre der Ladelogik nur zum Erzeugen der Nutzdaten. | F30 |
| SOLL-MQTT-02 | MQTT 3.1.1 über TCP, **ohne TLS** (1.4). Broker als IPv4-Adresse und Port (kein DNS). Benutzer und Passwort optional; das Passwort liegt im Klartext im Parameterspeicher (es muss beim Verbinden gesendet werden) und ist über HTTP und Shell nicht lesbar (Ausgabe `null`). Client-ID = Hostname (`net_hostname`). Keepalive 60 s, Clean Session, QoS 0 für alles. | F30 |
| SOLL-MQTT-03 | Nach einem Verbindungsabbruch oder Fehlversuch wird neu verbunden; Wartezeit 1 s, je weiterem Fehlversuch verdoppelt bis 60 s, nach einer erfolgreichen Verbindung wieder 1 s. Antwortfrist für die Verbindungsbestätigung (CONNACK) 10 s. Bleibt ein PINGREQ eine Keepalive-Spanne lang unbeantwortet, gilt die Verbindung als getrennt. | [ANN] |
| SOLL-MQTT-04 | **Erreichbarkeit** (eigene Erweiterung, nicht WARP): Last Will `<prefix>/online` = `false` (retained); nach jeder Verbindungsbestätigung `<prefix>/online` = `true` (retained). Fällt die Steuerung aus oder startet neu, meldet der Broker `false` spätestens nach 1,5 x Keepalive. | F30 |

**Zustände**

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-MQTT-10 | Topic = `<prefix>/<name>`, Nutzdaten wie `GET /<name>` bzw. das WebSocket-Payload (SOLL-API-06). Topics: `evse/state`, `evse/external_current`, `evse/limits`, mit Zähler zusätzlich `meters/0/value_ids`, `meters/0/values`, `meters/0/state`; diese retained. `evse/event` (SOLL-ERR-10) **nicht** retained. | WARP `mqtt.cpp`, F30 |
| SOLL-MQTT-11 | Nach jeder Verbindungsbestätigung werden alle Zustandstopics einmal gesendet. Danach `evse/*` bei Änderung, `meters/0/values` und `meters/0/state` nach jedem Zählerzyklus, `meters/0/value_ids` nur beim Verbinden. Je Topic höchstens ein Versand je `mqtt_interval` Sekunden; eine dazwischen liegende Änderung wird mit dem dann aktuellen Stand nachgeholt (wie WARP `interval`). Ereignisse werden sofort und einzeln gesendet; während einer Unterbrechung entstandene Ereignisse werden nicht nachgesendet (Lücke in `seq` sichtbar). | WARP `mqtt.cpp` |
| SOLL-MQTT-12 | `mqtt_prefix`: leer = `wallfree/<MAC-Ende>` (6 Hexziffern wie `uid` in `/info/name`). Erlaubt sind druckbare ASCII-Zeichen ohne Leerzeichen, `+` und `#`; nicht mit `/` oder `$` beginnend, nicht mit `/` endend. | MQTT 3.1.1 (Platzhalter, `$`-Topics) |

**Befehle**

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-MQTT-20 | Befehle nur mit `mqtt_commands` = 1 (Werkszustand 0). Abonniert werden nur die Befehlstopics (nicht `<prefix>/#`). Befehle stehen in einer Tabelle (Topic-Name, Behandlung); weitere Befehle erfordern nur einen Eintrag. Jeder Befehl läuft über die gemeinsame Zugriffsschicht (SOLL-API-20) mit denselben Prüfungen und Fehlertexten wie der HTTP-Zugang. | F30 |
| SOLL-MQTT-21 | Erster Befehl: `<prefix>/evse/external_current_update` (Name wie WARP), Nutzdaten `{"current": mA}` oder nur die Zahl, Wirkung wie SOLL-API-05 (Slot `external`, nur RAM). | WARP-API-Referenz |
| SOLL-MQTT-22 | Ignoriert (mit Zähler und Text im MQTT-Status und im Log, ohne Antworttopic) werden: Nachrichten mit Retain-Kennung, leere Nachrichten, Nachrichten über 128 Byte; abgelehnt werden ungültige Werte. **[ABW]** WARP nimmt für `external_current_update` auch gespeicherte (retained) Nachrichten an. Grund: Ein auf dem Broker gespeicherter Sollwert würde nach jedem Neustart unbemerkt den Startwert `external_current_default` (F12) ersetzen. | F30 |
| SOLL-MQTT-23 | **Abgrenzung:** Über MQTT sind nur Laufzeitwerte schreibbar, die nicht gespeichert werden (10.2); Konfiguration, persistente Parameter, Zugangsdaten, Neustart und Werksreset bleiben HTTP und Shell vorbehalten. Erweiterungen nur nach Absprache. Grund: Der Broker hat im Regelfall keine oder eine schwächere Anmeldung als die API (SOLL-API-07); jeder mit Schreibrecht am Broker kann die freigegebenen Befehle ausführen. | F30 |

**Status und Bedienung**

| Kennung | Zugriff | Inhalt (Soll) |
| --- | --- | --- |
| SOLL-MQTT-30 | `GET /mqtt/state` (an WARP `mqtt/state` angelehnt, Felder ab `published` eigene Erweiterung) | `{"connection_state": 0 aus / 1 nicht verbunden / 2 verbunden / 3 Fehler (Broker lehnt ab), "connection_start", "connection_end"` (Laufzeit in ms, 0 = nie)`, "last_error"` (0, negativer Fehlercode des Netzes oder CONNACK-Code)`, "published", "publish_errors", "commands", "rejected", "last_reject"` (Text)`, "prefix"` (wirksam)`}` |
| SOLL-MQTT-31 | Parameter | Änderungen an `mqtt_*` wirken sofort: bestehende Verbindung trennen, mit den neuen Werten neu verbinden (abweichend von den Netzparametern, die einen Neustart brauchen). |
| SOLL-MQTT-32 | Weboberfläche, Shell `mqtt` | Weboberfläche: Parameter unter Einstellungen/MQTT, Verbindungszustand unter Status → Verbindungen. Shell: Zustand und Zähler. |
| SOLL-MQTT-40 | Home-Assistant-Erkennung | Mit `mqtt_discovery` (Werk an) sendet die Steuerung nach jedem Verbinden retained Konfigurationen unter `homeassistant/<komponente>/wallfree_<MAC-Ende>/<objekt>/config` (MQTT Discovery). Werte kommen aus den vorhandenen Topics (SOLL-MQTT-10) über Wertvorlagen; keine zusätzlichen Zustandsnachrichten. Erreichbarkeit über `<prefix>/online`. |
| SOLL-MQTT-41 | Elemente | Sensoren Zustand (Text), erlaubter Strom und Vorgabe (A); Binärsensoren Fahrzeug angeschlossen, lädt, Fehler; mit `mqtt_commands` das Zahlenfeld Ladestrom (0 bis Maximum des Drehschalters, sendet mA an `evse/external_current_update`); mit Zähler Leistung (W) und Energie (kWh). Entfallende Elemente und alle bei ausgeschaltetem `mqtt_discovery` werden mit einer leeren retained Nachricht entfernt. Gerät: Name "Wallfree <MAC-Ende>", Hersteller Wallfree, Modell der Steuerung, Firmwarestand. |
| SOLL-MQTT-42 | Keine Abhängigkeit | Home Assistant braucht nur die eingebaute MQTT-Integration und einen Broker; ohne Home Assistant sind die Konfigurationen wirkungslos. |

Prüfung 12.6: PC – Prüfung und Kodierung der Parameter, Topicbildung und Präfixregeln,
Versandregel (Änderung, Intervall, Nachholen, Vollversand nach Verbinden), Befehlstabelle
(Zuordnung, Retain, leer, zu lang, ungültiger Wert). native_sim – Ende-zu-Ende gegen einen
Mosquitto-Broker (Zustände, Last Will, Befehl, Neuverbindung nach Broker-Neustart). H1 –
Betrieb auf der Steuerung im LAN.

### 12.7 Uhrzeit (SNTP)

Nutzer 2026-10-26 (ersetzt F32 "keine Uhrzeit"). Die Uhrzeit dient nur der Anzeige
(Protokoll, Verlauf, System); die Ladelogik benutzt sie nie.

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-TIME-01 | Mit `ntp_enable` (Werkszustand an) holt die Steuerung die Uhrzeit per SNTP. Ohne Zeitserver und ohne gültige RTC (SOLL-TIME-04), bei abgeschaltetem `ntp_enable` oder vor dem ersten Abgleich ohne gültige RTC ist die Uhrzeit unbekannt; alle Funktionen arbeiten dann wie ohne Uhrzeit (Zeiten relativ zum Start, Umrechnung im Browser). Fehler des Zeitservers sind kein Fehler der Ladelogik. | Nutzer 2026-10-26 |
| SOLL-TIME-02 | Server: `ntp_server` (Name oder IPv4, optional `:Port`); leer = Zeitserver aus DHCP (Option 42), sonst `pool.ntp.org`. Namen werden per DNS aufgelöst (DHCP-DNS, bei fester Adresse das Gateway). Änderungen wirken sofort. | [ANN] |
| SOLL-TIME-03 | Abgleich: nach dem Start und bei Änderung; nach Erfolg alle 6 h, nach Fehlschlag nach 60 s, verdoppelt bis 1 h. Antworten außerhalb 2025..2099 werden verworfen. `GET /system/info` meldet `time` = `{"time_ms", "synced_ms", "server"}` oder `null`; `synced_ms` ist `null`, solange die Zeit aus der RTC stammt (SOLL-TIME-04). | [ANN] |
| SOLL-TIME-04 | Keine Pufferbatterie: Die Uhrzeit übersteht Neustarts, solange die Versorgung anliegt, aber keinen Spannungsausfall. Jeder erfolgreiche Abgleich stellt die RTC des STM32 (UTC, Auflösung 1 s). Beim Start mit `ntp_enable` gilt eine gültige RTC bis zum ersten Abgleich als Uhrzeit; gültig heißt: Kalender seit dem letzten Spannungsausfall gestellt, Takt läuft, Zeit in 2025..2099. `/system/info` meldet dann `server` leer und `synced_ms` null. | Nutzer 2026-10-30; Hardware (externer 32-kHz-Takt an PC14, keine Batterie an VBAT bekannt) |

## 13. Parameterspeicher, Startablauf, Werksreset

### 13.1 Parameterspeicher (P1)

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-NVM-01 | Eigene Parameter liegen ausschließlich in **Sektor 7** (`0x08060000..0x0807FFFF`, 128 KB). Sektoren 0..3 werden nie beschrieben oder gelöscht (Treiber-/Partitionssperre). | Arch §3 (P1) |
| SOLL-NVM-02 | Format: Anhängen von Einträgen (Schlüssel, Länge, Wert, Prüfsumme); der jüngste gültige Eintrag gilt; Formatkennung und Version am Bereichsanfang. Ungültige Einträge werden ignoriert. **Geprüft 2026-10-19:** Zephyr ZMS scheidet aus (verlangt mindestens zwei Löscheinheiten, `zms.c` Zeile 1525, und löscht selbstständig beim Start und bei der Speicherbereinigung, unvereinbar mit SOLL-NVM-03/04) -> eigenes Log-Format, beschrieben in `include/wallfree/nvm/nvm.h`. | Arch §3, [ANN] |
| SOLL-NVM-03 | Persistente Schreibzugriffe im Betrieb erzeugen nur Anhängevorgänge (kein Löschen). Laufzeitwerte (10.2), insbesondere `external_current`, werden nie gespeichert. | Arch §8 |
| SOLL-NVM-04 | Verdichten (RAM-Kopie, Sektor löschen, neu schreiben) nur im sicheren Zustand: S1 bzw. S0, CP A, Schütz aus, IWDG verlängert (SOLL-RT-13). Ist der Bereich im Betrieb voll, bleiben neue Werte bis zum nächsten sicheren Zustand nur im RAM (Status "Speichern ausstehend"). | Arch §3, SOLL-RT-13 |
| SOLL-NVM-05 | Stromausfall während des Verdichtens: Beim nächsten Start ist der Bereich ungültig -> Werkseinstellungen und Meldung (wie Original); einmalige Übernahme nach SOLL-NVM-10 erneut möglich. | Parameterspeicher (Ausfallverhalten) |
| SOLL-NVM-10 | Sektor 3 (Originalformat: Kopf `0x0000`, Einträge Wert/Kennung, jüngster gilt) wird nur gelesen. Auf Kommando werden die Felder 327..330, 390, 409, 412, 520..527 und die Netzwerkwerte übernommen (519 nicht, F33), jeweils mit Grenzprüfung; nicht übernehmbare Werte werden gemeldet. Ein fehlender Eintrag gilt als 0 (das Original speichert nur Abweichungen vom RAM-Startwert 0). Keine automatische Übernahme. | Arch §3, [Parameterspeicher](../../wallbe2/docs/parameterspeicher.md), [ANN] Feldauswahl |

### 13.2 Startablauf

| Kennung | Schritt | Quelle |
| --- | --- | --- |
| SOLL-BOOT-01 | Pins nach SOLL-IO-02 (vor allem anderen). | E3 |
| SOLL-BOOT-02 | Takt: HSE 25 MHz ohne Bypass, PLL M 25/N 200/P 2/Q 5 -> 100 MHz; MCO1 = HSE an PA8. | Taktbaum |
| SOLL-BOOT-03 | IWDG starten (SOLL-RT-12), Resetgrund lesen und sichern. | E3 |
| SOLL-BOOT-04 | Parameter laden (Sektor 7); fehlen sie: Werkseinstellungen. | Parameterspeicher |
| SOLL-BOOT-05 | Reset-Taster prüfen (SOLL-BOOT-20). | Ist §14 |
| SOLL-BOOT-06 | DIP-, Drehschalter lesen; Konfigurationsprüfung (ERR-SWITCH, ERR-CONFIG). | Ist §10, E11 |
| SOLL-BOOT-07 | TIM1 starten mit CP +12 V DC, ADC/DMA starten, Schutz-Interrupts freigeben. | Ist §14 |
| SOLL-BOOT-08 | RCM-Selbsttest; danach mit D4 Entriegelungszyklus (SOLL-LOCK-20). | Decompilat `0x08039cd8` |
| SOLL-BOOT-09 | Netz: PA15 >= 10 ms Low [HYP PHY-Reset], dann Ethernet/IP starten; Netzstart verzögert den Ladeablauf nicht. | Ist §14 Schritt 2 |
| SOLL-BOOT-10 | Automat in S0; nach 5 s S1 (SOLL-SM-01). PB2 High nach erster Ausgangsberechnung (SOLL-IO-21). | E9 |

### 13.3 Werksreset

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-BOOT-20 | Ist PD2 beim Start Low, wird gewartet (IWDG wird bedient, Schütz aus). Losgelassen vor 5 s -> normaler Start; zwischen 5 und 10 s -> Notweg zum Bootloader (SOLL-BOOT-22); ununterbrochen **10 s** Low -> Werkseinstellungen schreiben (Sektor 7 loeschen/neu, sicherer Zustand gegeben), dann Power-LED an bis zum Loslassen, dann weiterer Start. Mit der Reset-Verzögerung von etwa 5 s (Hinweis Abschnitt 2.2) hält man für den Werksreset insgesamt etwa 15 s, passend zum Handbuch (> 10 s). **[ABW]** Original: Werksreset schon nach 5 s (E22). | Ist §14, E22, HB S. 16, Nutzer 2026-10-28, bestätigt 2026-10-29 |
| SOLL-BOOT-21 | Werkseinstellungen = Defaults aus Abschnitt 10 (auch Zugangsdaten mit erneutem Änderungszwang); Sektor 3 bleibt unberührt. Auch per Shell (SOLL-SH-08). | E20 |
| SOLL-BOOT-22 | **Notweg zum Bootloader** (eigene Erweiterung): Taster beim Start zwischen 5 und 10 s losgelassen -> Übergabe an den Original-Bootloader wie SOLL-BOOT-30, aber ohne Adresse (BKP13/14 = 0): Der Bootloader nimmt dann seine feste Adresse 192.168.0.8/24. Damit lässt sich eine Firmware aufspielen, auch wenn Weboberfläche oder Netzkonfiguration nicht mehr erreichbar sind. Ohne Upload kehrt der Bootloader nach seinen Wartezeiten in die vorhandene Firmware zurück (wallbe2 docs/bootloader.md). Ladelogik und Schütz sind dabei noch nicht gestartet. | Nutzer 2026-10-28, bestätigt 2026-10-29 |

Prüfung Abschnitt 13: PC – Parameterformat mit simuliertem Flash (Anhängen, voller
Bereich, Verdichten, Stromausfall an jeder Stelle, kaputte Prüfsumme), Import aus einem
synthetischen Sektor-3-Abbild; Startreihenfolge in `native_sim`. H0 – Bootloader auslesen
(Prüfung SOLL-BOOT-31, löscht der Bootloader Sektor 7?). H1 – Start, Werksreset mit
Taster, Persistenz.

## 14. Nicht umgesetzt / später

| Funktion | Original (Felder/Bits) | Behandlung |
| --- | --- | --- |
| Energiemessung als Messquelle der Ladelogik | 108..119, 132, Task `Modbus_Master_EM` | entfällt; Zähler nur zur Anzeige (12.5); Schnittstelle für Phasenströme vorgesehen (SOLL-PROT-30) |
| Überstromerkennung | 409, R107.8/.9 | spezifiziert, nur mit Messquelle |
| Schützüberwachung Modus 4 | 390 = 4 | spezifiziert (E4), nur mit Messquelle |
| Phasensymmetrie | 433, 3481 | entfällt |
| RFID | 423, 482, `c87`, R155.7 (`c7c.7`), Code 18 | entfällt; Code 18 wie 1 (F18) |
| OCPP | 427, 3843..3861, 466, `cec`, `c88`, R155.0 | entfällt; später allgemeine Implementierung über SOLL-API-20 und eigenen Strom-Slot |
| Mobilfunk | Modem-Task, PD0/PD1/PB10, PE10/PE11, 418 | entfällt (BKP15 = 0) |
| Master-Slave/Lastverteilung | 426, 438, 464, 996, 998, R107.15 | entfällt |
| Modbus TCP, Modbus-Register-Schnittstelle | Register-/Coil-Schnittstelle | entfällt; Modbus RTU nur als Client für den Zähler (12.5), Baudrate 386 -> `meter_baud` (kein Import) |
| TLS | – | vorerst nicht; Zugangsdaten daher im LAN mitlesbar |
| Originalweboberfläche, Feldnummern-API | `/daten`, Felder | entfällt (E21) |
| Feld 485 (OTP) | `0x1FFF7890` | entfällt (E13) |
| Felder 800, 803, 469, 470 | | entfallen (800 ersetzt durch Shell-Kommandos; 404 = DHCP lebt als `net_dhcp` weiter) |
| WARP-Funktionen über den evcc-Mindestumfang hinaus | `evse/low_level_state`, `start_charging`, `phase_auto_switch`, Charge-Tracker | schrittweise nach Absprache |

## 15. Entscheidungen und offene Punkte

### 15.1 Entscheidungen des Nutzers zu F1..F30

F1..F27 am 2026-10-19 entschieden (F12 am 2026-10-20 bestätigt), F28..F30 am 2026-10-21; die betroffenen
Anforderungen sind angepasst. Rahmenentscheidungen (Zephyr, Takt, Speicher, Weboberfläche,
Projekt): [Architektur](architektur.md#2-entscheidungen).

| Nr. | Thema | Entscheidung | Grund | Wirkt in |
| --- | --- | --- | --- | --- |
| F1 | Funktionen, die einen Energiezähler brauchen | "nicht verfügbar", Schnittstelle für spätere Messquelle vorsehen | der angebundene Zähler dient nur der Anzeige (F28); ohne verlässliche Messquelle wären die Funktionen wirkungslos oder trügerisch | 1.4, SOLL-PROT-22/30 |
| F2 | Zusatz-Entprellung Digitaleingänge | 2 Messblöcke (~16 ms) | Störfestigkeit; 16 ms Verzögerung sind unkritisch | SOLL-IO-12 |
| F3 | Schreiben 801/802/465 | 801 nur absenken; 802/465 nur in S1, 465 mit echtem Zyklus | Schreibzugriffe dürfen Schutz nicht aushebeln: Obergrenze nicht erhöhen, "verriegelt" nicht ohne Mechanik melden, keine Umkonfiguration mit Fahrzeug | SOLL-PAR-40 |
| F4 | Zeitwerte Watchdog/Reaktion | Startwerte annehmen, nach H1 nachschärfen | reale Laufzeiten erst auf der Hardware messbar | SOLL-RT-05/10..12, SOLL-PROT-05 |
| F5 | CP im sicheren Zustand | -12 V DC (F) | für das Fahrzeug eindeutig "nicht verfügbar" | SOLL-RT-20 |
| F6 | CP-Hysterese | gegen Pegelzustand | die Pegelauswertung soll nicht von Fehlerbits abhängen (Original vergleicht mit dem erzwungenen Status) | SOLL-CP-02 |
| F7 | Diode fehlt | 3 s -12 V, dann +12 V, Fehler bis CP A; Normprüfung offen | das Fahrzeug sieht F eindeutig, danach ist das Abstecken (A) messbar; das Original bleibt bei -12 V | SOLL-CP-07, 4.2 |
| F8 | Sofort- vs. geordnete Abschaltung | wie Tabelle 11.1 | sofort bei Gefahr oder unklarem Fahrzeugzustand, sonst geordnet über das Fahrzeug | 5.2, 11.1 |
| F9 | LD fällt im Laden weg | sofort aus, einmal neu verriegeln, bei Erfolg weiterladen | ohne gesicherte Verriegelung kein Strom; das Original verriegelt blockierend bei möglicherweise eingeschaltetem Schütz | SOLL-LOCK-40 |
| F10 | Eingangscode 19 | ignoriert Slot `external` | wie im Handbuch beschrieben | SOLL-CUR-04 |
| F11 | Werte über der Obergrenze | annehmen und über das Minimum begrenzen; 1..5999 und > 80000 ablehnen | evcc oder Nutzer sollen nicht scheitern, wenn Kabel oder Drehschalter weniger erlauben | SOLL-CUR-14 |
| F12 | Startwert `external` | keine Einschränkung (80000), parametrierbar wie WARP `external_defaults`; Sollwert, Grenzen und wirksamer Strom getrennt sichtbar (SOLL-API-32) | ohne Zutun soll geladen werden; ein Startwert 0 wurde erprobt und verworfen, weil ohne evcc nie geladen würde | SOLL-CUR-15 |
| F13 | RCM-Wiederholtest in A | alle 30 s | Rücksetzen möglich, ohne den Sensor dauernd zu testen | SOLL-PROT-11 |
| F14 | Schützfehler rücksetzen | nur Neustart oder Quittierung | ein klebendes Schütz ist gefährlich; das Original löscht den Fehler, sobald die Erkennung entfällt | SOLL-PROT-23 |
| F15 | D5/PE1 | wie Original (ohne sichtbare Wirkung), nach H1 neu entscheiden | Ziel von PE1 unbekannt; das Relais C3/C4 ist es nicht (Testgerät 2026-10-27) | SOLL-PROT-24 |
| F16 | Unterspannung | nur Warnung, RCM bleibt aktiv, kein Sprung in den Grundzustand | Schutz hat Vorrang; das Original setzt die RCM-Überwachung bei Unterspannung aus | SOLL-PROT-41 |
| F17 | D6 ohne LD-Eingang | Konfigurationsfehler | sichtbarer Fehler statt still fehlschlagender Verriegelung | SOLL-LOCK-03 |
| F18 | Eingangscode 18 | wie Code 1 | Portal nicht im Umfang | 9.1 |
| F19 | Startwert Freigabe 400 | Parameter `enable_register_default`, Default 0 | wie Original, aber einstellbar | SOLL-EN-03 |
| F20 | Zugriffsschutz | Web/API von Anfang an mit Benutzer/Passwort (Default mit Änderungszwang); Shell nur über die angemeldete Weboberfläche, kein Telnet auf der Steuerung (Nutzer 2026-10-28) | die API steuert das Laden; ein offener Port ohne Anmeldung wäre eine zweite, ungeschützte Steuerung | SOLL-API-07, SOLL-SH-19 |
| F21 | Pause persistent | nein | wie Original (468 nur im RAM); eine Pause soll keinen Neustart überdauern | 10.2 |
| F22 | Typ in `/info/name` | `warp2` | bei `warp3`/`warp4` würde evcc eine nicht vorhandene Phasenumschaltung bedienen | SOLL-API-01 |
| F23 | D/E/F an evcc | ehrlich 3/4 | evcc soll einen Fehler melden statt eines falschen Zustands | 12.1 |
| F24 | Parameter über HTTP | ja, alles von Anfang an | Nutzervorgabe (Architektur) | SOLL-API-30 |
| F25 | Netz-Default | DHCP an | Einbindung ins Heimnetz ohne Vorwissen | 10.1 |
| F26 | Doppelt belegte Eingangsfunktion | ODER | eindeutig; im Original gewinnt zufällig der zuletzt ausgewertete Kanal | SOLL-FN-02 |
| F27 | LD-Prüfung beim Entriegeln | Option `unlock_check`, Default aus | Signalform erst in H1 messbar; Default wie Original | SOLL-LOCK-14a |
| F28 | Energiezähler (2026-10-21, revidiert "Modbus nicht im Umfang") | Modbus-RTU-Zähler ABB B23 112-100, Eastron SDM72D-M, SDM630, erweiterbar; Auswahl "kein Zähler" als Default; **nur Anzeige** (Weboberfläche, evcc und andere Clients) | Messwerte für evcc und Anzeige sind nützlich und passen in den Speicher; für Schutzfunktionen sind Geräte und Bus zu unsicher; die Steuerung muss ohne Zähler voll funktionieren | 1.4, 12.5 |
| F29 | Modbus-Implementierung | Modbus-Client von Zephyr, gekapselt und austauschbar; Fehlerzähler zur Beurteilung | ein eigener Master wäre kaum kleiner; Austausch, falls die Fehlerzähler im Betrieb Probleme zeigen | SOLL-MTR-04/15, [Architektur](architektur.md#5-speicherbudget) |
| F30 | MQTT-Client (2026-10-21) | MQTT 3.1.1 ohne TLS, Zustände als WARP-JSON (kein Einzelwert je Topic); Home-Assistant-Erkennung seit 2026-10-27 (Nutzer: viele Nutzer haben Home Assistant, SOLL-MQTT-40..42); Befehle minimal (Ladestrom), erweiterbar, Default aus; zusätzlich Erreichbarkeit per Last Will | Anbindung an vorhandene MQTT-Umgebungen mit demselben Datenformat wie HTTP und WebSocket; der Ladestrom muss über MQTT setzbar sein; weitere Befehle sollen später ohne Umbau möglich sein | 12.6, 10.1 |
| F31 | Gliederung der Weboberfläche, Verlauf (2026-10-21) | Bereiche Übersicht, Status (bis 2026-10-27 "Insights", Nutzer: deutscher Begriff), Einstellungen, System statt "Einfach"/"Experte"; Leiste oben; Verlauf als Ringpuffer nur im RAM; keine Ladesitzungen | bei vier Bereichen versteckt ein Seitenmenü nur; Einstellungen und Status getrennt statt in einer Karte gemischt; Flash nicht für Verläufe (Verschleiß, Aufwand), Sitzungen vorerst nicht benötigt; einige KB RAM sind frei | SOLL-API-31/33 |
| F33 | Mindestpause vor erneutem Schütz-Ein (2026-11-07) | `contactor_min_off` ab Werk 5 s, einstellbar bis 10 s; Feld 519 wird beim Import nicht übernommen | Das Schütz des Testgeräts (Schneider iCT 25 A) zieht nach 0,5 s und einmal nach 3,3 s Pause nicht an und bleibt dann trotz Spannung an der Spule aus (230 V an A1/A2 gemessen, Nutzer 2026-11-07; vermutlich elektronische Spulenansteuerung, die ihren Anzugsimpuls erst nach einer Erholzeit wieder liefert; im Datenblatt nicht genannt). Im Original kommt zu den 500 ms von Feld 519 bei jedem Wiedereinschalten der blockierende RCM-Selbsttest (bis etwa 2,1 s); Wallfree testet nur einmal je Steckvorgang und bräuchte die Pause daher selbst. Eine Prüfung im Ein-Zustand über CT wurde erwogen und zurückgestellt: einfacher, und so kurze Schaltfolgen kommen im Betrieb kaum vor (Nutzer 2026-11-07) | SOLL-SM-60 |
| F32 | Uhrzeit (2026-10-21, geändert 2026-10-26) | SNTP mit Zeitserver aus DHCP bzw. `pool.ntp.org`, abschaltbar; ohne Zeitserver wie vorher (ms seit Start, Umrechnung im Browser); RTC nicht gestellt | Nutzer 2026-10-26: Protokoll übersteht Neustarts (SOLL-ERR-12) und braucht dafür echte Zeiten; Flash reicht seit dem Entfernen der Netz-Shell; die Ladelogik hängt nie an der Uhrzeit | SOLL-TIME-01..04 |

### 15.2 Annahmen und offene Punkte

| Punkt | Stand | Klärung |
| --- | --- | --- |
| PE1-Ziel | nicht das Relais C3/C4 (Testgerät 2026-10-27); Bahn nicht verfolgt | H1 (PE1 schalten und beobachten) |
| CP-Treiber, Quelle der -12 V | ICL7662 gesehen (Spannungsinverter), Beschaltung offen | Durchgang spannungsfrei, H1 |
| LD-Signalform der Steckdose (Impulsdauer, Impulse beim Entriegeln) | unbekannt; bestimmt SOLL-LOCK-14/15 | H1 |
| CP-Treiber (Polarität, Pegel), ADC-Skalierung CP/PP/Versorgung | Original-Skalierung übernommen [ANN] | H1/H2 |
| RCM: Inversionsstufe, direkter Pfad SWITCH_1/2 -> Schütztreiber, Konfigurationspins | [HYP] | H1/H3 |
| Quelle an PE0 (Klemme CT) | [HYP] | H1 |
| "Automatische Entriegelung bei Stromausfall" (HB S. 9): Hardwarefunktion? | in der Firmware nicht gefunden; auf dem Testgerät nicht vorhanden, andere Steuerungen haben Pufferelkos ([wallbe2 hardware.md](../../wallbe2/docs/hardware.md)); vermutlich reine Hardware, Wallfree ist davon nicht betroffen, solange das zutrifft | H1 |
| Bootloader: Prüfungen, Nutzung von BKP-Registern, löscht er Sektor 7, Start-SP | aus dem Testgerät ausgelesen (H0, RDP 0), eigener Webserver für den Upload; Analyse offen | [Bootloader](../../wallbe2/docs/bootloader.md#stand-und-offene-fragen) |
| IWDG-Umprogrammierung im Betrieb; Löschzeit Sektor 7 | Löschzeit höchstens 4000 ms laut Zephyr-Devicetree (`stm32f2.dtsi`, `max-erase-time`), nicht am Datenblatt geprüft (DS6329 fehlt); IWDG-Umprogrammierung über Register umgesetzt, auf Hardware ungeprüft | Datenblatt, H1 |
| Normzeiten IEC 61851-1 (Abschaltung nach C->B, Reaktionszeit des Fahrzeugs) | Norm nicht im Projekt; 5-s-Frist durch HB S. 38 gestützt | Normtext |
| Diodenprüfung: Vorgabe der IEC 61851-1 zur Ausgabe nach Fehlschlag | Norm nicht im Projekt | Normtext |
| LD-Signal Küster 03S (Fremdquelle nennt 02S: 0 V in beiden Endlagen, 12 V während der Fahrt) | Hypothese | H1-Messung |
| Deutung Feld 519 als Mindestpause vor erneutem Einschalten | [ANN] (Original: Wartezeit nach jedem Schütz-Aus im Task) | Decompilat bei Bedarf |
