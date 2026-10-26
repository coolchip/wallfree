#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Zeitleiste eines Firmware-Updates auf der Steuerung (SOLL-BOOT-30..33) mitschreiben.

Ohne HTTP: Der Bootloader bricht einen Upload bei jeder anderen HTTP-Anfrage ab (wallbe2
docs/bootloader.md). Zustand alle 0,5 s: "wallfree" (Telnet-Shell verbunden), "erreichbar"
(antwortet auf Ping, keine Shell: Bootloader oder Start) oder "weg"; dazu laufend das Log der
Telnet-Shell, solange Wallfree laeuft (Port 23, anders mit WALLFREE_TELNET_PORT, in der
Simulation 2323). Beenden mit Strg+C.
    python3 -I scripts/update-watch.py 192.168.1.50[:port] [datei.log]
"""
import os
import re
import socket
import sys
import threading
import subprocess
import time

T0 = time.monotonic()
lock = threading.Lock()
out = None
shell_up = False


def log(msg):
    line = f"{time.strftime('%H:%M:%S')} +{time.monotonic() - T0:7.1f}s  {msg}"
    with lock:
        print(line, flush=True)
        if out:
            out.write(line + "\n")
            out.flush()


def probe(host):
    if shell_up:
        return "wallfree"
    r = subprocess.run(["ping", "-c1", "-W1", host.split(":")[0]], stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL)
    return "erreichbar" if r.returncode == 0 else "weg"


def telnet(host):
    """Log der Shell mitschreiben; nach Abbruch erneut verbinden."""
    global shell_up
    while True:
        try:
            s = socket.create_connection(
                (host.split(":")[0], int(os.environ.get("WALLFREE_TELNET_PORT", "23"))), timeout=1)
        except OSError:
            time.sleep(1)
            continue
        # harter Reset schliesst die Verbindung nicht: Keepalive erkennt das nach etwa 3 s
        s.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
        s.setsockopt(socket.IPPROTO_TCP, socket.TCP_KEEPIDLE, 1)
        s.setsockopt(socket.IPPROTO_TCP, socket.TCP_KEEPINTVL, 1)
        s.setsockopt(socket.IPPROTO_TCP, socket.TCP_KEEPCNT, 2)
        log("telnet: verbunden")
        shell_up = True
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
        shell_up = False
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
