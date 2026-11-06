# SPDX-License-Identifier: Apache-2.0
#
# Abstandshalter fuer die Typ-2-Ladedose Phoenix Contact EV-T2M3SO12-4P-B-SET (1164417).
#
# Erzeugt vier Teile fuer den 3D-Druck:
#   Rohr            - wird mit dem originalen Lochbild (4x M5 auf 60 mm) aussen auf das
#                     Gehaeuse geschraubt, hinten Kragen mit Durchbruch fuer die Leitungen.
#   Platte          - neue "Gehaeusewand" vorn, traegt Ladedose (hinten) und Schutzdeckel
#                     (vorn), wird mit 4x M5 auf das Rohr geschraubt (Gewindeeinsaetze).
#   Dichtung-hinten - TPU, zwischen Kragen und Gehaeusewand der Wallbox.
#   Dichtung-vorn   - TPU, zwischen Rohr und Platte.
#
# Aufruf:  freecadcmd abstandshalter.py
# Ergebnis im Verzeichnis dieser Datei: abstandshalter.FCStd, *.step, *.stl
#
# Koordinaten: X nach rechts, Y nach oben, Z nach aussen (Z = 0 Gehaeusewand der Wallbox).
# Alle Masse in mm. Die Masse der Ladedose stammen aus deren Packungsbeilage
# (Wanddurchbruch und Lochbild), siehe README.md.

import math
import os

import FreeCAD as App
import MeshPart
import Part

# ---------------------------------------------------------------- Parameter
VERSATZ = 80.0           # Buchse wandert um dieses Mass nach aussen
                         # (Dichtung hinten + Rohr + Dichtung vorn + Platte)
AUSSEN = 116.0           # Aussenmass quadratisch
ECK_R_AUSSEN = 8.0       # Eckradius aussen
INNEN = 88.0             # Innenraum quadratisch (Dosenflansch 75 mm + Luft)
ECK_R_INNEN = 4.0
PLATTE_T = 5.0           # Dicke der vorderen Platte (zulaessig laut Hersteller bis 31 mm)
KRAGEN_T = 6.0           # Dicke des hinteren Kragens
DICHTUNG_T = 2.0         # Dicke der beiden TPU-Dichtungen

# Wanddurchbruch der Ladedose (Packungsbeilage, Bild 4)
DURCHBRUCH_D = 65.0      # Kreis
DURCHBRUCH_OBEN = 25.32  # Abstand Mitte -> gerade Oberkante
DURCHBRUCH_R = 16.75     # Rundung zwischen Oberkante und Kreis
LOCH_TEILUNG = 60.0      # Lochbild quadratisch
LOCH_D_PLATTE = 7.0      # Bohrung in der Platte (wie Gehaeusewand)
LOCH_D_KRAGEN = 5.5      # Durchgang M5 im Kragen

# Verbindung Platte <-> Rohr
VERB_TEILUNG = 104.0     # Lochbild quadratisch, ausserhalb des Schutzdeckels (85 mm breit)
VERB_D_PLATTE = 5.5      # Durchgang M5
EINSATZ_D = 6.4          # Bohrung fuer M5-Gewindeeinsatz (z. B. 9,5 mm lang)
EINSATZ_TIEFE = 12.0

# Entwaesserung unten im Rohr, kurz hinter der Platte (0 = keine)
ABLAUF_D = 5.0
ABLAUF_ABSTAND = 8.0     # von der Vorderkante des Rohrs

ROHR_L = VERSATZ - PLATTE_T - 2 * DICHTUNG_T
PLATTE_Z = ROHR_L + DICHTUNG_T   # Rueckseite der Platte


# ---------------------------------------------------------------- Hilfen
def rundes_quadrat(seite, r, hoehe, z0=0.0):
    """Quader seite x seite x hoehe, senkrechte Kanten mit Radius r, mittig in XY."""
    box = Part.makeBox(seite, seite, hoehe, App.Vector(-seite / 2, -seite / 2, z0))
    kanten = [k for k in box.Edges
              if abs(k.Vertexes[0].Point.z - k.Vertexes[1].Point.z) > 1e-6]
    return box.makeFillet(r, kanten) if r > 0 else box


def durchbruch(hoehe, z0=0.0):
    """Wanddurchbruch: Kreis, oben gerade abgeschnitten, Ecken gerundet."""
    R = DURCHBRUCH_D / 2
    r = DURCHBRUCH_R
    yo = DURCHBRUCH_OBEN
    cy = yo - r                                  # Mittelpunkt der Rundung, Y
    cx = math.sqrt((R - r) ** 2 - cy ** 2)       # Mittelpunkt der Rundung, X
    f = R / (R - r)
    tx, ty = cx * f, cy * f                      # Uebergang Rundung -> Kreis

    def p(x, y):
        return App.Vector(x, y, z0)

    def mitte_rundung(sx):
        a1 = math.atan2(yo - cy, 0.0)
        a2 = math.atan2(ty - cy, tx - cx)
        a = (a1 + a2) / 2
        return p(sx * (cx + r * math.cos(a)), cy + r * math.sin(a))

    kanten = [
        Part.LineSegment(p(-cx, yo), p(cx, yo)).toShape(),
        Part.Arc(p(cx, yo), mitte_rundung(1), p(tx, ty)).toShape(),
        Part.Arc(p(tx, ty), p(0, -R), p(-tx, ty)).toShape(),
        Part.Arc(p(-tx, ty), mitte_rundung(-1), p(-cx, yo)).toShape(),
    ]
    flaeche = Part.Face(Part.Wire(kanten))
    return flaeche.extrude(App.Vector(0, 0, hoehe))


def lochbild(teilung, d, hoehe, z0=0.0):
    s = teilung / 2
    loecher = [Part.makeCylinder(d / 2, hoehe, App.Vector(x, y, z0))
               for x in (-s, s) for y in (-s, s)]
    return loecher[0].fuse(loecher[1:])


# ---------------------------------------------------------------- Rohr
def mache_rohr():
    eps = 0.5
    rohr = rundes_quadrat(AUSSEN, ECK_R_AUSSEN, ROHR_L)
    innen = rundes_quadrat(INNEN, ECK_R_INNEN, ROHR_L - KRAGEN_T + eps, KRAGEN_T)
    rohr = rohr.cut(innen)
    rohr = rohr.cut(durchbruch(KRAGEN_T + 2 * eps, -eps))
    rohr = rohr.cut(lochbild(LOCH_TEILUNG, LOCH_D_KRAGEN, KRAGEN_T + 2 * eps, -eps))
    rohr = rohr.cut(lochbild(VERB_TEILUNG, EINSATZ_D, EINSATZ_TIEFE + eps,
                             ROHR_L - EINSATZ_TIEFE))
    if ABLAUF_D > 0:
        z = ROHR_L - ABLAUF_ABSTAND
        ablauf = Part.makeCylinder(ABLAUF_D / 2, (AUSSEN - INNEN) / 2 + 2 * eps,
                                   App.Vector(0, -AUSSEN / 2 - eps, z),
                                   App.Vector(0, 1, 0))
        rohr = rohr.cut(ablauf)
    return rohr


# ---------------------------------------------------------------- Platte
def mache_platte():
    eps = 0.5
    z0 = PLATTE_Z
    platte = rundes_quadrat(AUSSEN, ECK_R_AUSSEN, PLATTE_T, z0)
    platte = platte.cut(durchbruch(PLATTE_T + 2 * eps, z0 - eps))
    platte = platte.cut(lochbild(LOCH_TEILUNG, LOCH_D_PLATTE, PLATTE_T + 2 * eps, z0 - eps))
    platte = platte.cut(lochbild(VERB_TEILUNG, VERB_D_PLATTE, PLATTE_T + 2 * eps, z0 - eps))
    return platte


# ---------------------------------------------------------------- Dichtungen
def mache_dichtung_hinten():
    """Flachdichtung zwischen Kragen und Gehaeusewand, Z = -DICHTUNG_T .. 0."""
    eps = 0.5
    z0 = -DICHTUNG_T
    d = rundes_quadrat(AUSSEN, ECK_R_AUSSEN, DICHTUNG_T, z0)
    d = d.cut(durchbruch(DICHTUNG_T + 2 * eps, z0 - eps))
    d = d.cut(lochbild(LOCH_TEILUNG, LOCH_D_KRAGEN, DICHTUNG_T + 2 * eps, z0 - eps))
    return d


def mache_dichtung_vorn():
    """Rahmendichtung zwischen Rohr und Platte."""
    eps = 0.5
    z0 = ROHR_L
    d = rundes_quadrat(AUSSEN, ECK_R_AUSSEN, DICHTUNG_T, z0)
    d = d.cut(rundes_quadrat(INNEN, ECK_R_INNEN, DICHTUNG_T + 2 * eps, z0 - eps))
    d = d.cut(lochbild(VERB_TEILUNG, VERB_D_PLATTE, DICHTUNG_T + 2 * eps, z0 - eps))
    return d


# ---------------------------------------------------------------- Ausgabe
def main():
    hier = os.path.dirname(os.path.abspath(__file__))
    doc = App.newDocument("Abstandshalter")
    teile = {"Rohr": mache_rohr(), "Platte": mache_platte(),
             "Dichtung-hinten": mache_dichtung_hinten(),
             "Dichtung-vorn": mache_dichtung_vorn()}
    objekte = []
    for name, form in teile.items():
        if not form.isValid():
            raise RuntimeError(name + ": ungueltige Geometrie")
        obj = doc.addObject("Part::Feature", name.replace("-", "_"))
        obj.Shape = form
        objekte.append(obj)
        form.exportStep(os.path.join(hier, "abstandshalter-%s.step" % name.lower()))
        # STL in Druckorientierung: Rohr steht auf dem Kragen, Platte liegt flach
        druck = form.copy()
        druck.translate(App.Vector(0, 0, -druck.BoundBox.ZMin))
        netz = MeshPart.meshFromShape(Shape=druck, LinearDeflection=0.05,
                                      AngularDeflection=0.2)
        netz.write(os.path.join(hier, "abstandshalter-%s.stl" % name.lower()))
        bb = form.BoundBox
        print("%-15s %.1f x %.1f x %.1f mm, Volumen %.0f cm3" %
              (name, bb.XLength, bb.YLength, bb.ZLength, form.Volume / 1000))
    doc.recompute()
    doc.saveAs(os.path.join(hier, "abstandshalter.FCStd"))


main()
