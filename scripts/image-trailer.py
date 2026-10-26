#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Anhang mit Laenge und CRC32 an ein Wallfree-Abbild (SOLL-BOOT-35).

Format (16 Byte, little-endian) direkt hinter dem Abbild (Laenge = Linker-Symbol _flash_used,
durch 4 teilbar): Kennung 0x474D4957 ("WIMG"), Laenge, CRC32 (IEEE, wie zlib.crc32) ueber die
Laenge, invertierte Kennung. Die Firmware rechnet beim Start nach (hw_image_check).
    image-trailer.py zephyr.bin        Anhang setzen (im Platz; ersetzt einen vorhandenen)
    image-trailer.py --check datei     nur pruefen, Ausgabe Laenge und CRC
    image-trailer.py --if-exists datei wie oben, aber ohne Datei nichts tun (Build fuer native_sim)
Bricht ab, wenn das Ergebnis groesser ist als der Bootloader loescht (Sektor 4..6, 320 KB).
"""
import os
import struct
import sys
import zlib

MAGIC = 0x474D4957
MAX_IMAGE = 0x50000
TRAILER = 16


def parse(data):
    """@return (laenge, crc) eines gueltigen Anhangs oder None"""
    if len(data) < TRAILER:
        return None
    magic, length, crc, inv = struct.unpack_from("<IIII", data, len(data) - TRAILER)
    if magic != MAGIC or inv != (~MAGIC & 0xFFFFFFFF) or length != len(data) - TRAILER:
        return None
    if zlib.crc32(data[:length]) != crc:
        return None
    return length, crc


def main():
    check = sys.argv[1] == "--check"
    path = sys.argv[-1]
    if sys.argv[1] == "--if-exists" and not os.path.exists(path):
        return
    with open(path, "rb") as f:
        data = f.read()
    found = parse(data)
    if check:
        if not found:
            sys.exit(f"{path}: kein gueltiger Anhang")
        print(f"{path}: Laenge {found[0]}, CRC32 {found[1]:08x}")
        return
    if found:
        data = data[:found[0]]
    if len(data) % 4:
        sys.exit(f"{path}: Laenge {len(data)} nicht durch 4 teilbar")
    out = data + struct.pack("<IIII", MAGIC, len(data), zlib.crc32(data), ~MAGIC & 0xFFFFFFFF)
    if len(out) > MAX_IMAGE:
        sys.exit(f"{path}: {len(out)} Byte, hoechstens {MAX_IMAGE} (Sektor 4..6)")
    with open(path, "wb") as f:
        f.write(out)
    print(f"{path}: Anhang gesetzt, Laenge {len(data)}, CRC32 {zlib.crc32(data):08x}")


if __name__ == "__main__":
    main()
