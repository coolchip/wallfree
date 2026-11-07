# Unterschiede zur Originalfirmware

Wallfree ersetzt die Firmware der Ladesteuerung EV-CC-AC1-M3-CBC-RCM-ETH. Das Ladeverhalten
folgt weitgehend der Originalfirmware, damit eine umgerüstete Ladestation sich so verhält wie
vorher. Wo Wallfree bewusst anders arbeitet, steht es hier, jeweils mit dem Grund.

Vergleichsgrundlage ist die Firmware der wallbe-Steuerung (Stand SL-01.04.24), an einzelnen
Stellen auch die Phoenix-Contact-Firmware 1.27 für dieselbe Steuerung. Die Angaben zur
Originalfirmware beruhen auf einer Analyse des Programms, nicht auf Herstellerangaben. Die
genaue Festlegung jedes Punkts steht in der [Soll-Spezifikation](soll-spezifikation.md)
(Markierung **[ABW]**).

## Wer umrüstet, sollte das wissen

- **Werkseinstellungen der Ein- und Ausgänge** folgen dem Handbuch der Steuerung, nicht der
  wallbe-Firmware. Bei einer wallbe-Ladestation meldet der Verriegelungsmotor seine Stellung
  mit einem kurzen Impuls; dafür muss die Funktion des Eingangs LD auf **22 (gepulst)** stehen
  (Einstellungen → Laden, Expertenansicht). Ab Werk steht sie auf 3 (Dauersignal wie bei
  Phoenix). Die Einstellungen der Originalfirmware lassen sich übernehmen
  ([Rückweg und Übernahme](#rückweg-und-übernahme)).
- **Weboberfläche, API und Zugang sind neu.** Die Oberfläche und die Modbus-TCP-Schnittstelle
  der Originalfirmware gibt es nicht mehr. evcc wird über die Vorlage `tinkerforge-warp-ws`
  angebunden. Ab Werk gilt der Zugang `admin` / `wallfree`, der beim ersten Aufruf geändert
  werden muss.
- **Nicht enthalten:** RFID (als spätere Erweiterung vorgemerkt), OCPP, Mobilfunk, Lastmanagement mehrerer Ladestationen (das kann
  evcc), Modbus TCP.

## Schutzfunktionen

| Thema | Originalfirmware | Wallfree | Grund |
| --- | --- | --- | --- |
| Fehlerstromsensor | Das 30-mA-Signal wird nur ausgewertet, solange kein Fahrzeug lädt (Zustand A/B). Je nach Signal schaltet das Schütz sofort oder erst nach einer Frist ab. | Alle drei Signale des Sensors schalten in jedem Zustand sofort ab. | Der Schutz soll nicht vom Ladezustand abhängen. |
| Abschaltweg | Das Schütz wird nur im Ablauf der Ladelogik geschaltet; dieser wartet teils mehrere Sekunden (Verriegelung, Weckfolge, Selbsttest). | Ein eigener Schutzthread prüft jede Millisekunde und schaltet unabhängig vom Ablauf ab; die Ladelogik wartet nie. | Eine Schutzreaktion soll nicht auf andere Abläufe warten. |
| Unterspannung | Die Fehlerstromüberwachung pausiert, danach Neubeginn im Grundzustand. | Nur eine Warnung; die Fehlerstromüberwachung bleibt aktiv. | Der Schutz bleibt durchgehend wirksam. |
| Watchdog | Überwachung der Abläufe mit etwa 60 s, Hardware-Watchdog etwa 3 s; die Ausgänge bleiben bis zum Neustart, wie sie sind. | 250 ms; vor jedem Neustart Schütz aus und Ladesignal in den sicheren Zustand. | Hängende Software darf nicht weiterladen. |
| Prozessorausnahme | Bleibt stehen, bis der Watchdog neu startet (etwa 3 s). | Sofort sicherer Zustand, Ursache wird gesichert, Neustart. | wie oben |
| Selbsttest des Fehlerstromsensors | Zeitmessung mit zwei Antwortprofilen. | Feste Zeitfolge; jede Abweichung sperrt das Laden. | nachvollziehbar und passend zu den Prüfpunkten des Sensors |
| Schützüberwachung, Modus 4 | Meldet nur, wenn auf allen drei Phasen Spannung anliegt. | Jede Phase einzeln; der Modus braucht eine Messquelle der Ladelogik und ist ohne sie nicht wählbar. | Ein auf einer Phase hängender Kontakt soll erkannt werden. |
| Schützfehler | Verschwindet, sobald er nicht mehr erkannt wird. | Bleibt bis zum Neustart oder bis zur Quittierung in der Weboberfläche. | Ein Schützfehler soll bemerkt werden. |
| Diodenprüfung | Bei fehlender Fahrzeugdiode bleibt das Ladesignal auf -12 V. | 3 s -12 V, danach +12 V, damit das Abstecken erkannt wird; der Fehler bleibt bis dahin. | eindeutiger Fehler für das Fahrzeug, danach normaler Ablauf |
| Abbild der Firmware | – | Wallfree prüft beim Start die eigene Prüfsumme; ein beschädigtes Abbild lädt nicht. | Ein unvollständiges Update soll nicht laden. |

## Ladeablauf

| Thema | Originalfirmware | Wallfree | Grund |
| --- | --- | --- | --- |
| Anlauf nach dem Einschalten | 20 s | 5 s | genügt für Selbsttest und Entriegelung beim Start |
| Takt der Ladelogik | 25 ms | 10 ms | schnellere, gleichmäßige Reaktion |
| Ladeende | über verteilte Wartezeiten | eigener Zustand „Beenden“: Ladesignal aus, Schütz nach der Reaktion des Fahrzeugs, spätestens nach 5 s | eindeutige Reihenfolge |
| Vorrang mehrerer Ereignisse | die zuletzt ausgewertete Bedingung gewinnt | feste Reihenfolge: Schutzfehler, dann Freigabe und Verfügbarkeit, dann Entriegeln, dann Stromänderungen | eindeutiges Verhalten |
| Pause vor erneutem Einschalten des Schützes | 0,5 s, dazu bei jedem Wiedereinschalten ein Selbsttest von etwa 2 s | 5 s (einstellbar bis 10 s); beim Übernehmen der Originaleinstellungen bleibt dieser Wert | manche Schütze ziehen nach einer kurzen Pause nicht wieder an |
| Fahrzeug pausiert (B ohne Ladesignal) | Rückfall in einen früheren Zustand mit neuem Selbsttest | bleibt im Zustand, kein neuer Selbsttest | unnötige Tests vermeiden |
| Weckfolge | nach 5 s und nach jeder Stromsperre erneut | ab Werk nach 30 s, einmal je Steckvorgang; Wartezeit und Wiederholung einstellbar (`wakeup_delay`, `wakeup_mode`) | Fahrzeugkompatibilität ohne festes Verhalten |
| 5-%-Modus | nicht erreichbar | gibt 5 % aus, Schütz bleibt aus, Status „digitale Kommunikation nicht verfügbar“ | wie im Handbuch beschrieben; digitale Kommunikation unterstützt die Steuerung nicht |
| Ströme über 80 A | ergeben einen Tastgrad von 0 | werden abgelehnt | keine undefinierte Ausgabe |
| Tastgrad bei 51,1..52,4 A | etwas kleiner gerundet | Regel nach Norm (51,0 A) | beide bleiben unter dem Sollstrom |
| Drehschalter A..F | lädt nicht, ohne Meldung | Konfigurationsfehler mit Anzeige | sichtbarer Fehler statt stillem Stillstand |
| Kurzschluss des Ladesignals | der Zustand wird als „kein Fahrzeug“ geführt | eigener Fehlerzustand „CP ungültig“ | nicht mit Abstecken verwechselbar |

## Verriegelung

| Thema | Originalfirmware | Wallfree | Grund |
| --- | --- | --- | --- |
| Rückmeldung des Motors | wallbe-Firmware: gepulst; Phoenix 1.27: Auswertung der Flanken | beide Arten wählbar: Dauersignal (Code 3) und gepulst (Code 22, wie wallbe) | Beschaltungen unterscheiden sich; eine Auswertung der Flanken passt nicht zu gepulsten Rückmeldungen |
| Ablauf | wartet blockierend bis zur Rückmeldung (bis etwa 3 s, im Extremfall länger) | zeitgesteuert ohne Warten | Schutzreaktionen warten nicht auf den Motor |
| Rückmeldung verloren beim Laden | Wiederverriegeln, während das Schütz noch an sein kann | Schütz sofort aus, dann ein Wiederverriegelungsversuch | Laden nur mit gesicherter Verriegelung |
| Entriegeln klemmt | Versuche ohne Ende | nach 4 Fehlversuchen Fehlermeldung, keine weiteren Versuche bis zum nächsten Fahrzeug | schont den Motor |
| Rückmeldung eingeschaltet (D6), aber kein Eingang dafür eingestellt | Verriegeln schlägt ohne Meldung fehl | Konfigurationsfehler mit Anzeige | sichtbarer Fehler |

## Eingänge, Schalter und Einstellungen

| Thema | Originalfirmware | Wallfree | Grund |
| --- | --- | --- | --- |
| Entprellung der Eingänge | nur Mittelwert über 8 Perioden | zusätzlich zwei aufeinanderfolgende Messungen (etwa 16 ms) | robust gegen Störimpulse |
| Mehrere Eingänge mit derselben Funktion | der zuletzt ausgewertete gilt | ODER-Verknüpfung | vorhersagbar |
| DIP-Schalter D4/D6 | werden von einem werksseitig programmierten Merkmal der Steuerung überstimmt | nur die Schalter zählen | sichtbar und einstellbar |
| Werte außerhalb des Bereichs | werden ohne Meldung verworfen | werden mit Fehlermeldung abgelehnt | Rückmeldung an den Nutzer |
| Obergrenze absenken, DIP überschreiben, Verriegelung setzen | ohne Grenzen und jederzeit | nur absenken bzw. nur ohne Fahrzeug; „Verriegelung setzen“ löst einen echten Zyklus aus | Schreibzugriffe dürfen den Schutz nicht aushebeln |
| Freigabe über Schnittstelle | jeder Wert ungleich 0 gilt als 1 | nur 0 und 1 | eindeutig |
| Werkseinstellungen | wallbe: LR 17, CR 40, LD 22; Phoenix 1.27: LR 29, CR 17, LD 3, Schützüberwachung aus | nach Handbuch der Steuerung: ER 15, LR 29, VR 16, CR 17, LD 3, EN 1, XR 2, IN 11, ML 4; Schützüberwachung an (Spannung am Ladepunkt) | das Handbuch ist die Referenz; Schützüberwachung aus Sicherheitsgründen an |
| Einheit der Ströme an der Schnittstelle | 0,1 A bzw. A | mA wie WARP | kompatibel zu evcc |
| Reset-Taster beim Start gehalten | nach 5 s Werkseinstellungen | Loslassen zwischen 5 und 10 s startet den Bootloader (Notweg zum Aufspielen, Adresse 192.168.0.8); nach 10 s Werkseinstellungen. Mit der Verzögerung der Steuerung nach dem Drücken (etwa 5 s) insgesamt etwa 10 bzw. 15 s | Firmware aufspielen auch ohne erreichbare Weboberfläche; Werksreset wie im Handbuch nach über 10 s |

## Rückweg und Übernahme

- **Zurück zur Originalfirmware:** Wallfree benutzt den vorhandenen Bootloader der Steuerung und
  verändert ihn nicht. Über denselben Weg (Weboberfläche, System, Firmware-Update) lässt sich
  die Originalfirmware wieder aufspielen; die Weboberfläche fragt bei fremden
  Abbildern ausdrücklich nach. Auf einer Steuerung geprüft.
- **Einstellungen der Originalfirmware** bleiben dabei erhalten: Wallfree speichert seine
  eigenen Einstellungen in einem anderen Bereich des Flash und schreibt den Bereich der
  Originalfirmware nie. Ein Import dieser Einstellungen in Wallfree (Befehl
  `import-original` in der Konsole der Weboberfläche) ist eingebaut und auf einer Steuerung geprüft.
- **Voraussetzung:** eine Firmwaredatei des Herstellers. Wallfree enthält die Originalfirmware
  nicht und kann die installierte nicht auslesen. Schritt für Schritt:
  [Umrüsten auf Wallfree und zurück](umruesten.md).
