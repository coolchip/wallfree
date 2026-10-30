#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Zeitleiste eines Firmware-Updates auf der Steuerung (SOLL-BOOT-30..33) mitschreiben.

Ohne HTTP: Der Bootloader bricht einen Upload bei jeder anderen HTTP-Anfrage ab (wallbe2
docs/bootloader.md). Zustand alle 0,5 s per Ping, unterschieden an der TTL der Antwort:
"wallfree" (Zephyr, TTL 64), "bootloader" (lwIP, TTL 255; beides im Mitschnitt; nur ohne Router
dazwischen, wallbe2 analysis/box-t/update4.pcap) oder "weg". In der Simulation antwortet der PC
(TTL 64). Beenden mit Strg+C.
    python3 -I scripts/update-watch.py 192.168.1.50 [datei.log]
"""
import re
import subprocess
import sys
import time

T0 = time.monotonic()
out = None
NAMES = {64: "wallfree", 255: "bootloader"}


def log(msg):
    line = f"{time.strftime('%H:%M:%S')} +{time.monotonic() - T0:7.1f}s  {msg}"
    print(line, flush=True)
    if out:
        out.write(line + "\n")
        out.flush()


def probe(host):
    r = subprocess.run(["ping", "-c1", "-W1", host.split(":")[0]], capture_output=True, text=True)
    m = re.search(r"ttl=(\d+)", r.stdout)
    if r.returncode != 0 or m is None:
        return "weg"
    ttl = int(m.group(1))
    return NAMES.get(ttl, f"TTL {ttl}")


def main():
    global out
    host = sys.argv[1]
    if len(sys.argv) > 2:
        out = open(sys.argv[2], "a")
    last = None
    while True:
        st = probe(host)
        if st != last:
            log(f"antwortet: {st}")
            last = st
        time.sleep(0.5)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
