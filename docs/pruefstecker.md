# Prüfstecker, Stufe C und D

Stand: 2026-11-07. Plan für einen selbstgebauten Prüfstecker (Fahrzeugnachbildung an der
Typ-2-Steckdose) und den Prüfablauf der Stufe C auf dem Testgerät (CP-PWM, Verriegelung mit Stecker,
Fehlerstrom, Schütz, evcc). Prüfstecker gebaut (Nutzer 2026-11-07). Stufe C ist freigegeben
([Abschnitt 7](#7-freigabe-stufe-c)) und auf dem Testgerät durchgeführt (C1..C6, 2026-11-07; Ergebnisse in
[Implementierung](implementierung.md#prüfungen-auf-box-t)). Stufe D mit Fahrzeug am zweiten Gerät ist geplant
([Abschnitt 8](#8-stufe-d-laden-mit-fahrzeug-box-g)).

## 1. Grundsatz: keine Netzspannung im Prüfstecker

Der Prüfstecker ist ein Typ-2-Stecker, in dem **nur PE, CP und PP bestückt** sind; die
Kontakte L1, L2, L3 und N bleiben **leer** (Kontakthülsen nicht einsetzen). Schaltet das Schütz
ein, liegt die Netzspannung nur an den Buchsenkontakten in der Steckdose, die der eingesteckte
Stecker abdeckt; in den Prüfstecker, seine Schalter und Messbuchsen gelangt sie nicht. CP und PP
führen nur Kleinspannung (+-12 V bzw. die PP-Messspannung) gegen PE.

Die Netzspannung am Schützausgang misst der Nutzer bei Bedarf an den großen Klemmen in der
Wallbox (Nutzerregel: Klemmen ja, Platine nein).

## 2. Schaltung

Werte der Fahrzeugseite nach IEC 61851-1 Anhang A (Norm nicht im Projekt; Werte allgemein
veröffentlicht und über die Bandgrenzen in [SOLL-CP-01](soll-spezifikation.md#4-cp--und-pp-auswertung)
gegengeprüft).

![Schaltplan Prüfstecker](bilder/pruefstecker.svg)

| Stellung | CP gegen PE (Fahrzeugseite) | erwarteter CP-Pegel (positives Plateau) |
| --- | --- | --- |
| S1 aus | offen | +12 V (A) |
| S1 ein | D1 + 2,74 kOhm | etwa +9 V (B) |
| S1 + S2 | D1 + 882 Ohm (2,74k ‖ 1,3k) | etwa +6 V (C) |
| S1 + S3 | D1 + 246 Ohm (2,74k ‖ 270) | etwa +3 V (D, wird abgewiesen: `reject_state_d` = 1) |
| S4 gedrückt | Kurzschluss CP-PE | 0 V (E, ERR-CP-INVALID) |
| S5 gedrückt (mit S1) | ohne Diode | negatives Plateau etwa -8,8 V statt -12 V (ERR-DIODE) |

Die Pegel gelten für einen Quellwiderstand von 1 kOhm in der Wallbox (Norm; beim Testgerät nicht
gemessen). Die Diode sitzt **zwischen** S1 und den Widerständen (Anode zur CP-Seite, nur die positive
Halbwelle fließt), der Taster S4 schließt CP
direkt nach PE (vor der Diode).

PP-Klassen der Steckdose mit D1 an ([SOLL-PP-02](soll-spezifikation.md#43-pp-proximity-klemme-px)):

| S6 | Widerstand PP-PE | Wallfree `cable` | Bemerkung beim Testgerät (DIP 111: D1, D2, D3 an) |
| --- | --- | --- | --- |
| offen | > 2,2 kOhm | 0 | kein Kabel |
| 1 | 1,5 kOhm | 13 A | **ERR-CABLE-REJECT** (D2 an) |
| 2 | 680 Ohm | 20 A | zugelassen (D3 an) |
| 3 | 220 Ohm | 32 A | Normalfall für die Prüfung |
| 4 | 100 Ohm | 63 A | wird als 63 A gemeldet |
| 5 | 47 Ohm | ungültig | **ERR-PP-INVALID** |

S5 ist ein Taster: Wallfree erkennt die fehlende Diode nach 20 ms und hält den Fehler bis zum
Abstecken (SOLL-CP-07), ein kurzer Druck (etwa 0,5 s) reicht. Ein Kippschalter geht ebenso.

Schalter als Kippschalter (S1, S2, S3) statt eines Drehschalters für A..D: Ein
Drehschalter unterbricht beim Umschalten kurz (wirkt wie A). Die CP-Entprellung (20 ms)
überbrückt das zwar meist, Kippschalter bilden das Fahrzeug aber näher nach (R2 bleibt
immer verbunden, S2/S3 schalten zu).

## 3. Stückliste

| Teil | Angabe | Hinweis |
| --- | --- | --- |
| Typ-2-Stecker | Ladekabelstecker (Infrastrukturseite, passt in die Steckdose), zum Selbstkonfektionieren, mit Einzelkontakten | nur PE-, CP-, PP-Kontakt einsetzen; Markenware (z. B. Phoenix Contact, Mennekes), damit Verriegelung und Kodierung stimmen |
| Leitung | 3 Adern, z. B. 3 x 0,75 mm², etwa 1 m, mit Zugentlastung | PE grün-gelb; kein Laststrom |
| Gehäuse | isolierend, klein | Leitungseinführung mit Zugentlastung |
| D1 | 1N4007 (oder 1N4148) | Sperrspannung >= 50 V reicht |
| R2 | 2,74 kOhm 1 % (E96), sonst 2,7 kOhm 1 % | 0,25 W |
| R3 | 1,3 kOhm 1 % | 0,25 W |
| R4 | 270 Ohm 1 % | 0,25 W |
| PP-Widerstände | 1,5 kOhm, 680 Ohm, 220 Ohm, 100 Ohm, 47 Ohm, je 1 % | 0,25 W |
| S1, S2, S3 | Kippschalter 1 x Ein/Aus | Kleinspannung, beliebig |
| S4, S5 | Taster (Schließer) | S4 Kurzschluss CP-PE, S5 überbrückt D1 |
| S6 | Drehschalter 1 x 6 | PP-Klassen |
| Messbuchsen | 3 x 4-mm-Sicherheitsbuchse (CP, PP, PE) | für Multimeter/Oszilloskop |

Zubehör für die Fehlerstromprüfung (C4), getrennt vom Stecker:

| Teil | Angabe | Hinweis |
| --- | --- | --- |
| Prüfdraht | isolierte Litze, etwa 0,5 mm², 1..2 m | einmal durch den gelben Summenstromwandler, nur spannungsfrei einziehen |
| Batterie | 9-V-Block mit Clip | Stromquelle für Prüfdraht und Kontaktschleife |
| R5 | 1 kOhm, 0,25 W | etwa 9 mA Gleichstrom im Prüfdraht (Auslöseschwelle 6 mA DC) |
| S7 | Taster (Schließer) | schaltet den Fehlerstrom |
| R6 | 4,7 kOhm, 0,25 W | Kontaktschleife über C1/C2 (nur nach Bedingung in C4) |
| Oszilloskop | 2 Kanäle | vorhanden |

Belastung: Der höchste Strom fließt bei S4 (12 V über 1 kOhm, etwa 12 mA); alle Widerstände
bleiben weit unter 0,25 W.

## 4. Spannungsfreie Prüfung des Steckers

Vor dem ersten Stecken, Stecker nicht in der Wallbox, mit dem Multimeter zwischen den
Steckerkontakten (nicht nur an den Messbuchsen):

| Prüfung | Erwartet |
| --- | --- |
| L1, L2, L3, N gegen alles | offen (Kontakte leer) |
| CP-PE, S1 aus | offen |
| CP-PE, S1 ein, Diodentest (Rot an CP) | Durchlass etwa 0,6 V; umgekehrt gesperrt |
| CP-PE, S1 ein, Widerstand mit Rot an CP | etwa 2,7 kOhm + Diode (Anzeige je nach Messgerät höher) |
| wie vor, S2 zu / S3 zu | kleiner (882 Ohm / 246 Ohm + Diode) |
| CP-PE, S4 gedrückt | 0 Ohm |
| CP-PE, S1 ein, S5 gedrückt | 2,74 kOhm, in beiden Richtungen gleich |
| PP-PE je Stellung S6 | offen, 1,5k, 680, 220, 100, 47 Ohm |

## 5. Prüfablauf Stufe C

Probe in der Simulation: `T8StufeC` in [tests/e2e/test_e2e.py](../tests/e2e/test_e2e.py) spielt C1..C4
und die WARP-Seite von C6 mit den Einstellungen des Testgeräts durch (DIP 111, Drehschalter 4, LD 22,
CR 40; Schalter -> `sim`-Kommandos) und prüft die Erwartungen dieses Abschnitts; Pegel und Zeiten
am Oszilloskop zeigt sie nicht.

Allgemein: Wallfree wird nur über Shell und API gesteuert; der Nutzer schaltet F1, bedient die
Schalter am Prüfstecker und misst an CP/PE (Messbuchsen) bzw. an großen Klemmen. Vor jedem
Schritt nennt Claude Aktion und Beobachtungspunkte. Abbruch: unerwartetes Schützklicken,
Geruch, Motor läuft dauerhaft -> F1 aus.

Rahmen am Testgerät (Nutzer 2026-10-24): "Schütz abgeklemmt" heißt **Spule abgeklemmt**; die
Steckdose hat eine **Notentriegelung**; ein **Oszilloskop** ist vorhanden (CP-PWM, Pegel und
Reaktionszeiten direkt messen, Tastkopf an den Messbuchsen CP/PE des Prüfsteckers).
`unlock_check` bleibt aus.

**C0 Vorbereitung.** Prüfstecker nach Abschnitt 4 geprüft. Schützspule abklemmen (sie ist derzeit angeschlossen); der Nutzer bestätigt vor dem ersten Schritt, was an C1/C2 und an der Spule hängt.

**C1 Stecker ohne Fahrzeug (S1 aus), PP-Klassen.** Stecker stecken, S6 nacheinander durch alle
Stellungen, je > 1 s warten (SOLL-PP-03).

| S6 | Erwartet (`param get cable`, `errors`) |
| --- | --- |
| offen | `cable` 0, keine Fehler |
| 1,5k | 13 A, ERR-CABLE-REJECT |
| 680 / 220 / 100 | 20 / 32 / 63 A, keine Fehler |
| 47 | ERR-PP-INVALID |

Dabei bleibt CP +12 V DC, Zustand S1, nicht verriegelt; D9 aus verriegelt erst bei Fahrzeug (B).
Bei den Fehlern zeigt `status` "CP E": Das ist der gemeldete Status (mit Fehler E,
SOLL-CP-08), nicht der Pegel.

**C2 Fahrzeug erkannt (B), PWM.** S6 auf 220 Ohm (32 A), Schlüssel ein, S1 ein.

- Erwartet: Verriegeln (Motor, LD-Impuls, Connect-LED an), RCM-Selbsttest (im Protokoll als
  Zustandsfolge S3 -> S4 -> S5, kein eigenes Ereignis), S5 PWM_ON, PWM
  1 kHz mit 26,6 % (16 A, Drehschalter 4), `cp_pos` etwa 9 V, `cp_neg` etwa 12 V, Ready-LED
  2 Hz, Front-LED grün (CR 40: B ohne Fehler).
- Multimeter (DC) an CP-PE zeigt den Mittelwert, etwa 0,266 * 9 V - 0,734 * 12 V = **-6,4 V**.
- Strom über die Schnittstelle: `current 10000` -> Tastgrad 16,6 % (abgeschnitten), Mittelwert etwa
  -8,5 V; zurück mit `current 80000`.
- Schlüssel aus -> Ladeerlaubnis weg, PWM aus (CP +9 V DC); Schlüssel ein -> PWM wieder an.
- Nach `wakeup_delay` (30 s) in B: Weckfolge einmal je Steckvorgang, 3 s -12 V DC, 0,75 s
  +12 V DC, dann wieder PWM; kein Protokolleintrag, nur am Oszilloskop zu sehen (Zustand bleibt S5).
- S1 aus (A) -> PWM aus, Entriegeln (Motor), S1.

**C3 Laden ohne Schützspule (C).** S1 ein, warten bis PWM, dann S2 ein.

- Erwartet: S6 CHARGING, `contactor` (Soll) 1, Ready-LED an, Front-LED blau (VR 16: C oder D);
  kein Klicken, weil die Spule abgeklemmt ist; an der Steckdose keine Netzspannung.
- Relais C3/C4: schließt mit C1/C2 (Testgerät 2026-11-07, Durchgang C3-C4, an C3/C4 nichts angeschlossen).
- S2 aus (B) -> Schütz-Soll aus, PWM bleibt; S2 ein -> wieder ein (frühestens nach `contactor_min_off`, ab Werk 5 s).
- S3 statt S2 (D) -> ERR-STATE-D, Schütz-Soll aus.
- S4 kurz drücken während C -> ERR-CP-INVALID, Schütz-Soll sofort aus; nach dem Loslassen
  (>= 1,2 V) erlischt der Fehler und es lädt ohne Abstecken wieder (SOLL-SM-91, wie Original).
- S5 kurz drücken während PWM -> ERR-DIODE, CP 3 s -12 V, dann +12 V (SOLL-CP-07), Schütz-Soll aus;
  der Fehler bleibt nach dem Loslassen bis zum Abstecken (S1 aus).
- Schlüssel aus während C -> S7 STOPPING, PWM aus; Schütz-Soll bleibt, bis S2 aus (das
  Fahrzeug reagiert) oder nach `stop_timeout` 5 s, dann S3.
- Die Schützüberwachung (Via Sense, CT) prüft nur im Aus-Zustand; ohne Spule bleibt sie
  ruhig. Ein ERR-CONTACTOR hier wäre ein Befund (CT-Beschaltung).

**C4 Fehlerstrom und Reaktionszeit (ohne Schützspule).** Ziel: SOLL-PROT-04/05/07/08/11
auf der Hardware, Zeit vom Fehlerstrom bis zum Abschalten (Bender verlangt Schütz aus binnen
20 ms, Wallfree-Anteil <= 1 ms). Fehlerstrom ohne Netzspannung: Gleichstrom über einen
zusätzlichen Draht durch den Summenstromwandler.

Aufbau (Nutzer, F1 aus): Prüfdraht einmal durch den gelben Ringkern führen, außerhalb der
Wallbox: Batterie + -> R5 -> S7 -> Prüfdraht -> Batterie -. Der Draht berührt keine Klemmen.

- **C4a Fehlerstrom im Ruhezustand (kein Stecker).** S7 kurz drücken. Erwartet: ERR-RCM-DC6
  (SWITCH_2), Front-LED rot, Ereignis im Log; nach dem Loslassen Selbsttest in A frühestens 30 s
  nach dem letzten Versuch (`rcm_retest_interval`), bestanden -> Fehler gelöscht. S7 gedrückt
  halten während des Selbsttests -> Selbsttest scheitert, Fehler bleibt (Gegenprobe).
- **C4b Fehlerstrom beim Laden.** Prüfstecker wie C3 (S1 + S2, S6 CHARGING, Schütz-Soll 1),
  dann S7. Erwartet: Schütz-Soll sofort 0, CP +12 V DC, ERR-RCM-DC6 verriegelt (nur DC6; die
  Simulation setzt immer DC6 und AC30 zusammen). S2/S1 aus und
  wieder ein lädt nicht, bis in A (S1 aus) ein Selbsttest bestanden hat (SOLL-PROT-08/11).
- **C4c Reaktionszeit.** Durchgeführt an **C3/C4** (schaltet mit C1/C2 über PE2, nichts angeschlossen, keine
  Änderung an C1/C2 nötig): Batterie + -> R6 -> C3, C4 -> Batterie -; Kanal 1 zwischen R5 und S7 (fällt mit dem
  Fehlerstrom), Kanal 2 an C3 (steigt beim Öffnen). Ergebnis das Testgerät: 2,5..3 ms. Ursprünglicher Plan an C1/C2:
  Messpunkt für das Abschalten ist der Relaiskontakt C1/C2; PE2 auf der
  Platine erst, wenn ein Messpunkt bekannt ist (Fotos). Bedingung für die Kontaktschleife: Der
  Nutzer hat beide Adern an C1 und C2 spannungsfrei abgeklemmt und mit F1 ein gemessen, dass an
  C1 und C2 gegen PE, L und N **keine Spannung** liegt, bei offenem und (in C3) geschlossenem
  Relais. Liegt Spannung an, entfällt C4c in dieser Form. Schleife: Batterie + -> R6 -> C1,
  C2 -> Batterie -; Oszilloskop-Masse an Batterie -, Kanal 1 hinter S7 (springt mit dem
  Fehlerstrom), Kanal 2 an C1 (0 V bei geschlossenem Relais, 9 V nach dem Öffnen). Auslösen
  auf Kanal 1. Gemessen wird Sensor + Software + Relaisabfall zusammen; Sollwert < 20 ms.
  Drei Wiederholungen; Claude liest dazu Fehler und Log.
- Der AC-Zweig (SWITCH_1, 30 mA rms) bleibt ungeprüft: dafür bräuchte es einen Wechselstrom
  von 30 mA im Prüfdraht.

**C5 Laden mit Schütz.** F1 aus, Prüfdraht entfernen, Adern an C1/C2 wieder anklemmen und
Schützspule anschließen (Nutzer, spannungsfrei), F1 ein. Wiederholen von C3:

Ab hier wird der Prüfstecker nur bei F1 aus gezogen oder gesteckt: Ohne Stecker wären die
Buchsenkontakte offen, und bei eingeschaltetem Schütz läge dort Netzspannung.

- Schütz klickt bei C, fällt bei B, E, Diodenfehler, Schlüssel aus.
- `ct_sense` 1 bei eingeschaltetem Schütz, 0 nach dem Abschalten (Via Sense).
- Optional misst der Nutzer an den großen Klemmen am Schützausgang L-N (230 V bei ein, 0 V bei
  aus); am Prüfstecker liegt keine Netzspannung (Abschnitt 1).
- Nach jedem Aus: kein ERR-CONTACTOR (Via Sense sieht keine Spannung mehr).

**C6 evcc.** evcc auf dem PC (Docker wie in [Test und Simulation](test-und-simulation.md#evcc-gegen-die-simulation),
Vorlage `tinkerforge-warp-ws`, eigene Konfiguration gegen das Testgerät, Zugangsdaten des Testgeräts),
Schützspule wie in C5. Erwartet: evcc verbindet; S1 ein -> "connected", S2 ein -> "charging";
Modus Aus -> Ladestrom 0 = Stromsperre: PWM aus (CP +12 V DC), Schütz fällt, sobald S2 aus ist,
sonst nach `stop_timeout` 5 s; Modus Sofort -> PWM und Schütz wieder ein;
Strom in evcc ändern -> Tastgrad folgt; S1 aus -> "disconnected". Ohne Zähler und ohne Last
zeigt evcc keine Leistung.

**C7 offen für später.** Echtes Fahrzeug (H3), Fehlerstrom während des Ladens mit Last, AC-Zweig.

## 6. Bezug zu den Anforderungen

C1: SOLL-PP-01..05. C2: SOLL-CP-01..05, SOLL-LOCK-14/23, SOLL-EN-01, SOLL-LED-03/04,
SOLL-CUR. C3/C5: SOLL-SM, SOLL-CP-06/07, SOLL-PROT-20/21, SOLL-RT-20. C4: SOLL-PROT-04/05/07/08,
SOLL-PROT-10..12, SOLL-CP-09. C6: evcc-Anbindung (WARP-Schnittstelle). Ergebnisse werden in
[Implementierung](implementierung.md#offen) und [wallbe2 Hardware](../../wallbe2/docs/hardware.md)
eingetragen.

## 7. Freigabe Stufe C

Erteilt (Nutzer 2026-10-30); der verbindliche Wortlaut steht in den Sicherheitsgrenzen von
[wallbe2/AGENTS.md](../../wallbe2/AGENTS.md). Sie gilt für C1..C6 am Testgerät ohne Fahrzeug und Last.
Nicht enthalten: C7, die Befehle `bootloader`, `factory-reset` und `import-original`.

## 8. Stufe D: Laden mit Fahrzeug (zweites Gerät)

Geplant und freigegeben (Nutzer 2026-11-07, [wallbe2/AGENTS.md](../../wallbe2/AGENTS.md)); durchgeführt wird später. Ziel: was der Prüfstecker nicht nachbilden
kann, also Laststrom, ein Fahrzeug, das auf Ladesignal und Schütz reagiert, und lange Laufzeit
(Inbetriebnahmestufe H4, [Architektur](architektur.md#7-hardware-inbetriebnahme)).

**Aufbau.** das zweite Gerät (baugleiche Steuerung, in der Garage fachgerecht installiert, FI in der
Hausverteilung) statt das Testgerät. Ladekabel **fest angeschlossen**, **keine Verriegelung**
(deaktiviert, kein Motor eingebaut). Testfahrzeug **Nissan Leaf ZE0**, lädt nur **einphasig**.
Einstellungen des zweiten Geräts laut Weboberfläche des Originals: [wallbe2 Hardware Abschnitt 3](../../wallbe2/docs/hardware.md#3-ist-einstellungen-laut-weboberflaeche-box-g)
(u. a. D1 an trotz festem Kabel, D4/D6 aus, IN 20 A, Schützüberwachung aus).

**D0 Vorbereitung**

- Einstellungen der Originalfirmware festhalten (Weboberfläche, Bildschirmfotos). Die Firmware
  wird nicht gesichert: `SL-01.04.24` liegt vor (Rückweg über den Bootloader), Sektor 3 lässt
  Wallfree unverändert. Kein ST-LINK am zweiten Gerät, nur im Notfall.
- Wallfree über die Weboberfläche aufspielen wie beim Umrüsten ([Umrüsten](umruesten.md)).
  Das zweite Gerät (2026-11-08): `SL-01.04.24` übergibt dem Bootloader keine Adresse, er wartet daher unter
  192.168.0.8 (Bootloader-Version 1.03, MAC `xx:xx:xx:d4:e5:f6`); Upload über die USB-Karte am PC
  mit `scripts/update.sh --upload-only 192.168.0.8`. Wallfree (`23f3260`) bezieht wie das Original
  die Adresse per DHCP und bekommt im Heimnetz mit derselben MAC wieder 192.168.1.60, Name
  `wallfree-d4e5f6`.
- Das zweite Gerät bleibt im Bootloader hängen** (2026-11-08, Ursache offen): Nach Aus/Ein der Box
  (lief `23f3260`) und beim Update über die API von `46edcb3` auf `7eb08f9` (Übergabe an den
  Bootloader mit Adresse 192.168.1.60, Upload ohne Fehlermeldung, beim Laden mit Schlüssel)
  meldete sich Wallfree nicht zurück; das zweite Gerät war im Heimnetz verschwunden und wartete unter
  192.168.0.8 ("Bootloader-Version: 1.03") ohne Zeitablauf, nach über 10 min noch. Abhilfe
  jeweils `scripts/update.sh --upload-only 192.168.0.8` über die USB-Karte, danach startet
  Wallfree (`update_result` Modus 4). Nach dem Update lud der Leaf wieder (Schlüssel, S6). An
  Das Testgerät läuft derselbe Update-Weg zuverlässig (Modus 2). Modus 4 heißt beim untersuchten
  Bootloader des Testgeräts "kein startbares Programm" (ungültiger Stack-Pointer bei `0x08010000`),
  ohne Zeitablauf; das zweite Gerät hat Bootloader 1.03 (am 2026-11-09 mit `flashread` ausgelesen, Analyse
  offen, Optionsbytes: BOR aus). Update `7eb08f9` -> `9f39b42` am 2026-11-09 per DHCP
  (192.168.0.223 an der USB-Karte), nach dem Upload nur Ping statt `GET`: Wallfree nach etwa 10 s
  zurück unter derselben Adresse, Modus 2 ok, 192.168.0.8 nie erreichbar. Laut Analyse von 1.03
  landet der Bootloader nach einer Übergabe nur dann unter 192.168.0.8, wenn alle Upload-Versuche
  gescheitert sind; der erste Hänger folgte laut Gesprächsverlauf auf ein Update über die
  Weboberfläche, nicht auf ein Aus/Ein (nicht mehr sicher feststellbar). Abgebrochen hat die
  Uploads sehr wahrscheinlich ein Node-RED-Ablauf auf einem Server im Heimnetz, der die Webseite der Wallbox
  regelmäßig abfragte (im Heimnetz, an der USB-Karte nicht). Der dritte Hänger (2026-11-09
  17:49, Update im Heimnetz) kam von einem Mitschreibe-Skript am PC, das alle 2 s abfragte;
  danach an der USB-Karte per `--upload-only` an 192.168.0.8 auf `208a7f7` gebracht. Andere Aus/Ein an
  Das zweite Gerät liefen ohne Hänger (D3.9 vormittags mit `23f3260`, D3.9 abends mit `7eb08f9`). Analyse:
  [wallbe2 Bootloader](../../wallbe2/docs/bootloader.md#bootloader-103-box-g).
  Beim nächsten Hänger zuerst nur Aus/Ein versuchen und nach dem Start die Konsole `boot`
  (RCC_CSR, BKP10..16) sichern.
- `import-original` (2026-11-08, Sicherungen `wallbe2/analysis/box-g/import-2026-11-08/`): 15 Werte
  der Ladelogik und das Netz (DHCP) übernommen, alle gleich der Original-Weboberfläche: ER/LR/VR/CR
  15/17/16/40, LD/EN/ML/XR/IN 22/1/4/2/12 (IN 20 A), Schützüberwachung aus, D abweisen an,
  Verriegelungszeiten 0,5/0,5/2 s. DIP gelesen D1, D2, D3, D7, D10 (583), Drehschalter 4 (16 A),
  `contactor_min_off` 5 s, Zustand A ohne Fehler. `cable` war dabei 0, weil das Ladekabel
  abgeklemmt war; nach dem Anklemmen muss der PP-Widerstand des Kabels einen Wert liefern
  (Original beim Laden: 32 A, Bildschirmfoto 2026-10-19), sonst meldet Wallfree mit D1 im Zustand B
  ERR-PP-INVALID (SOLL-PP-04).
- evcc-Ladepunkt in der Oberfläche auf 1 Phase gestellt (Nutzer 2026-11-08).
- evcc gegen das zweite Gerät: `scripts/evcc-boxg.sh` (evcc 0.317.2 lokal auf dem PC, Oberfläche :7070).
  Vorlage [tools/evcc/box-g.example.yaml](../tools/evcc/box-g.example.yaml), die Datei mit
  Zugangsdaten liegt in `build/evcc-boxg/evcc.yaml` (nicht im Repo). Inhalt (Nutzer 2026-11-08):
  Fronius Solar API v1 als Netz-, PV- und Speicherzähler (nur lesend, ohne Anmeldung), Leaf über
  OVMS (Ladestand, `phases: 1`, 6..16 A), Ladepunkt startet in "aus" (D1/D2 mit "sofort", PV erst
  für D4), Intervall 10 s, feste Tarife. Nicht übernommen aus der alten Konfiguration: Modbus-Lader
  `phoenix-ev-eth` (Wallfree hat kein Modbus), `plant` (Kennung der alten Instanz),
  Fahrzeug-`identifiers` (Wallfree erkennt kein Fahrzeug), Stromgrenzen am Ladepunkt (in der
  evcc-Oberfläche). Der alte evcc-Container ist dabei aus. Erster Start (2026-11-08): Zähler
  grid/pv/battery und Fahrzeug verbunden, zweites Gerät "car disconnected", keine Fehler; evcc warnt bei
  Intervallen unter 30 s ("unexpected behavior"), für die Prüfungen bewusst 10 s.
- evcc für den Langzeittest (D4) auf einem Server im Heimnetz (Portainer), getrennt vom alten Container
  `evcc-fixed-0.204.3`, der gestoppt bleibt: Image `evcc/evcc:0.319.0-chargerSwitchDuration10m`,
  Konfiguration `evcc.yaml auf dem Server` (wie oben, aber Intervall 30 s, Modus PV
  nach jedem Neustart von evcc, OVMS-`cache` 2 min statt 15 min, damit das Limit kaum überschritten
  wird), eigenes Volume `evcc-wallfree-data`, Port 7070. Stack für Portainer: Dienst
  `evcc-wallfree` mit Befehl `evcc`, `restart: unless-stopped`, `TZ=Europe/Berlin`, die
  Konfiguration nur lesend nach `/etc/evcc.yaml`. Am Ladepunkt in der Oberfläche 1 Phase
  eingestellt, Standard-Limit des Leaf 80 % (Plan, Ankunft). Das OVMS-Modul meldet an den v2-Server
  alle 300 s ohne und alle 60 s mit verbundener App; der v3-Server (MQTT) passt nicht zur
  evcc-Vorlage `ovms`. Das Image ist evcc 0.319.0 mit einem
  Patch (Branch `wallfree/charger-switch-duration` in `einem eigenen evcc-Klon`): `chargerSwitchDuration`
  10 min statt 60 s, damit evcc den Leaf nach PWM-Aus nicht wieder einschaltet, solange er noch
  in C steht (Befund 4). Gebaut ohne buildx: Oberfläche und Programm in den Build-Containern
  aus dem evcc-Dockerfile (vite-plus, golang-alpine), das Programm dann in das offizielle Image
  0.319.0 gelegt. Nicht gleichzeitig mit `scripts/evcc-boxg.sh` betreiben (zwei Regler).
- Strom messen: Anzeige im Fahrzeug bzw. App, oder Stromzange an der Zuleitung (Nutzer, nur an
  isolierten Leitungen).

**D1 Grundablauf**

1. Normales Laden: Fahrzeug anstecken, B -> C, Schütz ein, Fahrzeug lädt mit dem eingestellten
   Strom auf L1.
2. CP-Pegel des echten Fahrzeugs in B und C (`cp_pos`, `cp_neg`): Abstand zu den Bandgrenzen.
3. Ladeende durch das Fahrzeug (Ladeziel erreicht): C -> B, Schütz ohne Last aus, kein Fehler.
4. Abstecken nach dem Ende, Zustand A, kein Fehler.

**D2 Steuerung beim Laden**

5. Strom über evcc ändern (16 -> 10 -> 6 A): Fahrzeug folgt; unter 6 A Stromsperre.
6. Pausieren und Fortsetzen (evcc Aus/Sofort): Fahrzeug geht auf B, Schütz fällt ohne Last, nach
   der Pause von 5 s wieder ein.
7. Schlüssel aus beim Laden: geordnetes Ende, das Fahrzeug senkt den Strom vor dem Schütz-Aus.

**D3 Sonderfälle**

8. Weckfolge: Fahrzeug lange in B (evcc gibt noch nicht frei) und schläft ein; wacht es mit der
   Weckfolge wieder auf, wenn evcc später freigibt?
9. Netzausfall beim Laden (Sicherung aus/ein): Wiederanlauf mit gestecktem Fahrzeug.
10. Neustart und Update bei gestecktem Fahrzeug.

**D4 Dauer**

11. Mehrstündige Ladung: kein Neustart, evcc und MQTT stabil, Prozessortemperatur, Thread-Stacks.

Bewusst nicht mit dem Fahrzeug: Fehlerstrom unter Last und der AC-Zweig (30 mA), dafür ein
Prüfaufbau mit Lastwiderstand. Dreiphasiges Laden und Verriegelung prüft dieser Aufbau nicht.

**Ergebnisse 2026-11-08** (Wallfree `23f3260`, Leaf mit 72 % Ladestand, evcc 0.317.2). Die
Ladeleistung ist aus dem Leistungsfluss des Fronius gerechnet (PV + Speicher + Netz - Haus), zweites Gerät
hat keinen Zähler.

- D1.1/2: Leaf angesteckt -> B mit +8,96 V, `cable` 32 A, keine Fehler; S3 wartet auf Freigabe.
  Mit `enable_register` 1 (Schlüssel EN aus, siehe Befund 3): S3 -> S4 -> S5 nach 2,4 s, Schütz
  0,9 s später ein, S6; C mit +6,03 / -11,49 V, Tastgrad 26,6 %, etwa 3,7 kW; `ct_sense` folgt dem
  Schütz (zweites Gerät hat den Hilfskontakt angeschlossen).
- D2.5: evcc 10 / 6 / 16 A -> Tastgrad 16,6 / 10,0 / 26,6 %, gerechnet etwa 2,3 / 1,4 / 3,7 kW;
  4..6 s vom evcc-Befehl bis zum Wert in der Box. Unter 6 A nicht geprüft (evcc sendet nicht unter
  `minCurrent`).
- D2.6: evcc Aus -> S7, PWM aus; der Leaf nimmt den Strom binnen höchstens etwa 3 s zurück
  (Fronius, 1-s-Auflösung), Schütz 5,0 s nach S7 aus, also ohne Last; S3. Der Leaf **bleibt in C**
  (+6,03 V DC) statt auf B zu gehen; evcc meldet alle 10 s "charger logic error: disabled but
  charging". Fortsetzen nach 31 s: S6 und Schütz ein binnen 0,1 s; der Leaf zieht erst etwa 35 s
  später Strom.
- Auslastung beim Laden: Stacks höchstens 73 % (`timesync`), Prozessor 18 °C.
- D2.7 Schlüssel (evcc Sofort 16 A, `enable_register` 0): Schlüssel ein -> `inputs` 2, S4, S5,
  S6 nach 3,3 s, Schütz ein, `ct_sense` 1, Leaf lädt; ohne Schlüssel blieb die Box in S3.
  Schlüssel aus beim Laden -> S7, Schütz 5,0 s später aus, S3, keine Fehler; Leaf bleibt in C.
  Laut Fronius (Box-Zeit um 2,06 s korrigiert, siehe Befund 6) fällt die Last 2..5 s nach S7, das
  Schütz öffnet 5,0 s nach S7: ohne nennenswerte Last, aber mit wenig Abstand (bei der evcc-Pause
  D2.6 fiel die Last schon nach etwa 1 s). Auflösung des Fronius etwa 1 s, Verzögerung unbekannt.
- D3.9 Netzausfall beim Laden (Sicherung etwa 10 s aus): zweites Gerät startet (`power_on`), Leaf in C,
  S3 ohne Fehler; kein Weiterladen, weil `enable_register` wieder 0 ist (Befund 3).
  `external_current` steht nach dem Start auf 80 000 (Festlegung `external_current_default`).
  evcc gerät in die Schleife aus Befund 1, die Box fällt danach aus dem Netz (Befund 2).
- D1.4 Abstecken (nach Aus/Ein, evcc frisch, Modus Aus): S3 -> S1, Entriegeln-Merker 1, keine
  Fehler; evcc "car disconnected" 9 s später.

**Ergebnisse 2026-11-08 abends** (Wallfree `7eb08f9`, Leaf mit 67 % Ladestand, Ladeziel im Leaf
80 %, 24 kWh, evcc 0.317.2 im Modus Sofort mit 16 A, Schlüssel ein). Abfrage alle 2 s.

- D1.1: angesteckt 18:08:44 -> B mit +8,95 V, evcc gibt frei; 18:08:47 C mit +6,04 / -11,50 V,
  Tastgrad 26,6 %, Schütz ein, `ct_sense` 1 im nächsten Abfragetakt.
- D3.9 Netzausfall beim Laden (Sicherung etwa 20 s aus, 18:32:33): `ct_sense` fiel im letzten
  Abfragetakt vor dem Ausfall auf 0, während die Steuerung das Schütz noch als ein führte (Schütz
  fällt beim Spannungseinbruch vor der Steuerung ab). Das zweite Gerät 18:33:01 wieder erreichbar
  (`power_on`), Leaf in C, keine Fehler; mit dem Schlüssel S3 -> S6 und Schütz ein 8,3 s nach dem
  Start, Leaf lädt weiter. evcc bemerkte den Abbruch der alten Verbindung 18:33:03
  (`connection reset by peer`), baute neu auf und blieb ohne weitere Fehler verbunden, keine
  Schleife; Ping und HTTP durchgehend erreichbar. Log der Box (Konsole `log`): "WebSocket
  abgewiesen (401)" 8,3 s und 22,1 s nach dem Start, danach keine weitere Abweisung.
- D1.3 Ladeende durch das Fahrzeug: 18:47:15 C -> B (+8,97 V), Schütz im selben Takt aus, Box in
  S5 mit weiter angebotener PWM (26,6 %), keine Fehler; evcc "stop charging" 18:47:24, Ladestand
  laut evcc 78 %, geladen laut evcc etwa 2,4 kWh (geschätzt, zweites Gerät hat keinen Zähler). Ob der
  Leaf den Strom vor dem Wechsel auf B zurücknahm, ist nicht gemessen (kein Zähler, Fronius nicht
  mitgeschrieben).
- Ladeende durch evcc mit dem gepatchten evcc auf einem Server im Heimnetz (Limit 80 % am Fahrzeug in evcc, im Leaf
  keine Begrenzung, Modus Sofort, 1 Phase): 21:20:00 S6 mit 16 A; 21:24:46 S7, PWM aus; Schütz
  21:24:50 aus, `ct_sense` folgt; Leaf bleibt in C bis 21:29:52, dann B mit +8,97 V, also wieder
  **5 min 6 s** (wie bei Befund 4). evcc meldete in der Zeit 10-mal "disabled but charging" (alle
  30 s), schaltete aber nicht wieder ein; "stop charging" 21:29:50. Keine Fehler in der Box.

Befunde, Umgang wird später geplant (Nutzer 2026-11-08):

1. **evcc nach Neustart der Box in Dauerschleife:** Nach dem Aus/Ein des zweiten Geräts baute evcc den
   WebSocket etwa 110-mal je Sekunde neu auf; erst ein Neustart von evcc half. Ursache: Zephyr
   sendete das 101 vor der Prüfung der Anmeldung, Wallfree schloss danach jede Verbindung mit
   der Nonce aus dem vorigen Start. evcc sah einen gelungenen Aufbau, versuchte es ohne Pause
   erneut und bekam nie das 401, nach dem es sich neu anmeldet. In der Simulation fehlte der
   Fehler, weil sie bei jedem Start dasselbe Nonce-Geheimnis würfelte (Fallstricke).
   **Behoben** (Zephyr-Patch, [Festlegung WebSocket-Anmeldung](implementierung.md#api-und-anmeldung)):
   Abweisung vor dem Upgrade mit 401 `stale=true`. Geprüft am Testgerät mit evcc 0.317.2 und
   `7eb08f9` (2026-11-08): Neustart der Box um 15:09:24, Ping und HTTP nach 13 s wieder da;
   evcc hielt die alte Verbindung bis zum RST um 15:09:54 (bis dahin `unknown evse status: 4`
   aus dem letzten Stand), baute dann neu auf: genau eine Abweisung (401 im Log der Box), danach
   verbunden ohne weitere Fehler, keine Schleife. Mit `23f3260` im selben Aufbau: Schleife mit
   etwa 132 Aufbauten/s, nach 5297 in 40 s kein Ping (Befund 2). Am zweiten Gerät mit `7eb08f9` den
   Netzausfall beim Laden wiederholt: keine Schleife, Box erreichbar (Ergebnisse abends).
2. Das zweite Gerät fällt aus dem Netz** (zweimal, beim zweiten Mal gezielt nach D3.9): Schleife ab
   10:07:19 mit etwa 125 WebSocket-Verbindungen je Sekunde; letzte angenommene Verbindung
   10:08:08 (rund 6000 nach Beginn), HTTP ab etwa 10:08:13 ohne Antwort, Ping und ARP ab etwa
   10:08:45 tot. Keine Erholung in 10 min, auch nicht nach Ziehen und Stecken des Ethernet-Kabels
   bei gestopptem evcc. Die Ladelogik lief weiter (Power-LED 1 Hz, Error aus, Ready/Connect wie
   S3 mit Fahrzeug), kein Watchdog-Reset; nur Aus/Ein half. Das Ereignisprotokoll geht dabei
   verloren (Backup-SRAM ohne VBAT). **Ursache** (am Testgerät nachgestellt, 2026-11-08: evcc gegen
   `23f3260`, Neustart der Box, nach 5297 Versuchen in 40 s kein Ping, kein ARP): Alle 14
   RX-Pakete hingen an bereits freigegebenen Netzkontexten (per SWD gelesen), jedes ein
   Close-Rahmen von evcc. Trifft der Rahmen ein, während der Server-Thread den Socket schließt,
   stellt Zephyr 4.4.2 ihn in die Warteschlange des schon freigegebenen Kontexts, wo ihn nichts
   mehr freigibt ([Fallstricke](implementierung.md#fallstricke)). **Behoben** mit dem
   Zephyr-Patch `0002`: am Testgerät mit `net-load.py wsreject` (antwortet wie evcc sofort auf den
   Close-Rahmen) ohne Fix ein verlorenes Paket nach etwa 350 Versuchen, mit Fix 1880 Versuche
   ohne Verlust.
3. **Freigabe ohne Schlüssel:** Mit D10 gab nur EN oder `enable_register` frei; evcc schreibt
   nur den Strom (WARP `evse/external_current`) und konnte so ohne Schlüssel nicht starten. Mit
   Schlüssel lud die Box auch ohne evcc (2026-11-09 rund 2 h mit 16 A bis fast voll, Startwert
   `external_current` 80 000), ohne Schlüssel lud der Leaf mit evcc gar nicht (2026-11-12).
   **Gelöst** nach F34 (Nutzer 2026-11-12): Mit D10 gibt `external` > 0 frei, Startwert mit D10
   ist 0; mit D7 und D10 lädt der Schlüssel unabhängig von evcc mit dem Höchststrom
   (SOLL-EN-01/12/13, SOLL-CUR-15). In der Simulation mit evcc geprüft, am zweiten Gerät noch nicht.
4. **Leaf bleibt nach PWM-Aus in C** (D2.6, nach Nutzer auch beim Ladeende mit dem Original):
   evcc kam mit dem Original nie gut damit zurecht, wenn das Fahrzeug nicht bald auf B zurückfiel
   (Nutzer 2026-11-08; offen, ob es an der Originalfirmware lag). Gemessen nach dem Ende über evcc
   (S7 11:34:26, PC-Zeit): C mit +6,05 V bis 11:39:31, dann B mit +8,97 V, also **etwa 5 min 6 s**
   nach dem PWM-Aus (abends noch einmal genau so, siehe oben). Bis dahin meldete evcc alle 10 s
   "disabled but charging" (38-mal), danach nicht mehr. evcc schaltet nach fest 60 s
   (`chargerSwitchDuration`, nicht einstellbar) wieder ein. Umgang (Nutzer 2026-11-08): Wallfree
   meldet den Zustand weiter so, wie er ist (C); gelöst in evcc. Lokal mit Patch auf 10 min
   geprüft (kein Wiedereinschalten), Issue bzw. PR bei evcc steht aus (dort besser einstellbar).
5. **Schützüberwachung:** das zweite Gerät hat den Hilfskontakt (`ct_sense` folgt dem Schütz), übernommen war
   aus dem Original `contactor_monitor` 0. Eingeschaltet (1, Via Sense, Nutzer 2026-11-08) und mit
   Laden, evcc-Pause (17 s), Fortsetzen und Ende geprüft: Schütz jeweils 5,0 s nach S7 aus,
   `ct_sense` folgt, kein ERR-CONTACTOR. Erledigt.
6. **Uhr lief etwa 500 ppm zu langsam** (zweites Gerät und das Testgerät): zweites Gerät ging 3,95 s nach, 2,2 h nach dem
   letzten Abgleich (2026-11-09). Gemessen über 10 min (Laufzeit aus `GET /system/info` gegen die
   PC-Uhr per NTP, Ausgleichsgerade, Restfehler unter 6 ms): zweites Gerät −497 ppm, Testgerät −571 ppm. Der
   CPU-Takt selbst stimmte (Testgerät, `DWT_CYCCNT` per SWD: −53 ppm, Quarz). **Ursache:** Der
   SysTick-Treiber von Zephyr verlor bei jedem Neuprogrammieren einige Takte, bei etwa 1800
   Aufrufen je Sekunde rund 516 ppm ([Fallstricke](implementierung.md#fallstricke)).
   **Behoben** mit Zephyr-Patch `0003` (`5cf8234`, Ausgleich beim Start gemessen: 3,0 Takte): an
   Das Testgerät Zephyr-Zählung gegen den CPU-Takt +0,9 ppm, gegen die PC-Uhr −52,7 ppm (10 min), also
   nur noch der Quarz (etwa 4,5 s am Tag, mit dem stündlichen Abgleich unter 0,25 s). Das zweite Gerät noch mit
   `208a7f7`. Der 32-kHz-Taktgeber an PC14 (treibt die RTC) liegt am Testgerät bei etwa −2,6 ppm
   (2026-11-11, [wallbe2 hardware.md](../../wallbe2/docs/hardware.md)); Wallfree nutzt ihn nur
   über die RTC (Uhrzeit über Neustarts, SOLL-TIME-04); die laufende Uhr zählt mit dem Quarz und
   wird stündlich per NTP abgeglichen (SOLL-TIME-03).
