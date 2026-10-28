# Umrüsten auf Wallfree und zurück

Diese Anleitung beschreibt, wie man Wallfree auf die Ladesteuerung EV-CC-AC1-M3-CBC-RCM-ETH
aufspielt und später wieder zur Originalfirmware zurückkehrt. Beides geht über die Weboberfläche,
ohne das Gehäuse zu öffnen und ohne weitere Geräte. Die Schritte sind an der Steuerung einer wallbe Eco 2.0
durchgeführt worden, ohne Fahrzeug und ohne Last. Was Wallfree anders macht, steht in
[Unterschiede zur Originalfirmware](unterschiede-originalfirmware.md).

## 1 Bevor Sie anfangen

- **Auf eigenes Risiko:** Wallfree ist für Erprobung durch versierte Anwender gedacht. **Laden
  ist auf Hardware noch nicht geprüft** (siehe [README](../README.md)). Eine Ladesteuerung
  schaltet Netzspannung und überwacht Fehlerströme; Fehler in der Firmware können Menschen
  gefährden.
- **Firmwaredatei des Herstellers bereithalten:** Wallfree enthält die Originalfirmware nicht und
  kann die installierte auch nicht auslesen. Wer zurück will, braucht eine Firmwaredatei des
  Herstellers für diese Steuerung (`.bin`). Sie kann eine andere Version sein als die
  installierte; geprüft ist der Rückweg nur mit der Firmware, die vorher auf derselben Steuerung
  lief.
- **Nichts anderes darf die Steuerung abfragen,** solange ein Update läuft (evcc, Home Assistant,
  Monitoring, weitere Browser-Tabs). Jede andere Anfrage bricht die Übertragung im Bootloader ab
  (Abschnitt 4).

Benötigt werden nur ein PC im selben Netz wie die Steuerung, ein Browser und die Wallfree-Datei
`wallfree-<version>.bin` (aus einer Veröffentlichung oder selbst gebaut mit
`scripts/build-image.sh`). Eine Veröffentlichung ist ein ZIP-Archiv mit der Datei, ihrer
Prüfsumme in `README.txt` und den Lizenztexten.

## 2 Wallfree aufspielen

Die Steuerung hat einen eigenen Bootloader, ein kleines Programm, das jede Firmwaredatei über eine
einfache Webseite annimmt. Er bleibt beim Umrüsten unverändert und ist auch der Rückweg.

1. Weboberfläche der Originalfirmware öffnen, Seite **Config**, Knopf **Firmware-Update**.
2. Die Steuerung startet nach etwa 10 s neu in den Bootloader. Er antwortet unter der bisherigen
   Adresse; hat die Originalfirmware keine gültige Adresse übergeben, unter **192.168.0.8**
   (dann den PC vorübergehend auf eine Adresse im Netz 192.168.0.x stellen). Die Modul-LEDs
   blinken gemeinsam.
3. Die Seite des Bootloaders im Browser öffnen (`http://<Adresse>/`), `wallfree-<version>.bin`
   auswählen und hochladen. Nur dieser eine Browser-Tab, keine anderen Abfragen. Der Bootloader
   wartet bis zu 2 Minuten auf die Datei; die Übertragung selbst dauert wenige Sekunden.
4. Nach der Bestätigung startet Wallfree, ab Werk mit einer Adresse per DHCP
   (siehe [Bedienungsanleitung, Abschnitt 2](anleitung/weboberflaeche.md#2-voraussetzungen-und-aufruf)).
5. Anmelden mit `admin` / `wallfree` und neue Zugangsdaten vergeben
   ([Abschnitt 3 der Anleitung](anleitung/weboberflaeche.md#3-erste-anmeldung)).
6. **Bei einer wallbe-Ladestation:** Die Verriegelung meldet gepulst. Unter Einstellungen →
   Laden (Expertenansicht) die Funktion des Eingangs **LD auf 22** stellen und speichern. Ab Werk
   steht sie auf 3; dann schlägt das Verriegeln fehl.
7. DIP-Schalter, Drehschalter und Status prüfen, erst dann ein Fahrzeug anschließen.

Die Einstellungen der Originalfirmware bleiben im Flash erhalten; Wallfree speichert seine
eigenen in einem anderen Bereich. Die Konsole der Weboberfläche (System, Expertenansicht) hat einen
Befehl `evse import-original`, der sie übernimmt; er ist auf einer Steuerung noch nicht ausgeführt worden.

Spätere Updates von Wallfree laufen über die eigene Weboberfläche
([Abschnitt 8.1 der Anleitung](anleitung/weboberflaeche.md#81-firmware-update)).

## 3 Zurück zur Originalfirmware

1. evcc und alle anderen Programme anhalten, die die Wallbox abfragen; andere Browser-Tabs mit der
   Wallbox schließen.
2. In Wallfree unter **System → Firmware-Update** die Firmwaredatei des Herstellers auswählen. Die
   Seite erkennt, dass es keine Wallfree-Datei ist, und fragt ausdrücklich nach.
3. **Update starten** und bestätigen. Nach der Übertragung meldet die Seite „Übertragen. Die
   Steuerung startet die aufgespielte Firmware.“ Ob die Originalfirmware läuft, kann sie nicht
   mehr prüfen; die Weboberfläche von Wallfree ist dann nicht mehr erreichbar.
4. Die Originalfirmware startet mit ihren alten Einstellungen und ihrer alten Adresse. Dort die
   Weboberfläche des Herstellers öffnen.

Die Einstellungen von Wallfree bleiben ebenfalls im Flash. Wer später wieder umrüstet (Abschnitt
2), findet sie unverändert vor.

## 4 Wenn etwas schiefgeht

Solange der Bootloader intakt ist, lässt sich jede Firmware über ihn neu aufspielen, Wallfree wie
die Originalfirmware. Weder Wallfree noch die Originalfirmware verändern ihn.

- **Update bricht während der Übertragung ab** (anderer Tab, evcc, Netz weg): Die bisherige
  Firmware ist dann schon gelöscht. Der Bootloader bleibt im Update-Betrieb, erst unter der
  bisherigen Adresse, danach unter **192.168.0.8**, und wartet auf eine Datei. Dort die gewünschte
  Datei erneut von Hand hochladen (oder mit `scripts/update.sh --upload-only <Adresse> <Datei>`).
  Ein halb geschriebenes Abbild startet er nicht.
- **Wallfree meldet „Prüfsumme … beschädigt“:** Die Datei kam unvollständig an. Wallfree lädt
  dann nicht; das Update wiederholen.
- **Die aufgespielte Firmware startet nicht** (falsche oder beschädigte Datei): Ist gar kein
  gültiges Programm da, bleibt der Bootloader im Update-Betrieb. Hängt die Firmware, startet der
  Watchdog die Steuerung immer wieder neu; beim fünften Mal öffnet der Bootloader für 2 Minuten
  seine Seite unter 192.168.0.8. Dort eine passende Datei hochladen.
- **Weboberfläche von Wallfree nicht erreichbar, Update darüber also nicht möglich:** Notweg über
  den Reset-Taster der Steuerung. Taster gedrückt halten: Die Power-LED bleibt etwa 5 Sekunden
  dunkel, blinkt dann langsam und nach weiteren 5 Sekunden schnell. **Während sie schnell blinkt,
  loslassen.** Wallfree übergibt dann an den Bootloader, der unter **192.168.0.8** auf eine Datei
  wartet (PC dazu im Netz 192.168.0.x); dort Wallfree oder die Originalfirmware hochladen. Kommt
  keine Datei, startet nach einigen Minuten wieder die bisherige Firmware. Die Einstellungen
  bleiben erhalten. Wer zu lange hält (bis die LED dauerhaft leuchtet), löst stattdessen den
  Werksreset aus.
