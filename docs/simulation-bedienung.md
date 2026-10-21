# Simulation bedienen

Die ganze Firmware laeuft als Linux-Programm (`native_sim`) mit einem Modell der Hardware:
Fahrzeug, Kabel, Eingaenge, Fehlerstromsensor, Verriegelungsaktor und Schuetz. Weboberflaeche,
HTTP-API, WebSocket und Telnet-Shell sind dieselben wie auf der Steuerung. Was die Simulation
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
| `--http-port=<port>` | Port fuer Weboberflaeche und API | 8081 |
| `--reset-button=<ms>` | Reset-Taster beim Start so lange halten (>= 5000 = Werksreset) | 0 |

DIP und Drehschalter gelten wie auf der Steuerung nur beim Start. Haeufige DIP-Masken:

| Maske | DIP | Bedeutung |
| --- | --- | --- |
| `0x0` | – | festes Kabel, keine Verriegelung, keine Freigabe noetig |
| `0x1` | D1 | Steckdose: PP auswerten (dann `sim cable` setzen) |
| `0x29` | D1, D4, D6 | Steckdose mit Verriegelung und LD-Rueckmeldung |
| `0x41` | D1, D7 | Steckdose, Laden nur mit Freigabe ueber EN (`sim din en 1`) |
| `0x109` | D1, D4, D9 | Steckdose, manuelle Verriegelung ueber ML (`sim din ml 1`) |

Danach erreichbar:

- Weboberflaeche und API: `http://127.0.0.1:8081/` (Werkszugang `admin` / `wallfree`; beim
  ersten Aufruf muss das Passwort geaendert werden)
- Telnet-Shell: `telnet 127.0.0.1 2323` (ohne Anmeldung)

Die Einstellungen liegen in `build/sim-flash.bin` und bleiben ueber Neustarts erhalten; Datei
loeschen = fabrikneu. **Achtung:** `scripts/evcc-sim.sh` setzt bei einer fabrikneuen Datei die
Zugangsdaten `evcc` / `evcc-sim-test` (wie in `tools/evcc/evcc.yaml`); sie gelten danach auch
fuer die Weboberflaeche. Passt keiner der beiden Zugaenge, Datei loeschen oder mit
`--reset-button=6000` starten (beides setzt alle Einstellungen zurueck). Beenden mit Strg+C im Terminal der Simulation. Ein Neustart ueber die
Oberflaeche oder `evse reboot` startet den Prozess selbst neu; das Modell (z. B. angestecktes
Fahrzeug) bleibt dabei erhalten.

Zum Testen mit evcc: [Test und Simulation](test-und-simulation.md#evcc-gegen-die-simulation).

## Telnet: Pfeiltasten und Historie

Der Telnet-Client startet im Zeilenmodus; Pfeiltasten erscheinen dann als `^[[A` und wirken erst
mit Enter. Fuer Historie (Pfeil hoch/runter), Zeilenbearbeitung und Tab-Vervollstaendigung nach
dem Verbinden einmal **Strg+]** druecken und `mode character` eingeben. (Eine `~/.telnetrc`
schaltet den Modus bei inetutils-telnet nicht um.) Das gilt genauso fuer die Steuerung.

## Das Kommando `sim`

`sim` ohne Argument entspricht `sim status`. `sim help` bzw. Tab zeigt die Unterkommandos.
Die Ladelogik selbst bedient man mit `evse ...` (`evse help`).

| Kommando | Wirkung |
| --- | --- |
| `sim status` | Modell und Ausgaenge: Fahrzeug, Diode, PP- und Eingangspegel, Sensor, Aktor, Tastgrad, Schuetz, Verriegelungsimpulse, Ausgaenge ER/LR/VR/CR, LEDs, Netz |
| `sim car <A..E>` | Fahrzeugzustand am CP: A = kein Fahrzeug, B = verbunden, C = Ladewunsch, D = mit Belueftung, E = Kurzschluss CP-PE |
| `sim diode <0\|1>` | Fahrzeugdiode vorhanden (0 = Diode fehlt -> Fehler beim PWM-Start) |
| `sim cable <0\|13\|20\|32\|63\|invalid>` | Kabel am PP (nur mit D1 ausgewertet); `0` = kein Kabel |
| `sim din <en\|ml\|xr\|in> <0\|1>` | Digitaleingang EN, ML, XR, IN; LD kommt aus dem Aktormodell |
| `sim ld <none\|permanent\|pulse>` | LD-Rueckmeldung des Aktors: keine, Kontakt geschlossen solange verriegelt, oder nur waehrend der Fahrt (wie Kuester; Default) |
| `sim actuator <ok\|stuck>` | Verriegelungsaktor faehrt bzw. klemmt |
| `sim rcm ok` | Fehlerstromsensor gesund, kein Fehlerstrom |
| `sim rcm fault` / `sim rcm clear` | Fehlerstrom an (SWITCH_1 und SWITCH_2) / aus |
| `sim rcm dead` | Sensor reagiert nicht auf den Testimpuls (Selbsttest schlaegt fehl) |
| `sim rcm err <0\|1>` | Sensorfehler ERR_OUT |
| `sim weld <0\|1>` | Schuetz verschweisst (Sense meldet weiter Spannung) |
| `sim link <0\|1>` | Netzverbindung fuer die Netzwerk-LED |
| `sim supply <mV>` | Versorgungsmessung (unter 9000 -> Warnung) |
| `sim hang` | Messbloecke anhalten; die Ueberwachung muss nach 50 ms zuruecksetzen |
| `sim meter <ok\|off\|frame\|flaky\|bad>` | simulierter Energiezaehler (nur mit `meter_type` != 0): normal, keine Antwort, gestoerte Telegramme, jede zweite Anfrage ohne Antwort, Spannung ausserhalb der Grenze |
| `sim meter energy <Wh>` | Zaehlerstand setzen (kleiner als bisher -> Ereignis "Zaehlerstand gesunken") |

Modellverhalten: Der Aktor braucht 200 ms fuer eine Fahrt. Beim Selbsttest faellt SWITCH_2 nach
330 ms, SWITCH_1 nach 770 ms, beide kehren nach 1,9 s zurueck (wie Bender). Das Modellfahrzeug
bleibt in C, wenn die PWM endet (ein echtes Fahrzeug ginge nach B).

## Beispielablaeufe

**Laden mit festem Kabel** (Start ohne Optionen):

```text
sim car B          # Fahrzeug verbunden -> Selbsttest, dann PWM (evse status: S5)
sim car C          # Ladewunsch -> Schuetz ein (S6)
evse current 10000 # Sollstrom 10 A (oder in der Oberflaeche)
evse current 0     # Laden aus: PWM aus, Schuetz nach Fahrzeugreaktion bzw. 5 s aus
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

**Fehlerfaelle beim Laden:**

```text
sim rcm fault      # Fehlerstrom: Schuetz sofort aus, ERR-RCM, Zustand S9
sim car A          # abstecken
sim rcm clear      # Fehlerstrom weg; nach 30 s Wiederholtest wird der Fehler geloescht
sim weld 1         # nach dem naechsten Ladeende: Schuetzfehler (CP F), quittieren mit evse errors clear
sim diode 0        # Diode fehlt beim naechsten PWM-Start: Fehler, 3 s -12 V
```

**Energiezaehler** (nur Anzeige): Zaehlertyp einstellen, neu starten, dann laden.

```text
evse sys set meter_type 2   # 1 ABB B23, 2 SDM630, 3 SDM72D-M V2, 4 SDM72D-M V1; 0 = keiner
evse reboot                 # Zaehlerparameter wirken nach dem Neustart
evse meter                  # Zustand, Messwerte, Fehlerzaehler
sim car C                   # laden: Strom und Leistung erscheinen im Zaehler
sim meter off               # Zaehler antwortet nicht: nach ~10 s Zustand "error", Laden laeuft weiter
sim meter ok
```

In der Oberflaeche: Karte "Zaehler" (Einfach), Messwerte und Fehlerzaehler (Experte),
Einstellung unter "Netz, Zaehler und System". evcc liest die Werte ueber `meters/0/*`.

**MQTT** (Broker auf dem PC: `mosquitto -p 1883`, oder ein Broker im LAN): wirkt sofort.

```text
evse sys set mqtt_broker 127.0.0.1
evse sys set mqtt_enable 1          # verbindet ohne Neustart
evse sys set mqtt_commands 1        # Ladestrom ueber MQTT setzbar (Werkszustand aus)
evse mqtt                           # Zustand, Praefix, Zaehler
```

Mitlesen und Ladestrom setzen (Praefix laut `evse mqtt`, in der Simulation `wallfree/000000`):

```sh
mosquitto_sub -t 'wallfree/000000/#' -v
mosquitto_pub -t wallfree/000000/evse/external_current_update -m 10000
```

In der Oberflaeche unter "Netz, Zaehler und System" (Experte), dort auch der Verbindungszustand.

**Werksreset:** `scripts/sim-run.sh --reset-button=6000` (Werkszugang danach wieder aktiv).
