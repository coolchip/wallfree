# Ersatzfirmware: Architektur (Diskussionsgrundlage)

Stand: 2026-10-19. Entscheidungen des Nutzers sind markiert, alles andere ist Vorschlag.
Grundlage fuer die Diskussion mit dem Nutzer vor der Soll-Spezifikation. Bezug:
[AGENTS.md](../../wallbe2/AGENTS.md) (Grundsatz Steuerung/Beschaltung, Umfang),
[Spezifikation](../../wallbe2/docs/ladeautomat-spezifikation.md) (Entscheidungen 1..22),
[Parameterspeicher](../../wallbe2/docs/parameterspeicher.md), [Bootloader](../../wallbe2/docs/bootloader.md).

## 1. Vorgaben des Nutzers (2026-10-19)

- Rahmenwerk offen, **Praeferenz Zephyr** (Vorerfahrung). Wichtiger: **moeglichst
  schmaler Ansatz**, damit spaeter Platz fuer Erweiterungen bleibt.
- Ethernet: voraussichtlich der Stack des Rahmenwerks, lwIP nur bei guten Gruenden.
- **Rueckruestung auf die Originalfirmware ist sehr wichtig.** Idee des Nutzers:
  Originalparameter unangetastet lassen, Ersatzfirmware mit eigenem Speicheransatz.
- **Bootloader moeglichst beibehalten** (vereinfacht den Wechsel zur Originalfirmware).
- **OCPP perspektivisch**, allgemeine Implementierung, nicht aus der wallbe-Firmware.
- **Entschieden (2026-10-19):** Rahmenwerk **Zephyr**. Shell-Zugriff gewuenscht,
  moeglichst auch ueber das Netz (**Telnet**). **TLS und OCPP vorerst ausgeklammert.**
  Installation der Zephyr-Umgebung freigegeben.
- Referenz: Open-Source-Wallbox **Tinkerforge WARP** (OCPP 1.6J); deren HTTP-Schnittstelle
  koennte uebernommen werden - **vorher abstimmen** (Abschnitt 8).
- Hardwaretests sind erwuenscht, sobald die Hardware auf dem Tisch liegt, mit einem
  Zwischenschritt zur Pruefung der Hardwareansteuerung (Pruefsignale, Messung mit
  Oszilloskop durch den Nutzer); eventuell EV-Simulator bauen oder kaufen.

## 2. Harte Randbedingungen (belegt)

| Punkt | Wert | Beleg |
| --- | --- | --- |
| MCU | STM32F207VET6: 512 KB Flash, 128 KB SRAM (+4 KB Backup-SRAM) | Nutzer (Chipaufdruck); RM0033 PDF-Seite 55 Tab. 2, Abschnitt Backup-SRAM |
| Flash-Sektoren | 0..3 je 16 KB, 4 = 64 KB, 5..7 je 128 KB (VET6 endet bei `0x0807FFFF`) | RM0033 Tab. 2 |
| Bootloader | Sektoren 0..2 (`0x08000000..0x0800BFFF`), Hypothese, nicht im Abbild | [Bootloader](../../wallbe2/docs/bootloader.md) |
| Originalparameter | Sektor 3 (`0x0800C000`, 16 KB) | [Parameterspeicher](../../wallbe2/docs/parameterspeicher.md) |
| Anwendung | ab `0x08010000` (Sektor 4), Vektortabelle dort, VTOR gesetzt | [Ladeadresse](../../wallbe2/docs/firmware-ladeadresse.md) |
| Originalabbild | bis `0x0805F677` (SL) bzw. `0x0805FA3F` (1.27): Sektoren 4..6; **Sektor 7 ungenutzt** | Dateigroessen |
| Bootloader-Uebergabe | RTC-Backup-Register 10/11/15/13/14, dann Systemreset | [Bootloader](../../wallbe2/docs/bootloader.md) |
| RAM-Bedarf Original | `.data`+BSS bis `0x2001FA44` (fast die vollen 128 KB inkl. Heap/Stacks) | Startup |

Damit stehen der Anwendung bei unangetastetem Sektor 3 hoechstens die Sektoren 4..7 zur
Verfuegung: **448 KB**, davon 64 KB + 3 x 128 KB. Wird ein eigener Parameterbereich in
einen 128-KB-Sektor gelegt, bleiben 320 KB (Sektoren 4..6, so viel wie das Original).

**Sicherheitsrelevant:** Der Flash hat nur eine Bank; nach PM0059 (nicht im Projekt,
Belegung ausstehend) blockiert ein Lese-/Befehlszugriff waehrend Loeschen/Programmieren
den Bus. Das Loeschen eines 128-KB-Sektors dauert in der Groessenordnung von Sekunden
(Datenblatt DS6329 fehlt). Code, der in dieser Zeit laufen muss (Schutzabschaltung),
muesste aus dem RAM laufen, oder Loeschvorgaenge sind nur in einem sicheren Zustand
erlaubt (Schuetz aus, kein Fahrzeug). Das Original hat dieselbe Eigenschaft bei Sektor 3.

## 3. Parameterspeicher: Optionen

Alle Optionen lassen Sektor 3 so, dass die Originalfirmware ihre Werte wiederfindet.

| Option | Beschreibung | Vorteile | Nachteile |
| --- | --- | --- | --- |
| **P1** Sektor 3 nur lesen, eigener Speicher in **Sektor 7** | Einmalige Uebernahme alter Werte als Startwerte moeglich; eigenes Format (z. B. Zephyr NVS/ZMS oder eigenes Log) in Sektor 7 | Original voellig unangetastet; Sektor 7 vom Original nicht belegt | Ein Sektor -> beim Verdichten Loesch-Fenster ohne Zweitkopie (wie Original); 128 KB fuer wenige KB Daten; Anwendung max. 320 KB; ob Bootloader/Originalupdate Sektor 7 loescht, unbekannt |
| **P2** Sektoren 6+7 als Zwei-Seiten-Speicher | Zephyr NVS/ZMS mit zwei 128-KB-Sektoren, stromausfallsicher | robust, Standardbaustein | Anwendung nur 192 KB (Sektoren 4+5) - fuer Netzwerk + spaeter OCPP voraussichtlich zu knapp |
| **P3** Sektor 3 mitbenutzen, eigene Kennungen | Eintraege im Originalformat mit eigenem Kennungsbereich; beim Verdichten alle Originaleintraege mitkopieren | kein Flash fuer Anwendung verloren | Sektor 3 wird geloescht/neu geschrieben (nicht "unangetastet"); Risiko fuer Originalwerte bei Stromausfall |
| **P4** Backup-SRAM (4 KB) + Flash-Sicherung | haeufige Werte im batteriegepufferten SRAM | kein Flashverschleiss | nur mit VBAT-Batterie dauerhaft; Vorhandensein unbekannt |

**Nutzerentscheidung (2026-10-19):** P4 entfaellt (keine Batterie in der Steuerung
vorgesehen). **P1 vorbereiten** und nach dem Auslesen des Bootloaders validieren; P3
bleibt denkbar. Im ungünstigsten Fall wird die Rueckkehr zur Originalfirmware neu
bewertet und der Flash vollstaendig genutzt.

Urspruengliche Empfehlung: **P1** (entspricht der Nutzeridee), mit eigenem, kleinem
Format, das stromausfallsicher ist, obwohl nur ein Sektor zur Verfuegung steht (z. B. zwei
Haelften mit Gueltigkeitskennung und CRC, Verdichten nur im sicheren Zustand). Zu klaeren:
loescht der Bootloader beim Update auch Sektor 7?

**Umsetzung (2026-10-19):** P1 mit eigenem Log-Format in einer Loescheinheit
([nvm.h](../include/wallfree/nvm/nvm.h)): Eintraege mit CRC-8 im Kopf und CRC-32 ueber den
Eintrag; Bereichskopf aus Kennung (direkt nach dem Loeschen) und Abschluss (zuletzt), sodass
ein unterbrochenes Verdichten beim Start als ungueltig erkannt wird. ZMS scheidet aus (mindestens
zwei Loescheinheiten, loescht selbststaendig). Ohne zweite Loescheinheit bleibt das Fenster
"geloescht, noch nicht neu geschrieben" bestehen; dann gelten Werkseinstellungen mit Meldung
(SOLL-NVM-05), wie im Original.

## 4. Rahmenwerk

Kriterien: Flash-/RAM-Bedarf, Treiber fuer STM32F2 (ETH, ADC+DMA, TIM1, IWDG, RTC-Backup),
KSZ8081, Echtzeitverhalten fuer den Schutzpfad, Erweiterbarkeit (HTTP/REST, spaeter
OCPP mit WebSocket und ggf. TLS), Testbarkeit auf dem PC.

| Kandidat | Einschaetzung (ohne Messung) |
| --- | --- |
| **Zephyr** | Nutzerpraeferenz. STM32F2 und STM32-Ethernet werden unterstuetzt, KSZ8081-PHY-Treiber vorhanden (Bestand im aktuellen Zephyr zu pruefen). Eigener IP-Stack, HTTP-Server, Settings/NVS/ZMS, Watchdog-, ADC-, PWM-APIs. Starke Konfigurierbarkeit (Kconfig) erlaubt schmale Builds; Netzwerk + HTTP kostet erfahrungsgemaess einen grossen Teil eines 320-KB-Budgets; **Groesse muss mit einem Test-Build gemessen werden.** Startadresse `0x08010000` ueber `CONFIG_FLASH_LOAD_OFFSET`/Partitionen ohne MCUboot moeglich. Teststrategie: native_sim fuer hardwareunabhaengige Module. |
| FreeRTOS + STM32Cube-HAL + lwIP | Naechste Verwandtschaft zum Original (gleiche Bausteine), sehr klein und gut bekannt; mehr Eigenbau (Treiberintegration, HTTP, Settings), weniger einheitliche Struktur. |
| Bare-Metal/libopencm3 + lwIP | Am schmalsten, aber am meisten Eigenarbeit und Risiko im Netzwerkteil. |

Vorschlag: Zephyr als Arbeitshypothese, **Entscheidung nach einem Groessen-Spike**: ein
minimaler Zephyr-Build fuer den STM32F207 mit Ethernet, DHCP, TCP, HTTP-Server, NVS,
ADC/PWM/Watchdog, gemessen mit `rom_report`/`ram_report`. Dafuer muss die Zephyr-Umgebung
lokal installiert werden (west ist vorhanden, aber defekt; kein Zephyr-SDK) - **Installation
nur nach Rueckfrage** (AGENTS.md).

Ethernet: der Zephyr-eigene Stack, wenn Zephyr gewaehlt wird. lwIP waere nur bei
FreeRTOS/Bare-Metal sinnvoll.

OCPP: OCPP 1.6J braucht WebSocket und JSON, fuer Produktivbetrieb in der Regel TLS.
TLS (mbedTLS) ist der groesste Posten und kann allein mehrere Dutzend bis ueber 100 KB
Flash und viel RAM belegen. Bei 320 KB Anwendungsbudget ist das der kritische Punkt fuer
die Wahl P1/P2 und fuer das Rahmenwerk.

## 5. Bootloader und Updatepfad

- Ersatzfirmware als Rohabbild ab `0x08010000`, Vektortabelle dort, SP-Startwert wie Original.
- Update-Anforderung wie Original: RTC-Backup-Register 10/11/13/14/15 setzen, Systemreset.
  Was der Bootloader danach erwartet (Protokoll, Pruefungen, Abbildgroesse, Anhang
  `0x55` + Nullen), ist **unbekannt**; dafuer muss der Bootloader ausgelesen werden
  (Freigabe ausstehend). Bis dahin ist der Wechsel Original -> Ersatz -> Original ueber den
  Bootloader eine Annahme, gestuetzt durch Community-Berichte (1.27 liess sich einspielen).
- Rueckweg zur Originalfirmware: ueber denselben Bootloaderpfad; Sektor 3 bleibt erhalten.

## 6. Ladelogik-Struktur (Skizze)

- Ladelogik als **hardwareunabhaengiges Modul** (Eingaenge: CP/PP-Messwerte, DIP,
  Eingangsfunktionen, Parameter, Zeit; Ausgaenge: PWM-Tastgrad, Schuetz, Verriegelung,
  LED/Ausgangsfunktionen, Status/Fehler), vollstaendig auf dem PC testbar.
- **Schutzpfad getrennt und ereignisgesteuert** (Entscheidungen 1/2): Fehlerstrom,
  Schuetzfehler usw. schalten direkt ab, unabhaengig vom Automaten-Thread.
- Verriegelung nicht blockierend (Entscheidung 10), LD permanent/gepulst (15).
- Watchdog mit kurzem Limit und sicherem Zustand (3).

## Hardware-Inbetriebnahme

Vorschlag fuer Stufen (jede Stufe erfordert vorher eine ausdrueckliche Aenderung der
Sicherheitsgrenzen in AGENTS.md):

| Stufe | Inhalt | Ausstattung |
| --- | --- | --- |
| H0 | Lesend: Debug-Anschluss, RDP-Stufe, Bootloader/Flash/OTP auslesen | Debugger (SWD) |
| H1 | Pruef-Firmware ohne Netzspannung am Lastkreis: CP-PWM mit festen Tastgraden, R1/R2-Impulse, Ausgaenge ER/LR/VR/CR, LEDs, Schuetzspule (nur Ansteuersignal), Eingaenge und ADC-Werte melden | Oszilloskop (vorhanden), Labornetzteil |
| H2 | CP/PP mit EV-Simulator: Zustaende A..E, Diode, PP-Widerstaende | EV-Simulator |
| H3 | RCM-Selbsttest und Fehlerstrompfad mit Pruefstrom | sicherer Pruefaufbau |
| H4 | Ladelogik komplett, mit Netzspannung, Fahrzeug oder Lastsimulator | Elektrofachkraft, FI in der Hausverteilung |

EV-Simulator: Fuer H2 genuegt ein selbstgebauter Pruefstecker (Diode, CP-Widerstaende
2,74 kOhm / 1,3 kOhm / 270 Ohm nach IEC 61851-1 Anhang A, PP-Widerstaende 1,5 kOhm /
680 / 220 / 100 Ohm, Schalter). Fertige Typ-2-Pruefadapter mit Messbuchsen sind fuer
Arbeiten mit Netzspannung (H4) sicherer. Widerstandswerte gegen die Norm pruefen
(nicht lokal vorhanden).

## 7. Groessentest Zephyr (2026-10-19)

Minimaler Build mit der geplanten Grundausstattung, **nur gebaut, nicht ausgefuehrt**:
[spike-size](../spike-size/) auf der vorlaeufigen Board-Beschreibung
[boards/phoenix/ev_cc_ac1](../boards/phoenix/ev_cc_ac1/) (STM32F207VE,
RMII-Pins und KSZ8081 im 25-MHz-Modus wie im Original, TIM1 CH1 PE9, ADC PA0/PB1, IWDG,
RNG, Partitionen wie in Abschnitt 2/3). Inhalt: IPv4, DHCP, TCP, Sockets, HTTP-Server mit
JSON-Ressource `/evse/state`, Shell nur ueber Telnet inkl. Netz- und Kernel-Kommandos,
Settings ueber ZMS in Sektor 7, ADC, PWM, Watchdog, Logging.

| Ergebnis | Wert |
| --- | --- |
| Flash | **206 680 B** von 320 KB (63 %), gelinkt ab `0x08010000` (Vektor SP `0x2000b6c0`, Reset `0x08016b45`) |
| RAM | **84 732 B** von 128 KB (65 %), davon Heap 16 KB, Ethernet-Puffer ~15 KB |

Groesste Flash-Posten (`west build -t rom_report`): Netz gesamt 86 KB (IP-Stack 35 KB,
davon TCP 11 KB; HTTP-Server 19 KB; Netz-Shell-Kommandos 17 KB; Sockets 7,5 KB; DHCP 3 KB;
Ethernet-L2 3,6 KB), Shell-Kern 18 KB inkl. Telnet 2,5 KB, Treiber 16 KB (Ethernet 6 KB),
Kernel 12 KB, libc/os 13 KB, Settings/ZMS ~4 KB.

Bewertung:
- Mit P1 bleiben **~113 KB Flash** fuer Ladelogik, REST-API, Weboberflaeche und
  Erweiterungen; die Netz-Shell-Kommandos (17 KB) sind optional. Zum Vergleich: das
  Original belegt mit Weboberflaeche, OCPP, Modbus und lwIP ~318 KB.
- OCPP ohne TLS (WebSocket-Client + JSON) duerfte hineinpassen; **OCPP mit TLS (mbedTLS)
  voraussichtlich nicht** neben P1 (Schaetzung, nicht gemessen). Dann waere der
  Speicherbereich neu zu entscheiden (z. B. Parameter in Sektor 3 mitbenutzen, P3).
- RAM: 44 KB Reserve; TLS braeuchte erfahrungsgemaess grosse Teile davon.
- NVS ist fuer die 128-KB-Sektoren des F2 ungeeignet: NVS adressiert innerhalb eines
  Sektors mit 16 Bit (`ADDR_OFFS_MASK 0x0000FFFF`, `subsys/kvss/nvs/nvs_priv.h`), also
  hoechstens 64 KB, und verlangt, dass ein NVS-Sektor ein Vielfaches der Loescheinheit
  ist (`nvs.c` Zeile 1269). Die Sektoren 5..7 sind 128 KB gross und nur ganz loeschbar.
  ZMS adressiert mit 32 Bit (`zms_priv.h`) und baut mit einem 128-KB-Bereich.
- mbedTLS wurde nur als Modul heruntergeladen (fuer spaeter), ist im Testbuild aber
  **nicht** enthalten (`CONFIG_MBEDTLS` nicht gesetzt, kein Eintrag im rom_report).
- MCO1 (25 MHz auf PA8 fuer den PHY) ist in Zephyrs F2-Devicetree nicht als Knoten
  vorhanden und muss fuer die Hardware ergaenzt werden.
- Start-SP: Zephyr legt den Stackzeiger beim Reset auf das Ende seines Interrupt-Stacks
  (`0x2000b6c0`), das Original auf das RAM-Ende (`0x20020000`). Beides ist gueltig. Nur
  falls der Bootloader den Wert prueft (z. B. "muss gleich 0x20020000 sein"), waere das
  relevant; ueblich ist nur "zeigt ins RAM". Klaert sich beim Auslesen des Bootloaders.

Umgebung: Zephyr **v4.4.2**, Zephyr-SDK **1.0.1** (`arm-zephyr-eabi-gcc` 14.3.0), west 1.5.0
in `~/zephyrproject/.venv`, Module per `manifest.project-filter` auf cmsis, cmsis_6,
hal_stm32, mbedtls begrenzt (ausserhalb des Repos).

```sh
cd ~/zephyrproject && . .venv/bin/activate && cd zephyr
west build -p always -b ev_cc_ac1 /pfad/zu/wallfree/spike-size \
  -d ~/zephyrproject/build-spike
west build -d ~/zephyrproject/build-spike -t rom_report   # bzw. ram_report
```

## 8. Referenz Tinkerforge WARP (nur Notizen, nicht abgestimmt)

Quelle: [WARP API-Referenz EVSE](https://docs.warp-charger.com/en/docs/interfaces/mqtt_http/api_reference/evse),
[esp32-firmware](https://github.com/Tinkerforge/esp32-firmware) (Lizenz noch pruefen).
Pfade `http://host/evse/<name>` (GET lesen, PUT/POST schreiben) bzw. MQTT; Stroeme in **mA**.

- `evse/state` (IEC-Zustand, Schuetz, `allowed_charging_current`, `error_state`,
  `lock_state`), `evse/low_level_state` (Rohwerte), `evse/hardware_configuration`.
- **Ladestrom-Slots:** wirksamer Strom = Minimum aller aktiven Slots (Kabel, Schalter,
  global, extern, Nutzer, Lastmanagement, OCPP ...). Entspricht der Stromkette der
  Spezifikation und waere ein gutes Modell dafuer.
- `evse/global_current` wird im Flash gespeichert; fuer haeufige Aenderungen
  `evse/external_current` (nur RAM). Gleiches Problem bei uns (Flash-Verschleiss,
  Loeschpausen) -> Sollstrom fuer Regelung nicht persistent.
- `start_charging`/`stop_charging`, `external_enabled`, `ev_wakeup`,
  `control_pilot_disconnect`, `gpio_configuration`.

Einheit: WARP nutzt mA; Entscheidung 21 wurde am 2026-10-19 auf **mA** geaendert.
Offen: Pfadnamen (WARP) statt Feldnummern; der Nutzer sieht sich die WARP-API zuerst an.

## 8a. Anbindung an evcc (2026-10-19)

Die evcc-Anbindung ist dem Nutzer sehr wichtig. Nutzerentscheidung: WARP-Schnittstelle
**schrittweise nach Bedarf** nachbauen, nicht blind komplett uebernehmen.

evcc spricht die WARP ueber den Treiber `charger/warp-ws.go` an (Vorlage
`tinkerforge-warp-ws`; aelter: MQTT, `warp2-mqtt.go`). Quelle gelesen am 2026-10-19
(github.com/evcc-io/evcc, Zweig master, `charger/warp-ws.go`, `charger/warp/types.go`):

| Zugriff | Zweck | Mindestinhalt fuer uns |
| --- | --- | --- |
| `GET /info/features` | Faehigkeiten | JSON-Liste, z. B. `[]` (ohne Zaehler, NFC, Phasenumschaltung) |
| `GET /info/name` | Typ | `{"name":…, "type":…, "display_type":…, "uid":…}`; bei Typ `warp3`/`warp4` schaltet evcc zusaetzlich `evse/phase_auto_switch` ab |
| WebSocket `/ws` (bzw. `wss`) | Ereignisse | Textrahmen mit JSON `{"topic": …, "payload": …}`; noetig: `evse/state` (`iec61851_state` 0/1/2 = A/B/C), `evse/external_current` (`current` in mA); optional `evse/user_current`, `evse/user_enabled`, `charge_tracker/current_charge`, `meters/<n>/…` |
| `POST /evse/external_current` `{"current": mA}` | Ladestrom setzen; 0 = Laden aus | Sollstrom nur im RAM (kein Flashschreiben) |

Damit genuegt fuer eine erste evcc-Anbindung ein kleiner Teil: zwei GET-Ressourcen, ein
POST und ein WebSocket-Endpunkt mit Statusmeldungen. Zephyrs HTTP-Server kann WebSocket
(`CONFIG_HTTP_SERVER_WEBSOCKET`, Beispiel `samples/net/sockets/http_server`); im
Groessentest noch nicht enthalten. Authentifizierung (user/password) ist bei evcc optional.

**Nutzerentscheidung (2026-10-19):** evcc wird ueber die vorhandene Vorlage
`tinkerforge-warp-ws` angebunden; in evcc soll vorerst kein neuer Wallbox-Typ noetig sein.
Die evcc-Vorlage fuer die Phoenix-Steuerung (Modbus TCP) wird bewusst nicht genutzt
(fruehere Probleme), und **Modbus entfaellt** in der Ersatzfirmware ganz (TCP und RTU).
Folge: Der evcc-Treiber `warp-ws.go` bestimmt den Mindestumfang und das Verhalten der
Schnittstelle; Aenderungen in evcc koennen Anpassungen erfordern.

## 9. Offene Entscheidungen fuer die Diskussion

1. Parameterspeicher: **P1 vorbereiten**, Validierung nach Bootloader-Auslesen (Nutzer 2026-10-19).
2. ~~Rahmenwerk~~: Zephyr entschieden, Groessentest erledigt (Abschnitt 7).
3. ~~OCPP/TLS~~: vorerst ausgeklammert; bei spaeterem TLS Speicherbereich neu bewerten.
6. HTTP-Schnittstelle: an WARP angelehnt, **schrittweise nach Bedarf** (Nutzer 2026-10-19);
   erster Zielumfang: evcc-Mindestumfang (Abschnitt 8a).
4. Auslesen des Bootloaders (H0), sobald die Hardware verfuegbar ist.
5. Reihenfolge der Inbetriebnahmestufen H0..H4.

## 10. Rahmenfragen vom 2026-10-19 (Nutzer)

### 10.1 Systemtakt: mehr als 100 MHz?

- **Moeglich:** STM32F207 laeuft bis **120 MHz** (RM0033 Rev 10, Text Zeile ~4195: HCLK
  max. 120 MHz, APB2 max. 60 MHz, APB1 max. 30 MHz). Mit dem 25-MHz-Quarz: PLLM 25,
  PLLN 240, PLLP 2 -> 120 MHz, PLLQ 5 -> 48 MHz (USB exakt; USB nicht im Umfang), APB1 /4 =
  30 MHz, APB2 /2 = 60 MHz.
- **Wartezyklen:** RM0033 Tab. 3: bei 2,7..3,6 V gelten fuer 90..120 MHz **3 WS** - dieselben
  3 WS, die das Original schon bei 100 MHz nutzt. 120 MHz kosten also keine zusaetzlichen
  Wartezyklen (Versorgungsspannung 3,3 V angenommen, nicht gemessen).
- **Nutzen:** ~20 % mehr Rechenleistung. Die Ladelogik braucht das nicht; der Netzwerkdurchsatz
  haengt kaum am Takt. Nachteile: etwas mehr Leistungsaufnahme/Waerme, Abweichung von der vom
  Hersteller erprobten Konfiguration (EMV), alle Timer-Teiler aendern sich (TIM1: PSC 59 statt 49).
- **Entschieden (Nutzer, 2026-10-19): 100 MHz** wie Original; 120 MHz bleibt spaetere Option,
  falls TLS/OCPP Rechenzeit brauchen (dann messen).

### 10.2 Ethernet 100 Mbit/s

- **Laeuft bereits mit 100 Mbit/s:** Der Originaltreiber nutzt Autonegotiation und stellt den
  MAC je nach Ergebnis auf 10 oder 100 Mbit/s ([Ethernet-PHY](../../wallbe2/docs/ethernet-phy.md), Schritte 5/6);
  RMII mit 50-MHz-REF_CLK aus dem KSZ8081RND, HB S. 10 "100Base-TX". An einem 100-Mbit-Switch
  ergibt sich also 100 Mbit/s Vollduplex.
- Der Zephyr-Treiber (STM32-ETH + KSZ8081) macht ebenfalls Autonegotiation; keine Aenderung
  noetig. Fuer eine Wallbox ist die Geschwindigkeit ohnehin unkritisch.

### 10.3 HTTP: moeglichst viel zugaenglich (Nutzervorgabe 2026-10-19)

- **Entschieden:** Alle Messwerte, Statuswerte und Parameter der gesamten Firmware sollen ueber
  HTTP lesbar und (soweit schreibbar) schreibbar sein. Zielgruppe der Firmware sind versierte
  Anwender; fuer einfache Anwender kann eine abgespeckte Oberflaeche angeboten werden.
- Folge fuer die [Soll-Spezifikation](soll-spezifikation.md): Frage F24 (Parameter zunaechst nur
  per Shell) ist damit entschieden: HTTP von Anfang an (SOLL-API-30 ff.).

### 10.4 Weboberflaeche: Rahmenwerk (entschieden: Preact + htm)

Vorgaben: spartanisch, ressourcenschonend (Flashreserve ~113 KB fuer Ladelogik, API, Oberflaeche
und Erweiterungen), aber zeitgemaess aussehend. Groessen sind Erfahrungswerte (minifiziert +
gzip), **nicht gemessen**:

| Option | Groesse (gz, ca.) | Bemerkung |
| --- | --- | --- |
| Vanilla JS + eigenes CSS | 0 KB Bibliothek | kleinstes Ergebnis, am meisten Handarbeit |
| **Preact + htm** | ~5 KB | Komponenten wie React, ohne Build-Schritt nutzbar; WARP nutzt Preact |
| petite-vue | ~7 KB | Vue-artige Bindungen direkt im HTML |
| Alpine.js | ~15 KB | deklarativ, groesser |
| CSS: eigenes Mini-CSS / klassenloses CSS (z. B. Pico.css, ~10 KB gz) | 3..10 KB | dunkles/helles Design moeglich |

Auslieferung in jedem Fall: wenige statische Dateien, gzip-komprimiert im Flash, vom Zephyr-
HTTP-Server mit `Content-Encoding: gzip` ausgeliefert; die Oberflaeche spricht nur die HTTP-/
WebSocket-API (kein serverseitiges Rendern). Eine Seite mit Ansicht "Einfach" und "Experte"
ist billiger als zwei getrennte Oberflaechen. Zielgroesse: <= 30 KB gz fuer die gesamte
Oberflaeche.

**Entschieden (Nutzer, 2026-10-19):** **Preact + htm** mit eigenem kleinem CSS.

### 10.5 Projektname und Ort

**Entschieden (Nutzer, 2026-10-19):** Projektname **Wallfree**. Die Ersatzfirmware kommt in ein
**neues, veroeffentlichbares Repository** (Firmware, Soll-Spezifikation, Tests); dieses Repo
bleibt Analyse-Archiv mit Originalfirmware und Handbuechern (urheberrechtlich geschuetzt, nicht
veroeffentlichen) und wird von dort verlinkt. Pfad `~/proj/wallfree`, Lizenz **Apache-2.0**
(Nutzer, 2026-10-19).
