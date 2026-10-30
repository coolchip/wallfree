# Technik und Lizenzen

Womit Wallfree gebaut ist und unter welchen Lizenzen die verwendete Software steht.

## Firmware

| Baustein | Version | Wofür |
| --- | --- | --- |
| [Zephyr RTOS](https://zephyrproject.org) | 4.4.2 | Betriebssystem: Threads, Watchdog, Netzwerk (IPv4, TCP/UDP, DHCP, DNS), HTTP-Server mit WebSocket, MQTT-Client, SNTP, Modbus RTU, Shell (Konsole der Weboberfläche), Protokollausgabe |
| Zephyr SDK | 1.0.1 | Compiler GCC 14.3 für ARM Cortex-M3, C-Bibliothek picolibc 1.8.10 |
| STM32Cube HAL/LL für STM32F2 | aus Zephyr (`hal_stm32`) | Ethernet-Treiber und Hilfsfunktionen für den STM32F207 |
| CMSIS | aus Zephyr | Zugriff auf den Prozessorkern |
| Mbed TLS / TF-PSA-Crypto | 4.1.0 / 1.1.0 | Hashfunktionen, u. a. SHA-1 für den WebSocket-Handshake (keine Verschlüsselung, kein TLS) |
| eigener Code | – | Ladelogik (`lib/evse`), Parameterspeicher (`lib/nvm`), API-Bausteine mit Digest-Anmeldung und MD5 (`lib/api`), Energiezähler (`lib/meter`), Hardwareschicht und Anwendung (`app/`) |

Die Ladelogik ist reines C ohne Hardwarebezug und läuft unverändert auf dem PC (Tests,
Simulation). Wallfree läuft ab `0x08010000` hinter dem unveränderten Bootloader der Steuerung;
Rahmenentscheidungen in [Architektur](architektur.md).

## Weboberfläche

| Baustein | Version | Wofür |
| --- | --- | --- |
| [Preact](https://preactjs.com) | 10 (in htm enthalten) | Oberfläche aus Komponenten mit Hooks |
| [htm](https://github.com/developit/htm) | 3.1.1 | HTML-artige Vorlagen in JavaScript ohne Übersetzungsschritt |
| eigenes CSS und JavaScript | – | `web/style.css`, `web/app.js`, ohne Rahmenwerk für das Aussehen |

Kein Build-Schritt (kein npm, kein Bundler); die Dateien werden beim Bauen der Firmware mit gzip
komprimiert und aus dem Flash ausgeliefert. Die Seite lädt nichts aus dem Internet.

## Entwicklung und Tests

| Werkzeug | Wofür |
| --- | --- |
| ztest und twister (Zephyr) auf `native_sim` | Unit-Tests der Bibliotheken als Linux-Programm |
| Python 3 (`unittest`) | Ende-zu-Ende-Tests gegen die ganze Firmware als PC-Simulation, Ersatz-Bootloader |
| Mosquitto | MQTT-Broker für die Tests |
| Node.js mit headless Chrome (DevTools-Protokoll) | Browser-Durchlauf, Bilder der Anleitung |
| [evcc](https://evcc.io) | Test der Anbindung (Vorlage `tinkerforge-warp-ws`) |
| gcovr | Abdeckung der Tests |
| pandoc und Chrome | Anleitung als PDF |
| OpenOCD mit ST-LINK | Entwicklung an der Steuerung (Debugger), für Anwender nicht nötig |

## Lizenzen

Wallfree steht unter der [Apache License 2.0](../LICENSE). Mitgelieferte und eingebundene
Software anderer Autoren:

| Software | Lizenz | im Firmware-Abbild |
| --- | --- | --- |
| Zephyr RTOS | Apache-2.0 | ja |
| darin HTTP-Parser (`http_parser.c`, `http_parser_url.c`; Joyent und Node-Beitragende, nach NGINX) | MIT | ja |
| darin `fnmatch.c`, `getopt.c` (aus NetBSD; Regents of the University of California, FreeBSD Foundation) | BSD-3-Clause | ja |
| CMSIS | Apache-2.0 | ja |
| STM32Cube HAL/LL (STMicroelectronics) | BSD-3-Clause | ja |
| Mbed TLS / TF-PSA-Crypto | Apache-2.0 (wahlweise GPL-2.0-or-later; Wallfree nutzt Apache-2.0) | ja |
| picolibc | BSD-artige Lizenzen ([COPYING.picolibc](https://github.com/picolibc/picolibc/blob/main/COPYING.picolibc)) | ja |
| libgcc | GPL-3.0 mit GCC Runtime Library Exception | ja |
| htm | Apache-2.0 ([web/vendor/LICENSE-htm](../web/vendor/LICENSE-htm)) | ja (Weboberfläche) |
| Preact | MIT ([web/vendor/LICENSE-preact](../web/vendor/LICENSE-preact)) | ja (Weboberfläche) |

Die Steuerung liefert die Hinweise selbst aus: Die Seite **System** nennt die Lizenz und verlinkt
`/lizenzen.txt` ([web/lizenzen.txt](../web/lizenzen.txt)) mit der Liste oben sowie den Texten der
MIT- und der BSD-Lizenzen. Der Text der Apache-Lizenz liegt wegen der Größe nur als Verweis bei
(etwa 4 KB im Flash); er steht in [LICENSE](../LICENSE) und gehört zu jeder Veröffentlichung.
Ebenso gehört `COPYING.picolibc` aus dem Zephyr SDK
(`gnu/arm-zephyr-eabi/share/licenses/picolibc/`) zu jedem veröffentlichten Abbild. Beides legt
`scripts/release-package.sh` mit dem Abbild, `lizenzen.txt` und Prüfsummen in ein ZIP-Archiv.

Geprüft mit `west spdx` (SPDX-Stückliste aller Quelldateien, die ins Abbild gelinkt werden): Bis auf
die vier Zephyr-Dateien oben steht alles unter den Lizenzen der Tabelle. Nicht erfasst werden die
vorgebauten Bibliotheken des SDK (picolibc, libgcc) und die Weboberfläche; sie stehen von Hand in
der Liste.
