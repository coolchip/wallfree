#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Wiki-Seiten aus docs/ erzeugen:  scripts/wiki-export.py <wiki-verzeichnis>

Quelle bleibt docs/ (fuer die Startseite docs/wiki-startseite.md und README.md); das Wiki auf GitHub ist eine Kopie fuer
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

# Kopf der Startseite (Logo, Einleitung, Hinweise); README.md liefert den Rest
HOME_HEAD = 'docs/wiki-startseite.md'

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
    return '%s="%s"' % (m.group(1), full)


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
    # Startseite: Kopf aus docs/wiki-startseite.md, dann aus README Status, Funktionen und Lizenz
    # (ohne Titel, Wahlspruch und Einleitung, die der Kopf selbst bringt), dann die Seitenliste
    text = text[text.index('**Status:**'):]
    keep = []
    for part in re.split(r'(?m)^(?=## )', text):
        head = part.splitlines()[0] if part else ''
        if head.startswith('## ') and head[3:] not in ('Funktionen', 'Lizenz'):
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
            # Links des README gelten relativ zum Repository, die des Kopfs relativ zu docs/
            head = open(os.path.join(REPO, HOME_HEAD), encoding='utf-8').read()
            head = re.sub(r'\A<!--[^\n]*-->\n\n', '', head)
            head = LINK.sub(lambda m: convert_link(HOME_HEAD, m, images), head)
            head = HTML_SRC.sub(lambda m: convert_src(HOME_HEAD, m, images), head)
            text = LINK.sub(lambda m: convert_link(src, m, images), home(text)) + HOME_PAGES
            text = head.replace('<!-- README -->\n', text)
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
