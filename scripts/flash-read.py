#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Flash, OTP oder Optionsbytes einer Steuerung mit Wallfree auslesen (SOLL-SH-24).

Setzt den Bereich aus Aufrufen von `flashread` ueber POST /system/shell zusammen und gibt
Laenge und SHA-256 aus. Nur lesend. Abzuege mit Herstellercode (Bootloader, Originalfirmware,
Parameter) gehoeren nach wallbe2, nicht in dieses Repository.

  scripts/flash-read.py [-u benutzer:passwort | --netrc datei] adresse bereich ausgabe.bin

Bereich: boot (Sektor 0..2, Bootloader), sektor3 (Parameter der Originalfirmware),
         firmware (Sektor 4..6), alle (Sektor 0..6), otp, optionen,
         oder anfang:laenge (z. B. 0x08000000:0x400)
"""
import argparse
import base64
import hashlib
import subprocess
import sys

BEREICHE = {
    "boot": (0x08000000, 0xC000),
    "sektor3": (0x0800C000, 0x4000),
    "firmware": (0x08010000, 0x50000),
    "alle": (0x08000000, 0x60000),
    "otp": (0x1FFF7800, 0x210),
    "optionen": (0x1FFFC000, 0x10),
}
STUECK = 1024


def shell(args, zeile):
    cmd = ["curl", "-s", "--max-time", "10", "--digest", "-X", "POST", "--data-binary", zeile]
    if args.netrc:
        cmd += ["--netrc-file", args.netrc]
    elif args.user:
        cmd += ["-u", args.user]
    cmd.append(f"http://{args.adresse}/system/shell")
    r = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if r.returncode != 0:
        raise RuntimeError(f"curl {r.returncode}")
    return r.stdout


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("-u", "--user", help="benutzer:passwort")
    p.add_argument("--netrc", help="netrc-Datei mit den Zugangsdaten")
    p.add_argument("adresse")
    p.add_argument("bereich")
    p.add_argument("ausgabe")
    args = p.parse_args()
    if not args.user and not args.netrc:
        p.error("Zugangsdaten fehlen (-u oder --netrc)")

    if args.bereich in BEREICHE:
        anfang, laenge = BEREICHE[args.bereich]
    else:
        try:
            a, l = args.bereich.split(":")
            anfang, laenge = int(a, 0), int(l, 0)
        except ValueError:
            p.error(f"Bereich unbekannt: {args.bereich}")

    daten = bytearray()
    while len(daten) < laenge:
        n = min(STUECK, laenge - len(daten))
        adr = anfang + len(daten)
        for versuch in range(3):
            try:
                text = shell(args, f"flashread 0x{adr:08x} {n}")
                stueck = base64.b64decode("".join(text.split()), validate=True)
                if len(stueck) == n:
                    break
                fehler = text.strip()[:200]
            except (RuntimeError, ValueError) as e:
                fehler = str(e)
        else:
            sys.exit(f"Abbruch bei 0x{adr:08x}: {fehler}")
        daten += stueck
        print(f"\r0x{adr + n:08x} ({len(daten) * 100 // laenge} %)", end="", file=sys.stderr)
    print(file=sys.stderr)

    with open(args.ausgabe, "wb") as f:
        f.write(daten)
    print(f"{args.ausgabe}: 0x{anfang:08x}, {len(daten)} Byte, SHA-256 {hashlib.sha256(daten).hexdigest()}")


if __name__ == "__main__":
    main()
