# Simulation bedienen

Die ganze Firmware läuft als Linux-Programm (`native_sim`) mit einem Modell der Hardware:
Fahrzeug, Kabel, Eingänge, Fehlerstromsensor, Verriegelungsaktor und Schütz. Weboberfläche,
HTTP-API, WebSocket und Konsole sind dieselben wie auf der Steuerung; zusätzlich gibt es hier eine
Telnet-Shell für das Modell (auf der Steuerung nicht). Was die Simulation
nicht abbildet: [Test und Simulation](test-und-simulation.md#abweichungen-der-simulation-von-der-steuerung).

## Starten

```sh
scripts/sim-run.sh [--build] [--dip=<maske>] [--rotary=<pos>] [--http-port=<port>] [--reset-button=<ms>]
```

| Option | Bedeutung | Default |
| --- | --- | --- |
| `--build` | vorher neu bauen (sonst nur, wenn noch kein Build existiert) | – |
| `--dip=<maske>` | DIP-Schalter D1..D10 als Bitmaske, Bit 0 = D1 (Beispiele unten) | 0 (alle aus) |
| `--rotary=<pos>` | Drehschalter 0..15 (0 = 5 %, 1..9 = 6/10/13/16/20/32/63/70/80 A, 10..15 = Fehler) | 6 (32 A) |
| `--http-port=<port>` | Port für Weboberfläche und API | 8081 |
| `--reset-button=<ms>` | Reset-Taster beim Programmstart so lange halten: 5000..9999 = Übergabe an den Bootloader, ab 10000 = Werksreset; gilt nicht für Neustarts der Firmware | 0 |

DIP und Drehschalter gelten wie auf der Steuerung nur beim Start. Häufige DIP-Masken:

| Maske | DIP | Bedeutung |
| --- | --- | --- |
| `0x0` | – | festes Kabel, keine Verriegelung, keine Freigabe nötig |
| `0x1` | D1 | Steckdose: PP auswerten (dann `sim cable` setzen) |
| `0x29` | D1, D4, D6 | Steckdose mit Verriegelung und LD-Rückmeldung |
| `0x41` | D1, D7 | Steckdose, Laden nur mit Freigabe über EN (`sim din en 1`) |
| `0x109` | D1, D4, D9 | Steckdose, manuelle Verriegelung über ML (`sim din ml 1`) |

Danach erreichbar:

- Weboberfläche und API: `http://127.0.0.1:8081/` (Werkszugang `admin` / `wallfree`; beim
  ersten Aufruf muss das Passwort geändert werden)
- Telnet-Shell: `telnet 127.0.0.1 2323` (ohne Anmeldung, nur in der Simulation); dieselben Kommandos
  auch in der Weboberfläche unter System -> Konsole (Expertenansicht)

Die Einstellungen liegen in `build/sim-flash.bin` und bleiben über Neustarts erhalten; Datei
löschen = fabrikneu. **Achtung:** `scripts/evcc-sim.sh` setzt bei einer fabrikneuen Datei die
Zugangsdaten `evcc` / `evcc-sim-test` (wie in `tools/evcc/evcc.yaml`); sie gelten danach auch
für die Weboberfläche. Passt keiner der beiden Zugänge, Datei löschen oder mit
`--reset-button=11000` starten (beides setzt alle Einstellungen zurück). Fragt der Browser danach nicht nach dem Werkszugang, schickt
er noch die alten Zugangsdaten: neuen Tab oder privates Fenster öffnen. Beenden mit Strg+C im Terminal der Simulation. Ein Neustart über die
Oberfläche oder `reboot` startet den Prozess selbst neu; das Modell (z. B. angestecktes
Fahrzeug) bleibt dabei erhalten.

Firmware-Update ausprobieren: unter System eine `.bin`-Datei wählen (z. B.
`build/ev_cc_ac1/zephyr/zephyr.bin`) und "Update starten". Die Simulation übergibt an einen
Ersatz-Bootloader (`scripts/bootloader-sim.py`), der wie das Original die Datei annimmt und
danach neu startet; aufgespielt wird nichts, es läuft weiter die Simulation. Notweg ohne
Browser: `scripts/update.sh -u <benutzer>:<passwort> 127.0.0.1:8081 <datei.bin>`.

Zum Testen mit evcc: [Test und Simulation](test-und-simulation.md#evcc-gegen-die-simulation).

## Telnet: Pfeiltasten und Historie

Der Telnet-Client startet im Zeilenmodus; Pfeiltasten erscheinen dann als `^[[A` und wirken erst
mit Enter. Für Historie (Pfeil hoch/runter), Zeilenbearbeitung und Tab-Vervollständigung nach
dem Verbinden einmal **Strg+]** drücken und `mode character` eingeben. (Eine `~/.telnetrc`
schaltet den Modus bei inetutils-telnet nicht um.) Das gilt genauso für die Steuerung.

## Das Kommando `sim`

`sim` ohne Argument entspricht `sim status`. `sim help` bzw. Tab zeigt die Unterkommandos.
Die Kommandos der Ladelogik stehen ohne Vorsatz daneben (`status`, `param ...`; `help` listet alle).

| Kommando | Wirkung |
| --- | --- |
| `sim status` | Modell und Ausgänge: Fahrzeug, Diode, PP- und Eingangspegel, Sensor, Aktor, Tastgrad, Schütz, Verriegelungsimpulse, Ausgänge ER/LR/VR/CR, LEDs, Netz |
| `sim car <A..E>` | Fahrzeugzustand am CP: A = kein Fahrzeug, B = verbunden, C = Ladewunsch, D = mit Belüftung, E = Kurzschluss CP-PE |
| `sim diode <0\|1>` | Fahrzeugdiode vorhanden (0 = Diode fehlt -> Fehler beim PWM-Start) |
| `sim cable <0\|13\|20\|32\|63\|invalid>` | Kabel am PP (nur mit D1 ausgewertet, sonst Warnung); `0` = kein Kabel |
| `sim din <en\|ml\|xr\|in> <0\|1>` | Digitaleingang EN, ML, XR, IN; LD kommt aus dem Aktormodell |
| `sim ld <none\|permanent\|pulse>` | LD-Rückmeldung des Aktors: keine, Kontakt geschlossen solange verriegelt, oder nur während der Fahrt (wie Küster; Default) |
| `sim actuator <ok\|stuck>` | Verriegelungsaktor fährt bzw. klemmt |
| `sim rcm ok` | Fehlerstromsensor gesund, kein Fehlerstrom |
| `sim rcm fault` / `sim rcm clear` | Fehlerstrom an (SWITCH_1 und SWITCH_2) / aus |
| `sim rcm dead` | Sensor reagiert nicht auf den Testimpuls (Selbsttest schlägt fehl) |
| `sim rcm err <0\|1>` | Sensorfehler ERR_OUT |
| `sim weld <0\|1>` | Schütz verschweißt (Sense meldet weiter Spannung) |
| `sim link <0\|1>` | Netzverbindung für die Netzwerk-LED |
| `sim supply <mV>` | Versorgungsmessung (unter 9000 -> Warnung) |
| `sim hang` | Messblöcke anhalten; die Überwachung muss nach 50 ms zurücksetzen |
| `sim sleep <ms>` | aufrufenden Thread blockieren; über die Konsole der Weboberfläche hält das den HTTP-Server an (Überwachung nach 10 s, Neustart nur ohne Ladevorgang) |
| `sim meter <ok\|off\|frame\|flaky\|bad>` | simulierter Energiezähler (nur mit `meter_type` != 0): normal, keine Antwort, gestörte Telegramme, jede zweite Anfrage ohne Antwort, Spannung außerhalb der Grenze |
| `sim meter energy <Wh>` | Zählerstand setzen (kleiner als bisher -> Ereignis "Zählerstand gesunken") |

Modellverhalten: Der Aktor braucht 200 ms für eine Fahrt. Beim Selbsttest fällt SWITCH_2 nach
330 ms, SWITCH_1 nach 770 ms, beide kehren nach 1,9 s zurück (wie Bender). Das Modellfahrzeug
bleibt in C, wenn die PWM endet (ein echtes Fahrzeug ginge nach B).

## Beispielabläufe

**Laden mit festem Kabel** (Start ohne Optionen):

```text
sim car B          # Fahrzeug verbunden -> Selbsttest, dann PWM (status: S5)
sim car C          # Ladewunsch -> Schuetz ein (S6)
current 10000      # Sollstrom 10 A (oder in der Oberflaeche)
current 0          # Laden aus: PWM aus, Schuetz nach Fahrzeugreaktion bzw. 5 s aus
sim car A          # abstecken
```

**Steckdose mit Verriegelung** (`scripts/sim-run.sh --dip=0x29`):

```text
sim cable 32       # Kabel einstecken
sim car B          # Fahrzeug -> verriegeln (LD-Impuls), Selbsttest, PWM
sim car C          # laden
sim car A          # abstecken -> entriegeln
sim actuator stuck # naechstes Mal klemmt der Aktor -> Fehlversuche, ERR-LOCK
```

**Fehlerfälle beim Laden:**

```text
sim rcm fault      # Fehlerstrom: Schuetz sofort aus, ERR-RCM, Zustand S9
sim car A          # abstecken
sim rcm clear      # Fehlerstrom weg; nach 30 s Wiederholtest wird der Fehler geloescht
sim weld 1         # nach dem naechsten Ladeende: Schuetzfehler (CP F), quittieren mit errors clear
sim diode 0        # Diode fehlt beim naechsten PWM-Start: Fehler, 3 s -12 V
```

**Energiezähler** (nur Anzeige): Zählertyp einstellen, neu starten, dann laden.

```text
sys set meter_type 2   # 1 ABB B23, 2 SDM630, 3 SDM72D-M V2, 4 SDM72D-M V1; 0 = keiner
reboot                 # Zaehlerparameter wirken nach dem Neustart
meter                  # Zustand, Messwerte, Fehlerzaehler
sim car C                   # laden: Strom und Leistung erscheinen im Zaehler
sim meter off               # Zaehler antwortet nicht: nach ~10 s Zustand "error", Laden laeuft weiter
sim meter ok
```

In der Oberfläche: Karte "Zähler" (Übersicht), Messwerte, Fehlerzähler und Leistung im
Verlauf (Status), Einstellung unter Einstellungen/Zaehler. evcc liest die Werte über `meters/0/*`.

**MQTT** (Broker auf dem PC: `mosquitto -p 1883`, oder ein Broker im LAN): wirkt sofort.

```text
sys set mqtt_broker 127.0.0.1
sys set mqtt_enable 1          # verbindet ohne Neustart
sys set mqtt_commands 1        # Ladestrom ueber MQTT setzbar (Werkszustand aus)
mqtt                           # Zustand, Praefix, Zaehler
```

Mitlesen und Ladestrom setzen (Präfix laut `mqtt`, in der Simulation `wallfree/000000`):

```sh
mosquitto_sub -t 'wallfree/000000/#' -v
mosquitto_pub -t wallfree/000000/evse/external_current_update -m 10000
```

In der Oberfläche unter Einstellungen/MQTT, dort auch der Verbindungszustand.

**Werksreset:** `scripts/sim-run.sh --reset-button=11000` (Werkszugang danach wieder aktiv).
