<!-- Kopf der Wiki-Startseite; scripts/wiki-export.py setzt danach Status und Funktionen aus README.md ein -->

<p align="center">
<picture>
<source media="(prefers-color-scheme: dark)" srcset="bilder/wallfree-logo-dunkel.svg">
<img src="bilder/wallfree-logo-hell.svg" alt="Wallfree" width="330">
</picture>
</p>

<p align="center"><b>Laden ohne Mauer.</b> Freie Firmware für die Ladesteuerung der wallbe Eco 2.0.</p>

Schön, dass du hier bist! Wallfree ist eine neue, offene Firmware für die Ladesteuerung
EV-CC-AC1-M3-CBC-RCM-ETH von Phoenix Contact, die zum Beispiel in der wallbe Eco 2.0 steckt. Sie
lädt dein Auto wie gewohnt, spricht direkt mit evcc und Home Assistant und hat eine aufgeräumte
Weboberfläche. Und wenn sie dir nicht gefällt, geht es jederzeit zurück zur Originalfirmware.

> [!CAUTION]
> **Nutzung auf eigene Gefahr, ohne Haftung.** Wallfree ist ein privates Projekt und wird so
> bereitgestellt, wie es ist, ohne jede Gewährleistung (siehe Lizenz Apache-2.0, Abschnitte 7
> und 8). Ich übernehme keine Haftung für Schäden an Ladestation, Fahrzeug, Gebäude oder
> Personen, die beim Aufspielen oder Betrieb entstehen. Eine Ladesteuerung schaltet
> Netzspannung und überwacht Fehlerströme; Fehler in der Firmware können Menschen gefährden.
> Einbau und Prüfung gehören in die Hände einer Elektrofachkraft.

> [!WARNING]
> **Firmware nur hochladen, wenn alle anderen Zugriffe auf die Ladestation abgeschaltet sind.**
> Vorher evcc, Home Assistant, Node-RED, Monitoring und Skripte anhalten und alle anderen
> Browser-Tabs mit der Ladestation schließen. Während der Übertragung nimmt der Bootloader der
> Steuerung die Datei an, und jede fremde Anfrage bricht sie ab. Die bisherige Firmware ist dann
> schon gelöscht und die Steuerung wartet im Bootloader; wie man sie wieder herausholt, steht unter
> [Umrüsten, Rettungsweg](umruesten.md#5-rettungsweg-steuerung-wartet-im-bootloader).

## Warum Wallfree?

Die wallbe Eco 2.0 ist eine solide Ladestation, und ihre Steuerung kann eigentlich alles, was
man zu Hause braucht. Die Firmware wird aber nicht mehr weiterentwickelt: Fehler bleiben, neue
Wünsche wie die Anbindung an evcc oder Home Assistant kommen nicht mehr dazu. Schade um gute
Hardware.

Wallfree setzt genau da an. Die Firmware ist von Grund auf neu geschrieben, verhält sich aber so
nah wie möglich am Original: DIP-Schalter, Drehschalter und Klemmen bedeuten dasselbe wie vorher,
eine umgerüstete Ladestation lädt also ohne Umbau weiter. Dazu kommen das, was im Alltag gefehlt
hat, und ein offener Quelltext, an dem jeder weiterbauen kann. Daher der Name: keine Mauer mehr
um die eigene Wallbox.

## Die zehn wichtigsten Vorteile

1. **Problemlos zurück zur Originalfirmware.** Der Rückweg läuft über dieselbe Weboberfläche,
   ohne Gehäuse öffnen und ohne Programmiergerät; die Einstellungen des Originals bleiben dabei
   erhalten. Nötig ist nur die Firmwaredatei des Herstellers
   ([Umrüsten](umruesten.md#3-zurück-zur-originalfirmware)).
2. **Umrüsten ohne Umbau.** Aufspielen über die Weboberfläche des Originals; Schalter, Klemmen und
   Eingangsfunktionen arbeiten wie gewohnt, die Einstellungen des Originals lassen sich
   übernehmen.
3. **Keine versteckte Ladestrom-Begrenzung.** Den höchsten Strom bestimmen allein der
   Drehschalter und die Hardware: Belastbarkeit des Ladekabels und was das Fahrzeug selbst
   zulässt. Alles Weitere sind Vorgaben, die du selbst einstellst (evcc, Eingänge), und die
   Übersicht zeigt jederzeit, was gerade begrenzt, zum Beispiel „begrenzt durch Kabel (20 A)“.
4. **Anbindung an evcc, Home Assistant und Energiezähler.** evcc über die Vorlage
   `tinkerforge-warp-ws`, etwa zum Laden mit PV-Überschuss; MQTT mit Home-Assistant-Erkennung
   (das Gerät legt sich selbst an); Zähler ABB B23, Eastron SDM630 und SDM72D-M über RS-485.
5. **Oszilloskop für das Ladesignal.** Ein Knopfdruck zeigt den Verlauf der Signalleitung zum
   Fahrzeug über eine Periode, mit den Spannungen der Fahrzeugzustände und dem Tastgrad. Lädt ein
   Auto nicht, sieht man sofort, ob es seinen Zustand richtig meldet.
6. **Aufgeräumte Weboberfläche.** Deutsch, auch auf dem Handy bedienbar, ohne Cloud; eine
   Expertenansicht zeigt bei Bedarf jeden Parameter mit Erklärung.
7. **Nachvollziehbar.** Ereignisprotokoll, das Neustarts übersteht, Fehler mit Grund und
   Rücksetzbedingung, Verlauf über 48 Stunden.
8. **Sicherheit im Blick.** Fehlerstromüberwachung mit Selbsttest vor jedem Laden,
   Schützüberwachung, eigener Schutzablauf im Millisekundentakt, Watchdog und Prüfsumme der
   Firmware.
9. **Updates mit Netz und doppeltem Boden.** Die Weboberfläche prüft die Datei vor dem
   Hochladen und das Ergebnis danach; über den Reset-Taster gibt es einen Notweg.
10. **Offen.** Quelltext unter Apache-2.0, Fehler lassen sich beheben, neue Funktionen ergänzen;
    auf dem PC läuft die ganze Firmware als Simulation zum Ausprobieren.

## Stand

<!-- README -->
