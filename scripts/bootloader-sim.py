#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Ersatz fuer den Update-Betrieb des Original-Bootloaders in der Simulation.

Die Simulation startet dieses Skript bei der Uebergabe (SOLL-BOOT-30) statt sich selbst neu
(Umgebungsvariable WALLFREE_SIM_BOOTLOADER = Pfad dieses Skripts). Es uebernimmt den HTTP-Port
und bildet das Verhalten nach, das wallbe2/docs/bootloader.md (Abschnitt Update-Betrieb)
beschreibt:

- jedes GET liefert eine Upload-Seite (eigener Text, kein Inhalt des Originals);
- POST /upload.cgi wie der Empfang des Originals (`0x08007654`), Paket fuer Paket: Content-Length
  noetig, ein nicht leerer Dateiname, die Dateidaten beginnen 16 Byte nach "octet-stream"; die
  Zaehlung beginnt im ersten Paket hinter der Zahl von Content-Length (Schwachstelle: enthaelt das
  erste Paket schon Daten und steht Content-Length nicht zuletzt, endet der Upload zu frueh und
  gilt trotzdem als Erfolg); im letzten Paket endet die Datei vor dem letzten "\\r\\n--";
- hoechstens 0x50000 Byte (Sektor 4..6, 320 KB); Erfolg nur mit gueltigem Start-SP;
- 120 s Warten auf den Beginn des Uploads, 30 s fuer die Uebertragung; jedes GET setzt den
  gemeinsamen Upload-Zustand zurueck (bricht einen laufenden Upload ab);
- danach Neustart der Simulation mit dem Ergebnis in BKP12 (Modus << 8 | Erfolg).

Vereinfacht: kein zweiter oder dritter Versuch (BKP11), das Abbild wird nicht ausgefuehrt; die
Simulation erhaelt in WALLFREE_SIM_IMAGE, ob das empfangene Abbild einen gueltigen Anhang hat
(SOLL-BOOT-35, "ok:<crc>" oder "bad:<crc>").
WALLFREE_SIM_BOOTLOADER_OUT: Datei fuer die empfangenen Daten (Pruefung in den Tests).
WALLFREE_SIM_BOOTLOADER_WAIT: Wartezeit in s statt 120 (Tests).
WALLFREE_SIM_BOOTLOADER_RATE / _ERASE_S: Tempo in Byte/s (Default 65000) und Loeschpause in s
  (Default 2,3), gemessen am Testgeraet. Jede andere Anfrage (GET) waehrend des Uploads bricht ihn ab
  wie im Original (gemeinsamer Zustand); nach dem Loeschen bleibt der Ersatz dann im
  Update-Betrieb, bis ein Upload gelingt (Original: Modus 4).
WALLFREE_SIM_BOOTLOADER_SEG: Aufteilung in Pakete (die Simulation empfaengt am Stueck):
  "headers" (Default) Kopfzeilen allein, dann je 1426 Byte (MSS des Originals) wie bei Browsern
  und curl ueblich; "mss" die ganze Anfrage in Stuecken zu 1426 Byte (erstes Paket mit Daten).
"""
import argparse
import os
import socket
import struct
import sys
import threading
import time
import zlib

MODE_WEB = 2
MAX_IMAGE = 0x50000
UPLOAD_S = 30
HOLD_S = 60
MSS = 1426
TRAILER_MAGIC = 0x474D4957

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


def read_head(conn, deadline):
    """Bis zum Ende der Kopfzeilen lesen. @return (kopf, rest, laenge) oder None"""
    data = b""
    while b"\r\n\r\n" not in data:
        conn.settimeout(max(0.1, deadline - time.monotonic()))
        chunk = conn.recv(4096)
        if not chunk:
            return None
        data += chunk
    head, rest = data.split(b"\r\n\r\n", 1)
    length = None
    for line in head.split(b"\r\n")[1:]:
        k, _, v = line.partition(b":")
        if k.strip().lower() == b"content-length":
            length = int(v.strip())
    return head, rest, length


def segments(request, mode):
    if mode == "mss":
        return [request[k:k + MSS] for k in range(0, len(request), MSS)]
    h = request.index(b"\r\n\r\n") + 4
    body = request[h:]
    return [request[:h]] + [body[k:k + MSS] for k in range(0, len(body), MSS)]


def receive(segs):
    """Empfang wie im Original, je Paket. @return Dateidaten oder None (kein Upload begonnen)"""
    state, headers_only, count, length = 0, False, 0, 0
    data = None
    for seg in segs:
        off = 0
        if state == 0:
            if not seg.startswith(b"POST /upload.cgi"):
                return None
            i = seg.find(b"Content-Length: ")
            end = 0
            if i >= 0:
                end = i + 16
                while end < len(seg) and seg[end:end + 1].isdigit():
                    end += 1
                length = int(seg[i + 16:end] or b"0")
            k = seg.find(b"octet-stream")
            state = 1
            if k < 0:
                headers_only = True
                continue
            off = k + 16
            count = len(seg) - end - 4  # Schwachstelle: zaehlt Kopfzeilen nach Content-Length mit
        elif state == 1 and headers_only:
            k = seg.find(b"octet-stream\r")
            off = k + 16 if k >= 0 else 0
            count += len(seg)
            headers_only = False
        else:
            count += len(seg)
        if data is None:
            # Dateiname im selben Paket wie "octet-stream", sonst Abbruch ohne Loeschen
            fn = seg.find(b"filename=\"")
            if fn < 0 or seg[fn + 10:fn + 11] == b"\"":
                return None
            data = bytearray()  # hier loescht das Original Sektor 4..6
        if count < length or length == 0:
            data += seg[off:]
            continue
        u = 4
        while seg[len(seg) - u:len(seg) - u + 4] != b"\r\n--" and u < len(seg):
            u += 1
        if len(seg) - off > u:
            data += seg[off:len(seg) - u]
        return bytes(data)
    return bytes(data) if data is not None else None


def image_state(data):
    """SOLL-BOOT-35 fuer die Simulation: "ok:<crc>" oder "bad:<crc>" """
    if len(data) >= 24:
        magic, n, crc, inv = struct.unpack_from("<IIII", data, len(data) - 16)
        if magic == TRAILER_MAGIC and inv == ~TRAILER_MAGIC & 0xFFFFFFFF and \
                n == len(data) - 16 and zlib.crc32(data[:n]) == crc:
            return f"ok:{crc:08x}"
    return f"bad:{zlib.crc32(data[:max(0, len(data) - 16)]):08x}"


class Box:
    """Gemeinsamer Zustand wie im Original: ein Upload-Zustand fuer alle Verbindungen."""

    def __init__(self):
        self.lock = threading.Lock()
        self.gen = 0            # jedes GET erhoeht ihn (setzt den Upload-Zustand zurueck)
        self.erased = False     # Sektor 4..6 geloescht: ohne erfolgreichen Upload kein Programm
        self.upload_deadline = None
        self.result = None


def reply_hold(conn, box, body):
    """Antwort auf einen gelungenen Upload wie der Bootloader der Steuerung: HTTP/1.0 ohne
    Content-Length, danach startet er die Firmware, ohne die Verbindung zu schliessen (Testgeraet,
    Mitschnitt wallbe2 analysis/box-t/update4.pcap). Ein Kindprozess haelt die Verbindung
    deshalb auch nach dem Ende der Simulation noch HOLD_S offen."""
    try:
        conn.sendall(b"HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n\r\n" + body)
    except OSError:
        return
    if os.fork() == 0:
        try:
            box.srv.close()
            time.sleep(HOLD_S)
        finally:
            os._exit(0)


def upload(conn, box, head, rest, length):
    """Upload Paket fuer Paket mit Loeschpause und Tempo des Originals; bricht ab, sobald
    waehrenddessen eine andere Verbindung ein GET schickt."""
    rate = float(os.environ.get("WALLFREE_SIM_BOOTLOADER_RATE", "65000"))
    erase_s = float(os.environ.get("WALLFREE_SIM_BOOTLOADER_ERASE_S", "2.3"))
    with box.lock:
        my = box.gen
        box.upload_deadline = box.upload_deadline or time.monotonic() + UPLOAD_S
        deadline = box.upload_deadline
    body = rest
    erased_now = False
    while length is not None and len(body) < length:
        if not erased_now and body:
            with box.lock:
                box.erased = True  # erstes Paket mit Dateiname: das Original loescht hier
            erased_now = True
            time.sleep(erase_s)
        conn.settimeout(max(0.1, deadline - time.monotonic()))
        chunk = conn.recv(MSS)
        if not chunk:
            break
        body += chunk
        time.sleep(len(chunk) / rate)
        with box.lock:
            if box.gen != my:
                print("bootloader-sim: Upload abgebrochen (andere Anfrage)", flush=True)
                return
    mode = os.environ.get("WALLFREE_SIM_BOOTLOADER_SEG", "headers")
    data = receive(segments(head + b"\r\n\r\n" + body, mode)) if length is not None else None
    if data is None:
        reply(conn, PAGE)
        return
    out = os.environ.get("WALLFREE_SIM_BOOTLOADER_OUT")
    if out:
        with open(out, "wb") as f:
            f.write(data)
    ok = MAX_IMAGE >= len(data) >= 8 and \
        (int.from_bytes(data[0:4], "little") & 0x2FF00000) == 0x20000000
    os.environ["WALLFREE_SIM_IMAGE"] = image_state(data)
    print(f"bootloader-sim: {len(data)} Byte empfangen, "
          f"{'gueltig' if ok else 'ungueltig'} ({mode})", flush=True)
    reply_hold(conn, box, b"<!DOCTYPE html><html><body><h1>Upload abgeschlossen</h1></body></html>")
    with box.lock:
        box.result = MODE_WEB << 8 | (1 if ok else 0)


def handle(conn, box, deadline):
    with conn:
        try:
            req = read_head(conn, deadline)
            if req is None:
                return
            head, rest, length = req
            if head.startswith(b"POST /upload.cgi"):
                upload(conn, box, head, rest, length)
                return
            with box.lock:
                box.gen += 1
            reply(conn, PAGE)
        except (OSError, ValueError):
            pass


def serve(port, wait_s):
    """@return Wert fuer BKP12"""
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("", port))
    srv.listen(8)
    box = Box()
    box.srv = srv
    start = time.monotonic()
    print(f"bootloader-sim: Update-Betrieb auf Port {port}", flush=True)
    while True:
        with box.lock:
            result, erased, upload_deadline = box.result, box.erased, box.upload_deadline
        if result is not None:
            time.sleep(1.0)
            return result
        deadline = upload_deadline or start + wait_s
        if time.monotonic() >= deadline:
            if not erased:
                print("bootloader-sim: Zeit abgelaufen", flush=True)
                return MODE_WEB << 8
            # wie das Original nach geloeschtem Bereich: weiter im Update-Betrieb
            print("bootloader-sim: kein startbares Programm, warte weiter", flush=True)
            with box.lock:
                box.upload_deadline = None
            start = time.monotonic()
            continue
        srv.settimeout(0.2)
        try:
            conn, _ = srv.accept()
        except socket.timeout:
            continue
        threading.Thread(target=handle, args=(conn, box, start + wait_s + UPLOAD_S),
                         daemon=True).start()


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
