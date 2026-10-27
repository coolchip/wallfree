# Umrüsten auf Wallfree und zurück

Diese Anleitung beschreibt, wie man die Originalfirmware der Ladesteuerung
EV-CC-AC1-M3-CBC-RCM-ETH sichert, Wallfree aufspielt und später wieder zur Originalfirmware
zurückkehrt. Alle Schritte sind an einer Steuerung (Testgerät, Originalfirmware 0.81) durchgeführt
worden, ohne Fahrzeug und ohne Last. Was Wallfree anders macht, steht in
[Unterschiede zur Originalfirmware](unterschiede-originalfirmware.md).

## 1 Bevor Sie anfangen

- **Netzspannung:** Die Steuerung sitzt in der Ladestation und wird aus dem Netz versorgt.
  Arbeiten im geöffneten Gehäuse nur spannungsfrei oder durch eine Elektrofachkraft. Den Debugger
  nur bei ausgeschalteter Ladestation anstecken und abziehen; bei eingeschalteter Ladestation
  nichts im Gehäuse berühren.
- **Laden ist auf Hardware noch nicht geprüft** (siehe [README](../README.md)). Wallfree ist
  derzeit für Erprobung durch versierte Anwender gedacht.
- **Ohne Sicherung kein Rückweg:** Wallfree enthält die Originalfirmware nicht und kann sie nicht
  auslesen. Wer später zurück will, braucht eine eigene Sicherung (Abschnitt 2).
- **Nichts weiter angeschlossen lassen,** was die Steuerung abfragt (evcc, Home Assistant,
  Monitoring), solange ein Update läuft. Jede andere Anfrage bricht die Übertragung im Bootloader
  ab.

Benötigt:

- PC im selben Netz wie die Steuerung, mit Browser.
- Die Wallfree-Datei `wallfree-<version>.bin` (aus einer Veröffentlichung oder selbst gebaut mit
  `scripts/build-image.sh`).
- Für die Sicherung: ein SWD-Debugger (z. B. ST-LINK) mit [OpenOCD](https://openocd.org) und
  passende Leitungen zur Stiftleiste der Steuerung.

## 2 Originalfirmware sichern

Gesichert wird der gesamte Flash der Steuerung über die Debug-Schnittstelle (SWD). Die Sicherung
enthält den Bootloader, die Einstellungen und die Originalfirmware. Die Steuerung läuft dabei
normal weiter; gelesen wird nur.

### 2.1 Anschluss

Die 6-polige Stiftleiste (2 × 3, Raster 2,54 mm) liegt neben dem Prozessor, Pin 1 ist markiert.
Belegung am Testgerät:

| Pin | Signal | Pin | Signal |
| --- | --- | --- | --- |
| 1 | SWDIO | 2 | 3,3 V (nur Referenz für den Debugger) |
| 3 | SWCLK | 4 | SWO |
| 5 | NRST | 6 | GND |

Die Steuerung nicht über den Debugger versorgen; der Debugger misst an Pin 2 nur die Spannung.
Erst alles verbinden, dann die Ladestation einschalten.

### 2.2 Leseschutz prüfen

```sh
openocd -f interface/stlink.cfg -f target/stm32f2x.cfg \
    -c "init" -c "mdw 0x40023c14" -c "shutdown"
```

Die Ausgabe zeigt eine Zahl wie `0x40023c14: 0fffaaed`. Entscheidend ist das zweite Byte von
rechts:

- **`aa`:** kein Leseschutz, weiter mit 2.3.
- **jeder andere Wert:** Der Flash ist für den Debugger gesperrt. **Den Leseschutz nicht
  aufheben:** Das löscht den gesamten Flash einschließlich Bootloader, die Steuerung wäre danach
  ohne Debugger nicht mehr nutzbar. Ohne Sicherung besser nicht umrüsten.

### 2.3 Flash auslesen

```sh
openocd -f interface/stlink.cfg -f target/stm32f2x.cfg -c "init" \
    -c "dump_image flash.bin 0x08000000 0x80000" \
    -c "dump_image flash2.bin 0x08000000 0x80000" \
    -c "dump_image otp.bin 0x1fff7800 528" \
    -c "dump_image optionbytes.bin 0x1fffc000 16" \
    -c "shutdown"
cmp flash.bin flash2.bin && echo gleich
```

Beide Lesevorgänge müssen gleich sein (`gleich`). `flash.bin` ist 512 KB groß.

### 2.4 Datei für den Rückweg herausschneiden

Über die Weboberfläche lässt sich nur der Bereich der Firmware aufspielen (320 KB ab
`0x08010000`), nicht der ganze Flash:

```sh
dd if=flash.bin of=original-firmware.bin bs=1024 skip=64 count=320
```

`flash.bin`, `otp.bin`, `optionbytes.bin` und `original-firmware.bin` an einem sicheren Ort
aufbewahren. Die Dateien enthalten Seriennummer und Einstellungen der Steuerung und gehören nicht
ins Netz.

Eine Firmwaredatei des Herstellers aus einer anderen Quelle kann eine andere Version sein als die
installierte; ob sie zu Bootloader und gespeicherten Einstellungen der eigenen Steuerung passt,
ist nicht geprüft. Die eigene Sicherung ist der sichere Weg.

## 3 Wallfree aufspielen

Der Bootloader der Steuerung nimmt jede Firmwaredatei über eine einfache Webseite an. Er bleibt
beim Umrüsten unverändert.

1. Weboberfläche der Originalfirmware öffnen, Seite **Config**, Knopf **Firmware-Update**.
2. Die Steuerung startet nach etwa 10 s neu in den Bootloader. Er antwortet unter der bisherigen
   Adresse; hat die Originalfirmware keine gültige Adresse übergeben, unter **192.168.0.8**
   (dann den PC vorübergehend auf eine Adresse im Netz 192.168.0.x stellen). Die Modul-LEDs
   blinken gemeinsam.
3. Die Seite des Bootloaders im Browser öffnen (`http://<Adresse>/`), `wallfree-<version>.bin`
   auswählen und hochladen. Nur dieser eine Browser-Tab, keine anderen Abfragen. Der Bootloader
   wartet bis zu 2 Minuten auf die Datei; die Übertragung selbst dauert wenige Sekunden.
4. Nach der Bestätigung startet Wallfree. Adresse: wie zuvor eingestellt, ab Werk per DHCP
   (siehe [Bedienungsanleitung, Abschnitt 2](anleitung/weboberflaeche.md#2-voraussetzungen-und-aufruf)).
5. Anmelden mit `admin` / `wallfree` und neue Zugangsdaten vergeben
   ([Abschnitt 3 der Anleitung](anleitung/weboberflaeche.md#3-erste-anmeldung)).
6. **Bei einer wallbe-Ladestation:** Die Verriegelung meldet gepulst. Unter Einstellungen →
   Laden (Expertenansicht) die Funktion des Eingangs **LD auf 22** stellen und speichern. Ab Werk
   steht sie auf 3; dann schlägt das Verriegeln fehl.
7. DIP-Schalter, Drehschalter und Status auf der Übersicht prüfen, erst dann ein Fahrzeug
   anschließen.

Die Einstellungen der Originalfirmware bleiben im Flash erhalten; Wallfree speichert seine
eigenen in einem anderen Bereich. Die Telnet-Shell hat einen Befehl `evse import-original`, der
sie übernimmt; er ist auf einer Steuerung noch nicht ausgeführt worden.

Spätere Updates von Wallfree laufen über die eigene Weboberfläche
([Abschnitt 8.1 der Anleitung](anleitung/weboberflaeche.md#81-firmware-update)).

## 4 Zurück zur Originalfirmware

1. evcc und alle anderen Programme anhalten, die die Wallbox abfragen; andere Browser-Tabs mit der
   Wallbox schließen.
2. In Wallfree unter **System → Firmware-Update** die Datei `original-firmware.bin` aus 2.4
   auswählen. Die Seite erkennt, dass es keine Wallfree-Datei ist, und fragt ausdrücklich nach.
3. **Update starten** und bestätigen. Nach der Übertragung meldet die Seite „Übertragen. Die
   Steuerung startet die aufgespielte Firmware.“ Ob die Originalfirmware läuft, kann sie nicht
   mehr prüfen; die Weboberfläche von Wallfree ist dann nicht mehr erreichbar.
4. Die Originalfirmware startet mit ihren alten Einstellungen und ihrer alten Adresse (am Testgerät
   die feste Adresse 192.168.0.8). Dort die Weboberfläche des Herstellers öffnen.

Die Einstellungen von Wallfree bleiben ebenfalls im Flash. Wer später wieder umrüstet (Abschnitt
3), findet sie unverändert vor.

## 5 Wenn etwas schiefgeht

- **Update bricht während der Übertragung ab** (anderer Tab, evcc, Netz weg): Die bisherige
  Firmware ist dann schon gelöscht. Der Bootloader bleibt im Update-Betrieb, erst unter der
  bisherigen Adresse, danach unter **192.168.0.8**, und wartet auf eine Datei. Dort
  die Datei erneut von Hand hochladen (oder mit `scripts/update.sh --upload-only <Adresse>
  <Datei>`). Ein halb geschriebenes Abbild startet er nicht.
- **Wallfree meldet „Prüfsumme … beschädigt“:** Die Datei kam unvollständig an. Wallfree lädt
  dann nicht; das Update wiederholen.
- **Steuerung antwortet gar nicht mehr:** Mit dem Debugger lässt sich der ganze Flash aus
  `flash.bin` zurückschreiben, z. B. `openocd -f interface/stlink.cfg -f target/stm32f2x.cfg -c
  "program flash.bin 0x08000000 verify reset exit"`. Das stellt den Zustand zum Zeitpunkt der
  Sicherung her, einschließlich der damaligen Einstellungen.
