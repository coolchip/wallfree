#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Wiki-Seiten aus docs/ erzeugen:  scripts/wiki-export.py <wiki-verzeichnis>

Quelle bleibt docs/ (fuer die Startseite README.md); das Wiki auf GitHub ist eine Kopie fuer
Anwender. Das Skript schreibt die Seiten nach PAGES in das Verzeichnis (ein Klon von
<repo>.wiki.git), kopiert die eingebundenen Bilder und stellt Links um: Links zwischen den
Seiten zeigen auf die Wiki-Seite, Links auf andere Dateien des Repositorys auf GitHub, Links ins
nicht veroeffentlichte Analyse-Repository werden zu reinem Text. Andere Dateien im Verzeichnis
bleiben unberuehrt; geloeschte Seiten von Hand entfernen.
"""
import os
import re
import shutil
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
GITHUB = 'https://github.com/coolchip/wallfree'
BRANCH = 'main'

# Quelle (relativ zum Repository) -> Name der Wiki-Seite
PAGES = {
    'README.md': 'Home',
    'docs/umruesten.md': 'Umrüsten',
    'docs/hardware.md': 'Hardware',
    'docs/anleitung/weboberflaeche.md': 'Bedienungsanleitung',
    'docs/unterschiede-originalfirmware.md': 'Unterschiede-zur-Originalfirmware',
    'docs/vergleich-weboberflaeche.md': 'Weboberfläche-vorher-und-nachher',
    'docs/technik-und-lizenzen.md': 'Technik-und-Lizenzen',
    'docs/pruefprotokoll.md': 'Prüfprotokoll',
    'docs/pruefstecker.md': 'Prüfstecker',
    'docs/simulation-bedienung.md': 'Simulation',
}

# Abschnitte des README, die auf der Wiki-Startseite fehlen (fuer Entwickler)
DEV_SECTIONS = ('Inhalt', 'Bauen', 'Testen (PC)', 'Herkunft der Befunde')

SIDEBAR = """**Wallfree**

- [Startseite](Home)
- [Umrüsten auf Wallfree und zurück](Umrüsten)
- [Hardware der Steuerung](Hardware)
- [Bedienungsanleitung](Bedienungsanleitung)
- [Unterschiede zur Originalfirmware](Unterschiede-zur-Originalfirmware)
- [Weboberfläche vorher und nachher](Weboberfläche-vorher-und-nachher)

**Prüfung und Entwicklung**

- [Prüfprotokoll](Prüfprotokoll)
- [Prüfstecker](Prüfstecker)
- [Simulation bedienen](Simulation)
- [Technik und Lizenzen](Technik-und-Lizenzen)
- [Soll-Spezifikation]({gh}/blob/{br}/docs/soll-spezifikation.md)
- [Architektur]({gh}/blob/{br}/docs/architektur.md)
- [Implementierung]({gh}/blob/{br}/docs/implementierung.md)
- [Test und Simulation]({gh}/blob/{br}/docs/test-und-simulation.md)
""".format(gh=GITHUB, br=BRANCH)

FOOTER = ('Diese Seite wird aus dem Repository erzeugt ({src}); Änderungen bitte dort, '
          'nicht im Wiki.')

LINK = re.compile(r'(!?)\[((?:[^\[\]]|\[[^\]]*\])*)\]\(([^)\s]+)\)')


HTML_SRC = re.compile(r'\b(src|srcset)="([^":]+)"')


def convert_src(src, m, images):
    full = os.path.normpath(os.path.join(os.path.dirname(src), m.group(2)))
    images.add(full)
    # GitHub stellt in HTML nur src um, nicht srcset; deshalb beide mit voller Adresse
    return '%s="%s/wiki/%s"' % (m.group(1), GITHUB, full)


def convert_link(src, m, images):
    bang, text, target = m.group(1), m.group(2), m.group(3)
    if re.match(r'^[a-z]+:', target) or target.startswith('#'):
        return m.group(0)
    path, _, anchor = target.partition('#')
    full = os.path.normpath(os.path.join(os.path.dirname(src), path))
    if full.startswith('..'):
        # ausserhalb des Repositorys (nicht veroeffentlichtes Analyse-Repository)
        return text
    if bang:
        images.add(full)
        return '%s[%s](%s)' % (bang, text, full)
    frag = '#' + anchor if anchor else ''
    if full in PAGES:
        return '[%s](%s%s)' % (text, PAGES[full], frag)
    kind = 'tree' if os.path.isdir(os.path.join(REPO, full)) else 'blob'
    return '[%s](%s/%s/%s/%s%s)' % (text, GITHUB, kind, BRANCH, full, frag)


def home(text):
    # Startseite: README ohne die Abschnitte fuer Entwickler, danach die Seitenliste
    keep = []
    for part in re.split(r'(?m)^(?=## )', text):
        head = part.splitlines()[0] if part else ''
        if head.startswith('## ') and head[3:] in DEV_SECTIONS:
            continue
        keep.append(part)
    return ''.join(keep)


# Seitenliste am Ende der Startseite; Links schon im Wiki-Format, nicht mehr umstellen
HOME_PAGES = ('\n## Seiten\n\n' + SIDEBAR.split('\n', 2)[2].split('\n\n**')[0] + '\n\n'
              'Für Entwickler: Bauen, Testen und Aufbau des Quelltexts stehen im '
              '[README des Repositorys](%s#bauen).\n' % GITHUB)


def main():
    if len(sys.argv) != 2 or not os.path.isdir(sys.argv[1]):
        sys.exit(__doc__)
    out = sys.argv[1]
    images = set()
    for src, page in PAGES.items():
        text = open(os.path.join(REPO, src), encoding='utf-8').read()
        if src == 'README.md':
            text = LINK.sub(lambda m: convert_link(src, m, images), home(text))
            text = HTML_SRC.sub(lambda m: convert_src(src, m, images), text) + HOME_PAGES
        else:
            # Ueberschrift der Datei entfaellt, das Wiki zeigt den Seitennamen
            text = re.sub(r'\A# [^\n]*\n\n', '', text)
            text = LINK.sub(lambda m: convert_link(src, m, images), text)
        text = text.rstrip('\n') + '\n'
        with open(os.path.join(out, page + '.md'), 'w', encoding='utf-8') as f:
            f.write(text)
    for img in sorted(images):
        dst = os.path.join(out, img)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(os.path.join(REPO, img), dst)
    with open(os.path.join(out, '_Sidebar.md'), 'w', encoding='utf-8') as f:
        f.write(SIDEBAR)
    with open(os.path.join(out, '_Footer.md'), 'w', encoding='utf-8') as f:
        f.write(FOOTER.format(src='[docs/](%s/tree/%s/docs)' % (GITHUB, BRANCH)) + '\n')
    print('%d Seiten, %d Bilder nach %s' % (len(PAGES), len(images), out))


if __name__ == '__main__':
    main()
