# Abstandshalter für die Typ-2-Ladedose

Versetzt die Ladedose Phoenix Contact **EV-T2M3SO12-4P-B-SET** (Art.-Nr. 1164417, CHARX connect,
Rückwandmontage) um 100 mm nach außen, damit im Gehäuse der Wallbox Platz frei wird. Gedacht für
den 3D-Druck.

## Teile

| Datei | Teil | Maße | Druckorientierung |
| --- | --- | --- | --- |
| `abstandshalter-rohr.stl` / `.step` | Rohr mit hinterem Kragen | 116 × 116 × 95 mm | steht auf dem Kragen, ohne Stützen |
| `abstandshalter-platte.stl` / `.step` | vordere Platte (neue Gehäusewand) | 116 × 116 × 5 mm | flach |
| `abstandshalter.FCStd` | beide Teile in FreeCAD | | |
| `abstandshalter.py` | Erzeugung, alle Maße als Parameter oben | | |

Neu erzeugen nach Änderung der Parameter:

```bash
freecadcmd abstandshalter.py
```

## Aufbau

Ein einteiliger Würfel lässt sich nicht montieren: Der Flansch der Ladedose (75 mm) passt
nicht durch den Wanddurchbruch, und ihre Schrauben liegen genau hinter den Schrauben, mit
denen der Würfel an der Wallbox hält. Deshalb zwei Teile:

1. **Rohr an die Wallbox:** Kragen außen auf die Gehäusewand (originaler Durchbruch, 4 × M5 auf
   □60 mm). Schrauben von innen im Rohr durch Kragen und Gehäusewand, Mutter mit Scheibe innen
   in der Wallbox. Zwischen Kragen und Gehäusewand eine Flachdichtung.
2. **Ladedose an die Platte**, wie sonst an der Gehäusewand: Dose von hinten in den Durchbruch,
   Dichtrahmen und Schutzdeckel vorn, verschrauben von hinten mit 1,4 Nm. Schraubenlänge laut
   Hersteller 15 bis 17,5 mm plus Wanddicke, bei 5 mm Platte also **M5 × 20** (ISO 7380 mit
   Scheibe).
3. Leitungen durch das Rohr in die Wallbox führen.
4. **Platte auf das Rohr:** 4 × M5 auf □104 mm (außerhalb des Schutzdeckels, 85 mm breit), in
   Gewindeeinsätze M5 (Bohrung 6,4 mm, bis 12 mm tief) vorn im Rohr. Zwischen Platte und Rohr
   eine Flachdichtung.

Der Durchbruch hat die gerade Kante oben, der Verriegelungsaktor sitzt oben mittig. Die Dose
darf laut Hersteller nicht seitlich, kopfüber oder nach oben gedreht eingebaut werden.

## Maße der Ladedose (Packungsbeilage, Bild 4)

- Wanddurchbruch: Kreis Ø 65 mm, oben gerade bei 25,32 mm über der Mitte, Übergänge R 16,75 mm
- Lochbild 4 × Ø 7 mm auf □60 mm
- Flansch □75 mm, Tiefe hinter der Wand 54,4 mm (mit Aktor 73,4 mm); Innenraum des Rohrs
  88 × 88 × 89 mm
- Wanddicke höchstens 31 mm

## Hinweise

- **Werkstoff:** außen UV- und witterungsfest, z. B. ASA; PETG nur geschützt. Das Rohr wird Teil
  der Umhüllung spannungsführender Teile; ein schwer entflammbarer Werkstoff (UL 94 V-0) ist
  vorzuziehen.
- **Füllung:** Die Wände sind 14 mm dick (Platz für die Gewindeeinsätze); mit 4 Perimetern und
  20 bis 30 % Füllung bleibt das Teil leicht und steif.
- **Ablauf:** Unten im Rohr, 8 mm hinter der Platte, ist eine Bohrung Ø 5 mm für Wasser, das
  durch die Dose nach hinten abläuft (`ABLAUF_D = 0` schaltet sie ab).
- **Leitungslängen:** Die Einzeladern zur Dose müssen um 100 mm länger sein als bisher.
- Vor dem Druck die Lage der Gehäusebohrungen und den Durchbruch an der eigenen Wallbox
  nachmessen.
