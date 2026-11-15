# Abstandshalter für die Typ-2-Ladedose

Versetzt die Ladedose Phoenix Contact **EV-T2M3SO12-4P-B-SET** (Art.-Nr. 1164417, CHARX connect,
Rückwandmontage) um 80 mm nach außen, damit im Gehäuse der Wallbox Platz frei wird. Gedacht für
den 3D-Druck.

## Druckteile

| Datei (`.stl` / `.step`) | Teil | Maße | Werkstoff | Druckorientierung |
| --- | --- | --- | --- | --- |
| `abstandshalter-rohr` | Rohr mit hinterem Kragen | 93 × 126 × 71,2 mm | ASA | steht auf dem Kragen, ohne Stützen |
| `abstandshalter-platte` | vordere Platte (neue Gehäusewand) | 93 × 126 × 4,8 mm | ASA | flach |
| `abstandshalter-dichtung-hinten` | Dichtung Kragen/Wallbox | 93 × 126 × 2 mm | TPU | flach |
| `abstandshalter-dichtung-vorn` | Dichtung Rohr/Platte | 93 × 126 × 2 mm | TPU | flach |

Dazu `abstandshalter.FCStd` (alle Teile in FreeCAD, in Einbaulage) und `abstandshalter.py`
(Erzeugung, alle Maße als Parameter oben). Neu erzeugen nach Änderung der Parameter:

```bash
freecadcmd abstandshalter.py
```

Der Versatz von 80 mm setzt sich zusammen aus Dichtung hinten (2 mm), Rohr (71,2 mm), Dichtung
vorn (2 mm) und Platte (4,8 mm, so dick wie die originale Gehäusewand).

## Querschnitt

Gemessen von der Mitte der Ladedose:

| | innen | Wand | außen |
| --- | --- | --- | --- |
| links, rechts | 39 mm | 7,5 mm | 46,5 mm |
| oben | 58 mm | 12 mm | 70 mm |
| unten | 44 mm | 12 mm | 56 mm |

- **Seiten:** Die Wand liegt direkt am Rahmen der Dose (ca. 77 mm, 0,5 mm Luft je Seite).
  Neben der Dose sind in der Wallbox nur 8 mm bis zum nächsten Bauteil frei; Rahmen bis
  Außenkante sind 8 mm.
- **Oben:** Über der geraden Kante sitzt der Verriegelungsaktor, er reicht bis 53 mm über die
  Mitte; 5 mm Luft darüber.
- **Oben und unten** tragen die 12 mm dicken Wände die Gewindeeinsätze für die Platte; die
  Seitenwände sind dafür zu dünn.

## Teileliste

Alle Schrauben, Muttern und Scheiben in Edelstahl A2 (außen A4).

| Anz. | Teil | Norm | Verwendung |
| --- | --- | --- | --- |
| 1 | Rohr, gedruckt (ASA) | | |
| 1 | Platte, gedruckt (ASA) | | |
| 1 | Dichtung hinten, gedruckt (TPU) | | zwischen Kragen und Gehäusewand |
| 1 | Dichtung vorn, gedruckt (TPU) | | zwischen Rohr und Platte |
| **Rohr an die Wallbox** | | | |
| 4 | Flachkopfschraube M5 × (16 mm + Wanddicke der Wallbox), z. B. M5 × 20 bei 3 mm Wand | ISO 7380 | von innen im Rohr durch Kragen, Dichtung und Gehäusewand |
| 8 | Unterlegscheibe M5 (5,3 × 10 × 1) | ISO 7089 | unter Kopf und Mutter |
| 4 | Sicherungsmutter M5 mit Klemmteil | ISO 10511 | innen in der Wallbox |
| **Ladedose an die Platte** | | | |
| 1 | Ladedose EV-T2M3SO12-4P-B-SET mit Schutzdeckel und Dichtrahmen (vorhanden) | | |
| 4 | Flachkopfschraube M5 × 20 | ISO 7380 | von hinten durch Dosenflansch und Platte, hält Dose und Schutzdeckel, 1,4 Nm |
| 4 | Unterlegscheibe M5 | ISO 7089 | unter dem Kopf |
| **Platte an das Rohr** | | | |
| 4 | Gewindeeinsatz M5 zum Einschmelzen, Außen-Ø ca. 7 mm, Länge 9,5 mm (Bohrung 6,4 mm) | | vorn in der oberen und unteren Wand des Rohrs |
| 4 | Zylinderschraube M5 × 16 | ISO 4762 | durch Platte und Dichtung in die Gewindeeinsätze |
| 4 | Dichtscheibe M5 (Edelstahl mit EPDM) oder Unterlegscheibe M5 | | unter dem Kopf |

Schraubenlängen:

- Ladedose: laut Hersteller 15 bis 17,5 mm plus Wanddicke; bei 4,8 mm Platte also 19,8 bis
  22,3 mm.
- Platte: 4,8 mm Platte + 2 mm Dichtung + 9 mm Einschraubtiefe = 16 mm (Bohrung 12 mm tief).
- Kragen: 6 mm Kragen + 2 mm Dichtung + 2 Scheiben + Mutter + Überstand = 16 mm plus Wanddicke
  der Wallbox.

## Aufbau

Ein einteiliger Würfel lässt sich nicht montieren: Der Flansch der Ladedose (75 mm) passt
nicht durch den Wanddurchbruch, und ihre Schrauben liegen genau hinter den Schrauben, mit
denen der Würfel an der Wallbox hält. Deshalb Rohr und Platte getrennt:

1. Gewindeeinsätze mit dem Lötkolben vorn in die vier Bohrungen oben und unten im Rohr
   einschmelzen.
2. **Rohr an die Wallbox:** Dichtung hinten und Rohr außen auf die Gehäusewand (originaler
   Durchbruch, 4 × M5 im Quadrat 60 × 60 mm). Schrauben von innen im Rohr, Muttern innen in
   der Wallbox.
3. **Ladedose an die Platte**, wie sonst an der Gehäusewand: Dose von hinten in den Durchbruch,
   Dichtrahmen und Schutzdeckel vorn, von hinten verschrauben mit 1,4 Nm.
4. Leitungen durch das Rohr in die Wallbox führen.
5. **Platte auf das Rohr:** Dichtung vorn dazwischen, 4 × M5 im Rechteck 68 mm breit, oben
   64 mm und unten 50 mm von der Mitte (über und unter dem Schutzdeckel), gleichmäßig über
   Kreuz anziehen, bis die Dichtung anliegt; nicht so fest, dass sich die Einsätze lösen.

Der Durchbruch hat die gerade Kante oben, der Verriegelungsaktor sitzt oben mittig. Die Dose
darf laut Hersteller nicht seitlich, kopfüber oder nach oben gedreht eingebaut werden.

## Maße der Ladedose (Packungsbeilage, Bild 3 und 4)

- Wanddurchbruch: Kreis Ø 65 mm, oben gerade bei 25,32 mm über der Mitte, Übergänge R 16,75 mm
- Lochbild 4 × Ø 7 mm im Quadrat 60 × 60 mm
- Flansch 75 × 75 mm, 5,3 mm dick; Gehäuse dahinter 54,4 mm, vorn ragt die Dose 13,8 mm vor
  den Flansch. Hinter der Platte braucht die Dose also 59,7 mm; das Rohr ist bis zum Kragen
  65,2 mm tief.
- Wanddicke höchstens 31 mm

## Hinweise

- **Werkstoff:** außen UV- und witterungsfest, z. B. ASA; PETG nur geschützt. Das Rohr wird Teil
  der Umhüllung spannungsführender Teile; ein schwer entflammbarer Werkstoff (UL 94 V-0) ist
  vorzuziehen.
- **Dichtungen:** TPU möglichst weich (85A bis 95A), 100 % Füllung, damit kein Wasser durch das
  Füllmuster zieht. Die hintere Dichtung braucht eine ebene Auflage von 93 × 126 mm auf der
  Wallbox; vorher prüfen.
- **Füllung Rohr:** Seitenwände 7,5 mm, oben und unten 12 mm (Platz für die Gewindeeinsätze);
  mit 4 Perimetern und 20 bis 30 % Füllung bleibt das Teil leicht und steif.
- **Ablauf:** Unten im Rohr, 8 mm hinter der Platte, ist eine Bohrung Ø 5 mm für Wasser, das
  durch die Dose nach hinten abläuft (`ABLAUF_D = 0` schaltet sie ab).
- **Leitungslängen:** Die Einzeladern zur Dose müssen um 80 mm länger sein als bisher.
- Vor dem Druck die Lage der Gehäusebohrungen und den Durchbruch an der eigenen Wallbox
  nachmessen.
