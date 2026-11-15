# Prüfprotokoll

Prüfungen von Wallfree an der Hardware, mit Einzelwerten. Zusammenfassung:
[Implementierung](implementierung.md#prüfungen-auf-der-steuerung). Bestandene Prüfungen belegen
keine elektrische Sicherheit.

Geprüft wurde an zwei Ladestationen wallbe Eco 2.0. Das **Testgerät** steht auf dem Tisch
(Firmware 0.81 ab Werk, Bootloader und Originalparameter unverändert); Prüfstecker als
Fahrzeugnachbildung ([Prüfstecker](pruefstecker.md)), Schütz ohne Last, die Schützspule nur
zeitweise abgeklemmt. Das **zweite Gerät** ist fest installiert (Ladekabel fest angeschlossen,
keine Verriegelung, Bootloader 1.03) und lädt ein Fahrzeug. Steuerung über Shell/API von
Wallfree; unter Spannung wurde nur an den Klemmen gemessen, nie auf der Platine. Kennungen wie
`3e05884` bezeichnen den geprüften Stand der Firmware.

## 1. Testgerät

| Bereich | Ergebnis |
| --- | --- |
| Start, Netz | Start über den Original-Bootloader, Takt, Ethernet/PHY, DHCP; Geräte-MAC aus dem OTP, Hostname aus dem MAC-Ende |
| Eingänge (Stufe A) | EN (Schlüsselschalter, 0/12 V), ML, XR, IN je 12 V -> `inputs` Bit 1..4; XR mit D8: ab -> S10, an -> S1; IN Code 8 -> 6 A, Code 11 -> 16 A |
| Ausgänge, LEDs (Stufe A) | ER/LR/VR/CR je 12 V an der Klemme (Register-Ausgänge 30..33); Front-LED ER rot, VR blau, CR grün; Power- und Error-LED; mit `dip_override` D8 -> S10, CP -12 V, Error-LED an (`dip_override` in S10 nicht schreibbar, SOLL-PAR-40) |
| Verriegelung (Stufe B) | LD ist ein Impuls von grob 60..100 ms während der Fahrt: Code 22 nötig, mit Code 3 schlägt Verriegeln fehl; mit D9 aus entriegelt der Automat in S1 bei CP A sofort wieder (SOLL-LOCK-23). Das Testgerät: `in_function_ld` 22, `out_function_cr` 40 (grün im Leerlauf wie das zweite Gerät, Nutzer 2026-10-24) |
| Stufe C mit Prüfstecker (`3e05884`, 2026-11-07, Schützspule abgeklemmt, CT angeschlossen) | C1 PP-Klassen: 1,5 kOhm -> 13 A und ERR-CABLE-REJECT, 680/220/100 Ohm -> 20/32/63 A ohne Fehler, 47 Ohm -> ERR-PP-INVALID (`cable` -1), offen -> 0; Fehler erlöschen mit der Stellung, gemeldeter Status dabei E; CP bleibt A mit +11,90..11,92 V, keine Verriegelung. C2 (S6 220 Ohm, Schlüssel ein, S1): B mit +8,95 V, Verriegeln, Selbsttest, nach 2,4 s S5 PWM 26,6 %; Plateaus +8,95 / -11,48 V; Multimeter -6,11 V (gerechnet -6,05 V); `current 10000` -> 16,6 %, -8,12 V; Front-LED grün, Ready-LED 2 Hz; Schlüssel aus -> S3, +8,95 V DC, ein -> S5, -6,1 V; S1 aus -> +11,95 V, Entriegeln, S1. Weckfolge nicht am Oszilloskop beobachtet. C3 (S2): S6 CHARGING, C mit +6,05 V, Relais C1/C2 klickt, **C3/C4 schließt mit** (PE2 schaltet beide Relais), Front-LED blau, Ready-LED an, `ct_sense` 0 (Spule ab); S2 aus/ein -> S5/S6; S3 (D) -> ERR-STATE-D, Relais aus, Front-LED rot, bleibt nach S3 aus bis CP A (SOLL-SM-52); S5 -> ERR-DIODE, Relais aus, nach 3 s +12 V DC; Schlüssel aus beim Laden -> S7, Relais fällt nach 5,0 s (`stop_timeout`), S3. **Befund S4** (Kurzschluss CP-PE während C): ERR-DIODE statt ERR-CP-INVALID, Laden kommt erst nach dem Abstecken wieder; behoben über SOLL-CP-07 (Diodenprüfung nur bei positivem Plateau >= 2,0 V). C4 (Prüfdraht durch den Summenstromwandler, 9-V-Block über 1 kOhm, etwa 9 mA DC): C4a in A -> ERR-RCM-DC6, Front-LED rot, Selbsttest 32 s später bestanden, Fehler gelöscht; S7 45 s gehalten -> Selbsttest während des Fehlerstroms scheitert, Fehler erst nach dem Loslassen gelöscht (63 s). C4b beim Laden -> ERR-RCM-DC6, Schütz-Soll im selben Millisekundenschritt aus (Ereignisprotokoll), Relais fällt, CP E; S2 aus/ein lädt nicht; nach S1 aus Selbsttest in A bestanden, dann lädt es wieder. Vorher ohne Schlüssel: S1 ein bleibt in S3 (keine Freigabe), kein Laden. C4c Reaktionszeit (Kontaktschleife an C3/C4 statt C1/C2, weil C3/C4 mit PE2 schaltet und frei ist; Oszilloskop Kanal 1 zwischen R5 und S7, Kanal 2 an C3): Fehlerstrom bis Relais offen **2,5 / 3 / 2,5 ms** (Sensor, Abschaltpfad und Relaisabfall zusammen; Soll < 20 ms); Wallfree setzt Fehler und Schütz-Soll im selben Millisekundenschritt. Ob der Hardwarepfad über das Fehlerstrommodul oder die Software zuerst abschaltet, zeigt diese Messung nicht. C5 mit Schütz (Spule und CT angeschlossen): C -> Schütz klickt, `ct_sense` 1; S2 aus/ein, Schlüssel aus (S7, Schütz nach 5,0 s aus) und ein; nach jedem Aus kein ERR-CONTACTOR. **Befund:** Das Schütz (Schneider iCT 25 A) zieht nach einer kurzen Pause nicht an, obwohl Relais und Soll ein sind: Pause 0,5 s und einmal 3,3 s -> bleibt aus (`ct_sense` 0, Stellungsanzeige aus) bis zum nächsten Aus/Ein; 3,2 s, 5,8 s und 7..12 s -> zieht an. Im Fehlerfall liegen 230 V an A1/A2 (Nutzer gemessen), das Schütz nimmt sie also nicht an. Wallfree erkennt das nicht (Überwachung nur im Aus). Nachprüfung mit `e2b67dc` (Schützpause 5 s gesetzt): S4 beim Laden -> ERR-CP-INVALID, Schütz im selben Millisekundenschritt aus, nach dem Loslassen Fehler weg, Schütz 5,0 s nach dem Abfallen wieder ein, `ct_sense` 1; S2 aus/ein nach 0,5 s -> Schütz 5,0 s nach dem Abfallen wieder ein. RTC: mit `e2b67dc` nach Neustart keine Uhrzeit aus der RTC (Jahr und Tag aus `RTC_DR` vertauscht gelesen, per SWD gefunden: BDCR `0x8107`, DR `0x0026D107`, ISR `0x37`, Uhr selbst richtig). Mit `23f3260`: "Uhrzeit aus der RTC" nach 0,78 s; nach Neustart bei 4,7 s Laufzeit Uhrzeit da (`server` leer), 1,3 s hinter der PC-Uhr (bis 1 s ohne Sekundenbruchteil plus Abfragedauer), danach Abgleich; F1 10 s aus: keine Uhrzeit aus der RTC (`time` null bei 9,3 s), Abgleich von `pool.ntp.org` nach 10,8 s, danach auf 1 ms gleich mit der PC-Uhr. C6 evcc 0.317.2 (Docker, Vorlage `tinkerforge-warp-ws`, `23f3260`): S1 -> "car connected", "start charging", Schütz ein; Modus Aus -> S7, Schütz nach 5,0 s aus, `ct_sense` 0; Modus Sofort und S2 -> wieder S6; Höchststrom 10 A -> `external_current` 10000, Tastgrad 16,6 %, zurück 16 A -> 26,6 %; S1 aus -> "car disconnected", Entriegeln. evcc warnt "disabled but charging", solange der Prüfstecker nach dem Aus in C bleibt (ein Fahrzeug ginge auf B); einmal auch beim Stecken mit S2 schon an |
| Fehlerstrom bei eingeschaltetem Schütz mit Störfilter (`ce14d18`, 2026-11-13, Prüfstecker, Schütz und CT angeschlossen) | Viele Schaltvorgänge ohne Fehler; nach etwa 6 min 60 verworfene Interrupts auf SWITCH_1 (0, 2 oder 4 je Einschalten), 1 auf ERR_OUT, kein RCM-TRIP. 9-V-Block durch den Wandler (etwa 9 mA DC) beim Laden: RCM-DC6, Schütz-Soll aus und S9 im selben Millisekundenschritt, Interrupt nur auf SWITCH_2 (kein RCM-AC30), kein RCM-TRIP, der Filter verwarf das echte Signal nicht; Fehler blieb bis S1 aus, Selbsttest in A bestanden nach 1,4 s, danach wieder geladen. Gegenprobe ohne Fahrzeug (S1 offen, Schütz aus): RCM-DC6, Front-LED rot, S1 -> S9; Interrupt SWITCH_2 +1 (dazu je ein Selbsttest), nichts verworfen; nach dem Loslassen Wiederholtest nach 30 s bestanden, Fehler 32,4 s nach dem Setzen gelöscht, S9 -> S1 |
| Reset-Taster | wirkt auf NRST und PD2; der Prozessor bleibt nach dem Drücken etwa 5 s im Reset, auch wenn der Taster gehalten wird (vermutlich RC-Glied an NRST), und startet dann. Kurz oder unter etwa 5 s -> nur Neustart (`reset_pin`); gehalten -> nach etwa 5 s blinkt die Power-LED 1 Hz, nach weiteren 5 s Werksreset, also insgesamt etwa 10 s (Testgerät 2026-10-27: nach dem Drücken etwa 5 s dunkel (Prozessor im Reset), dann blinkt die Power-LED 1 Hz (Wallfree wartet); Loslassen vor Ablauf -> nur Neustart, Startereignis um die Wartezeit später); Notweg zum Bootloader (SOLL-BOOT-22, `1f0b419`, 2026-10-30): im 5-Hz-Blinken losgelassen -> Power-LED aus, kurz darauf blinken alle vier LEDs schnell (Bootloader), 192.168.0.8 antwortet auf Ping und HTTP; Upload mit `scripts/update.sh --upload-only`, danach Wallfree mit `reset_reason` `update`, Update-Ereignis `ok`, Prüfsumme passt, Einstellungen und Parameter unverändert, gleiche DHCP-Adresse. Wer nach Zeit statt nach der LED loslässt, lässt leicht im 1-Hz-Blinken los (normaler Start, Startereignis später) |
| Parameterspeicher | `FLASH_SR` beim Start `0xc0` (aus dem Bootloader, Fallstricke); erste Speicherung danach fehlerfrei |
| Import aus Sektor 3 (`c5fce83`, 2026-10-28) | `import-original` las die Werte der Originalfirmware 0.81: 16 Werte der Ladelogik übernommen (u. a. LR 17, CR 9, LD 3; `overcurrent_enable` fehlte im Original und gilt als 0) und 4 Netzwerkwerte (feste Adresse). Danach über die API zurückgestellt (0,3 s, ohne Neustart): LR 29, CR 40, LD 22, DHCP; `restart_required` blieb falsch, kein Fehler. |
| Update | über die Weboberfläche in etwa 20 s, Erfolg nur bei passender Prüfsumme; Rückweg zur Originalfirmware 0.81 und zurück geprüft (Sektor 3 und 7 erhalten). Frühere Abbrüche: jede Abfrage während des Uploads bricht ihn im Bootloader ab (Seite fragt deshalb nichts ab; am zweiten Gerät mit Bootloader 1.03 so dreimal hängen geblieben, 2026-11-08/09, Node-RED bzw. Mitschreibe-Skript; ohne andere Abfragen 2026-11-12 im Heimnetz gelungen, [wallbe2 Bootloader 1.03](../../wallbe2/docs/bootloader.md#bootloader-103-box-g); Testgerät 2026-11-13: drei Versuche abgebrochen durch einen offenen Browser-Tab mit der Oberfläche derselben Box, der Bootloader wartete danach noch unter der bisherigen Adresse (Restversuch, erst nach Ablauf aller Versuche zu je höchstens 120 s unter 192.168.0.8), nach Schließen des Tabs `--upload-only` an diese Adresse im ersten Versuch gelungen); wechselnde MAC von Bootloader und Wallfree verzögerte per ARP-Cache (behoben mit Geräte-MAC) |
| Browser-Tabs und Verbindungsplätze (`cfb6def`, 2026-10-28, `scripts/web-tabs-load.mjs`, daneben alle 0,5 s eine Anfrage mit neuer Verbindung) | vorher (`b5ce020`) mit offenen Handy-Tabs jede Minute 8..10 s keine Antwort (Ursache Festlegung Verbindungsplätze). Frist: 6 stumme Verbindungen, nach 5,1 s wieder erreichbar (`net-load.py idle`). 4 Tabs auf Status, 2 verborgen, 5 min: verborgene 0 Anfragen, sichtbare je 2 Verbindungen nacheinander, 592 von 592 Anfragen beantwortet. Mit der Frist 10 s (`7cfd6fa`) im gleichen Aufbau einmal 2,5 s Abweisung in 7,5 min. Ohne Tabs 6..8 ms |
| Netzlast (`scripts/net-load.py`) | Mit kooperativen Netz-Threads startete das Testgerät unter 4 parallelen Abfragern neu (Fallstricke); seit `eca1ad4` kein Neustart: 83 Anfragen/s, höchstens 123 ms; 3 WebSockets, Takt 2,0 s; 2300 Verbindungsabbrüche in 60 s ohne Rest; 30 min Dauerlast mit 2 WebSockets und MQTT ohne Neustart, WebSocket-Lücke höchstens 2,1 s. `wsreject` (2026-11-08): mit 101 und sofortigem Close-Rahmen nur etwa 5 Versuche/s, weil Wallfree zuerst schließt und jede Verbindung 1,5 s in TIME_WAIT einen der 12 Netzkontexte hält; währenddessen wird HTTP meist abgewiesen, danach nach etwa 1 s wieder erreichbar. Ohne Fix `0002` Paketleck (Fallstricke), mit Fix keins (1880 Versuche) |
| MQTT (Mosquitto 2.1 im LAN) | Verbindung sofort nach `mqtt_enable`; Zustände retained; Befehl als Zahl und JSON, 3 A abgelehnt, ohne `mqtt_commands` wirkungslos, retained Befehl verworfen; nach Broker-Neustart in 4 s verbunden; Last Will bei Übernahme der Sitzung nach Neustart |
| evcc 0.317.2 | Anmeldung und WebSocket ohne Fehler, ohne Fahrzeug "car disconnected", `external_current` = 0 |
| Version (`v0.1`, 2026-10-27) | Update auf 0.1 erfolgreich, `/system/info` mit `firmware` 0.1 und `build` d45de9d, Prüfsumme passt zur Datei, Protokoll läuft über den Update-Neustart weiter. Kopfzeile: Netzkabel gezogen -> nach wenigen Sekunden "getrennt" mit Hinweisleiste, eingesteckt -> wenige Sekunden bis "verbunden", ohne Neustart |
| MQTT per Name, Home-Assistant-Erkennung (`v0.1`, 2026-10-27) | Broker per Name im LAN (Mosquitto, ohne Anmeldung) aufgelöst und sofort verbunden; nach `online` 6 Konfigurationen retained, Ladestrom/Leistung/Energie als leere Nachricht entfernt (ohne `mqtt_commands`, ohne Zähler); Gerät mit `sw` `0.1+d45de9d`. Gefunden: Wertvorlage "Zustand" auf 95 Byte gekürzt (ohne `}}`), behoben in 0.2-dev. Mit `590465a`: `mqtt_commands` an -> Ladestrom-Feld 0..16 A (Drehschalter), aus -> wieder entfernt; kein Home Assistant vorhanden, Prüfung nur mit `mosquitto_sub` |
| Toter Broker (`b5ce020`, 2026-10-27) | Test-Broker auf dem PC (Mosquitto, Port 18830), per SIGSTOP eingefroren (TCP bleibt offen, keine Antworten): letzte Nachricht 21:11:07, PINGREQ nach 60 s ohne Antwort, Trennung mit -116 (ETIMEDOUT) nach 121 s (106 s nach dem Einfrieren); nach dem Auftauen sofort wieder verbunden, `online` retained. Ohne Zähler (kein Dauerversand); mit Zähler siehe Zählerbus (Ausfall nach 230 s erkannt) |
| Protokoll, Uhrzeit, CP-Aufnahme (`6eb0e08`, 2026-10-27) | Ereignisprotokoll übersteht den Neustart; Uhrzeit von `pool.ntp.org` (Router meldet per DHCP keinen Zeitserver, DNS über DHCP funktioniert); CP-Aufnahme im Leerlauf +11,9 V |
| Prozessortemperatur | plausibel (31,8 Grad knapp 3 min nach dem Start, 33 Grad im Betrieb), Referenz bestätigt (`adc_vref_mv` 2493..2494, siehe Fallstricke); Abgleich `mcu_temp_offset` -3,5 Grad eingestellt (Nutzer 2026-10-29) |
| Konsole statt Telnet (SOLL-SH-19/21/22) | läuft auf dem Testgerät (`6a6d3d5`), Kommandos ohne Vorsatz `evse` (`1f0b419`); Stacks unter [Ressourcen](implementierung.md#ressourcen) |
| Ausgang C1/C2 (eigener Testbau mit einem Kommando, das C1/C2 dreimal 2 s schaltet; nicht in `main`, 2026-10-30) | PE2 schaltet das Relais an C1/C2, dieses die Schützspule: Relais und Schütz hörbar, Durchgang an C1-C2. Aufbau abweichend von der Freigabe: Schütz und CT-Leitung waren angeschlossen (vom Nutzer erst nachher bemerkt), die Steckdose führte also kurz Netzspannung, nichts war eingesteckt. Damit auch Via Sense bestätigt: Schütz gegen das Soll eingeschaltet -> etwa 1 s nach Testbeginn ERR-CONTACTOR (`contactor_error` 6). |
| Zählerbus mit Nachbildung (`3e05884`, 2026-10-30, `scripts/meter-emu.py`, FT232R-Adapter, 9600 Bd 8N1, SDM72D-M V2) | UART4 mit Sendefreigabe PC12 geht an Klemme A/B. Adapter D+ an A, D- an B: `status` `ok`, 2 Anfragen/s, alle 13 Werte richtig, in 60 s kein Fehler. Andersherum vertauscht: Box sendet, Adapter liest verfälschte Bytes, die Antwort bleibt aus, Fehlerzähler `timeouts`, Zustand `error`. Leitung offen: nur `timeouts` (21 in 55 s), keine `frame_errors`, Zustand `error`. Der Nachbildung nach je 25 s: keine Antwort -> `timeouts` +12, gestörte CRC -> `frame_errors` +15, Ausnahme -> `exceptions` +15 mit Code 2, NaN -> `implausible` +5; jeweils `error`, Werte `null`, nach Rückkehr von selbst `ok`; `evse/state` unverändert. ABB B23 (`meter_type` = 1, Funktion 03, drei Blöcke) mit 8N1 und 8E1: `ok`, alle 13 Werte richtig, Ungültig-Kennungen -> damals `implausible`, `error`, danach von selbst `ok` (seit F39 leert eine Kennung nur den betroffenen Wert). Nach jedem Start scheitern die ersten Anfragen (2..3 Timeouts, einmal ein `frame_errors`), danach keine mehr; Ursache nicht untersucht, ohne Wirkung auf den Betrieb. Zweiter Adapter mit CH343 (QinHeng `1a86:55d3`, Kernel-Treiber `cdc_acm`, `--port /dev/ttyACMx`), 2026-11-11: ABB B23 8N1 `ok` mit allen 13 Werten; keine Antwort, gestörte CRC, Ausnahme und NaN wie oben gezählt (`timeouts` +12, `frame_errors` +15, `exceptions` +15 mit Code 2, `implausible` +5), jeweils `error` mit Werten `null`, danach von selbst `ok`. Mit Zähler und `mqtt_interval` 1: Broker eingefroren, Ausfall nach 230 s erkannt (Sendefehler -104), danach sofort neu verbunden; Zählerbetrieb ungestört |
| Echter Zähler ABB B23 (`c28f363`, 2026-11-14, alle drei Phasen an L1, ohne Last) | Zähler (gebraucht, Werkseinstellung unbekannt) stand auf Adresse 1, 19200 Bd, keine Parität: `meter_baud` 19200. Die mit A/B beschrifteten Klemmen des ABB sind gegenüber dem Testgerät vertauscht (A der Steuerung = D+): erst nach Tausch am Zähler Antworten. Alle drei Blöcke antworten (mitgehört mit dem CH343-Adapter); der Zähler liefert für die Einspeise-Energie `FF..FF`, dadurch wurde jeder Satz verworfen, behoben mit F39. Danach `ok`: U1..U3 236 V, f 50,05 Hz, Bezug 82,39 kWh, Einspeisung `null`; nach dem Start 3 Timeouts, danach keine |
| Stacks | siehe [Ressourcen](implementierung.md#ressourcen); keiner über 82 % |
| Flash lesen (`9f39b42`, 2026-11-08, `scripts/flash-read.py` über die Konsole) | Sektor 0..2 (48 KB), Sektor 3, OTP und Optionsbytes in 4 s gelesen, bytegleich mit der SWD-Sicherung H0 und mit einem Lesen per ST-LINK ohne Halt zur gleichen Zeit; Sektor 4..6 gleich dem aufgespielten Abbild, dahinter gelöscht. Optionsbytes waren `ff aa 00 55` (RDP 0, BOR aus). Sektor 7, Bereiche über das Ende von Sektor 6 hinaus und RAM abgewiesen, Länge über 1024 abgewiesen. Box lief dabei ohne Neustart weiter |
| BOR Stufe 3 (2026-11-09, per ST-LINK) | Optionsbytes jetzt `f3 aa 0c 55`: RDP 0, BOR Stufe 3 (etwa 2,7 V), sonst unverändert; Wallfree `9f39b42` startet, Sektor 0..6 nach der Rücknahme bytegleich zurückgespielt, Einstellungen wiederhergestellt, MQTT verbunden. Ablauf und Fallstrick siehe [Fallstricke](implementierung.md#fallstricke). Aus/Ein-Verhalten mit BOR noch nicht gezielt geprüft |

## 2. Laden mit Fahrzeug (Stufe D, zweites Gerät)

Ziel: was der Prüfstecker nicht nachbilden kann, also Laststrom, ein Fahrzeug, das auf
Ladesignal und Schütz reagiert, und lange Laufzeit. Fahrzeug Nissan Leaf ZE0, lädt nur
einphasig; evcc mit der Vorlage `tinkerforge-warp-ws`, Wechselrichter mit Leistungsmessung
(Fronius) als Vergleich. Einstellungen: D1, D2, D3, D7, D10, Drehschalter 4 (16 A).

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
- D3.9 Netzausfall beim Laden (Sicherung etwa 10 s aus): das zweite Gerät startet (`power_on`), Leaf in C,
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
  laut evcc 78 %, geladen laut evcc etwa 2,4 kWh (geschätzt, das zweite Gerät hat keinen Zähler). Ob der
  Leaf den Strom vor dem Wechsel auf B zurücknahm, ist nicht gemessen (kein Zähler, Fronius nicht
  mitgeschrieben).
- Ladeende durch evcc mit dem gepatchten evcc auf einem Server im Heimnetz (Limit 80 % am Fahrzeug in evcc, im Leaf
  keine Begrenzung, Modus Sofort, 1 Phase): 21:20:00 S6 mit 16 A; 21:24:46 S7, PWM aus; Schütz
  21:24:50 aus, `ct_sense` folgt; Leaf bleibt in C bis 21:29:52, dann B mit +8,97 V, also wieder
  **5 min 6 s** (wie bei Befund 4). evcc meldete in der Zeit 10-mal "disabled but charging" (alle
  30 s), schaltete aber nicht wieder ein; "stop charging" 21:29:50. Keine Fehler in der Box.

**Ergebnisse 2026-11-12** (Wallfree `0ce1265`, Schlüssel aus):

- D4: `208a7f7` lief vor dem Update 3 Tage 3 h ohne Neustart (`reset_reason` `power_on`), mit evcc
  auf einem Server im Heimnetz.
- D3.10 Update bei gestecktem Fahrzeug (Leaf in B, Schütz aus, evcc angehalten, keine andere
  Abfrage): Update über die Weboberfläche im Heimnetz gelungen, `reset_reason` `update`, Modus 2
  ok, Prüfsumme passt, Uhrzeit nach etwa 12 s per NTP. Danach `external_current` 0, Box wartet in
  B (F34).
- Freigabe durch evcc (Befund 3): evcc auf Laden -> `external_current` 16000, S6, Schütz ein,
  erlaubt 15,96 A, `limited_by` `external`.

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
2. **Das zweite Gerät fällt aus dem Netz** (zweimal, beim zweiten Mal gezielt nach D3.9): Schleife ab
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
3. **Freigabe ohne Schlüssel:** Mit D10 gab nur EN oder `enable_register` frei (das Register entfiel mit F37); evcc schreibt
   nur den Strom (WARP `evse/external_current`) und konnte so ohne Schlüssel nicht starten. Mit
   Schlüssel lud die Box auch ohne evcc (2026-11-09 rund 2 h mit 16 A bis fast voll, Startwert
   `external_current` 80 000), ohne Schlüssel lud der Leaf mit evcc gar nicht (2026-11-12).
   **Gelöst** nach F34 (Nutzer 2026-11-12): Mit D10 gibt `external` > 0 frei, Startwert mit D10
   ist 0; mit D7 und D10 lädt der Schlüssel unabhängig von evcc mit dem Höchststrom
   (SOLL-EN-01/12/13, SOLL-CUR-15). In der Simulation mit evcc geprüft; am zweiten Gerät mit `0ce1265` am 2026-11-12 bestätigt: Schlüssel aus, evcc auf Laden, der Leaf lädt mit 16 A (`limited_by` external, Zustand C, Schütz zu).
4. **Leaf bleibt nach PWM-Aus in C** (D2.6, nach Nutzer auch beim Ladeende mit dem Original):
   evcc kam mit dem Original nie gut damit zurecht, wenn das Fahrzeug nicht bald auf B zurückfiel
   (Nutzer 2026-11-08; offen, ob es an der Originalfirmware lag). Gemessen nach dem Ende über evcc
   (S7 11:34:26, PC-Zeit): C mit +6,05 V bis 11:39:31, dann B mit +8,97 V, also **etwa 5 min 6 s**
   nach dem PWM-Aus (abends noch einmal genau so, siehe oben). Bis dahin meldete evcc alle 10 s
   "disabled but charging" (38-mal), danach nicht mehr. evcc schaltet nach fest 60 s
   (`chargerSwitchDuration`, nicht einstellbar) wieder ein. **Gelöst** nach F36
   (Nutzer 2026-11-13): Die WARP selbst meldet C nur, wenn Strom erlaubt ist, sonst B; Wallfree
   meldet `iec61851_state` jetzt ebenso. Nach dem Ausschalten sieht evcc sofort „verbunden“, kein
   Wiedereinschalten. Vorher lokal mit evcc-Patch (10 min statt 60 s) geprüft; der Patch und ein
   Issue bei evcc sind damit nicht mehr nötig. Am zweiten Gerät mit `f6f0036` bestätigt (Nutzer 2026-11-14):
   keine Warnung mehr.
5. **Schützüberwachung:** das zweite Gerät hat den Hilfskontakt (`ct_sense` folgt dem Schütz), übernommen war
   aus dem Original `contactor_monitor` 0. Eingeschaltet (1, Via Sense, Nutzer 2026-11-08) und mit
   Laden, evcc-Pause (17 s), Fortsetzen und Ende geprüft: Schütz jeweils 5,0 s nach S7 aus,
   `ct_sense` folgt, kein ERR-CONTACTOR. Erledigt.
6. **Uhr lief etwa 500 ppm zu langsam** (zweites Gerät und Testgerät): zweites Gerät ging 3,95 s nach, 2,2 h nach dem
   letzten Abgleich (2026-11-09). Gemessen über 10 min (Laufzeit aus `GET /system/info` gegen die
   PC-Uhr per NTP, Ausgleichsgerade, Restfehler unter 6 ms): zweites Gerät −497 ppm, Testgerät −571 ppm. Der
   CPU-Takt selbst stimmte (Testgerät, `DWT_CYCCNT` per SWD: −53 ppm, Quarz). **Ursache:** Der
   SysTick-Treiber von Zephyr verlor bei jedem Neuprogrammieren einige Takte, bei etwa 1800
   Aufrufen je Sekunde rund 516 ppm ([Fallstricke](implementierung.md#fallstricke)).
   **Behoben** mit Zephyr-Patch `0003` (`5cf8234`, Ausgleich beim Start gemessen: 3,0 Takte): an
   das Testgerät Zephyr-Zählung gegen den CPU-Takt +0,9 ppm, gegen die PC-Uhr −52,7 ppm (10 min), also
   nur noch der Quarz (etwa 4,5 s am Tag, mit dem stündlichen Abgleich unter 0,25 s). Das zweite Gerät mit
   `0ce1265` (2026-11-12, 10 min beim Laden, Laufzeit gegen die PC-Uhr per NTP, Restfehler 1,5 ms):
   +21 ppm statt −497 ppm, also nur noch ihr Quarz. Der 32-kHz-Taktgeber an PC14 (treibt die RTC) liegt am Testgerät bei etwa −2,6 ppm
   (2026-11-11, [wallbe2 hardware.md](../../wallbe2/docs/hardware.md)); Wallfree nutzt ihn nur
   über die RTC (Uhrzeit über Neustarts, SOLL-TIME-04); die laufende Uhr zählt mit dem Quarz und
   wird stündlich per NTP abgeglichen (SOLL-TIME-03).
