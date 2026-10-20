# Soll-Spezifikation der Ersatzfirmware "Wallfree"

Stand: 2026-10-20, Entwurf (SOLL-ERR-11 Zugriffsprotokoll ergaenzt). Beschreibt, **was die Ersatzfirmware tun soll**, bezogen auf
die Klemmen der Steuerung **EV-CC-AC1-M3-CBC-RCM-ETH** (Phoenix Contact). Das
Originalverhalten steht in der [Ladeautomat-Spezifikation](../../wallbe2/docs/ladeautomat-spezifikation.md)
(im Folgenden "Ist-Spez."). Diese Datei ist Grundlage fuer Implementierung und Tests.
Sie ist **kein Sicherheitsnachweis**. Weder bestandene Tests noch diese Spezifikation
belegen elektrische Sicherheit oder Eignung fuer den Ladebetrieb.

Bearbeitungsstand: alle Abschnitte im Entwurf (Schritte 1..3, 2026-10-19). Die Fragen F1..F27 sind am 2026-10-19 entschieden
([Abschnitt 15](#15-offene-punkte-annahmen-und-fragen-an-den-nutzer)).

## Lesehilfe

- **Kennungen:** `SOLL-<Bereich>-<Nr>`. Bereiche: GEN (allgemein), IO (Klemmen),
  RT (Zeitmodell, Watchdog), CP, PP, SM (Ladeautomat), CUR (Strom), PROT (Schutzpfad),
  LOCK (Verriegelung), FN (Ein-/Ausgangsfunktionen), EN (Freigabe), PAR (Parameter),
  ERR (Fehler/Status), LED, API, SH (Shell), NVM (Parameterspeicher), BOOT (Start).
- **Quelle** je Anforderung: `E<n>` = Entscheidung n der
  [Ist-Spez.](../../wallbe2/docs/ladeautomat-spezifikation.md#entscheidungen-des-nutzers-stand-2026-10-18),
  `Ist §n` = Abschnitt n der Ist-Spez., `HB S. n` = Steuerungshandbuch
  [108191_de_04](../../wallbe2/docs/um_de_ev_cc_ac1_m3_cbc_rcm_eth_108191_de_04.pdf) PDF-Seite n,
  `Arch §n` = [Architektur](architektur.md), `1.27 §n` =
  [Firmwarevergleich](../../wallbe2/docs/firmware-vergleich-1.27.md), `AG` = [AGENTS.md](../../wallbe2/AGENTS.md),
  `Norm` = IEC 61851-1 (Normtext liegt **nicht** im Projekt; Normbezuege stammen aus dem
  Handbuch oder sind als ungeprueft markiert).
- **Abweichung** vom Original (SL-01.04.24) ist mit **[ABW]** markiert, eine Annahme
  ohne Beleg mit **[ANN]**, eine Hardwarezuordnung ohne Leiterbahnbeleg mit **[HYP]**.
  Offene Nutzerfragen tragen die Kennung `F<n>` (gesammelt in
  [Abschnitt 15](#15-offene-punkte-annahmen-und-fragen-an-den-nutzer)).
- **Pruefung:** Am Ende jedes Abschnitts steht, wie die Anforderungen geprueft werden:
  `PC` = hardwareunabhaengiger Test der Ladelogik auf dem PC (Zephyr `native_sim` oder
  Host-Unit-Test), `H1..H4` = Hardwarestufen aus der
  [Architektur](architektur.md#hardware-inbetriebnahme) (jede Stufe erst nach
  ausdruecklicher Freigabe in AGENTS.md).
- Feldnummern (z. B. "Feld 520") bezeichnen nur die **Herkunft** im Original. Die
  Ersatzfirmware hat eigene Parameternamen ([Abschnitt 10](#10-parameter)); die
  Feldnummern-Schnittstelle des Originals ist kein Ziel (E21).

## 1. Zweck, Geltungsbereich, Begriffe, Normbezug, Abgrenzung

### 1.1 Zweck und Geltungsbereich

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-GEN-01 | Die Ersatzfirmware steuert einen Ladepunkt Mode 3, Ladefall B und C, mit einer Fahrzeugschnittstelle, ueber die Klemmen der Steuerung EV-CC-AC1-M3-CBC-RCM-ETH. | HB S. 9 |
| SOLL-GEN-02 | Alle Festlegungen beziehen sich auf die **Klemmen der Steuerung** (Tab. 3-1). Eigenschaften einer bestimmten Wallbox (wallbe eco 2.0, Steckdose, Motor, LEDs, Schluesselschalter) duerfen keine feste Programmierung ausloesen; Unterschiede werden ueber Parameter abgebildet. | AG Grundsatz |
| SOLL-GEN-03 | Die Ersatzfirmware ist mindestens so flexibel wie das Original: **alle** Betriebsarten, die DIP D1..D10, der Drehschalter und die konfigurierbaren Ein-/Ausgangsfunktionen erlauben, werden unterstuetzt; ausgenommen sind Funktionen ausserhalb des Umfangs (1.4). | AG Anforderungen, Ist §10 |
| SOLL-GEN-04 | Verhalten nach IEC 61851-1; Abweichungen nur fuer dokumentierte Massnahmen zur Fahrzeugkompatibilitaet (z. B. Weckfolge). Jede Abweichung ist hier genannt. | AG Grundsatz |
| SOLL-GEN-05 | Die Ladelogik ist ein **hardwareunabhaengiges Modul** mit definierten Ein-/Ausgaengen (Messwerte, Schalterstellungen, Parameter, Zeit -> CP-Tastgrad, Schuetz, Verriegelung, Ausgangsfunktionen, Status), das vollstaendig auf dem PC getestet werden kann. | Arch §6 |
| SOLL-GEN-06 | Der Schutzpfad (Fehlerstrom, Schuetzfehler, Versorgung, Watchdog) ist vom Ladeautomaten getrennt und wirkt ereignisgesteuert direkt auf das Schuetz. | E1, E2 |

Beispielkonfiguration der vorhandenen Wallbox: [Geraetekonfiguration](../../wallbe2/docs/geraetekonfiguration.md)
(keine Vorgabe).

### 1.2 Plattform (entschieden, nicht Gegenstand dieser Spezifikation)

- MCU STM32F207VET6, HSE-Quarz 25 MHz, SYSCLK 100 MHz (Nutzer 2026-10-19; wie Original, [Taktbaum](../../wallbe2/docs/takt-adc-gpio.md#taktbaum)).
- Zephyr v4.4.2, vorlaeufiges Board `boards/phoenix/ev_cc_ac1`.
- Anwendung als Rohabbild ab `0x08010000` (Flash-Sektoren 4..6, max. 320 KB), Bootloader in
  Sektoren 0..2 bleibt, Sektor 3 (Originalparameter) wird nur gelesen, eigener
  Parameterbereich in Sektor 7 (P1), siehe [Abschnitt 13](#13-parameterspeicher-startablauf-werksreset).
- Netz: Zephyr-IP-Stack, KSZ8081RND ueber RMII, PHY-Adresse 0 ([Ethernet-PHY](../../wallbe2/docs/ethernet-phy.md)).

### 1.3 Begriffe

| Begriff | Bedeutung |
| --- | --- |
| CP-Zustand | Buchstabe A..F nach IEC 61851-1 (HB S. 37 Tab. 6-1), gemessen am CP-Plateau |
| CP-Rohzustand | nur aus dem Pegel entprellter Zustand, ohne Erzwingung durch Fehler (Original `c29`) |
| CP-Status | entprellter, ggf. durch Fehler erzwungener Zustand (Original `c28`) |
| PWM an | CP mit 1-kHz-Rechteck und Tastgrad 3..97 % (B2/C2/D2) |
| CP +12 V DC | Tastgrad 100 %, "PWM aus" (B1/C1), Original CCR 1000 |
| CP -12 V DC | Tastgrad 0 %, Zustand F ("nicht verfuegbar"), Original CCR 0 |
| Stromsperre | Laden ist freigegeben, aber der wirksame Strom ist 0 bzw. < 6 A: CP +12 V DC, Schuetz aus |
| Freigabe | logische Ladeerlaubnis aus EN/Schnittstelle je nach DIP D7/D10 (Abschnitt 9) |
| Verfuegbarkeit | XR (D8) bzw. Schnittstelle (D10); fehlt sie -> CP-Status F |
| Steckvorgang | Zeitraum von Verlassen des CP-Zustands A bis zur Rueckkehr nach A |
| Schutzfehler | Fehler, die sofort und unabhaengig vom Automaten das Schuetz oeffnen (Abschnitt 7) |
| sicherer Zustand | Schuetz aus, CP gemaess SOLL-RT-20, Verriegelungsaktor unbestromt |
| Slot | eine einzelne Strombegrenzung; wirksamer Strom = Minimum aller aktiven Slots (Abschnitt 6) |
| mA | Einheit aller Stroeme an Schnittstellen (E21) |

### 1.4 Abgrenzung (nicht im Umfang)

Energiemessung, Kartenleser/RFID, OCPP, Mobilfunk, Master-Slave/Lastverteilung,
Modbus (TCP und RTU), TLS, Nachbau der Originalweboberflaeche. Fuer OCPP wird spaeter eine
allgemeine Implementierung vorgesehen; die Architektur laesst Platz (eigener Slot in der
Stromkette, Freigabequelle, Statusereignisse). Details: [Abschnitt 14](#14-nicht-umgesetzt--spaeter).

**Folge fuer abhaengige Funktionen** (siehe F1): Ueberstromerkennung (Feld 409),
Schuetzueberwachung Modus 4 "Via Energy Meter" und Phasensymmetrie (Feld 433) brauchen
Messwerte eines externen Zaehlers. Ohne Messquelle sind sie in der Ersatzfirmware **nicht
verfuegbar**; die Logik wird gegen eine abstrakte Messquelle spezifiziert (SOLL-PROT-30 ff.),
damit sie spaeter angeschlossen werden kann.

Pruefung Abschnitt 1: Review dieser Spezifikation gegen AGENTS.md; Rueckverfolgbarkeit
Kennung -> Test in der Testliste der Implementierung.

## 2. Klemmen und Signale

Belegarten: **FW** = Firmwarebefund im Original (Pin aus Code, keine Leiterbahn),
**HB** = Handbuch, **N** = Nutzerangabe, **[HYP]** = Hypothese. Pinbelegungen sind
**keine Schaltplanbestaetigung** ([Taktbaum/ADC/GPIO](../../wallbe2/docs/takt-adc-gpio.md)). Spannungsangaben
an Klemmen setzen voraus, dass die ADC-Skalierung des Originals ("mV") die Klemmenspannung
wiedergibt [ANN]; Bestaetigung in H1/H2.

### 2.1 Klemmen nach Tabelle 3-1 (HB S. 13/14)

| Nr. | Klemme | Funktion (HB) | MCU | Richtung, Pegel | Abtastung / Ansteuerung (Soll) | Beleg |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | C1/C2 | Relaiskontakt Lastschuetz, 230 V AC, 16 A | **PE2** | Ausgang, High = Relais an | nur ueber Schuetz-Schnittstelle (SOLL-PROT-01) | FW (alle Schreiber in `0x0803ba2c`), Relais-Treiber [HYP] |
| 1 | CT | Contactor Test: Spannung am Schuetzausgang nach Ladeende | **PE0** | Eingang, EXTI steigende Flanke | Flankenzeitstempel; "Via Sense" (SOLL-PROT-20) | FW, Verbindung CT -> PE0 [HYP] |
| 2 | L/N/PE | Versorgung 230 V AC | (PC0) | – | interne Versorgungsueberwachung ueber ADC IN10/PC0 | FW, Bedeutung (12-V-Schiene) [HYP] |
| 3 | Ethernet | RJ45, 100Base-TX | RMII: PA1, PA2, PA7, PB11, PB12, PB13, PC1, PC4, PC5; MCO1 **PA8**; Reset **PA15** [HYP] | – | Zephyr-Treiber; PA8 = 25 MHz fuer PHY-XI [HYP]; PA15 >= 10 ms Low beim Start | FW, [Ethernet-PHY](../../wallbe2/docs/ethernet-phy.md) |
| 4 | 12 V | Ausgang 12 V DC, max. 200 mA | – | – | keine Ansteuerung bekannt | HB |
| 4 | LD | Digitaleingang (Default Verriegelungsrueckmeldung) | **PA3** (ADC IN3) | analog, < 3 V aus / > 9 V ein, Eingangswiderstand 3 kOhm | ADC-Block (SOLL-IO-10) | FW, HB S. 10 |
| 4 | EN | Digitaleingang (Default Freigabe) | **PA4** (IN4) | wie LD | wie LD | FW |
| 4 | ML | Digitaleingang (Default manuelle Verriegelung) | **PA5** (IN5) | wie LD | wie LD | FW |
| 4 | XR | Digitaleingang (Default Verfuegbarkeit) | **PA6** (IN6) | wie LD | wie LD | FW |
| 4 | IN | Digitaleingang (Default Ladestrom 16 A) | **PC3** (IN13) | wie LD | wie LD | FW |
| 4 | A/B | RS-485 (Modbus RTU) | UART, DE an PC12/PD7 | – | **nicht genutzt** (Modbus nicht im Umfang); Treiberfreigaben PC12/PD7 dauerhaft Low (Empfang) | FW [Restumfang §7](../../wallbe2/docs/restumfang.md#7-nicht-zugeordnete-pins-punkt-8) |
| 4 | GND | Systemerde | – | – | – | HB |
| 4 | ER | Digitalausgang (Default Fehler) | **PB4** | Ausgang, aktiv High, 8..30 V ueber 12a, max. 600 mA | Funktionscode (Abschnitt 9) | FW, Treiber DRV8844 [HYP] |
| 4 | LR | Digitalausgang (Default Lock Request) | **PB5** | wie ER | wie ER | FW |
| 4 | VR | Digitalausgang (Default Fahrzeug bereit) | **PB6** | wie ER | wie ER | FW |
| 4 | CR | Digitalausgang (Default PWM an) | **PB7** | wie ER | wie ER | FW |
| 4 | – | Freigabe Ausgangstreiber | **PB2** | Ausgang, High = frei | nach Initialisierung der Ausgaenge High (SOLL-IO-21) | FW, DRV8844 nSLEEP [HYP] |
| 4 | 12a | Speisung der Ausgaenge 8..30 V DC | – | – | – | HB |
| 5 | RCM | Fehlerstromsensor (Bender CCE4400/D0569) | **PE3** SWITCH_1, **PE4** SWITCH_2, **PE6** ERR_OUT, **PE5** TEST_IN | Eingaenge ohne Pull, High = ausgeloest (Inversion zum Bender-Pegel [HYP]); PE5 Open-Drain, Low = Test | EXTI + zyklische Kontrolle (SOLL-PROT-02) | FW, [RCM/Bender](../../wallbe2/docs/rcm-bender.md) |
| 6 | SIM | Mobilfunk | PD0, PD1, PB10 | – | nicht genutzt; Pins in sicherem Ruhezustand wie Original-Init (PD1 Low) | FW |
| 7 | R1/R2 | Steuerspannung Verriegelungsaktor, 12 V, max. 1 A, Impuls 500 ms (Default) bis 3 s | **PA9** (Verriegeln), **PA10** (Entriegeln) | Ausgaenge, Ruhe High, Impuls = Low | Abschnitt 8 | FW, HB S. 9, A4953-Ansteuerung [HYP] |
| 7 | CP | Control Pilot | **PE9** (TIM1_CH1, PWM), **PA0** (IN0, CP+), **PB1** (IN9, CP-) | PWM 1 kHz, +-12 V ueber Treiber [HYP] | Abschnitt 4 | FW |
| 7 | PX | Proximity | **PB0** (IN8) | analog | Abschnitt 4 | FW |
| 8 | C3/C4 | Relaisausgang, "reserviert" | **PE1** [HYP] | Ausgang | D5-Funktion (SOLL-PROT-24); Zuordnung PE1 -> C3/C4 ist eine Vermutung | FW (PE1-Schreiber), Zuordnung offen |
| 9 | Antenne | Mobilfunk | – | – | nicht genutzt | HB |

### 2.2 Bedienelemente (HB S. 15/16)

| Element | MCU | Pegel | Soll-Auswertung | Beleg |
| --- | --- | --- | --- | --- |
| DIP D1..D10 | PD15, PD14, PD13, PD12, PD11, PD10, PA12, PA11, PB15, PB14 | Pull-up, Low = ON | nur in der Initialisierung gelesen (SOLL-IO-30) | FW, HB S. 15 |
| Drehschalter Bit 0..3 | PC9, PC8, PC6, PC7 | Pull-up, Low = 1 | nur in der Initialisierung gelesen; Position 0..15 | FW |
| LED Power (gruen) | PE15 | aktiv Low | Abschnitt 11 | FW, HB S. 16 |
| LED Error (rot) | PE14 | aktiv Low | Abschnitt 11 | FW |
| LED Connect (gelb) | PE12 | aktiv Low | Abschnitt 11 | FW |
| LED Ready (gruen) | PE13 | aktiv Low | Abschnitt 11 | FW |
| LED Status Modem | PE10/PE11 | aktiv Low | aus (Mobilfunk nicht im Umfang) | FW |
| (Netzwerkstatus) | PC2 | Ausgang | Netzwerkanzeige (SOLL-LED-06) | FW, Bedeutung [HYP] |
| Reset-Taster | **PD2** [HYP] | Low = gedrueckt [HYP] | Werksreset beim Start (SOLL-BOOT-20) | FW (`main`), HB S. 16 |

Hinweis: Laut HB S. 16 startet "einfache Betaetigung" das Geraet neu. Da das Original PD2
nur beim Start liest, liegt der Taster vermutlich zusaetzlich am NRST oder an einer
Reset-Schaltung [HYP]; offen fuer die Hardwarepruefung.

### 2.3 Anforderungen an Ein-/Ausgangssignale

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-IO-01 | Unbenutzte bzw. ausser Umfang liegende Pins (PB3, PB8..PB10, PC12, PC13, PD0, PD1, PD7, PE7, PE8, PE10, PE11) werden wie in der Original-GPIO-Initialisierung in einen definierten Ruhezustand gesetzt; PC12/PD7 (RS-485-DE) Low. | FW [Taktbaum](../../wallbe2/docs/takt-adc-gpio.md#gpio-initialisierung-0x08040ce0-pinuebersicht) |
| SOLL-IO-02 | Erste Massnahme nach dem Reset, vor Takt- und Netzinitialisierung: PE2 (Schuetz) Low, PA9/PA10 High (Verriegelungsaktor in Ruhe), PE1 Low, PB2 Low, PB4..PB7 Low, PE5 High (kein RCM-Test). | Ist §14 (Startpegel), E3 |
| SOLL-IO-10 | Die fuenf Digitaleingaenge werden im ADC-Block (SOLL-RT-03) gemessen. Ein: Pegel > 9,0 V; Aus: < 3,0 V; dazwischen bleibt der letzte Wert (Hysterese). | HB S. 10, FW `0x0803c7d4` |
| SOLL-IO-11 | Der Rohpegel jedes Eingangs (ein/aus) ist unabhaengig von seiner Funktion als Status lesbar (Original Felder 200..203, 208). | Ist §2, HB Tab. 9-3 |
| SOLL-IO-12 | Ein Eingangswechsel gilt erst als uebernommen, wenn er in zwei aufeinanderfolgenden ADC-Bloecken besteht (Entprellung ca. 16 ms). **[ABW]** Original uebernimmt ohne zusaetzliche Entprellung (nur Mittelung ueber 8 Perioden). | F2 entschieden |
| SOLL-IO-20 | Die Ausgaenge ER/LR/VR/CR werden aktiv High angesteuert, Zustand jeweils als Status lesbar (Original 204..207). | Ist §12 |
| SOLL-IO-21 | PB2 wird erst High gesetzt, nachdem alle Ausgaenge ihren berechneten Wert haben (kein Impuls beim Start). | FW `0x08039f8c` (Original: Taskstart), [ANN] |
| SOLL-IO-30 | DIP-Schalter und Drehschalter werden **nur** in der Initialisierung gelesen; Aenderungen wirken erst nach Neustart. Ausnahme: Schreiben ueber die Schnittstelle (SOLL-PAR-40, F3). | HB S. 15, Ist §10 |
| SOLL-IO-31 | Ein Wert ueber die Schnittstelle fuer DIP/Drehschalter-Ueberschreibung ist als Status getrennt von der tatsaechlichen Schalterstellung lesbar. | [ANN] |

Pruefung Abschnitt 2: PC – Eingangshysterese, Entprellung, DIP-/Drehschalterdekodierung
mit simulierten Rohwerten. H1 – Pegel an jeder Klemme messen (Ausgaenge, R1/R2-Impulse,
CP-PWM, PB2-Startverhalten), Eingangsschwellen mit Labornetzteil, Zuordnung LED/Pin,
DIP/Drehschalter-Bitfolge. H2 – CP/PX-Skalierung mit EV-Simulator.

## 3. Zeitmodell, Aufgabenaufteilung, Watchdog, sicherer Zustand

### 3.1 Ablaufeinheiten

| Einheit | Kontext / Prioritaet | Takt | Aufgabe |
| --- | --- | --- | --- |
| TIM1 | Hardware | 1 kHz, center-aligned | CP-PWM auf PE9; Update loest ADC-Gruppen aus |
| ADC/DMA-ISR | Interrupt | je Messblock (8 PWM-Perioden, ~8 ms) | Mittelung, CP/PP/Eingaenge/Versorgung bereitstellen, Ereignis an Automat |
| Schutz-ISR | Interrupt hoechster Anwendungsprioritaet (EXTI3/EXTI4/EXTI9_5 fuer PE3/PE4/PE6, EXTI0 fuer PE0) | Ereignis | Fehlerstrom -> Schuetz sofort aus; Sense-Zeitstempel |
| Schutzthread | Thread, hoechste Thread-Prioritaet | 1 ms | zyklische Kontrolle der RCM-Pegel, Schuetzueberwachung, Versorgung, Ueberwachung der anderen Threads, IWDG |
| Automatenthread | Thread unter dem Schutzthread | 10 ms und bei Messblock-Ereignis | CP-/PP-Auswertung, Ladeautomat, Strom/PWM, Verriegelung, Ausgaenge, LEDs |
| Netz-/Shell-Threads | niedrige Prioritaet | ereignisgesteuert | HTTP, WebSocket, Telnet-Shell, Parameterspeicher-Schreiben |

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-RT-01 | CP-PWM: TIM1 CH1 auf PE9, 1,000 kHz, Aufloesung mindestens 1 Promille (Original: PSC 49, ARR 1000, center-aligned). Die PWM laeuft in Hardware unabhaengig von der Software weiter. | FW [Taktbaum](../../wallbe2/docs/takt-adc-gpio.md#tim1--cp-pwm) |
| SOLL-RT-02 | ADC-Abtastung synchron zum CP-Plateau: CP+ im aktiven (positiven) Abschnitt, CP- im inaktiven Abschnitt (Original: CNT < 50 bzw. > 950); Mittelung ueber 8 Perioden. | FW |
| SOLL-RT-03 | Messblock: CP+, CP-, PP, LD/EN/ML/XR/IN, Versorgung je ~8 ms. Bei Tastgrad 0 % bzw. 100 % gibt es nur ein Plateau; die Auswertung muss das beruecksichtigen (SOLL-CP-04). | FW, [ANN] |
| SOLL-RT-04 | Der Automatenthread **blockiert nie**: keine Wartezeit > 1 ms in einem Zyklus. Alle Fristen (Verriegelungsimpuls, Weckfolge, Selbsttest, Schuetz-Frist) sind Zeitgeber-Teilzustaende. **[ABW]** Original wartet blockierend bis ~4,25 s (Weckfolge), ~3 s bzw. 13,5 s (Lock), ~2,1 s (Selbsttest). | E2, E10, Ist §8 |
| SOLL-RT-05 | Zykluszeit des Automaten 10 ms (Jitter < 2 ms). **[ABW]** Original 25 ms + Laufzeit. | [ANN] |
| SOLL-RT-06 | Zeitbasis: monotone Millisekunden (64 Bit oder ueberlaufsicher); alle Fristen werden als "seit Zeitpunkt t vergangen >= Dauer" geprueft. | [ANN] |
| SOLL-RT-07 | Prioritaeten: Schutz-ISR > ADC-ISR > Netz-ISR; Schutzthread > Automatenthread > alle uebrigen Threads. Kein Netz- oder Shellkommando darf Schutz- oder Automatenthread blockieren (keine gemeinsamen Sperren mit unbegrenzter Haltezeit). | E2 |

### 3.2 Watchdog

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-RT-10 | Jeder ueberwachte Thread meldet sich zyklisch beim Schutzthread. Limits: Automatenthread **100 ms**, Messblock-Ereignis (ADC) **50 ms**, Schutzthread selbst ueber den IWDG. Ueberschreitung -> sofort sicherer Zustand (SOLL-RT-20), Ursache sichern, danach Reset. | E3, F4 (Startwerte, nach H1 nachschaerfen) |
| SOLL-RT-11 | Netz- und Shell-Threads: Limit 10 s. Ueberschreitung -> Fehlermeldung im Protokoll; Neustart nur, wenn das Schuetz aus ist und kein Ladevorgang laeuft (kein Abschalten unter Last nur wegen der Schnittstelle). Umsetzung: ueberwacht werden Speicher-Thread, WebSocket-Thread und System-Workqueue; HTTP-Server- und Telnet-Thread gehoeren zu Zephyr und melden sich nicht (nicht ueberwacht). Neustart nur in S1/S9/S10 mit Schuetz aus. | F4 |
| SOLL-RT-12 | IWDG wird nur vom Schutzthread nachgeladen, wenn alle Limits eingehalten sind. Zeitlimit **250 ms** im Betrieb. **[ABW]** Original: Task-Limit 60 s + IWDG ~3 s. | E3, F4 |
| SOLL-RT-13 | Fuer Flash-Loeschvorgaenge (blockieren den Bus fuer Sekunden, Arch §2) wird das IWDG-Limit **vorher** auf den Maximalwert verlaengert und danach zurueckgesetzt; Loeschen nur im sicheren Zustand (SOLL-NVM-05). Laenge der Loeschzeit und Umprogrammierbarkeit des IWDG im Betrieb sind gegen RM0033/DS6329 zu pruefen. | Arch §2, [ANN] |
| SOLL-RT-14 | Grund des letzten Resets (Watchdog, Fault, Update, Werksreset, Spannungseinbruch) wird ueber den Neustart erhalten (RTC-Backup-Register, nicht BKP10/11/13/14/15, siehe SOLL-BOOT-30) und ist als Status lesbar. Umsetzung: Code "WF"+Grund in **BKP19** vor jedem Software-Reset, beim Start mit den Flags aus RCC_CSR verknuepft (Watchdog vor Software, NRST-Pin zuletzt, weil jede interne Quelle den Pin pulst, RM0033 RCC); lesbar ueber `GET /system/info` und `evse status`. | Original BKP19, [ANN] |

### 3.3 Sicherer Zustand und Ausnahmen

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-RT-20 | **Sicherer Zustand:** (1) PE2 Low (Schuetz aus) als erste Aktion, (2) CP auf **-12 V DC** (Zustand F, Tastgrad 0 %, F5 entschieden), (3) PA9/PA10 High (kein Aktorimpuls), (4) ER gemaess Funktion, LED Error an. Der Verriegelungszustand wird nicht veraendert. | E3 |
| SOLL-RT-21 | HardFault, MemManage, BusFault, UsageFault, NMI mit Fehler, Assertion und Stack-Ueberlauf: sicherer Zustand direkt ueber Registerzugriffe (ohne Treiberaufrufe), Ursache sichern, Systemreset. **[ABW]** Original kreist ohne Abschaltung bis IWDG (~3 s). | E3, [Restumfang §6](../../wallbe2/docs/restumfang.md#6-fehler--und-ausnahmepfade-punkt-7) |
| SOLL-RT-22 | Neustart- und Updateanforderungen werden erst ausgefuehrt, wenn das Schuetz aus ist (geordnetes Ladeende mit Frist SOLL-SM-41); Hoechstwartezeit 15 s wie Original, danach sicherer Zustand und Reset. | [Restumfang §6](../../wallbe2/docs/restumfang.md), [Bootloader](../../wallbe2/docs/bootloader.md) |
| SOLL-RT-23 | Nach jedem Reset gilt bis zum Ende der Initialisierung der Zustand aus SOLL-IO-02. | E3 |

Pruefung Abschnitt 3: PC – Automat mit simulierter Zeit (keine blockierenden Aufrufe,
Fristen), Watchdog-Supervisor mit kuenstlich haengendem Thread. H1 – Zykluszeiten und
ISR-Latenz mit Testpins am Oszilloskop; Watchdog-Ausloesung (haengender Thread) ->
Schuetzsignal PE2 Low und CP-Zustand messen; Fault-Injektion (absichtlicher HardFault in
Pruef-Firmware) -> sicherer Zustand vor Reset.

## 4. CP- und PP-Auswertung

Grundlage: [Restumfang §1/§2](../../wallbe2/docs/restumfang.md#1-cp-klassifikator-0x08039fe0-vollstaendig),
Ist §2a, HB S. 37 (Tab. 6-1), S. 41 (Tab. 6-4). Spannungen gelten am CP-Plateau
(Klemme CP gegen PE). Die Umrechnung ADC -> Volt uebernimmt zunaechst die
Original-Skalierung (`x*39/100 + 300` bzw. `14050 - x*397/1000` je Summe ueber 8 Werte)
[ANN]; Kalibrierung in H2.

### 4.1 CP-Pegel und Zustaende

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-CP-01 | Bandtabelle fuer den Pegel `v` des positiven Plateaus (Grenzwert gehoert zum zuerst genannten Band): `v` >= 11,0 V -> A; 10,0..10,999 -> A, wenn Rohzustand bisher A, sonst B; 8,0..9,999 -> B; 7,0..7,999 -> B, wenn bisher <= B, sonst C; 5,0..6,999 -> C; 4,0..4,999 -> C, wenn bisher <= C, sonst D; 2,0..3,999 -> D; 1,0..1,999 -> D, wenn bisher <= D, sonst E; `v` < 1,0 V -> Kandidat "CP ungueltig". `v` > 13 V gilt als A ohne Fehler. | Ist §2a |
| SOLL-CP-02 | Die Hysterese in den Zwischenbaendern bezieht sich auf den **CP-Rohzustand** (nur Pegel). **[ABW]** Original vergleicht mit dem ggf. durch Fehler erzwungenen Status `c28`; dadurch haengt die Pegelauswertung im Original von Fehlerbits ab. | F6 entschieden |
| SOLL-CP-03 | Entprellung: Der Rohzustand uebernimmt einen neuen Kandidaten, wenn dieser **ununterbrochen > 20 ms** vom bisherigen Rohzustand abweicht; uebernommen wird der dann aktuelle Kandidat. | Ist §2a |
| SOLL-CP-04 | Ausgewertet wird nur ein Plateau, das tatsaechlich ausgegeben wird: bei CP -12 V DC (Tastgrad 0 %) gibt es kein positives Plateau; dann bleibt der Rohzustand unveraendert und "CP ungueltig" wird nicht gemeldet (Original: Sonderfall Weckfolge `c2a` = 5). Bei CP +12 V DC gibt es kein negatives Plateau; die Diodenpruefung entfaellt. | Ist §2a, [ANN] |
| SOLL-CP-05 | Nach jeder Aenderung der CP-Ausgabe (Tastgrad, DC-Pegel) werden die Messwerte der folgenden 2 Messbloecke (~16 ms) verworfen (Einschwingen von Treiber und Fahrzeugkapazitaet). | [ANN] |
| SOLL-CP-06 | **CP ungueltig** (Kurzschluss/0 V, Zustand E nach Tab. 6-1): `v` < 1,0 V und seit dem ersten Block mit `v` < 1,0 V mehr als 20 ms vergangen -> Fehler ERR-CP-INVALID, Status **sofort** E (ohne weitere Entprellung). Der Startzeitpunkt wird erst bei `v` >= 1,2 V verworfen; zwischen 1,0 und 1,2 V bleibt er erhalten, der Fehler gilt dort aber nicht (wie Original). Waehrend `v` < 1,0 V bleibt der Rohzustand unveraendert. **[ABW]** Original setzt den Rohzustand dann auf A, was als "Fahrzeug getrennt" missdeutet werden kann. | Ist §2a, Restumfang §1 Schritt 4 |
| SOLL-CP-07 | **Diodenpruefung:** nur bei laufender PWM (Tastgrad 3..97 %) seit > 100 ms und ohne Stromsperre: Ist das negative Plateau weniger negativ als **-10,0 V** (Betrag <= 9,999 V) ununterbrochen > 20 ms -> Fehler ERR-DIODE, Status **sofort** F. Ausgabe (F7): **3 s -12 V DC** (eindeutiges F fuer das Fahrzeug), danach **+12 V DC**, damit das Abstecken (A) erkannt wird; Fehler bleibt bis CP A. **[ABW]** Original bleibt bei -12 V. Normtext zur Diodenpruefung nicht im Projekt (Pruefung offen); Dauer 3 s [ANN]. | Ist §2a, HB S. 37, F7 |
| SOLL-CP-08 | Der **CP-Status** (nach aussen gemeldet, Ausgangsfunktionen) entsteht aus dem Rohzustand durch Erzwingung (Reihenfolge, erste gilt): (1) Verfuegbarkeit fehlt (SOLL-EN-10) oder ERR-DIODE oder ERR-CONTACTOR -> **F**; (2) ERR-CP-INVALID -> **E** sofort; (3) jeder andere aktive Fehler der Klassen P und V (Abschnitt 11) -> **E**, sofern Rohzustand != A oder Fehler gilt "immer" (Tabelle 11.1); (4) sonst Rohzustand. Der Status wird wie in SOLL-CP-03 entprellt, ausser bei den als "sofort" bezeichneten Faellen. | Ist §2a, Restumfang §1 Schritt 6 |
| SOLL-CP-09 | Fehlerstrom bei Rohzustand A fuehrt **nicht** zu E, sondern startet den RCM-Selbsttest zur Ruecksetzung (SOLL-PROT-11). | Ist §2a (d) |

### 4.2 CP-Ausgabe je Lage

| Lage | CP-Ausgabe | Tastgrad (Promille) |
| --- | --- | --- |
| Initialisierung, kein Fahrzeug, verriegeln, warten auf Freigabe/Strom, Stromsperre, Ladeende, Entriegeln, Fehler (E) | +12 V DC | 1000 |
| PWM an (B2/C2/D2) | 1 kHz | nach SOLL-CUR-20, 5 % im 5-%-Modus |
| Verfuegbarkeit fehlt (F), Schuetzfehler, Weckfolge Phase 1 | -12 V DC | 0 |
| Weckfolge Phase 2 | +12 V DC | 1000 |
| sicherer Zustand (SOLL-RT-20) | -12 V DC (F5) | 0 |
| Diode fehlt | 3 s -12 V DC, danach +12 V DC (F7) | 0, dann 1000 |

### 4.3 PP (Proximity, Klemme PX)

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-PP-01 | PP wird nur mit **D1 an** (Ladefall B, Steckdose) ausgewertet. Ohne D1 (Ladefall C, festes Kabel) gilt als Kabelstrom der Wert des Drehschalters; keine PP-Fehler, keine Kabelabweisung. | HB S. 15, Ist §2a |
| SOLL-PP-02 | Widerstandsklassen nach HB Tab. 6-4: < 75 Ohm -> ungueltig; 75..150 -> 63 A; 150..330 -> 32 A; 330..1000 -> 20 A; 1000..2200 -> 13 A; > 2200 Ohm -> kein Kabel (0 A). Implementierung zunaechst mit den Original-Schwellen in der Original-Skalierung (821/1521/2821/5331/7401) [ANN], Pruefung der Zuordnung Pegel -> Widerstand in H2. Der Wert 63/70 A wird als **63 A** gemeldet. | HB S. 41, Restumfang §2 |
| SOLL-PP-03 | Uebernahme eines neuen Kabelwerts erst, wenn derselbe Kandidat **> 1000 ms** ununterbrochen anliegt; danach erneute Bewertung etwa jede Sekunde. Waehrend der Frist gilt der alte Wert. | Restumfang §2 |
| SOLL-PP-04 | Fehler **ERR-PP-INVALID**: Kandidat "ungueltig" uebernommen, oder Kabelwert 0 uebernommen, waehrend der CP-Rohzustand nicht A und nicht F ist (Fahrzeug erkannt, aber kein Kabel). Ruecksetzung, sobald die Bedingung bei einer Bewertung nicht mehr gilt. | Restumfang §2 |
| SOLL-PP-05 | **Kabelabweisung** nur mit D1 und D2: Kabelwert 13 A -> ERR-CABLE-REJECT; zusaetzlich bei D3 aus: Kabelwert 20 A -> ERR-CABLE-REJECT. Ruecksetzung bei anderem Kabelwert. | HB S. 15, Restumfang §2 |
| SOLL-PP-06 | Kabelwert 0 uebernommen (Stecker gezogen) loescht ERR-LOCK (Verriegelungsfehler). | Restumfang §2 |

Pruefung Abschnitt 4: PC – Bandtabelle mit Grenzwerten (jeder Grenzwert und +-1 mV),
Hysterese je Richtung, 20-ms-Entprellung mit Bloecken von 8 ms (Uebernahme nach dem 3.
abweichenden Block), "CP ungueltig" mit 1,0/1,2-V-Hysterese, Diodenpruefung nur bei PWM,
Verwerfen nach Ausgabewechsel; PP-Klassen, 1-s-Frist, Kabelabweisung fuer alle
Kombinationen D1/D2/D3. H2 – EV-Simulator: Zustaende A..E, Diode entfernt, Kurzschluss
CP-PE, PP-Widerstaende 100/220/680/1500 Ohm sowie < 75 Ohm und offen; gemessene
Klemmenspannungen gegen die Bandgrenzen.

## 5. Ladeautomat

### 5.1 Zustaende

Die Nummerierung ist neu; die Spalte "Original" nennt den entsprechenden Zustand der
Ist-Spez. (Ist §4).

| Zustand | Name | Original | CP-Ausgabe | Schuetz | Bedeutung |
| --- | --- | --- | --- | --- | --- |
| S0 | INIT | Anlauf | +12 V DC | aus | Anlaufverzoegerung, Selbsttest, Entriegeln beim Start |
| S1 | IDLE | Z0 | +12 V DC | aus | kein Fahrzeug (A) bzw. Ruhe |
| S2 | LOCKING | Z1 | +12 V DC | aus | Fahrzeug erkannt bzw. manuelle Verriegelung, Verriegelung laeuft |
| S3 | WAIT | Z3 | +12 V DC (B1) | aus | verriegelt, wartet auf Freigabe, Strom >= 6 A, Verfuegbarkeit |
| S4 | SELFTEST | Z4 | +12 V DC | aus | RCM-Selbsttest vor dem ersten PWM-Start je Steckvorgang |
| S5 | PWM_ON | Z5/Z6 | PWM | aus | B2: wartet auf C/D; enthaelt Teilzustand Weckfolge; im 5-%-Modus Dauerzustand |
| S6 | CHARGING | Z7 | PWM | **ein** | C2/D2: Laden |
| S7 | STOPPING | (in Z7/Z2 implizit) | +12 V DC | ein, bis Fahrzeug reagiert oder Frist | geordnetes Ladeende |
| S8 | UNLOCKING | Z2 | +12 V DC | aus | Entriegeln |
| S9 | ERROR | Z8 | +12 V DC | aus | Fehler (Status E) |
| S10 | UNAVAILABLE | Z9 | -12 V DC | aus | nicht verfuegbar (Status F) |

**[ABW]** S7 ist ein eigener Zustand (im Original verteilt auf Aus(5000)-Wartezeiten in
Z7/Z3/Z2). Zustand 10 des Originals (toter Code) entfaellt.

### 5.2 Vorrangreihenfolge (E7)

Je Zyklus wird zuerst die folgende Liste geprueft; die **erste** zutreffende Zeile
bestimmt die Reaktion. **[ABW]** Im Original ueberschreiben spaetere Zuweisungen fruehere
(Ist §4, Z5/Z7), wodurch z. B. ein Entriegelungswunsch bei CP C ignoriert wird.

| Rang | Bedingung | Reaktion | Quelle |
| --- | --- | --- | --- |
| 1 | Schutzfehler (Klasse P, Abschnitt 7/11) aktiv | Schuetz ist bereits durch den Schutzpfad aus; -> S9 bzw. S10 (Schuetzfehler) | E1, E2, E7a |
| 2 | Konfigurationsfehler (Drehschalter A..F, SOLL-LOCK-03) | -> S9, kein Laden bis Neustart | E11 |
| 3 | CP-Rohzustand E oder F, ERR-CP-INVALID, ERR-DIODE | Schuetz **sofort** aus -> S9 bzw. S10 | Ist §6 (Aus(0) bei CP-Roh A/B/F) |
| 4 | CP-Rohzustand A in S4..S7 | Schuetz **sofort** aus (Selbsttest abbrechen) -> S8 (bzw. S1 ohne Verriegelung) | Ist Z7, SOLL-SM-42 |
| 5 | Fehler der Klasse V mit Sofortabschaltung (Tabelle 11.1) | Schuetz sofort aus -> S9 | F8 |
| 6 | Verfuegbarkeit entzogen | geordnetes Ende (S7) -> S10 | E7b |
| 7 | uebrige Fehler der Klasse V | geordnetes Ende (S7) -> S9 | F8 |
| 8 | Freigabe entzogen oder Stromsperre | geordnetes Ende (S7) -> S3 | E7b |
| 9 | Entriegelungswunsch (D9 mit ML = 0, Entriegeln-Anforderung) | geordnetes Ende (S7) -> S8 | E7c |
| 10 | regulaerer Zustandsablauf (5.3) | | |
| 11 | Stromaenderung | neuer Tastgrad im selben Zyklus | E7d |

### 5.3 Uebergaenge

"CP X" bezeichnet den entprellten Rohzustand, sofern nicht "Status" genannt ist.
"verriegelt" heisst Verriegelungszustand LOCKED oder D4 aus (SOLL-LOCK-01).

| Kennung | Von | Bedingung | Aktion / Nach | Quelle |
| --- | --- | --- | --- | --- |
| SOLL-SM-01 | S0 | ab Ende der Initialisierung **5 s** vergangen und Start-Selbsttest (SOLL-PROT-10) sowie Start-Entriegelung (SOLL-LOCK-20) beendet | -> S1 | E9 **[ABW]** Original 20 s |
| SOLL-SM-02 | S0 | waehrend der Anlaufzeit | CP +12 V DC, Messung laeuft, Ausgangsfunktion 40 blinkt, kein Verriegeln, kein Laden | E9, Ist §12 Code 40 |
| SOLL-SM-10 | S1 (Eintritt und je Zyklus) | – | Loeschen der steckvorgangsbezogenen Fehler (Tabelle 11.1, Ruecksetzen "A"), Zaehler Verriegelungsversuche, Merker Weckfolge und Selbsttest des Steckvorgangs, Entriegeln-Anforderung; Ist-Strom 0 | Ist Z0 |
| SOLL-SM-11 | S1 | verriegelt (LOCKED), D4 an, D9 aus, CP A | Entriegeln -> S8 | Ist Z0 |
| SOLL-SM-12 | S1 | D9 an und Verriegelungswunsch (ML) = 1 | -> S2 | Ist Z0/Z1 |
| SOLL-SM-13 | S1 | CP B, C oder D | -> S2 | Ist Z0 |
| SOLL-SM-20 | S2 | D1 an und Kabelwert 0 | bleibt (wartet auf Stecker), kein Verriegeln | Ist Z1 |
| SOLL-SM-21 | S2 | Entriegeln-Anforderung gesetzt | kein Verriegeln, bleibt bis CP A -> S1 | Ist Z1 (`c8b`), [ANN] |
| SOLL-SM-22 | S2 | D9 an und ML = 0 | nicht verriegelt: -> S1; verriegelt: -> S8 | Ist Z1 |
| SOLL-SM-23 | S2 | CP A und D9 aus | -> S1 (laufenden Verriegelungszyklus geordnet beenden, dann S8 falls verriegelt) | Ist Z1 |
| SOLL-SM-24 | S2 | sonst | Verriegelungszyklus (Abschnitt 8); Erfolg und CP B/C/D -> S3; Erfolg und CP A (nur D9) -> bleibt verriegelt in S2 | Ist Z1 |
| SOLL-SM-30 | S3 | verriegelt, Freigabe (Abschnitt 9), Verfuegbarkeit, keine Stromsperre (Abschnitt 6), kein Fehler, CP B/C/D | Selbsttest dieses Steckvorgangs noch nicht bestanden: -> S4; sonst -> S5 | Ist Z3, E18 |
| SOLL-SM-31 | S3 | CP A | D9 aus: -> S8; D9 an: -> S1 (bleibt verriegelt) | Ist Z3 |
| SOLL-SM-40 | S4 | Selbsttest bestanden | Merker "Selbsttest bestanden" fuer diesen Steckvorgang; -> S5 | Ist Z4, E16, E18 |
| SOLL-SM-41 | S4 | Selbsttest fehlgeschlagen | Schutzfehler ERR-RCM-SELFTEST -> S9 | E16 |
| SOLL-SM-42 | S4 | Rang 1..9 tritt waehrend des Tests ein | Test abbrechen (TEST_IN freigeben), Reaktion nach 5.2; Test gilt als nicht bestanden, kein Fehler | [ANN] |
| SOLL-SM-50 | S5 | Eintritt | PWM mit Tastgrad nach Abschnitt 6; Zeitstempel "PWM an" | Ist Z5 |
| SOLL-SM-51 | S5 | CP C (bzw. D bei "D zulassen") seit Entprellung, PWM seit >= 100 ms aktiv, Bedingungen SOLL-SM-60 erfuellt, kein 5-%-Modus | -> S6 | Ist Z5, Ist §6 (`c2c`) |
| SOLL-SM-52 | S5 | CP D und "D abweisen" an (Default) | Fehler ERR-STATE-D -> S9 (ueber Rang 7) | Ist Z5, Feld 412 |
| SOLL-SM-53 | S5 | 5-%-Modus und CP C/D | bleibt S5, Schuetz bleibt aus, Status "digitale Kommunikation nicht verfuegbar" (SOLL-ERR-30) | E6 |
| SOLL-SM-54 | S5 | CP B ununterbrochen seit Wartezeit `wakeup_delay` (Default 30 s), Weckfolge nach `wakeup_mode` zulaessig, kein 5-%-Modus | Weckfolge (5.4) | E8, E17 |
| SOLL-SM-55 | S5 | CP B ohne laufende PWM (z. B. nach Weckfolge) | **bleibt S5**, kein Rueckfall nach S3 und kein neuer Selbsttest | E18 **[ABW]** Original Z5/Z6 -> Z3 |
| SOLL-SM-60 | S5 -> S6 | Schuetz-Einschaltbedingungen (alle): Status C oder D (D nur ohne "D abweisen"), PWM laeuft >= 100 ms mit Tastgrad nach SOLL-CUR-20, Freigabe, Verfuegbarkeit, keine Stromsperre, kein aktiver Fehler, verriegelt, Selbsttest dieses Steckvorgangs bestanden, keine Schuetzfehler-Erkennung, seit dem letzten Schuetz-Aus >= `contactor_min_off` (Feld 519) | Schuetz ein (SOLL-PROT-01) | Ist §6, [ANN] Deutung 519 |
| SOLL-SM-61 | S6 | je Zyklus | Ladedauer fortschreiben; Tastgrad nachfuehren; bei CP D Ausgangsfunktion "Belueftung" (Code 28) | Ist Z7 |
| SOLL-SM-62 | S6 | CP B (Fahrzeug beendet) | Schuetz **sofort** aus, -> S5 (PWM bleibt an, Fahrzeug darf erneut laden) | Ist Z7, E18, Norm (Abschaltung nach C->B; Zeit ungeprueft) |
| SOLL-SM-63 | S6 | LD faellt weg (D4, D6, LD permanent) > 100 ms | siehe SOLL-LOCK-40 | Ist Z7, F9 |
| SOLL-SM-70 | S7 | Eintritt | CP +12 V DC (PWM aus), Frist `stop_timeout` = **5 s** starten | E7b, HB S. 38 |
| SOLL-SM-71 | S7 | CP-Rohzustand B, A, E oder F, oder Frist abgelaufen | Schuetz aus; danach Ziel aus 5.2 (S3, S8, S9, S10) | E7b, HB S. 38 ("Wenn 5 s nach Abschaltung des PWM-Signals S2 nicht geoeffnet wird, wird der Ladevorgang unabhaengig vom Fahrzeugstatus beendet") |
| SOLL-SM-72 | S7 | Ursache entfaellt vor Schuetz-Aus (z. B. Strom wieder >= 6 A) | Ladeende trotzdem abschliessen (Schuetz aus), dann regulaer ueber S3 neu starten | [ANN] |
| SOLL-SM-80 | S8 | Schuetz aus (immer, da Eintritt nur ueber S7 oder aus Zustaenden ohne Schuetz) | Entriegelungszyklus (Abschnitt 8); Erfolg: CP E -> S9, CP F -> S10, sonst -> S1 | Ist Z2, E10 |
| SOLL-SM-81 | S8 | D9 an, ML = 1, CP nicht E/F, kein Entriegelungs-Fehler | Abbruch -> S2 (erneut verriegeln) | Ist Z2 |
| SOLL-SM-90 | S9 | CP A | Fehler mit Ruecksetzen "A" loeschen -> S1 | Ist Z8 |
| SOLL-SM-91 | S9 | CP B/C/D und kein Fehler mehr aktiv | -> S1 | Ist Z8 |
| SOLL-SM-92 | S9 | Status F (Verfuegbarkeit fehlt, Schuetzfehler) | -> S10 | Ist Z8 |
| SOLL-SM-100 | S10 | Ursache des F entfallen | -> S1 (Neubewertung nach SOLL-CP-05) | Ist Z9 |
| SOLL-SM-101 | S10 | Ursache entfallen, aber anderer Fehler aktiv | -> S9 | Ist Z9 |

Hinweis zu SOLL-SM-62: Das Original faellt in diesem Fall nach Z3 zurueck und loest ueber
Z4 einen neuen Selbsttest aus; E18 schliesst den erneuten Test aus, daher direkt S5.

### 5.4 Weckfolge (E8, E17)

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-SM-110 | Ablauf: Phase 1 CP **-12 V DC fuer 3000 ms**, Phase 2 CP **+12 V DC fuer 750 ms**, danach PWM wie vor der Weckfolge. Schuetz in allen Phasen aus. Nicht blockierend. | Ist Z5, E8 |
| SOLL-SM-111 | Ausloesung: CP B in S5 fuer `wakeup_delay` (Default **30000 ms**, Bereich 0 = aus, 5000..300000 ms) seit PWM-Start. | E17, HB S. 40 |
| SOLL-SM-112 | Wiederholung `wakeup_mode`: 0 = einmal je Steckvorgang (**Default**, wie 1.27); 1 = zusaetzlich erneut nach jeder Stromsperre bzw. Unterbrechung durch die Ladestation (wallbe-Verhalten). Das wallbe-Verhalten ergibt sich mit `wakeup_delay` = 5000 und `wakeup_mode` = 1. | E17, 1.27 §3.2 |
| SOLL-SM-113 | Waehrend Phase 1 gibt es kein positives Plateau (SOLL-CP-04); "CP ungueltig" und Zustandswechsel werden nicht ausgewertet. In Phase 2 wird gemessen; meldet das Fahrzeug C, wird die Weckfolge regulaer bis zum PWM-Start fortgesetzt (kein Schuetz ohne PWM). | Ist §2a, [ANN] |
| SOLL-SM-114 | Rang 1..9 der Vorrangliste brechen die Weckfolge sofort ab. | E7 |

Pruefung Abschnitt 5: PC – Zustandsuebergangstabelle vollstaendig (jede Zeile ein Test),
Vorrangreihenfolge mit gleichzeitig eintretenden Ereignissen (z. B. Entriegelungswunsch +
CP C, Fehler + Stromaenderung), Fristen 5 s/100 ms/Weckfolge mit simulierter Zeit,
Weckfolge in beiden Modi und mit Abbruch, Selbsttest einmal je Steckvorgang,
Anlauf 5 s. Abgleich gegen Ist-Spez. als Regressionsliste (jede **[ABW]** als bewusst
abweichender Test). H2 – Ablauf A->B->C->B->A und Weckfolge am EV-Simulator
(Oszilloskop: CP-Verlauf, Schuetz-Ansteuersignal ohne Netzspannung). H4 – Ladevorgang
mit Fahrzeug.

## 6. Stromvorgabe

### 6.1 Slot-/Minimum-Modell

Alle Stroeme in **mA** (E21). Der wirksame Strom `I` ist das Minimum aller aktiven Slots.
Das entspricht der Stromkette des Originals (Ist §6) und dem Slot-Modell von WARP
(Arch §8).

| Kennung | Slot | Wert | aktiv | Quelle |
| --- | --- | --- | --- | --- |
| SOLL-CUR-01 | `hardware` | Drehschalter: Pos. 1..9 = 6000, 10000, 13000, 16000, 20000, 32000, 63000, 70000, 80000 mA; ueber die Schnittstelle absenkbar (Original Feld 801, F3) | immer | HB S. 16, Ist §10 |
| SOLL-CUR-02 | `cable` | Kabelwert aus PP (13000/20000/32000/63000) bzw. ohne D1 = `hardware` | mit D1 und gueltigem Kabelwert | Ist §6 Schritt 4 |
| SOLL-CUR-03 | `input` | kleinster Wert aller aktiven Eingaenge mit Code 8..15 (6, 10, 13, 16, 20, 32, 63, 70 A) | Eingang aktiv | Ist §6 Schritt 7 |
| SOLL-CUR-04 | `external` | Sollstrom der Schnittstelle (WARP `evse/external_current`, Original Feld 528) | immer, ausser Eingang Code 19 aktiv (F10 entschieden: wie HB) | E21, Arch §8a, HB S. 50 |
| SOLL-CUR-05 | (Platzhalter) | spaeter OCPP, Lastmanagement | nicht implementiert | AG, Arch §6 |

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-CUR-10 | `I` = min(aktive Slots). | Ist §6 |
| SOLL-CUR-11 | **Stromsperre**, wenn `I` < 6000 mA, oder Eingang Code 20 (Pause) aktiv, oder Pause-Anforderung der Schnittstelle (Original Feld 468) gesetzt. Stromsperre -> CP +12 V DC, kein Schuetz (geordnetes Ende, Rang 8). 5100..5999 mA werden **nicht** als Tastgrad < 10 % ausgegeben. | E19, Ist §6 Schritt 9/10 |
| SOLL-CUR-12 | **5-%-Modus** (Drehschalter 0, Schnittstellen-Anforderung Original Feld 401, oder Eingang Code 7): ist keine Stromsperre aktiv, wird Tastgrad **50 Promille** ausgegeben; das Schuetz schliesst in diesem Modus nie; Status "digitale Kommunikation nicht verfuegbar". Die Stromslots sind wirkungslos. **[ABW]** Original: unerreichbar, CCR 1000. | E6, HB S. 39 |
| SOLL-CUR-13 | Plausibilitaet: Ein berechnetes `I` > `hardware` oder > 80000 mA ist ein interner Fehler (Klasse P, ERR-INTERNAL) mit Sofortabschaltung. Konstruktionsbedingt darf das nie auftreten. | E5 |
| SOLL-CUR-14 | Schreibzugriffe auf Strom-Slots: zulaessig 0 oder 6000..80000 mA; andere Werte werden **mit Fehlermeldung abgelehnt**. Ein Wert ueber dem aktuellen `hardware`-Grenzwert wird angenommen und wirkt ueber das Minimum (kein Fehlerzustand). | E14, F11 entschieden |
| SOLL-CUR-15 | Startwert von `external` nach jedem Start (Erststart und Neustart): Parameter `external_current_default`, **Default 0** = kein Laden, bis die Schnittstelle (evcc) oder der Nutzer einen Strom setzt **[ABW]** (Original 528 = Drehschalterwert). Fuer Betrieb ohne Schnittstelle z. B. auf 80000 stellen (dann begrenzt nur Drehschalter/Kabel). Aenderungen von `external` werden **nicht** gespeichert. Grund: Nach einem Neustart waehrend des Ladens sendet evcc seinen Wert nicht erneut, solange es den Slot fuer freigegeben haelt (`external_current` > 0); mit 0 erkennt evcc die Abweichung und setzt seinen Strom neu (in der Simulation geprueft, 2026-10-20). | Arch §8 (kein Flashschreiben), F12 (2026-10-19, geaendert 2026-10-20) |
| SOLL-CUR-16 | Aenderungen von `I` waehrend PWM an wirken im selben Automatenzyklus auf den Tastgrad; Wechsel ueber/unter 6 A fuehrt ueber Stromsperre/Neustart der PWM (S7 -> S3 -> S5). | E7d |

### 6.2 Tastgrad

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-CUR-20 | Ausgegeben wird der **groesste** Tastgrad (Promille, ganzzahlig), dessen Strom nach HB Tab. 6-3 `I` nicht uebersteigt: 6000 <= `I` <= 51000: `d` = floor(`I` / 60); 51000 < `I` < 52500: `d` = 850; 52500 <= `I` <= 80000: `d` = floor(`I` / 250) + 640. Beispiele: 6000 -> 100; 16000 -> 266 (15,96 A); 32000 -> 533; 80000 -> 960. | HB S. 39, Ist §6 Schritt 11 |
| SOLL-CUR-21 | Gemeldet werden `I` (wirksamer Sollstrom, mA) und der dem Tastgrad entsprechende Strom `allowed_charging_current` (mA). | Arch §8 |
| SOLL-CUR-22 | **[ABW]** Das Original rundet bei 51,1..52,4 A auf einen kleineren Tastgrad (z. B. 51,1 A -> 844); die Soll-Regel liefert in diesem Bereich 850 (= 51,0 A). Beide bleiben unter dem Sollstrom. | Ist §6 |

Pruefung Abschnitt 6: PC – Minimum aller Slot-Kombinationen, Code-Reihenfolge der
Eingaenge, 5-%-Modus aus allen drei Quellen mit/ohne Stromsperre, Grenzen 5999/6000,
51000/51001/52499/52500, 80000, Ablehnung ungueltiger Schreibwerte, Plausibilitaetsfehler
durch Fehlerinjektion. H1 – Tastgrad am Oszilloskop fuer 6/10/16/32/63/80 A und 5 %.

## 7. Schutzpfad

Grundlage: [RCM/Bender](../../wallbe2/docs/rcm-bender.md), Ist §7, 1.27 §3.4, E1, E2, E4, E12, E16, E22.
**Sicherheitshinweis:** Die Soll-Reaktionszeiten betreffen nur die Firmware; die
Abschaltzeit des Gesamtsystems (Relais, Schuetz) und die Frage, ob SWITCH_1/2 zusaetzlich
direkt auf den Schuetztreiber wirken, sind Hardwarefragen ([Hardwarefragen](../../wallbe2/docs/hardware-offene-fragen.md)).

### 7.1 Schuetz-Schnittstelle

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-PROT-01 | Das Schuetz (PE2) wird ueber genau eine Schnittstelle geschaltet: **Aus** darf jede Stelle (Schutz-ISR, Schutzthread, Automat, Ausnahmebehandlung) jederzeit; **Ein** nur der Automat in S6 und nur, wenn kein Schutzfehler verriegelt ist. Ein Schutzfehler setzt eine Sperre, die Ein-Befehle bis zur Ruecksetzung (Abschnitt 11) wirkungslos macht. | E2 |
| SOLL-PROT-02 | Mit jedem Schuetz-Aus durch den Schutzpfad wird zugleich die CP-Ausgabe auf +12 V DC gesetzt (PWM aus), Ausnahme Schuetzfehler (-12 V DC). | Ist §7, E7a |
| SOLL-PROT-03 | Der Schuetzzustand (Soll und Ansteuerpegel) ist als Status lesbar. | Ist §12 Code 27 |

### 7.2 Fehlerstrom (RCM)

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-PROT-04 | Die drei Sensorausgaenge **SWITCH_1 (PE3), SWITCH_2 (PE4), ERR_OUT (PE6)** werden in **jedem** Zustand ausgewertet, auch in S0 und S1. Jeder "ausgeloest"-Pegel schaltet sofort ab (Schuetz aus nach SOLL-PROT-01/02). | E1 **[ABW]** Original: SWITCH_1 nur bei CP A/B und Sensorprofil 0 |
| SOLL-PROT-05 | Erfassung: Flankeninterrupt (EXTI) auf jeden Wechsel nach "ausgeloest" **und** zyklische Pegelkontrolle im Schutzthread (1 ms). Software-Reaktionszeit vom Signalwechsel bis PE2 Low **<= 1 ms** (ISR-Pfad), Rueckfallebene zyklisch <= 2 ms. | E2, F4 |
| SOLL-PROT-06 | Aktive Pegel ("ausgeloest" = High an der MCU, Inversion zum Bender-Ausgang) sind eine Eigenschaft der Steuerung und werden in der Board-Beschreibung festgelegt, nicht als Laufzeitparameter. Bestaetigung in H3. | [HYP] RCM/Bender |
| SOLL-PROT-07 | Fehlerbits: ERR_OUT -> ERR-RCM-SENSOR; SWITCH_2 -> ERR-RCM-DC6 (Fehlerstrom >= 6 mA DC bzw. 30 mA AC im IEC-Modus); SWITCH_1 -> ERR-RCM-AC30 (30 mA rms). Gemeinsamer Sammelzustand "Fehlerstrom" (Original R107 Bit 15). Bei gleichzeitigen Signalen werden alle zutreffenden Bits gesetzt **[ABW]** (Original: nur ein Zweig je Aufruf). | Ist §7, rcm-bender |
| SOLL-PROT-08 | Fehlerstromfehler sind **verriegelnd**: Sie bleiben gesetzt, auch wenn der Sensor sich selbst wieder einschaltet (Auto-Reclose). Ruecksetzung nur durch einen bestandenen Selbsttest im CP-Rohzustand A (SOLL-PROT-11). | rcm-bender Vorschlag 3, Ist §2a (d) |

### 7.3 RCM-Selbsttest (E16)

Zeitfolge nach Phoenix 1.27 (`0x0803bbb8`, Decompilat `analysis/ghidra-1.27/export/decomp.c`,
am 2026-10-19 gelesen), Zeitpunkte ab Teststart:

| t | Schritt | Fehlschlag -> |
| --- | --- | --- |
| 0 ms | Vorbedingung: ERR_OUT, SWITCH_1, SWITCH_2 nicht ausgeloest, Schuetz aus | Test nicht starten (Fehler bleibt bestehen) |
| 100 ms | ERR_OUT erneut pruefen | ERR-RCM-SENSOR + ERR-RCM-SELFTEST |
| 200..300 ms | TEST_IN (PE5) Low fuer **100 ms** | – |
| 1000 ms | SWITCH_2 muss ausgeloest sein (Bender M1) | ERR-RCM-SELFTEST + ERR-RCM-DC6 |
| 1700 ms | SWITCH_1 muss ausgeloest sein (M2) | ERR-RCM-SELFTEST + ERR-RCM-AC30 |
| 2400 ms | SWITCH_1 und SWITCH_2 duerfen nicht beide noch ausgeloest sein (M3) | ERR-RCM-SELFTEST |
| bis 3400 ms | **[ABW]** beide wieder nicht ausgeloest (vollstaendige Rueckkehr vor Ladefreigabe) | ERR-RCM-SELFTEST |

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-PROT-10 | Der Selbsttest laeuft nach der Zeitfolge oben, nicht blockierend, nur bei Schuetz aus. Erwartete Ausloesungen waehrend des Tests sind keine Fehlerstromfehler; jede Abweichung setzt **sofort** ERR-RCM-SELFTEST (Klasse P) und sperrt die Freigabe. | E16 |
| SOLL-PROT-11 | Anlaesse: (1) beim Start (S0); (2) vor dem ersten PWM-Start je Steckvorgang (S4); (3) zur Ruecksetzung eines Fehlerstrom-/Selbsttestfehlers im CP-Rohzustand A, fruehestens `rcm_retest_interval` = **30 s** nach dem letzten Versuch (F13). Nicht bei Rueckkehr der PWM innerhalb eines Steckvorgangs. | E16, E18, F13 |
| SOLL-PROT-12 | Ein bestandener Test loescht alle RCM-Fehlerbits. | 1.27 `0x0803bbb8` |
| SOLL-PROT-13 | Das zweite Antwortprofil des Originals (`cf3` = 1, Reaktion 390..710 ms, SWITCH_1 ignoriert) wird **nicht** uebernommen. **[ABW]** wie 1.27. | E16, rcm-bender |

### 7.4 Schuetzueberwachung (Feld 390)

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-PROT-20 | Modus `contactor_monitor`: 0 aus; 1 **Via Sense** (Klemme CT, PE0); 2 Rueckmeldekontakt NO (Eingang Code 5); 3 Rueckmeldekontakt NC (Eingang Code 6); 4 Via Energy Meter. Default **1** (E20). | Ist §7, takt-adc-gpio |
| SOLL-PROT-21 | Pruefung nur bei ausgeschaltetem Schuetz, beginnend `contactor_check_delay` (Vorschlag 200 ms) nach dem Aus-Befehl. Fehler ERR-CONTACTOR, wenn ununterbrochen >= 250 ms gilt: Modus 1 – letzte steigende Flanke an PE0 juenger als 100 ms; Modus 2 – Eingang Code 5 aktiv; Modus 3 – Eingang Code 6 inaktiv. (Original: Zaehler > 9 bei 25-ms-Zyklus, also ~250 ms.) | Ist §7, [ANN] Verzoegerung |
| SOLL-PROT-22 | Modus 4: jede Phase einzeln, Fehler wenn **irgendeine** Phase > 50 V. **Nur mit Messquelle** (F1); ohne Messquelle wird das Setzen von Modus 4 abgelehnt. **[ABW]** Original wertet nur alle drei Phasen gemeinsam. | E4 |
| SOLL-PROT-23 | Wirkung ERR-CONTACTOR: Klasse P, CP-Status F, kein erneutes Einschalten. Ruecksetzung **nur durch Neustart oder ausdrueckliche Quittierung** (Web/Shell), und nur wenn der Fehler nicht mehr erkannt wird (F14). **[ABW]** Original loescht das Bit, sobald die Erkennung entfaellt. | Ist §7, F14 |
| SOLL-PROT-24 | **D5:** Ist D5 an und ERR-CONTACTOR gesetzt, wird PE1 **200 ms** spaeter auf Low gesetzt. Befund 2026-10-19: Im Original wird PE1 nur Low geschrieben (GPIO-Init und dieser Pfad; kein High-Schreiber in SL und 1.27 gefunden); die Funktion ist damit im Original ohne sichtbare Wirkung. Entschieden (F15): **wie Original** (PE1 bleibt Low, D5 ohne sichtbare Wirkung); nach Klaerung des PE1-Ziels an der Hardware (H1, Kandidat Relais C3/C4) neu entscheiden. | E12, Decompilat SL Zeile 38338, 1.27 Zeile 39145 |

### 7.5 Ueberstrom (Feld 409), nur mit Messquelle

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-PROT-30 | Die Ladelogik bietet einen Eingang fuer Phasenstroeme (mA, je Phase, mit Zeitstempel/Gueltigkeit). Ohne Messquelle ist die Ueberstromerkennung **nicht verfuegbar**; Parameter `overcurrent_enable` ist dann nur 0 schreibbar. | F1 |
| SOLL-PROT-31 | Mit Messquelle: Fehler ERR-OVERCURRENT, wenn ein Phasenstrom >= 1,25 x Tastgrad-Strom fuer 5 s oder >= 1,1 x fuer 95 s ueberschreitet. Ruecksetzung bei CP A. | E22, Ist §7 |
| SOLL-PROT-32 | Ausfall der Messquelle bei aktivierter Ueberstromerkennung -> ERR-METER-COMM. | Ist §7 (c7b.1) |

### 7.6 Versorgung und interne Pruefungen

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-PROT-40 | Versorgungswert (ADC IN10/PC0, Original-Skalierung, Bedeutung [HYP] 12-V-Schiene): > 10,0 = gut, < 9,0 = zu niedrig, dazwischen Hysterese. | Ist §3, takt-adc-gpio |
| SOLL-PROT-41 | Versorgung zu niedrig fuer > 100 ms -> **Warnung** WARN-SUPPLY (keine Abschaltung, kein Fehlerzustand, wie Original). Die RCM-Ueberwachung bleibt **immer** aktiv (E1). Ruecksetzung nach 2500 ms ununterbrochen "gut"; **kein** Sprung in den Grundzustand. **[ABW]** Original setzt die RCM-Ueberwachung bei Unterspannung aus (ausser in Z7) und kehrt nach 2500 ms nach Z0 zurueck. | Ist §3, F16 |
| SOLL-PROT-42 | Der Schutzpfad selbst (EXTI-Konfiguration, Pegel PE2) wird zyklisch auf Plausibilitaet geprueft (z. B. Ausgangsregister PE2 entspricht Sollzustand); Abweichung -> ERR-INTERNAL. | E3, [ANN] |

Pruefung Abschnitt 7: PC – Schuetz-Schnittstelle (Ein-Sperre), Selbsttest-Zeitfolge mit
simulierten Pegeln (alle Fehlschlagpunkte, Abbruch), Verriegelung der RCM-Fehler,
Schuetzueberwachung Modi 1..3 mit Zeitreihen, Modus-4-Ablehnung, Ueberstrom mit
simulierter Messquelle, Versorgungshysterese. H1 – Latenz EXTI -> PE2 mit Testsignal am
Oszilloskop (Signalgenerator statt Sensor, ohne Netzspannung); D5/PE1-Pegel. H3 –
Selbsttest mit echtem Sensor, Pruefstrom 6 mA DC und 30 mA AC im sicheren Pruefaufbau;
Abschaltzeit Gesamtsystem messen.

## 8. Verriegelung

Grundlage: Ist §9, E10, E13, E15, HB S. 9 (Ansteuerung 12 V, max. 1 A, 500 ms Default bis
3 s; Rueckmeldung "potenzialfreier Kontakt, geschlossen bei erfolgreicher Verriegelung";
"automatische Entriegelung bei Stromausfall"), [Firmwarevergleich 1.27 §3.5](../../wallbe2/docs/firmware-vergleich-1.27.md#35-verriegelung-und-dip-schalter).

### 8.1 Verriegelungszustand

Zustaende: INIT, UNLOCKED, LOCKING, LOCKED, UNLOCKING, ERROR (entspricht WARP
`lock_state` 0..5). Persistent gespeichert wird der zuletzt erreichte Endzustand
(Original Feld 465), damit nach einem Neustart bekannt ist, ob entriegelt werden muss.

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-LOCK-01 | **D4 aus:** keine Aktorimpulse; ein Verriegelungs-/Entriegelungswunsch setzt den Zustand sofort auf LOCKED bzw. UNLOCKED. | Ist §9 |
| SOLL-LOCK-02 | **D4 an:** Verriegelungszyklus nach 8.2. D4 und D6 wirken immer; es gibt kein Feld 485. | E13, E15 |
| SOLL-LOCK-03 | **D6 an:** Die LD-Funktion liefert der erste der Eingaenge LD, EN, ML, XR, IN (in dieser Reihenfolge) mit Funktionscode **22** (gepulst) bzw., falls keiner, **3** (permanent). Die Auswahl erfolgt ueber Feld 520 bzw. die Funktionsparameter der Eingaenge. Ist D6 an und kein Eingang mit Code 3 oder 22 konfiguriert -> Konfigurationsfehler ERR-CONFIG (kein Laden, F17 entschieden). **[ABW]** Original: Verriegelung schlaegt dann still fehl. | E15, Ist §9, F17 |

### 8.2 Verriegelungszyklus (nicht blockierend, E10)

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-LOCK-10 | Zwischen dem Ende eines Zyklus und dem Beginn des naechsten liegen mindestens `lock_cycle_gap` ms (Feld 527, 500..10000, Default 2000). | Ist §9 Schritt 3 |
| SOLL-LOCK-11 | Verriegeln: PA9 Low fuer `lock_pulse` ms (Feld 525, 100..3000, Default 500), dann High. Entriegeln: PA10 Low fuer `unlock_pulse` ms (Feld 526, 100..3000, Default 500), dann High. PA9 und PA10 sind **nie gleichzeitig** Low; zwischen zwei Impulsen mindestens 10 ms beide High. | Ist §9 Schritt 4, HB S. 9, [ANN] Gegenseitigkeit |
| SOLL-LOCK-12 | Ohne D6: Nach dem Impuls gilt der Zielzustand als erreicht. | Ist §9 Schritt 6 |
| SOLL-LOCK-13 | D6 mit **LD permanent (Code 3):** 500 ms nach Impulsende wird LD gelesen: Verriegeln erfolgreich, wenn LD aktiv; Entriegeln erfolgreich, wenn LD inaktiv. | Ist §9 Schritt 6, E15 |
| SOLL-LOCK-14 | D6 mit **LD gepulst (Code 22):** Waehrend des Verriegelungsimpulses wird der LD-Rohpegel in jedem Messblock abgetastet. Erfolg, wenn LD mindestens **10 ms** (mindestens zwei aufeinanderfolgende Messbloecke) aktiv war und danach innerhalb des Impulsfensters wieder inaktiv wird. Beim Entriegeln gilt LD nach dem Impuls als inaktiv (keine Auswertung). **Keine** Flankenauswertung ausserhalb der Impulse **[ABW]** gegenueber 1.27 (Flankentoggle), wie SL. | E15, Ist §9 Schritt 4, 1.27 §3.5 |
| SOLL-LOCK-14a | Hintergrund (Fremdquelle, Hypothese): Beim Kuester-Aktor (02S/03S, Phoenix EV-T2M3S-E-LOCK12V) liegt die Rueckmeldung in beiden Endlagen auf 0 V und waehrend der Fahrt auf +12 V. Code 22 erkennt damit "Motor ist von einer Endlage in die andere gefahren". Das gleiche Kriterium ist beim **Entriegeln** als Option `unlock_check` auswertbar (F27: Default **aus** = wie Original; einschalten erst nach Messung des Signals in H1). | [Hardwarefragen](../../wallbe2/docs/hardware-offene-fragen.md#verriegelung-ld-rueckmeldung-aus-firmwarevergleich-127-2026-10-19) |
| SOLL-LOCK-15 | Abtastrate fuer LD gepulst: Ist die Impulsdauer des Rueckmeldekontakts kuerzer als zwei Messbloecke, wird LD waehrend eines Verriegelungsimpulses mit 1 ms abgetastet (eigene ADC-Abfrage). Entscheidung nach Messung in H1. | [ANN], Hardwareerinnerung LD-Signalform |
| SOLL-LOCK-16 | Ein Zyklus endet nach dem Ergebnis plus 10 ms; der Zeitpunkt des Endes ist Bezug fuer SOLL-LOCK-10. Waehrend eines Zyklus laufen Schutzpfad, CP-Auswertung und Automat unveraendert weiter. | E10, Ist §9 Schritt 5 |
| SOLL-LOCK-17 | Ein laufender Impuls wird nur durch den sicheren Zustand abgebrochen (beide Ausgaenge High); der Zyklus gilt dann als fehlgeschlagen. | [ANN] |

### 8.3 Ablauf, Fehlversuche, Entriegeln

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-LOCK-20 | Beim Start (S0) mit D4: ein Entriegelungszyklus, unabhaengig vom gespeicherten Zustand (wie Original-Lade-Init `FUN_08039cd8`: `Lock(0)` nach dem Selbsttest). Ist das Ergebnis "verriegelt", gilt ERR-UNLOCK. | Decompilat SL `0x08039cd8` (2026-10-19 gelesen) |
| SOLL-LOCK-21 | Verriegeln in S2: bis zu **5** Versuche; nach jedem Fehlschlag ERR-LOCK setzen und einen Entriegelungszyklus ausfuehren, dann erneut versuchen; nach dem 5. Fehlschlag -> S9 mit ERR-LOCK (bis CP A bzw. Kabelwert 0). Erfolg loescht ERR-LOCK. ERR-LOCK erzwingt E erst ab dem 3. Fehlschlag (bzw. sofort mit D9). | Ist Z1, Restumfang §1 Schritt 6 |
| SOLL-LOCK-22 | Entriegeln (S8) **nur bei ausgeschaltetem Schuetz und CP +12 V DC** (keine PWM). Bis zu **4** Versuche; nach jedem Fehlschlag ERR-UNLOCK setzen und erneut verriegeln; nach dem 4. Fehlschlag -> S9 mit ERR-UNLOCK, Zustand bleibt LOCKED. | E10, Ist Z2 |
| SOLL-LOCK-23 | **D9 aus** (automatisch): Verriegeln bei Fahrzeug erkannt (CP B/C/D) und gueltigem Kabelwert; Entriegeln bei CP A. | HB S. 15, Ist Z0/Z1 |
| SOLL-LOCK-24 | **D9 an** (manuell): Verriegelungswunsch = Funktion ML (Eingang Code 4 Pegel bzw. Code 17 Umschalten bei steigender Flanke, Mindestabstand 200 ms) oder Schnittstellenwunsch (Original Feld 403). Wunsch 1 -> verriegeln (auch ohne Fahrzeug); Wunsch 0 -> geordnetes Ladeende, dann entriegeln. | HB S. 15, Ist §10, E7c |
| SOLL-LOCK-25 | **Entriegeln-Anforderung** (Original Feld 440, unabhaengig von D9): beendet einen Ladevorgang geordnet, entriegelt und verhindert ein erneutes Verriegeln bis CP A. | Ist Z1/Z3/Z5/Z7, E7c |
| SOLL-LOCK-26 | Ein Verriegelungszyklus waehrend S6 ist nur als **Wiederverriegeln** zulaessig (SOLL-LOCK-40); ein Entriegelungsimpuls bei eingeschaltetem Schuetz ist unzulaessig und wird von der Aktorschnittstelle abgewiesen (ERR-INTERNAL). | E10 |

### 8.4 LD-Ueberwachung waehrend des Ladens

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-LOCK-40 | Mit D4, D6 und **LD permanent**: LD inaktiv > 100 ms in S6 -> ERR-LD-LOST. Entschieden (F9): Schuetz **sofort** aus (Verriegelung nicht gesichert), PWM aus, ein Wiederverriegelungszyklus; Erfolg -> Fehler loeschen und ueber S3 neu starten (ohne neuen Selbsttest); Misserfolg -> S9 bis CP A. **[ABW]** Original: blockierendes Wiederverriegeln bei moeglicherweise eingeschaltetem Schuetz, Ladeende erst ueber erzwungenes E. | Ist Z7, E10, F9 |
| SOLL-LOCK-41 | Mit LD gepulst entfaellt die Ueberwachung waehrend des Ladens (der Rueckmeldewert aendert sich nur im Verriegelungszyklus). | Ist §9, 1.27 §3.5 |

Pruefung Abschnitt 8: PC – Zyklus mit simulierter Zeit fuer D4/D6/Code 3/Code 22,
Mindestabstand 527, Impulslaengen 100/500/3000 ms, nie gleichzeitig Low, Fehlversuche
5/4, Start-Entriegelung, D9 mit Code 4/17/Schnittstelle, Entriegeln-Anforderung,
LD-Verlust im Laden; nachweisen, dass Schutzreaktionen waehrend eines Zyklus nicht
verzoegert werden. H1 – Impulse an R1/R2 (Oszilloskop, Aktor angeschlossen oder
Ersatzlast), Signalform des LD-Kontakts der vorhandenen Steckdose (gepulst/permanent,
Impulse beim Entriegeln, Dauer). H2 – Ablauf mit EV-Simulator und Stecker.

## 9. Ein-/Ausgangsfunktionen und Freigabelogik

### 9.1 Eingangsfunktionen (LD, EN, ML, XR, IN)

Jeder der fuenf Eingaenge hat einen Funktionscode (Parameter `in_function_<klemme>`,
Original Felder 520..524). Codes und Texte nach `input_list` (`status.html`) und HB
Tab. 8-2 (S. 49/50); Wirkung nach [Taktbaum §Digitale Eingaenge](../../wallbe2/docs/takt-adc-gpio.md#digitale-eingaenge-0x0803c7d4-korrigiert).

| Code | Funktion | Soll-Wirkung |
| --- | --- | --- |
| 0 | keine | – |
| 1 | EN permanent | Freigabe aktiv, solange Eingang ein |
| 16 | EN gepulst | jede steigende Flanke schaltet die EN-Freigabe um, Mindestabstand 200 ms |
| 18 | EN mit Portal-Logging | wie 1 (Portal nicht im Umfang; F18 entschieden) |
| 2 | XR | Verfuegbarkeit (mit D8) |
| 3 | LD permanent | Verriegelungsrueckmeldung als Pegel (Abschnitt 8) |
| 22 | LD gepulst | Verriegelungsrueckmeldung als Impuls waehrend des Verriegelungsimpulses (Abschnitt 8); sonst ohne Wirkung |
| 4 | ML permanent | steigende Flanke: Verriegelungswunsch 1, fallende Flanke: 0 (mit D9) |
| 17 | ML gepulst | steigende Flanke schaltet den Verriegelungswunsch um, Mindestabstand 200 ms |
| 5 / 6 | Schuetzrueckmeldung NO / NC | Eingang fuer Schuetzueberwachung Modus 2/3 |
| 7 | 5 % | 5-%-Modus (SOLL-CUR-12) |
| 8..15 | Maximal Current 6/10/13/16/20/32/63/70 A | Slot `input` (SOLL-CUR-03) |
| 19 | Maximal Current | Slot `external` wird ignoriert (HB S. 50: "Begrenzungen ueber Modbus TCP werden ignoriert"), F10 |
| 20 | Pause (Suspended EVSE) | Stromsperre (SOLL-CUR-11) |
| 21 | Error Input | Fehler ERR-INPUT solange aktiv (Original R155 Bit 2) |

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-FN-01 | Codes ausserhalb {0..22} werden beim Schreiben abgelehnt. | E14, Restumfang §3 |
| SOLL-FN-02 | Haben mehrere Eingaenge dieselbe Funktion, gilt fuer Pegelfunktionen ODER (aktiv, wenn irgendeiner aktiv ist), fuer Flankenfunktionen jede Flanke an jedem dieser Eingaenge. **[ABW]** Original: zuletzt ausgewerteter Kanal gewinnt. | F26 entschieden |
| SOLL-FN-03 | Verriegelungswunsch (ML) kann ausserdem ueber die Schnittstelle gesetzt werden (Original Feld 403); das juengste Ereignis (Flanke oder Schreibzugriff) gilt. | Ist §2 |

### 9.2 Freigabe und Verfuegbarkeit

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-EN-01 | **Ladeerlaubnis** liegt vor, wenn mindestens eine Bedingung gilt: (a) D7 an und EN-Funktion aktiv; (b) D10 an und Schnittstellen-Freigabe (`enable_register`, Original Feld 400) = 1; (c) weder D7 noch D10 an. **[ABW]** Die RFID-Whitelist (`c87`) und OCPP-Autorisierung (`c88`) entfallen (nicht im Umfang). | Ist §5, HB S. 15 |
| SOLL-EN-02 | **Freigabe** = Ladeerlaubnis und keiner der Sperrgruende: ERR-CABLE-REJECT, nicht verriegelt (D4 an und Zustand != LOCKED), Fehlerstrom-/RCM-Fehler, ERR-CONTACTOR, sowie jeder weitere aktive Fehler der Klassen P/V/K. | Ist §5 (erweitert) |
| SOLL-EN-03 | Startwert `enable_register` nach Neustart = Parameter `enable_register_default` (Default 0 wie Original, `.data` `c86` = 0; F19 entschieden). | Datei `0x4cdf6` = 0 |
| SOLL-EN-10 | **Verfuegbarkeit** fehlt, wenn (D8 an und XR-Funktion inaktiv) oder (D10 an und `available_register` (Original Feld 402) != 1). Folge: CP-Status F, -12 V DC (nach geordnetem Ende, Rang 6). | Ist §2a, Ist §6, HB S. 15 |
| SOLL-EN-11 | Startwert `available_register` nach Neustart: 1 (Original: `.data` `c89` = 1, Lade-Init setzt 1). | Datei `0x4cdf9` = 1, Decompilat `0x08039cd8` |
| SOLL-EN-12 | Die evcc-Anbindung (Slot `external`) ist von der Freigabe unabhaengig: `external` = 0 bewirkt Stromsperre, nicht Entzug der Freigabe. Mit D10 an ist zusaetzlich `enable_register` = 1 noetig. | Arch §8a, [ANN] |

### 9.3 Ausgangsfunktionen (ER, LR, VR, CR)

Parameter `out_function_<klemme>` (Original Felder 327..330), Auswertung in jedem
Automatenzyklus. "ohne Fehler" = kein aktiver Fehler; "Status" = CP-Status (SOLL-CP-08).
Codes nach Ist §12 und HB Tab. 8-3 (S. 50).

| Code | Bedingung fuer High |
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
| 18 / 19 | gueltiger / kein gueltiger Kabelwert |
| 20..23 | Kabelwert 13 / 20 / 32 / 63 A |
| 24 / 25 | Kabelwert in {13, 20} / {13, 20, 32} |
| 26 | Kabel abgewiesen |
| 27 | Schuetz ein |
| 28 | Belueftung (S6 mit Status D) |
| 29 | Verriegelungsanforderung (letzter Verriegelungswunsch = verriegeln) |
| 30..33 | Schnittstellen-Ausgang 1..4 (Original Coils 405..408), Startwert 0 |
| 34 / 35 / 36 | ERR-OVERCURRENT / ERR-CONTACTOR / ERR-STATE-D |
| 37 | (B ohne Fehler) oder C oder D |
| 38 | A/B mit aktivem Eingangscode 19; Nachlauf 1 s nach Schuetz aus |
| 39 | Freigabe erteilt und (C oder D, oder B nach vorheriger Stromsperre) **[ABW]** ohne RFID-Blinken (Feld 430) |
| 40 | waehrend S0 1-Hz-Blinken, danach wie 9 |
| 41 | A oder B ohne Fehler und PWM aus **[ABW]** ohne RFID-Blinken |
| 42 | S6 mit Status B bei laufender PWM ("Suspended EV", 1.27) |
| sonst | Low; Schreiben abgelehnt (gueltig 0..42) |

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-FN-10 | Alle Codes 0..42 werden unterstuetzt (Tabelle). Defaults: ER 15, LR 29, VR 16, CR 17. | E20, Ist §12 |
| SOLL-FN-11 | Im sicheren Zustand behalten die Ausgaenge ihre Funktion; ER (Default 15) zeigt den Fehler. | [ANN] |

Pruefung Abschnitt 9: PC – Wahrheitstabelle jedes Ausgangscodes ueber alle Status- und
Fehlerkombinationen, jeder Eingangscode (Pegel, Flanke, 200-ms-Sperre), Freigabematrix
D7/D10/EN/400, Verfuegbarkeit D8/D10/XR/402. H1 – Pegel an ER/LR/VR/CR, Eingaenge mit
Labornetzteil (3/9 V), Schluesselschalter an EN (wallbe, Beispielkonfiguration).

## 10. Parameter

Spalten: **P** = persistent (Parameterspeicher, Abschnitt 13), **B** = haengt von der
aeusseren Beschaltung ab (Beispielwerte in [Geraetekonfiguration](../../wallbe2/docs/geraetekonfiguration.md)).
Alle Schreibzugriffe mit Grenzpruefung; ungueltige Werte werden mit Fehlermeldung
abgelehnt (E14). Namen sind Vorschlaege (WARP-angelehnt, englisch).

### 10.1 Konfiguration

| Name | Einheit | Bereich | Default | P | Feld (Original) | B | Quelle |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `out_function_er` / `_lr` / `_vr` / `_cr` | Code | 0..42 | 15 / 29 / 16 / 17 | ja | 327 / 328 / 329 / 330 | ja | E20 |
| `in_function_ld` / `_en` / `_ml` / `_xr` / `_in` | Code | 0..22 | 3 / 1 / 4 / 2 / 11 | ja | 520 / 521 / 522 / 523 / 524 | ja | E15, E20 |
| `lock_pulse` | ms | 100..3000 | 500 | ja | 525 | ja | Ist §9, HB S. 9 |
| `unlock_pulse` | ms | 100..3000 | 500 | ja | 526 | ja | Ist §9 |
| `lock_cycle_gap` | ms | 500..10000 | 2000 | ja | 527 | ja | Ist §9 |
| `contactor_min_off` | ms | 0..2000 | 500 | ja | 519 | ja | Ist §8, Deutung [ANN] |
| `contactor_monitor` | Code | 0..4 (4 nur mit Messquelle) | 1 | ja | 390 | ja | E20, SOLL-PROT-22 |
| `reject_state_d` | 0/1 | 0..1 | 1 | ja | 412 | nein | Ist §13 |
| `overcurrent_enable` | 0/1 | 0..1 (1 nur mit Messquelle) | 0 | ja | 409 | nein | SOLL-PROT-30 |
| `wakeup_delay` | ms | 0 (aus), 5000..300000 | 30000 | ja | – | nein | E17 |
| `wakeup_mode` | Code | 0 einmal je Steckvorgang, 1 auch nach Stromsperre | 0 | ja | – | nein | E17 |
| `external_current_default` | mA | 0, 6000..80000 | 0 | ja | – | nein | F12 |
| `enable_register_default` | 0/1 | 0..1 | 0 | ja | – (neu, F19) | nein | F19 |
| `net_dhcp`, `net_ip`, `net_netmask`, `net_gateway`, `net_hostname` | – | IPv4 / Text | DHCP an (F25), statisch 192.168.0.8/24, 192.168.0.1, `wallfree-<MAC-Ende>` | ja | 404 (DHCP), 315..318 / 319..322 / 323..326 (IP / Maske / Gateway), laut Original-Webseite `lan.html` | nein | F25 |
| `telnet_enable` | 0/1 | 0..1 | 1 | ja | – | nein | F20 (spaeter Default 0 oder Telnet entfernen) |
| `web_user`, `web_password` | Text | Benutzer 1..32 Zeichen, Passwort 8..64 Zeichen (gespeichert als Hash) | `admin` / Default-Passwort, **Aenderung beim ersten Zugriff erzwungen** | ja | – | nein | F20 |
| `unlock_check` | 0/1 | 0..1 | 0 | ja | – | ja | F27 |

Netz- und Systemparameter (`net_*`, `telnet_enable`) wirken **nach einem Neustart**
(`POST /system/restart` oder Shell); bis dahin laeuft die beim Start angewandte Konfiguration.
Pruefung: Einzeladresse (1..223, nicht 127), Netzmaske zusammenhaengend /8../30, Gateway im Netz
der Adresse oder 0.0.0.0, Hostname 1..32 Zeichen aus Buchstaben, Ziffern, '-' (leer =
automatisch). Ist die gespeicherte feste Adresse widerspruechlich, startet das Netz mit DHCP.
`telnet_enable` = 0 stoppt die Kommandoverarbeitung; Port 23 bleibt offen, solange Telnet im
Build enthalten ist (Zephyr kann den Port zur Laufzeit nicht schliessen).

Feste Konstanten (keine Parameter, Aenderung nur nach Absprache): Anlauf 5 s (E9),
Frist geordnetes Ende 5 s (E7), Ueberstrom 1,25 x/5 s und 1,1 x/95 s (E22), Werksreset
5 s (E22), Weckfolge 3000/750 ms (E8), RCM-Testfolge (E16), CP-/PP-Fristen 20 ms/1000 ms,
Diodenschwelle -10 V, Eingangsschwellen 3/9 V, `rcm_retest_interval` 30 s (F13),
`contactor_check_delay` 200 ms, Diodenfehler-Ausgabe -12 V 3 s (F7).

### 10.1a Schreibregeln

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-PAR-01 | Jeder Parameter und Laufzeitwert ist ueber HTTP und Shell lesbar; schreibbare Werte sind dort schreibbar (SOLL-API-30). | E14, Nutzer 2026-10-19 |
| SOLL-PAR-02 | Jeder Schreibzugriff wird gegen Bereich und Zustandsbedingung geprueft; Ablehnung mit Fehlercode und Klartext an den Absender, nie stilles Verwerfen. | E14 |
| SOLL-PAR-03 | Persistente Parameter wirken sofort und werden ohne Loeschvorgang angehaengt (SOLL-NVM-03). | [ANN] |
| SOLL-PAR-40 | **801/802/465 (F3 entschieden):** `hardware_limit_override` nur **absenken** (6000 mA .. Drehschalterwert); `dip_override` und `lock_state` nur in S1 (kein Fahrzeug) schreibbar, nicht persistent (`lock_state` bleibt als Ergebnis persistent); ein Schreiben von `lock_state` loest einen echten Ver-/Entriegelungszyklus aus statt nur den Merker zu setzen. **[ABW]** Original: ohne Grenze, jederzeit, 465 nur Merker. | E14, F3 |

### 10.2 Laufzeitwerte (schreibbar, nicht persistent)

| Name | Einheit | Bereich | Startwert | Feld (Original) | Bemerkung |
| --- | --- | --- | --- | --- | --- |
| `external_current` | mA | 0, 6000..80000 | `external_current_default` | 528 | Slot `external`; evcc `POST /evse/external_current` |
| `enable_register` | 0/1 | 0..1 | `enable_register_default` | 400 | **[ABW]** Original deutet jeden Wert != 0 als 1; Soll lehnt andere Werte ab (V11) |
| `available_register` | 0/1 | 0..1 | 1 | 402 | |
| `pwm5_request` | 0/1 | 0..1 | 0 | 401 | 5-%-Modus |
| `pause_request` | 0/1 | 0..1 | 0 | 468 | Stromsperre; nicht persistent (F21 entschieden) |
| `lock_request` | 0/1 | 0..1 | 0 | 403 | Verriegelungswunsch (mit D9) |
| `unlock_request` | 0/1 | 0..1 | 0 | 440 | Entriegeln-Anforderung, geloescht bei CP A |
| `register_output_1..4` | 0/1 | 0..1 | 0 | 405..408 | fuer Ausgangscodes 30..33 |
| `hardware_limit_override` | mA | 6000..Drehschalterwert (nur absenken) | Drehschalterwert | 801 | SOLL-PAR-40 |
| `dip_override` | Bitmaske D1..D10 | 0..0x3FF, nur in S1 | Schalterstellung | 802 (Original nur D1..D8) | SOLL-PAR-40 |
| `lock_state` | Zustand | 0/1, nur in S1, loest Zyklus aus | gespeicherter Wert | 465 | persistent wie Original; SOLL-PAR-40 |

### 10.3 Nur lesbar (Status)

CP-Status und -Rohzustand, Ausgabe (Tastgrad/DC), CP+/CP-/PP/Versorgung als Messwert,
Kabelwert, wirksamer Strom `I`, `allowed_charging_current`, aktive Slots, Automatenzustand
S0..S10, Freigabe, Verfuegbarkeit, Ladeerlaubnisquelle, Eingangs-Rohpegel und
-Funktionszustaende, Ausgangszustaende, Verriegelungszustand und -zaehler, Schuetz Soll/Ist,
Fehler (aktiv, Historie), Ladedauer, Laufzeit, Resetgrund, Firmwareversion, DIP- und
Drehschalterstellung (tatsaechlich und wirksam).

Pruefung Abschnitt 10: PC – Grenzwerte jedes Parameters (min-1, min, max, max+1),
Ablehnung mit Fehlermeldung, Defaults nach Werksreset gegen diese Tabelle (automatisch aus
einer Tabelle im Quelltext erzeugt). H1 – Persistenz ueber Neustart.

## 11. Fehler- und Statusmodell, LED-Verhalten

### 11.1 Fehler

Klassen: **P** = Schutzfehler (Schutzpfad, sofort Schuetz aus, Ein-Sperre), **V** =
Fahrzeug-/Ablauffehler (Automat; "sofort" = Schuetz sofort aus, "geordnet" = ueber S7),
**K** = Konfigurationsfehler (kein Laden bis Neustart bzw. Korrektur). Bitpositionen nach
HB Tab. 9-2 (S. 72/74) zur Wiedererkennung, 0-basiert.

| Kennung | Bedeutung | Original | Klasse / Reaktion | E-/F-Wirkung | Ruecksetzen |
| --- | --- | --- | --- | --- | --- |
| ERR-CABLE-REJECT | Kabel abgewiesen (13 A bzw. 13/20 A) | R107.0/.1 | V geordnet | E immer | anderer Kabelwert |
| ERR-PP-INVALID | PP ungueltig bzw. kein Kabel bei Fahrzeug | R107.2 | V **sofort** | E (bei -1 immer, sonst Roh != A) | Bedingung entfaellt |
| ERR-CP-INVALID | CP < 1 V (Kurzschluss) | R107.3 | V **sofort** | E sofort | Pegel >= 1,2 V |
| (Status) | F wegen fehlender Verfuegbarkeit | R107.4 | – | F | Verfuegbarkeit |
| ERR-LOCK | Verriegelung fehlgeschlagen | R107.5 | V | E ab 3. Versuch (mit D9 immer), Roh != A | Erfolg, CP A, Kabelwert 0 |
| ERR-UNLOCK | Entriegelung fehlgeschlagen | R107.6 | V | E immer | Erfolg, CP A |
| ERR-LD-LOST | LD im Laden weggefallen | R107.7 | V **sofort** (F9) | E, Roh != A | Wiederverriegeln erfolgreich, CP A |
| ERR-OVERCURRENT | Ueberstrom (nur mit Messquelle) | R107.8 | V **sofort** | E, Roh != A | CP A |
| ERR-METER-COMM | Messquelle ausgefallen | R107.9 | V geordnet | E immer | Messquelle wieder gueltig |
| ERR-STATE-D | Status D abgewiesen | R107.10 | V **sofort** | E, Roh != A | CP A |
| ERR-CONTACTOR | Schuetzfehler | R107.11 | **P** | F sofort | F14 |
| ERR-DIODE | Diode fehlt | R107.12 | V **sofort** | F sofort (Ausgabe 3 s -12 V, dann +12 V) | CP A |
| (Sammel) | Fehlerstrom detektiert | R107.14 | P | E, Roh != A | Selbsttest bestanden |
| ERR-INPUT | Error Input (Code 21) | R155.1 | V **sofort** | E immer | Eingang inaktiv |
| ERR-RCM-SELFTEST | RCM-Selbsttest fehlgeschlagen | R155.3 | **P** | E | Selbsttest bestanden |
| ERR-RCM-SENSOR | Sensorfehler (ERR_OUT) | R155.4 | **P** | E | Selbsttest bestanden |
| ERR-RCM-DC6 | SWITCH_2 (6 mA DC / 30 mA AC) | R155.5 | **P** | E | Selbsttest bestanden |
| ERR-RCM-AC30 | SWITCH_1 (30 mA rms) | R155.6 | **P** | E | Selbsttest bestanden |
| ERR-SWITCH | Drehschalter A..F | neu (E11) | K | E | Neustart |
| ERR-CONFIG | Konfiguration widerspruechlich (z. B. D6 ohne LD-Eingang) | neu | K | E | Korrektur und Neustart bzw. Parameterkorrektur |
| WARN-SUPPLY | Versorgung zu niedrig (nur Warnung) | neu (F16) | Warnung, keine Abschaltung | keine | 2500 ms gut |
| ERR-INTERNAL | Plausibilitaet, Watchdog-Vorwarnung, unzulaessiger Aktorbefehl | neu (E3, E5) | **P** | E | Neustart |

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-ERR-01 | Jeder Fehler ist einzeln als aktiv/inaktiv lesbar; zusaetzlich eine **Historie** (letzte 32 Ereignisse mit Zeit, Fehler, Zustand, RAM) und ein Sammelwert "seit Start aufgetreten" (Original Fehlerverlauf `0x200138d0`). | Restumfang §5, [ANN] |
| SOLL-ERR-02 | "Ruecksetzen bei CP A" bedeutet: Rohzustand A entprellt (Fahrzeug getrennt). | Ist Z0 |
| SOLL-ERR-03 | Eine Quittierung ueber Shell/Schnittstelle loescht nur Fehler, deren Ursache nicht mehr besteht; Klasse-P-Fehler nur in S1/S9 ohne Fahrzeug. | E14, [ANN] |
| SOLL-ERR-10 | Fehler, Zustandswechsel, Verriegelungsergebnisse und Schuetzschaltungen werden als Ereignisse an die Schnittstellen gemeldet (WebSocket, Protokoll). | Arch §8a |
| SOLL-ERR-11 | **Zugriffsprotokoll:** Fehlgeschlagene Anmeldungen (falsches Passwort) mit Absenderadresse und alle Ereignisse nach SOLL-ERR-10 werden protokolliert: als Logzeile und zusaetzlich in einem Ringpuffer im RAM (letzte 32 Eintraege, geht beim Neustart verloren), abrufbar ueber `GET /system/log` mit derselben Anmeldung wie die uebrige API. Nicht protokolliert werden Schreibzugriffe, erfolgreiche Anmeldungen und Shell-Kommandos. | Nutzer 2026-10-20 |
| SOLL-ERR-30 | Status "digitale Kommunikation nicht verfuegbar": im 5-%-Modus bei CP C/D gesetzt, kein Fehler, LED Error aus, Meldung an der Schnittstelle. | E6 |

### 11.2 LEDs (HB Tab. 3-3, S. 16)

Blinkformen: 1 Hz (500 ms an/500 ms aus), 2 Hz (250/250 ms), 5 Hz (100/100 ms).

| Kennung | LED | Soll | Quelle |
| --- | --- | --- | --- |
| SOLL-LED-01 | Power (PE15) | blinkt 1 Hz, sobald die Firmware laeuft (auch in S0); 5 Hz nach Werksreset bis zum Loslassen des Tasters | Ist §11, Ist §14 |
| SOLL-LED-02 | Error (PE14) | an bei jedem aktiven Fehler sowie in S9/S10 | Ist §11 |
| SOLL-LED-03 | Connect (PE12) | an bei LOCKED; sonst blinkt 1 Hz bei gueltigem Kabelwert und D1; sonst aus | Ist §11 |
| SOLL-LED-04 | Ready (PE13) | Schuetz ein: an; sonst PWM an: 2 Hz; sonst Status B/C/D: 1 Hz; sonst aus | Ist §11, HB S. 16 |
| SOLL-LED-05 | Status Modem (PE10/PE11) | aus | Umfang |
| SOLL-LED-06 | PC2 [HYP] | an: Netz verbunden (Link und IP-Adresse); 1 Hz: kein Link oder keine Adresse | [ANN] |

Pruefung Abschnitt 11: PC – Fehlertabelle als Testmatrix (Setzen, Wirkung auf Status und
Schuetz, Ruecksetzen je Zeile), LED-Ausgabe je Zustand. H1 – LED-Zuordnung auf der
Platine. H2/H3 – Fehler durch EV-Simulator bzw. Pruefstrom ausloesen.

## 12. Schnittstellen

### 12.1 HTTP und WebSocket (erster Umfang: evcc ueber `tinkerforge-warp-ws`)

Grundlage: Arch §8a; evcc `charger/warp-ws.go` (master, gelesen 2026-10-19): `Status()`
bildet `iec61851_state` 0/1/2 auf A/B/C ab, Werte >= 3 ergeben einen Fehler;
`Enabled()` = `external_current.current` > 0; `Enable()` sendet den zuletzt gesetzten
Strom bzw. 0; `MaxCurrentMillis()` sendet mA; `/info/features` bestimmt die
Zusatzfunktionen, `/info/name` den Typ (bei `warp3`/`warp4` schaltet evcc
`evse/phase_auto_switch` ab).

| Kennung | Zugriff | Inhalt (Soll) |
| --- | --- | --- |
| SOLL-API-01 | `GET /info/name` | `{"name": <net_hostname>, "type": "warp2", "display_type": "Wallfree", "uid": <aus MAC>}` – Typ `warp2` (F22 entschieden) |
| SOLL-API-02 | `GET /info/features` | `[]` (kein Zaehler, kein NFC, keine Phasenumschaltung) |
| SOLL-API-03 | `GET /evse/state` | `{"iec61851_state": 0..4, "charger_state": 0..4, "contactor_state": .., "contactor_error": .., "allowed_charging_current": mA, "error_state": .., "lock_state": 0..5}` |
| SOLL-API-04 | `GET /evse/external_current` | `{"current": mA}` |
| SOLL-API-05 | `POST /evse/external_current` mit `{"current": mA}` (auch WARP-Kurzform: nur die Zahl) | setzt Slot `external` (nur RAM); 0 = Laden aus (Stromsperre); erlaubt 0 und 6000..80000, sonst HTTP 400 mit Fehlertext (E14) |
| SOLL-API-06 | WebSocket `GET /ws` | Textrahmen mit `{"topic": <name>, "payload": <wie GET>}`; beim Verbinden alle Topics einmal, danach bei jeder Aenderung; Topics `evse/state`, `evse/external_current` (optional spaeter `evse/user_current`, `evse/user_enabled`) |
| SOLL-API-07 | alle | **Zugriffsschutz von Anfang an (F20):** Weboberflaeche und HTTP-/WebSocket-API nur mit Benutzername/Passwort (`web_user`/`web_password`, konfigurierbar). Werkszustand: Default-Zugangsdaten, beim ersten Zugriff muss das Passwort geaendert werden; bis dahin beantwortet die API nur die Passwortaenderung (sonst HTTP 403). **Verfahren (geprueft 2026-10-19): HTTP-Digest, MD5, `qop=auth`** – evcc `charger/warp/connection.go` nutzt `transport.Digest` (`icholy/digest`, Challenge je Host zwischengespeichert, danach Anmeldung vorab, auch beim WebSocket-Aufbau). Gespeichert wird nur HA1. **Bremse:** je Absenderadresse nach 5 Fehlversuchen Sperre 10 s, je weiterer Fehlversuch verdoppelt bis 5 min, Antwort 429 mit `Retry-After`; ein Erfolg setzt zurueck; gesperrt wird auch mit richtigem Passwort, aber nur diese Adresse. Passwortaenderung ueber `POST /credentials` `{"user", "password"}`; `GET /credentials` meldet `must_change`. Ohne TLS kann Mitlesen im LAN Anfragen wiederholbar machen (Hinweis in der Doku). Mindestens 2 gleichzeitige WebSocket-Clients. |
| SOLL-API-30 | `GET /evse/params`, `POST /evse/params`, `GET /evse/param_info`, `GET/POST /system/config`, `GET /system/config_info`, `POST /system/restart`, `GET /system/info`, `GET /evse/errors`, `GET /system/log` (SOLL-ERR-11) | **Nutzervorgabe 2026-10-19:** Alle Parameter (10.1, 10.2) und alle Mess- und Statuswerte (10.3) sind ueber HTTP lesbar, schreibbare Werte ueber HTTP schreibbar, mit denselben Grenzpruefungen und Fehlermeldungen wie die Shell (E14). Umsetzung: `GET /evse/params` = `{"name": Wert, ...}`; `POST /evse/params` mit `{"name": Wert, ...}` prueft und schreibt jeden Wert einzeln (abgelehnte mit Klartext in `{"errors": {...}}`, HTTP 400; bei Syntaxfehler wird nichts geschrieben); persistente Werte werden gespeichert; `GET /evse/param_info` = Name, Art, Einheit, Bereich, Default, Feld des Originals, Hilfetext |
| SOLL-API-31 | Weboberflaeche `/`, `/app.js`, `/style.css`, `/vendor.js` | statische, gzip-komprimierte Dateien, die nur die API nutzen; Ansicht "Einfach" und "Experte"; Rahmenwerk Preact + htm mit eigenem CSS (Nutzer 2026-10-19, [Architektur 10.4](architektur.md#104-weboberflaeche-rahmenwerk-entschieden-preact--htm)). Umsetzung: hinter derselben Digest-Anmeldung wie die API (auch im Werkszugang erreichbar, damit die Passwortaenderung moeglich ist); live ueber `/ws`, sonst Abfrage alle 2 s |

Abbildungen fuer `evse/state` (WARP-Semantik, Werte beim Implementieren gegen die
[WARP-API-Referenz](https://docs.warp-charger.com/en/docs/interfaces/mqtt_http/api_reference/evse) pruefen):

| Feld | Soll-Abbildung |
| --- | --- |
| `iec61851_state` | CP-Status A = 0, B = 1, C = 2, D = 3, E/F = 4 (F23 entschieden; evcc meldet bei 3/4 einen Fehler) |
| `charger_state` | 0 kein Fahrzeug (S0, S1; S8 ohne Fahrzeug); 1 wartet auf Freigabe/Strom (S2, S3; S8 mit Fahrzeug); 2 bereit, PWM an (S4, S5); 3 laedt (S6, S7); 4 Fehler (S9, S10) |
| `allowed_charging_current` | Strom des ausgegebenen Tastgrads (SOLL-CUR-21), 0 ohne PWM (CP +12 V oder -12 V); gilt ebenso fuer den Statuswert gleichen Namens |
| `lock_state` | 0 INIT, 1 UNLOCKED, 2 LOCKING, 3 LOCKED, 4 UNLOCKING, 5 ERROR |
| `error_state` | WARP: 0 OK, 2 Schalterfehler, 3 DC-Fehlerstromueberwachung, 4 Schuetzfehler, 5 Kommunikationsfehler. Abbildung: 0 kein Fehler; 3 RCM (Vorrang); 4 ERR-CONTACTOR; 2 ERR-SWITCH/ERR-CONFIG; **1 = sonstiger Fehler** (bei WARP unbelegt, eigene Erweiterung) |
| `contactor_state` | WARP meint Spannung vor/nach dem Schuetz; die Steuerung misst das nicht, daher aus dem Schuetz-Soll: 3 ein, 0 aus |
| `contactor_error` | 0 ohne ERR-CONTACTOR; sonst 2 (sollte geschlossen sein) bei Soll ein, 6 (sollte offen sein, Spannung danach) bei Soll aus |

Quelle WARP-Semantik: docs.warp-charger.com, API-Referenz EVSE (gelesen 2026-10-19).
Rahmenformat (geprueft 2026-10-19): evcc liest je Rahmen beliebig viele JSON-Objekte
nacheinander; gesendet wird ein Objekt je Rahmen (auch fuer Browser einfach).

### 12.2 Interne Schnittstelle der Ladelogik

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-API-20 | Alle Zugriffe von aussen (HTTP, Shell, spaeter OCPP) laufen ueber eine gemeinsame Schicht mit Funktionen: Slot schreiben, Freigabe/Verfuegbarkeit/Pause/5 %/Verriegelungs-/Entriegelungswunsch setzen, Parameter lesen/schreiben (mit Grenzpruefung und Fehlercode), Status lesen, Fehler quittieren, Ereignisse abonnieren. | AG, Restumfang §5 (Zugriffsschicht des Originals als Vorbild) |
| SOLL-API-21 | Die Schicht ist nicht blockierend fuer die Ladelogik (Nachrichten oder kurze, begrenzte Sperren). | SOLL-RT-07 |

### 12.3 Shell (Telnet, ggf. UART)

| Kennung | Kommando | Wirkung |
| --- | --- | --- |
| SOLL-SH-01 | `evse status` | Zustand, CP, PP, Strom, Fehler, Verriegelung, Schuetz |
| SOLL-SH-02 | `evse param list` / `get <name>` / `set <name> <wert>` | Parameter; `set` mit Grenzpruefung, Fehlermeldung bei Ablehnung |
| SOLL-SH-03 | `evse current <mA>` | Slot `external` |
| SOLL-SH-04 | `evse enable 0|1`, `evse avail 0|1`, `evse pause 0|1`, `evse pwm5 0|1` | Laufzeitwerte (10.2) |
| SOLL-SH-05 | `evse lock 0|1`, `evse unlock` | Verriegelungswunsch, Entriegeln-Anforderung |
| SOLL-SH-06 | `evse errors` / `evse errors clear` | aktive Fehler mit Ruecksetzbedingung, Historie, Quittierung (SOLL-ERR-03; quittierbar ist nur ERR-CONTACTOR, alle anderen loeschen sich nach Tabelle 11.1 selbst). HTTP: `GET /evse/errors`, `POST /evse/errors/ack` |
| SOLL-SH-07 | `evse selftest` | RCM-Selbsttest, nur in S1 bei CP A |
| SOLL-SH-08 | `evse factory-reset` | Werkseinstellungen, nur im sicheren Zustand, Bestaetigung erforderlich |
| SOLL-SH-09 | `evse import-original` | einmalige Uebernahme aus Sektor 3 (SOLL-NVM-10), nur im sicheren Zustand |
| SOLL-SH-10 | `evse bootloader` | Update-Uebergabe (SOLL-BOOT-30) |
| SOLL-SH-11 | `evse reboot` | Neustart nach SOLL-RT-22 (HTTP: `POST /system/restart`). Zephyrs `kernel reboot` ist entfernt (`CONFIG_REBOOT=n`), weil es die Wartezeit umgehen wuerde (SOLL-SH-20) |
| SOLL-SH-19 | Telnet-Shell ohne Anmeldung, Default **an** (`telnet_enable` = 1, F20); spaeter wird Telnet entfernt oder per Default abgeschaltet. |
| SOLL-SH-20 | Shell-Kommandos sind gegenueber dem Schutzpfad nicht privilegiert: kein Kommando kann Schutzfunktionen abschalten, das Schuetz direkt einschalten oder Aktorimpulse ausserhalb der Ablaufregeln ausloesen. | AG Sicherheitsgrenzen |

### 12.4 Update ueber den Bootloader

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-BOOT-30 | Update-Anforderung wie Original: nur im sicheren Zustand (Schuetz aus, kein Ladevorgang; Hoechstwartezeit 15 s nach SOLL-RT-22), dann RTC-Backup-Register entsperren, **BKP10 = 2, BKP11 = 3, BKP15 = 0, BKP13 = aktuelle IP-Adresse, BKP14 = aktuelle Netzmaske**, Interrupts sperren, Systemreset (AIRCR `0x05FA0004`). | [Bootloader](../../wallbe2/docs/bootloader.md), 1.27 §3.9 |
| SOLL-BOOT-31 | Die Bedeutung dieser Werte fuer den Bootloader ist nicht belegt; vor dem ersten Einsatz Bootloader auslesen und pruefen (H0). | Arch §5 |

Pruefung Abschnitt 12: PC/native_sim – HTTP- und WebSocket-Antworten gegen Beispiel-JSON,
Ablehnung ungueltiger Werte, evcc-Treiber gegen `native_sim` mit Netz (evcc lokal, kein
Geraetezugriff) – nur nach Absprache; Shell-Kommandos inkl. Ablehnungen. H1 – Netzbetrieb
auf der Steuerung (ohne Last), Update-Uebergabe erst nach H0.

## 13. Parameterspeicher, Startablauf, Werksreset

### 13.1 Parameterspeicher (P1)

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-NVM-01 | Eigene Parameter liegen ausschliesslich in **Sektor 7** (`0x08060000..0x0807FFFF`, 128 KB). Sektoren 0..3 werden nie beschrieben oder geloescht (Treiber-/Partitionssperre). | Arch §3 (P1) |
| SOLL-NVM-02 | Format: Anhaengen von Eintraegen (Schluessel, Laenge, Wert, Pruefsumme); der juengste gueltige Eintrag gilt; Formatkennung und Version am Bereichsanfang. Ungueltige Eintraege werden ignoriert. **Geprueft 2026-10-19:** Zephyr ZMS scheidet aus (verlangt mindestens zwei Loescheinheiten, `zms.c` Zeile 1525, und loescht selbststaendig beim Start und bei der Speicherbereinigung, unvereinbar mit SOLL-NVM-03/04) -> eigenes Log-Format, beschrieben in `include/wallfree/nvm/nvm.h`. | Arch §7, [ANN] |
| SOLL-NVM-03 | Persistente Schreibzugriffe im Betrieb erzeugen nur Anhaengevorgaenge (kein Loeschen). Laufzeitwerte (10.2), insbesondere `external_current`, werden nie gespeichert. | Arch §8 |
| SOLL-NVM-04 | Verdichten (RAM-Kopie, Sektor loeschen, neu schreiben) nur im sicheren Zustand: S1 bzw. S0, CP A, Schuetz aus, IWDG verlaengert (SOLL-RT-13). Ist der Bereich im Betrieb voll, bleiben neue Werte bis zum naechsten sicheren Zustand nur im RAM (Status "Speichern ausstehend"). | Arch §2, SOLL-RT-13 |
| SOLL-NVM-05 | Stromausfall waehrend des Verdichtens: Beim naechsten Start ist der Bereich ungueltig -> Werkseinstellungen und Meldung (wie Original); einmalige Uebernahme nach SOLL-NVM-10 erneut moeglich. | Parameterspeicher (Ausfallverhalten) |
| SOLL-NVM-10 | Sektor 3 (Originalformat: Kopf `0x0000`, Eintraege Wert/Kennung, juengster gilt) wird nur gelesen. Auf Kommando werden die Felder 327..330, 390, 409, 412, 519..527 und die Netzwerkwerte uebernommen, jeweils mit Grenzpruefung; nicht uebernehmbare Werte werden gemeldet. Keine automatische Uebernahme. | Arch §3, [Parameterspeicher](../../wallbe2/docs/parameterspeicher.md), [ANN] Feldauswahl |

### 13.2 Startablauf

| Kennung | Schritt | Quelle |
| --- | --- | --- |
| SOLL-BOOT-01 | Pins nach SOLL-IO-02 (vor allem anderen). | E3 |
| SOLL-BOOT-02 | Takt: HSE 25 MHz ohne Bypass, PLL M 25/N 200/P 2/Q 5 -> 100 MHz; MCO1 = HSE an PA8. | Taktbaum |
| SOLL-BOOT-03 | IWDG starten (SOLL-RT-12), Resetgrund lesen und sichern. | E3 |
| SOLL-BOOT-04 | Parameter laden (Sektor 7); fehlen sie: Werkseinstellungen. | Parameterspeicher |
| SOLL-BOOT-05 | Reset-Taster pruefen (SOLL-BOOT-20). | Ist §14 |
| SOLL-BOOT-06 | DIP-, Drehschalter lesen; Konfigurationspruefung (ERR-SWITCH, ERR-CONFIG). | Ist §10, E11 |
| SOLL-BOOT-07 | TIM1 starten mit CP +12 V DC, ADC/DMA starten, Schutz-Interrupts freigeben. | Ist §14 |
| SOLL-BOOT-08 | RCM-Selbsttest; danach mit D4 Entriegelungszyklus (SOLL-LOCK-20). | Decompilat `0x08039cd8` |
| SOLL-BOOT-09 | Netz: PA15 >= 10 ms Low [HYP PHY-Reset], dann Ethernet/IP starten; Netzstart verzoegert den Ladeablauf nicht. | Ist §14 Schritt 2 |
| SOLL-BOOT-10 | Automat in S0; nach 5 s S1 (SOLL-SM-01). PB2 High nach erster Ausgangsberechnung (SOLL-IO-21). | E9 |

### 13.3 Werksreset

| Kennung | Anforderung | Quelle |
| --- | --- | --- |
| SOLL-BOOT-20 | Ist PD2 beim Start Low, wird gewartet (IWDG wird bedient, Schuetz aus): ununterbrochen **5 s** Low -> Werkseinstellungen schreiben (Sektor 7 loeschen/neu, sicherer Zustand gegeben), dann Power-LED 5 Hz bis zum Loslassen, dann weiterer Start. Kuerzer -> normaler Start. **[ABW]** zum Handbuch (> 10 s), wie Original (E22). | Ist §14, E22, HB S. 16 |
| SOLL-BOOT-21 | Werkseinstellungen = Defaults aus Abschnitt 10 (auch Zugangsdaten mit erneutem Aenderungszwang); Sektor 3 bleibt unberuehrt. Auch per Shell (SOLL-SH-08). | E20 |

Pruefung Abschnitt 13: PC – Parameterformat mit simuliertem Flash (Anhaengen, voller
Bereich, Verdichten, Stromausfall an jeder Stelle, kaputte Pruefsumme), Import aus einem
synthetischen Sektor-3-Abbild; Startreihenfolge in `native_sim`. H0 – Bootloader auslesen
(Pruefung SOLL-BOOT-31, loescht der Bootloader Sektor 7?). H1 – Start, Werksreset mit
Taster, Persistenz.

## 14. Nicht umgesetzt / spaeter

| Funktion | Original (Felder/Bits) | Behandlung |
| --- | --- | --- |
| Energiemessung, Messquelle | 108..119, 132, Task `Modbus_Master_EM` | entfaellt; Schnittstelle fuer Phasenstroeme/-spannungen vorgesehen (SOLL-PROT-30) |
| Ueberstromerkennung | 409, R107.8/.9 | spezifiziert, nur mit Messquelle |
| Schuetzueberwachung Modus 4 | 390 = 4 | spezifiziert (E4), nur mit Messquelle |
| Phasensymmetrie | 433, 3481 | entfaellt |
| RFID | 423, 482, `c87`, R155.7 (`c7c.7`), Code 18 | entfaellt; Code 18 wie 1 (F18) |
| OCPP | 427, 3843..3861, 466, `cec`, `c88`, R155.0 | entfaellt; spaeter allgemeine Implementierung ueber SOLL-API-20 und eigenen Strom-Slot |
| Mobilfunk | Modem-Task, PD0/PD1/PB10, PE10/PE11, 418 | entfaellt (BKP15 = 0) |
| Master-Slave/Lastverteilung | 426, 438, 464, 996, 998, R107.15 | entfaellt |
| Modbus TCP/RTU | Register-/Coil-Schnittstelle, 386 | entfaellt |
| TLS | – | vorerst nicht; Zugangsdaten daher im LAN mitlesbar |
| Originalweboberflaeche, Feldnummern-API | `/daten`, Felder | entfaellt (E21) |
| Feld 485 (OTP) | `0x1FFF7890` | entfaellt (E13) |
| Felder 404, 800, 803, 469, 470 | | entfallen (800 ersetzt durch Shell-Kommandos) |
| WARP-Funktionen ueber den evcc-Mindestumfang hinaus | `evse/low_level_state`, `start_charging`, `phase_auto_switch`, Charge-Tracker | schrittweise nach Absprache |

## 15. Offene Punkte, Annahmen und Fragen an den Nutzer

### 15.1 Entscheidungen des Nutzers zu F1..F27 (2026-10-19)

Alle Fragen am 2026-10-19 beantwortet; die betroffenen Anforderungen sind angepasst.

| Nr. | Thema | Entscheidung | Wirkt in |
| --- | --- | --- | --- |
| F1 | Funktionen, die einen Energiezaehler brauchen | "nicht verfuegbar", Schnittstelle fuer spaetere Messquelle vorsehen | 1.4, SOLL-PROT-22/30 |
| F2 | Zusatz-Entprellung Digitaleingaenge | 2 Messbloecke (~16 ms) | SOLL-IO-12 |
| F3 | Schreiben 801/802/465 | 801 nur absenken; 802/465 nur in S1, 465 mit echtem Zyklus | SOLL-PAR-40 |
| F4 | Zeitwerte Watchdog/Reaktion | Startwerte annehmen, nach H1 nachschaerfen | SOLL-RT-05/10..12, SOLL-PROT-05 |
| F5 | CP im sicheren Zustand | -12 V DC (F) | SOLL-RT-20 |
| F6 | CP-Hysterese | gegen Pegelzustand | SOLL-CP-02 |
| F7 | Diode fehlt | 3 s -12 V, dann +12 V, Fehler bis CP A; Normpruefung offen | SOLL-CP-07, 4.2 |
| F8 | Sofort- vs. geordnete Abschaltung | wie Tabelle 11.1 | 5.2, 11.1 |
| F9 | LD faellt im Laden weg | sofort aus, 1x neu verriegeln, bei Erfolg weiterladen | SOLL-LOCK-40 |
| F10 | Eingangscode 19 | ignoriert Slot `external` (wie HB) | SOLL-CUR-04 |
| F11 | Werte ueber Obergrenze | annehmen und begrenzen; 1..5999/>80000 ablehnen | SOLL-CUR-14 |
| F12 | Startwert `external` | ~~keine Einschraenkung (80000)~~; **geaendert 2026-10-20: 0** (Laden erst nach Vorgabe, wie WARP `external_defaults` parametrierbar) | SOLL-CUR-15 |
| F13 | RCM-Wiederholtest in A | alle 30 s | SOLL-PROT-11 |
| F14 | Schuetzfehler ruecksetzen | nur Neustart oder Quittierung | SOLL-PROT-23 |
| F15 | D5/PE1 | wie Original (ohne Wirkung), nach H1 neu entscheiden | SOLL-PROT-24 |
| F16 | Unterspannung | nur Warnung, RCM bleibt aktiv, kein Sprung in den Grundzustand | SOLL-PROT-41 |
| F17 | D6 ohne LD-Eingang | Konfigurationsfehler | SOLL-LOCK-03 |
| F18 | Eingangscode 18 | wie Code 1 | 9.1 |
| F19 | Startwert Freigabe 400 | Parameter `enable_register_default`, Default 0 | SOLL-EN-03 |
| F20 | Zugriffsschutz | Web/API von Anfang an mit Benutzer/Passwort (konfigurierbar, Default mit Aenderungszwang); Telnet offen, Default an, spaeter entfernen/aus | SOLL-API-07, SOLL-SH-19 |
| F21 | Pause persistent | nein | 10.2 |
| F22 | Typ in `/info/name` | `warp2` | SOLL-API-01 |
| F23 | D/E/F an evcc | ehrlich 3/4 | 12.1 |
| F24 | Parameter ueber HTTP | ja, alles von Anfang an (Nutzervorgabe) | SOLL-API-30 |
| F25 | Netz-Default | DHCP an | 10.1 |
| F26 | Doppelt belegte Eingangsfunktion | ODER | SOLL-FN-02 |
| F27 | LD-Pruefung beim Entriegeln | Option `unlock_check`, Default aus | SOLL-LOCK-14a |

Weitere Rahmenentscheidungen vom selben Tag: Projektname **Wallfree**, neues Repository,
Lizenz Apache-2.0, Systemtakt 100 MHz, Weboberflaeche Preact + htm
([Architektur §10](architektur.md#10-rahmenfragen-vom-2026-10-19-nutzer)).

### 15.2 Annahmen und offene Punkte (ohne Nutzerentscheidung zu klaeren)

| Punkt | Stand | Klaerung |
| --- | --- | --- |
| PE1-Ziel (C3/C4?), PA15-Ziel (PHY-RST#?), PA8 -> PHY-XI | [HYP] | H1, [Erinnerung](../../wallbe2/docs/hardware-offene-fragen.md#erinnerung-sobald-die-hardware-auf-dem-tisch-liegt-nutzerwunsch-2026-10-19) |
| LD-Signalform der Steckdose (Impulsdauer, Impulse beim Entriegeln) | unbekannt; bestimmt SOLL-LOCK-14/15 | H1 |
| CP-Treiber (Polaritaet, Pegel), ADC-Skalierung CP/PP/Versorgung | Original-Skalierung uebernommen [ANN] | H1/H2 |
| RCM: Inversionsstufe, direkter Pfad SWITCH_1/2 -> Schuetztreiber, Konfigurationspins | [HYP] | H1/H3 |
| Quelle an PE0 (Klemme CT) | [HYP] | H1 |
| Reset-Taster: PD2 und/oder NRST (HB: einfache Betaetigung = Neustart) | offen | H1 |
| "Automatische Entriegelung bei Stromausfall" (HB S. 9): Hardwarefunktion? | in der Firmware nicht gefunden | H1 |
| Bootloader: Pruefungen, Nutzung von BKP-Registern, loescht er Sektor 7, Start-SP | unbekannt | H0 |
| IWDG-Umprogrammierung im Betrieb; Loeschzeit Sektor 7 | ZMS entfaellt (SOLL-NVM-02). Loeschzeit hoechstens 4000 ms laut Zephyr-Devicetree (`stm32f2.dtsi`, `max-erase-time`), nicht am Datenblatt geprueft (DS6329 fehlt); IWDG-Umprogrammierung ueber Register umgesetzt, auf Hardware ungeprueft | Datenblatt, H1 |
| Normzeiten IEC 61851-1 (Abschaltung nach C->B, Reaktionszeit des Fahrzeugs) | Norm nicht im Projekt; 5-s-Frist durch HB S. 38 gestuetzt | Normtext |
| WARP: Rahmenformat WebSocket, Codes `error_state`/`contactor_state`, Authentifizierungsverfahren der evcc-Vorlage (Basic/Digest) | nicht im Detail geprueft | WARP-Referenz, `warp-ws.go` |
| Diodenpruefung: Vorgabe der IEC 61851-1 zur Ausgabe nach Fehlschlag | Norm nicht im Projekt | Normtext |
| LD-Signal Kuester 03S (Fremdquelle nennt 02S: 0 V in beiden Endlagen, 12 V waehrend der Fahrt) | Hypothese | H1-Messung |
| Deutung Feld 519 als Mindestpause vor erneutem Einschalten | [ANN] (Original: Wartezeit nach jedem Schuetz-Aus im Task) | Decompilat bei Bedarf |
