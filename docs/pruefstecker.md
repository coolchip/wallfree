# Pruefstecker und Stufe C

Stand: 2026-10-24. Plan fuer einen selbstgebauten Pruefstecker (Fahrzeugnachbildung an der
Typ-2-Steckdose) und den Pruefablauf der Stufe C auf dem Testgerät (CP-PWM, Verriegelung mit Stecker,
Schuetz). **Stufe C ist noch nicht freigegeben:** Die Sicherheitsgrenzen in
[wallbe2/AGENTS.md](../../wallbe2/AGENTS.md#sicherheitsgrenzen) erlauben derzeit nichts an der
Steckdose; vor C0 braucht es eine ausdrueckliche Freigabe des Nutzers.

## 1. Grundsatz: keine Netzspannung im Pruefstecker

Der Pruefstecker ist ein Typ-2-Stecker, in dem **nur PE, CP und PP bestueckt** sind; die
Kontakte L1, L2, L3 und N bleiben **leer** (Kontakthuelsen nicht einsetzen). Schaltet das Schuetz
ein, liegt die Netzspannung nur an den Buchsenkontakten in der Steckdose, die der eingesteckte
Stecker abdeckt; in den Pruefstecker, seine Schalter und Messbuchsen gelangt sie nicht. CP und PP
fuehren nur Kleinspannung (+-12 V bzw. die PP-Messspannung) gegen PE.

Die Netzspannung am Schuetzausgang misst der Nutzer bei Bedarf an den grossen Klemmen in der
Wallbox (Nutzerregel: Klemmen ja, Platine nein).

## 2. Schaltung

Werte der Fahrzeugseite nach IEC 61851-1 Anhang A (Norm nicht im Projekt; Werte allgemein
veroeffentlicht und ueber die Bandgrenzen in [SOLL-CP-01](soll-spezifikation.md#4-cp--und-pp-auswertung)
gegengeprueft).

```
 CP o----+----------------------+------------- Messbuchse CP
         |                      |
       [S4] Taster "E"       [S1] "Fahrzeug"      (aus = A)
         |                      |
         |                     -+-
         |                     \ /  D1 1N4007, Anode Richtung CP
         |                     -+-   [S5] parallel zu D1: "Diode ueberbruecken"
         |                      |
         |         +------------+------------+
         |         |            |            |
         |      [R2 2,74k]  [S2]"laden"  [S3]"Belueftung"
         |         |            |            |
         |         |        [R3 1,3k]    [R4 270]
         |         |            |            |
 PE o----+---------+------------+------------+---- Messbuchse PE

 PP o----[S6 Drehschalter]-------------------------- Messbuchse PP
           offen | 1,5k | 680 | 220 | 100 | 47     (jeweils nach PE)
```

| Stellung | CP gegen PE (Fahrzeugseite) | erwarteter CP-Pegel (positives Plateau) |
| --- | --- | --- |
| S1 aus | offen | +12 V (A) |
| S1 ein | D1 + 2,74 kOhm | etwa +9 V (B) |
| S1 + S2 | D1 + 882 Ohm (2,74k ‖ 1,3k) | etwa +6 V (C) |
| S1 + S3 | D1 + 246 Ohm (2,74k ‖ 270) | etwa +3 V (D, wird abgewiesen: `reject_state_d` = 1) |
| S4 gedrueckt | Kurzschluss CP-PE | 0 V (E, ERR-CP-INVALID) |
| S5 ein (mit S1) | ohne Diode | negatives Plateau etwa -8,8 V statt -12 V (ERR-DIODE) |

Die Pegel gelten fuer einen Quellwiderstand von 1 kOhm in der Wallbox (Norm; beim Testgerät nicht
gemessen). Die Diode sitzt **zwischen** S1 und den Widerstaenden (Anode zur CP-Seite, nur die positive
Halbwelle fliesst), der Taster S4 schliesst CP
direkt nach PE (vor der Diode).

PP-Klassen der Steckdose mit D1 an ([SOLL-PP-02](soll-spezifikation.md#43-pp-proximity-klemme-px)):

| S6 | Widerstand PP-PE | Wallfree `cable` | Bemerkung beim Testgerät (DIP 111: D1, D2, D3 an) |
| --- | --- | --- | --- |
| offen | > 2,2 kOhm | 0 | kein Kabel |
| 1 | 1,5 kOhm | 13 A | **ERR-CABLE-REJECT** (D2 an) |
| 2 | 680 Ohm | 20 A | zugelassen (D3 an) |
| 3 | 220 Ohm | 32 A | Normalfall fuer die Pruefung |
| 4 | 100 Ohm | 63 A | wird als 63 A gemeldet |
| 5 | 47 Ohm | ungueltig | **ERR-PP-INVALID** |

Schalter als Kippschalter (S1, S2, S3, S5) statt eines Drehschalters fuer A..D: Ein
Drehschalter unterbricht beim Umschalten kurz (wirkt wie A). Die CP-Entprellung (20 ms)
ueberbrueckt das zwar meist, Kippschalter bilden das Fahrzeug aber naeher nach (R2 bleibt
immer verbunden, S2/S3 schalten zu).

## 3. Stueckliste

| Teil | Angabe | Hinweis |
| --- | --- | --- |
| Typ-2-Stecker | Ladekabelstecker (Infrastrukturseite, passt in die Steckdose), zum Selbstkonfektionieren, mit Einzelkontakten | nur PE-, CP-, PP-Kontakt einsetzen; Markenware (z. B. Phoenix Contact, Mennekes), damit Verriegelung und Kodierung stimmen |
| Leitung | 3 Adern, z. B. 3 x 0,75 mm², etwa 1 m, mit Zugentlastung | PE gruen-gelb; kein Laststrom |
| Gehaeuse | isolierend, klein | Leitungseinfuehrung mit Zugentlastung |
| D1 | 1N4007 (oder 1N4148) | Sperrspannung >= 50 V reicht |
| R2 | 2,74 kOhm 1 % (E96), sonst 2,7 kOhm 1 % | 0,25 W |
| R3 | 1,3 kOhm 1 % | 0,25 W |
| R4 | 270 Ohm 1 % | 0,25 W |
| PP-Widerstaende | 1,5 kOhm, 680 Ohm, 220 Ohm, 100 Ohm, 47 Ohm, je 1 % | 0,25 W |
| S1, S2, S3, S5 | Kippschalter 1 x Ein/Aus | Kleinspannung, beliebig |
| S4 | Taster (Schliesser) | Kurzschluss CP-PE |
| S6 | Drehschalter 1 x 6 | PP-Klassen |
| Messbuchsen | 3 x 4-mm-Sicherheitsbuchse (CP, PP, PE) | fuer Multimeter/Oszilloskop |

Belastung: Der hoechste Strom fliesst bei S4 (12 V ueber 1 kOhm, etwa 12 mA); alle Widerstaende
bleiben weit unter 0,25 W.

## 4. Spannungsfreie Pruefung des Steckers

Vor dem ersten Stecken, Stecker nicht in der Wallbox, mit dem Multimeter zwischen den
Steckerkontakten (nicht nur an den Messbuchsen):

| Pruefung | Erwartet |
| --- | --- |
| L1, L2, L3, N gegen alles | offen (Kontakte leer) |
| CP-PE, S1 aus | offen |
| CP-PE, S1 ein, Diodentest (Rot an CP) | Durchlass etwa 0,6 V; umgekehrt gesperrt |
| CP-PE, S1 ein, Widerstand mit Rot an CP | etwa 2,7 kOhm + Diode (Anzeige je nach Messgeraet hoeher) |
| wie vor, S2 zu / S3 zu | kleiner (882 Ohm / 246 Ohm + Diode) |
| CP-PE, S4 gedrueckt | 0 Ohm |
| CP-PE, S1 + S5 ein | 2,74 kOhm, in beiden Richtungen gleich |
| PP-PE je Stellung S6 | offen, 1,5k, 680, 220, 100, 47 Ohm |

## 5. Pruefablauf Stufe C

Allgemein: Wallfree wird nur ueber Shell und API gesteuert; der Nutzer schaltet F1, bedient die
Schalter am Pruefstecker und misst an CP/PE (Messbuchsen) bzw. an grossen Klemmen. Vor jedem
Schritt nennt Claude Aktion und Beobachtungspunkte. Abbruch: unerwartetes Schuetzklicken,
Geruch, Motor laeuft dauerhaft -> F1 aus.

Vor C0 zu klaeren: Was ist bei "Schuetz abgeklemmt" getrennt (Spule, Hauptkontakte oder
beides)? Hat die Steckdose eine Notentriegelung, falls der Stecker verriegelt bleibt? Ist ein
Oszilloskop vorhanden (sonst Pegel ueber `cp_pos`/`cp_neg` und Multimeter-Mittelwert)?

**C0 Freigabe und Vorbereitung.** Freigabe Stufe C in wallbe2/AGENTS.md (Wortlaut stimmt der
Nutzer ab). Pruefstecker nach Abschnitt 4 geprueft. Schuetzspule weiter abgeklemmt.

**C1 Stecker ohne Fahrzeug (S1 aus), PP-Klassen.** Stecker stecken, S6 nacheinander durch alle
Stellungen, je > 1 s warten (SOLL-PP-03).

| S6 | Erwartet (`evse param get cable`, `evse errors`) |
| --- | --- |
| offen | `cable` 0, keine Fehler |
| 1,5k | 13 A, ERR-CABLE-REJECT |
| 680 / 220 / 100 | 20 / 32 / 63 A, keine Fehler |
| 47 | ERR-PP-INVALID |

Dabei bleibt CP A (+12 V); D9 aus verriegelt erst bei Fahrzeug (B).

**C2 Fahrzeug erkannt (B), PWM.** S6 auf 220 Ohm (32 A), Schluessel ein, S1 ein.

- Erwartet: Verriegeln (Motor, LD-Impuls, Connect-LED an), S3 WAIT bzw. S5 PWM_ON, PWM
  1 kHz mit 26,6 % (16 A, Drehschalter 4), `cp_pos` etwa 9 V, `cp_neg` etwa 12 V, Ready-LED
  2 Hz, Front-LED gruen (CR 40: B ohne Fehler).
- Multimeter (DC) an CP-PE zeigt den Mittelwert, etwa 0,266 * 9 V - 0,734 * 12 V = **-6,4 V**.
- Strom ueber die Schnittstelle: `evse current 10000` -> Tastgrad 16,7 %, Mittelwert etwa
  -8,5 V; zurueck mit `evse current 80000`.
- Schluessel aus -> Ladeerlaubnis weg, PWM aus (CP +9 V DC); Schluessel ein -> PWM wieder an.
- Nach `wakeup_delay` (30 s) in B: Weckfolge einmal je Steckvorgang (Ereignis im Log).
- S1 aus (A) -> PWM aus, Entriegeln (Motor), S1.

**C3 Laden ohne Schuetzspule (C).** S1 ein, warten bis PWM, dann S2 ein.

- Erwartet: S6 CHARGING, `contactor` (Soll) 1, Ready-LED an, Front-LED blau (VR 16: C oder D);
  kein Klicken, weil die Spule abgeklemmt ist; an der Steckdose keine Netzspannung.
- S2 aus (B) -> Schuetz-Soll aus, PWM bleibt; S2 ein -> wieder ein (`contactor_min_off` 500 ms).
- S3 statt S2 (D) -> ERR-STATE-D, Schuetz-Soll aus.
- S4 kurz druecken waehrend C -> ERR-CP-INVALID, Schuetz-Soll sofort aus.
- S5 ein waehrend PWM -> ERR-DIODE, CP 3 s -12 V, dann +12 V (SOLL-CP-07), Schuetz-Soll aus.
- Schluessel aus waehrend C -> geordnetes Ende (SOLL-SM), Schuetz-Soll aus.
- Die Schuetzueberwachung (Via Sense, CT) prueft nur im Aus-Zustand; ohne Spule bleibt sie
  ruhig. Ein ERR-CONTACTOR hier waere ein Befund (CT-Beschaltung).

**C4 Laden mit Schuetz.** F1 aus, Schuetzspule anklemmen (Nutzer, spannungsfrei), F1 ein.
Wiederholen von C3:

- Schuetz klickt bei C, faellt bei B, E, Diodenfehler, Schluessel aus.
- Optional misst der Nutzer an den grossen Klemmen am Schuetzausgang L-N (230 V bei ein, 0 V bei
  aus); am Pruefstecker liegt keine Netzspannung (Abschnitt 1).
- Nach jedem Aus: kein ERR-CONTACTOR (Via Sense sieht keine Spannung mehr).

**C5 offen fuer spaeter.** Reaktionszeit RCM -> PE2 (Oszilloskop, SOLL-PROT-05), Fehlerstrom
waehrend des Ladens, echtes Fahrzeug (H3).

## 6. Bezug zu den Anforderungen

C1: SOLL-PP-01..05. C2: SOLL-CP-01..05, SOLL-LOCK-14/23, SOLL-EN-01, SOLL-LED-03/04,
SOLL-CUR. C3/C4: SOLL-SM, SOLL-CP-06/07, SOLL-PROT-20/21, SOLL-RT-20. Ergebnisse werden in
[Implementierung](implementierung.md#offen) und [wallbe2 Hardware](../../wallbe2/docs/hardware.md)
eingetragen.
