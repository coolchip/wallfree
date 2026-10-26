#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Zeitleiste eines Firmware-Updates auf der Steuerung (SOLL-BOOT-30..33) mitschreiben.

Fragt alle 0,5 s GET /system/info ohne Anmeldung ab und protokolliert jeden Wechsel, wer
antwortet: "wallfree" (JSON, auch 401), "bootloader" (HTML) oder "weg"; dazu laufend das
Log der Telnet-Shell, solange Wallfree laeuft (Port 23, anders mit WALLFREE_TELNET_PORT, in der
Simulation 2323). Beenden mit Strg+C.
    python3 -I scripts/update-watch.py 192.168.1.50[:port] [datei.log]
"""
import os
import re
import socket
import sys
import threading
import time
import urllib.error
import urllib.request

T0 = time.monotonic()
lock = threading.Lock()
out = None


def log(msg):
    line = f"{time.strftime('%H:%M:%S')} +{time.monotonic() - T0:7.1f}s  {msg}"
    with lock:
        print(line, flush=True)
        if out:
            out.write(line + "\n")
            out.flush()


def probe(host):
    try:
        with urllib.request.urlopen(f"http://{host}/system/info", timeout=0.8) as r:
            body = r.read(200)
    except urllib.error.HTTPError as e:
        body = e.read(200)
    except OSError:
        return "weg"
    return "wallfree" if body.lstrip().startswith(b"{") else "bootloader" if body else "weg"


def telnet(host):
    """Log der Shell mitschreiben; nach Abbruch erneut verbinden."""
    while True:
        try:
            s = socket.create_connection(
                (host.split(":")[0], int(os.environ.get("WALLFREE_TELNET_PORT", "23"))), timeout=1)
        except OSError:
            time.sleep(1)
            continue
        log("telnet: verbunden")
        s.settimeout(None)
        buf = b""
        try:
            while True:
                d = s.recv(4096)
                if not d:
                    break
                buf += d
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    t = re.sub(r"\x1b\[[0-9;]*[A-Za-z]", "", line.decode("latin1")).strip()
                    if "<" in t and ">" in t:
                        log("log: " + t)
        except OSError:
            pass
        log("telnet: getrennt")
        s.close()
        time.sleep(1)


def main():
    global out
    host = sys.argv[1]
    if len(sys.argv) > 2:
        out = open(sys.argv[2], "a")
    threading.Thread(target=telnet, args=(host,), daemon=True).start()
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
