#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Ersatz fuer den Update-Betrieb des Original-Bootloaders in der Simulation.

Die Simulation startet dieses Skript bei der Uebergabe (SOLL-BOOT-30) statt sich selbst neu
(Umgebungsvariable WALLFREE_SIM_BOOTLOADER = Pfad dieses Skripts). Es uebernimmt den HTTP-Port
und bildet das Verhalten nach, das wallbe2/docs/bootloader.md (Abschnitt Update-Betrieb)
beschreibt:

- jedes GET liefert eine Upload-Seite (eigener Text, kein Inhalt des Originals);
- POST /upload.cgi: Content-Length noetig, ein nicht leerer Dateiname, die Dateidaten beginnen
  16 Byte nach "octet-stream" und enden vor dem letzten "\\r\\n--";
- hoechstens 0x60000 Byte (Sektor 4..6); Erfolg nur mit gueltigem Start-SP;
- 120 s Warten auf den Beginn des Uploads, 30 s fuer die Uebertragung;
- danach Neustart der Simulation mit dem Ergebnis in BKP12 (Modus << 8 | Erfolg).

Vereinfacht: kein zweiter oder dritter Versuch (BKP11), das Abbild wird nicht ausgefuehrt.
WALLFREE_SIM_BOOTLOADER_OUT: Datei fuer die empfangenen Daten (Pruefung in den Tests).
WALLFREE_SIM_BOOTLOADER_WAIT: Wartezeit in s statt 120 (Tests).
"""
import argparse
import os
import socket
import sys
import time

MODE_WEB = 2
MAX_IMAGE = 0x60000
UPLOAD_S = 30

PAGE = (b"<!DOCTYPE html><html><head><title>Bootloader (Simulation)</title></head><body>"
        b"<h1>Bootloader (Simulation)</h1>"
        b"<form action=\"/upload.cgi\" enctype=\"multipart/form-data\" method=\"post\">"
        b"<input type=\"file\" name=\"datafile\"><input type=\"submit\" value=\"Upload\">"
        b"</form></body></html>")


def reply(conn, body, status=b"200 OK"):
    head = b"HTTP/1.0 " + status + b"\r\nContent-Type: text/html\r\nContent-Length: " + \
        str(len(body)).encode() + b"\r\nConnection: close\r\n\r\n"
    try:
        conn.sendall(head + body)
    except OSError:
        pass


def read_request(conn, deadline):
    """Kopf und, falls angegeben, den ganzen Rumpf lesen. @return (kopf, rumpf) oder None"""
    data = b""
    while b"\r\n\r\n" not in data:
        conn.settimeout(max(0.1, deadline - time.monotonic()))
        chunk = conn.recv(4096)
        if not chunk:
            return None
        data += chunk
    head, body = data.split(b"\r\n\r\n", 1)
    length = None
    for line in head.split(b"\r\n")[1:]:
        k, _, v = line.partition(b":")
        if k.strip().lower() == b"content-length":
            length = int(v.strip())
    while length is not None and len(body) < length:
        conn.settimeout(max(0.1, deadline - time.monotonic()))
        chunk = conn.recv(65536)
        if not chunk:
            break
        body += chunk
    return head, body, length


def extract(body):
    """Dateidaten wie der Bootloader herausloesen. @return bytes oder None"""
    fn = body.find(b"filename=\"")
    if fn < 0 or body[fn + 10:fn + 11] == b"\"":
        return None
    i = body.find(b"octet-stream\r")
    if i < 0:
        return None
    end = body.rfind(b"\r\n--")
    if end < i + 16:
        return None
    return body[i + 16:end]


def serve(port, wait_s):
    """@return Wert fuer BKP12"""
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("", port))
    srv.listen(4)
    start = time.monotonic()
    upload_deadline = None
    print(f"bootloader-sim: Update-Betrieb auf Port {port}", flush=True)
    while True:
        deadline = upload_deadline or start + wait_s
        if time.monotonic() >= deadline:
            print("bootloader-sim: Zeit abgelaufen", flush=True)
            return MODE_WEB << 8
        srv.settimeout(max(0.1, deadline - time.monotonic()))
        try:
            conn, _ = srv.accept()
        except socket.timeout:
            continue
        with conn:
            try:
                req = read_request(conn, deadline)
            except (OSError, ValueError):
                continue
            if req is None:
                continue
            head, body, length = req
            line = head.split(b"\r\n", 1)[0]
            if not line.startswith(b"POST /upload.cgi"):
                reply(conn, PAGE)
                continue
            upload_deadline = upload_deadline or time.monotonic() + UPLOAD_S
            data = extract(body) if length is not None else None
            if data is None:
                reply(conn, PAGE)
                continue
            out = os.environ.get("WALLFREE_SIM_BOOTLOADER_OUT")
            if out:
                with open(out, "wb") as f:
                    f.write(data)
            ok = len(data) <= MAX_IMAGE and len(data) >= 8 and \
                (int.from_bytes(data[0:4], "little") & 0x2FF00000) == 0x20000000
            print(f"bootloader-sim: {len(data)} Byte empfangen, "
                  f"{'gueltig' if ok else 'ungueltig'}", flush=True)
            reply(conn, b"<!DOCTYPE html><html><body><h1>Upload abgeschlossen</h1></body></html>")
            time.sleep(1.0)
            return MODE_WEB << 8 | (1 if ok else 0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, required=True)
    ap.add_argument("cmd", nargs=argparse.REMAINDER)
    a = ap.parse_args()
    cmd = a.cmd[1:] if a.cmd and a.cmd[0] == "--" else a.cmd
    if not cmd:
        sys.exit("bootloader-sim: Programm zum Neustart fehlt")
    wait_s = float(os.environ.get("WALLFREE_SIM_BOOTLOADER_WAIT", "120"))
    result = serve(a.port, wait_s)
    os.environ["WALLFREE_SIM_BKP12"] = f"{result:x}"
    os.execv(cmd[0], cmd)


if __name__ == "__main__":
    main()
