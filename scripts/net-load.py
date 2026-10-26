#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Netz-Lasttest gegen eine laufende Steuerung (Testgeraet) oder die Simulation.

Nur Python-Standardbibliothek; liest nur (GET, WebSocket, Telnet-Diagnose), schreibt keine
Parameter. Vor und nach jeder Stufe ein Schnappschuss (Laufzeit, Resetgrund, `net conn`,
`kernel thread stacks`); ein Neustart zwischen zwei Schnappschuessen gilt als Fehler.
    net-load.py HOST -u USER:PW [--http 80] [--telnet 23] STUFE [STUFE ...] [-t SEK]
Stufen:
    snap    nur Schnappschuss
    http    -n Abfrager (Default 4) ohne Pause, je Anfrage eine neue Verbindung
    keep    -n Abfrager mit Keep-Alive (eine Verbindung je Abfrager, wie ein Browser)
    ws      4 WebSockets (3 erlaubt): Takt evse/state, groesste Luecke
    churn   Verbindungsaufbau ohne Anfrage, mit RST, halbe Anfragen; danach Erreichbarkeit
    idle    alle HTTP-Plaetze mit stummen Verbindungen belegen, Freigabe nach Zeitlimit messen
    mixed   2 WebSockets + http + churn gleichzeitig
    long    Dauerlast wie Browser + evcc (2 WebSockets, Abfrage alle 2 s, Seite alle 60 s),
            Schnappschuss alle 5 min
"""
import argparse
import base64
import hashlib
import http.client
import json
import os
import re
import secrets
import socket
import statistics
import struct
import sys
import threading
import time

REALM = "wallfree"
# ohne Zaehler liefern /meters/0/* 404 (so gewollt), daher nicht dabei
PATHS = ["/evse/state", "/system/info", "/evse/params", "/evse/limits", "/evse/errors",
         "/mqtt/state", "/evse/history", "/system/log", "/evse/param_info",
         "/", "/app.js", "/style.css"]


def md5(s):
    return hashlib.md5(s.encode()).hexdigest()


class Digest:
    """HTTP-Digest (MD5, qop=auth) wie evcc: Nonce wiederverwenden, bei 401 neu."""

    def __init__(self, user, password):
        self.user, self.password, self.nonce, self.nc = user, password, None, 0

    def take(self, header):
        m = re.search(r'nonce="([^"]+)"', header or "")
        if not m:
            raise RuntimeError(f"Challenge ohne Nonce: {header!r}")
        self.nonce, self.nc = m.group(1), 0

    def header(self, method, uri):
        self.nc += 1
        nc, cnonce = f"{self.nc:08x}", secrets.token_hex(8)
        ha1 = md5(f"{self.user}:{REALM}:{self.password}")
        resp = md5(f"{ha1}:{self.nonce}:{nc}:{cnonce}:auth:{md5(f'{method}:{uri}')}")
        return (f'Digest username="{self.user}", realm="{REALM}", nonce="{self.nonce}", '
                f'uri="{uri}", algorithm=MD5, qop=auth, nc={nc}, cnonce="{cnonce}", '
                f'response="{resp}"')


class Stats:
    def __init__(self, name):
        self.name, self.lock = name, threading.Lock()
        self.lat, self.status, self.err = [], {}, {}

    def ok(self, status, dt):
        with self.lock:
            self.status[status] = self.status.get(status, 0) + 1
            self.lat.append(dt)

    def fail(self, e):
        k = type(e).__name__
        with self.lock:
            self.err[k] = self.err.get(k, 0) + 1

    def report(self, secs):
        n = sum(self.status.values())
        s = f"{self.name}: {n} Antworten ({n / secs:.1f}/s) {self.status}"
        if self.lat:
            q = sorted(self.lat)
            s += (f", Zeit ms p50 {q[len(q) // 2] * 1e3:.0f} p95 {q[int(len(q) * .95)] * 1e3:.0f}"
                  f" max {q[-1] * 1e3:.0f}")
        if self.err:
            s += f", Fehler {self.err}"
        return s


class Target:
    def __init__(self, a):
        self.host, self.http, self.telnet = a.host, a.http, a.telnet
        self.user, _, self.pw = a.user.partition(":")

    # ------------------------------------------------------------ HTTP
    def conn(self, timeout=5):
        return http.client.HTTPConnection(self.host, self.http, timeout=timeout)

    def get(self, c, auth, path):
        """GET ueber Verbindung c; nach 401 einmal mit neuer Challenge. @return (status, body)"""
        for _ in range(2):
            h = {"Authorization": auth.header("GET", path)} if auth.nonce else {}
            c.request("GET", path, headers=h)
            r = c.getresponse()
            body = r.read()
            if r.status == 401 and _ == 0:
                auth.take(r.getheader("WWW-Authenticate"))
                if r.getheader("Connection", "").lower() == "close":
                    c.close()
                continue
            return r.status, body
        return r.status, body

    def get_once(self, auth, path, timeout=5):
        c = self.conn(timeout)
        try:
            return self.get(c, auth, path)
        finally:
            c.close()

    def info(self):
        st, body = self.get_once(Digest(self.user, self.pw), "/system/info")
        if st != 200:
            raise RuntimeError(f"/system/info: {st}")
        return json.loads(body)

    # ------------------------------------------------------------ Telnet
    def shell(self, cmds, timeout=8):
        """Kommandos ausfuehren, Ausgabe roh; schonend trennen (FIN nach Lesen, sonst
        blockiert Zephyr 4.4.2 die Systemworkqueue, implementierung.md Fallstricke)."""
        s = socket.create_connection((self.host, self.telnet), timeout=3)
        buf = b""

        def read(until, t):
            nonlocal buf
            end = time.monotonic() + t
            s.settimeout(0.2)
            while time.monotonic() < end:
                try:
                    d = s.recv(4096)
                    if not d:
                        break
                    buf += d
                except socket.timeout:
                    pass
                if until(clean(buf)):
                    break

        s.sendall(b"\r\n")
        read(lambda t: t.rstrip().endswith("~$"), 3)
        buf = b""
        s.sendall("".join(c + "\r\n" for c in cmds).encode() + b"kernel uptime\r\n")
        read(lambda t: re.search(r"Uptime: \d+ ms", t) is not None, timeout)
        read(lambda t: False, 0.3)
        try:
            s.shutdown(socket.SHUT_WR)
            s.settimeout(1)
            while s.recv(4096):
                pass
        except OSError:
            pass
        s.close()
        return clean(buf)


def clean(b):
    b = re.sub(rb"\xff[\xfb-\xfe].", b"", b)
    b = re.sub(rb"\xff.", b"", b)
    t = b.decode(errors="replace")
    return re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", t).replace("\r", "")


class WebSocket:
    def __init__(self, t, timeout=5):
        auth = Digest(t.user, t.pw)
        st, _ = t.get_once(auth, "/evse/state")  # Challenge holen
        self.s = socket.create_connection((t.host, t.http), timeout=timeout)
        key = base64.b64encode(os.urandom(16)).decode()
        req = (f"GET /ws HTTP/1.1\r\nHost: {t.host}\r\nUpgrade: websocket\r\n"
               f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n"
               f"Authorization: {auth.header('GET', '/ws')}\r\n\r\n")
        self.s.sendall(req.encode())
        head = b""
        while b"\r\n\r\n" not in head:
            d = self.s.recv(1024)
            if not d:
                raise ConnectionError("beim Aufbau geschlossen")
            head += d
        self.status = int(head.split(b"\r\n")[0].split()[1])
        self.buf = head.partition(b"\r\n\r\n")[2]

    def _need(self, n, deadline):
        while len(self.buf) < n:
            self.s.settimeout(max(0.01, deadline - time.monotonic()))
            d = self.s.recv(4096)
            if not d:
                raise ConnectionError("geschlossen")
            self.buf += d

    def recv(self, timeout=5):
        deadline = time.monotonic() + timeout
        while True:
            self._need(2, deadline)
            b0, n, off = self.buf[0], self.buf[1] & 0x7F, 2
            if n == 126:
                self._need(4, deadline)
                n, off = struct.unpack(">H", self.buf[2:4])[0], 4
            elif n == 127:
                self._need(10, deadline)
                n, off = struct.unpack(">Q", self.buf[2:10])[0], 10
            self._need(off + n, deadline)
            payload, self.buf = self.buf[off:off + n], self.buf[off + n:]
            op = b0 & 0x0F
            if op == 0x8:
                raise ConnectionError("Close-Rahmen")
            if op == 0x1:
                return json.loads(payload.decode())

    def close(self):
        try:
            self.s.sendall(bytes([0x88, 0x80]) + os.urandom(4))
        except OSError:
            pass
        self.s.close()


# ---------------------------------------------------------------- Stufen
def snapshot(t, label):
    i = t.info()
    out = t.shell(["net conn", "kernel thread stacks"])
    tcp = out.partition("Send-Seq")[2].partition("\n\n")[0]
    states = {}
    for m in re.finditer(r"^0x[0-9a-f]+\s+0x[0-9a-f]+\s.*?(\w+)\s*$", tcp, re.M):
        states[m.group(1)] = states.get(m.group(1), 0) + 1
    ctx = len(re.findall(r"^\[\s*\d+\] 0x[0-9a-f]+\s+\d", out.partition("Handler")[0], re.M))
    states["Kontexte"] = ctx  # von CONFIG_NET_MAX_CONTEXTS
    stacks = {m.group(1): (int(m.group(2)), int(m.group(3))) for m in re.finditer(
        r"0x[0-9a-f]+\s+(.+?)\s+\(real size\s+(\d+)\):\s+unused\s+(\d+)", out)}
    used = ", ".join(f"{k} {sz - un}/{sz}" for k, (sz, un) in sorted(stacks.items()))
    print(f"[{label}] Laufzeit {i['uptime_ms'] / 1000:.0f} s, Reset {i['reset_reason']}, "
          f"Firmware {i['firmware']}, TCP {states or 0}")
    print(f"[{label}] Stacks: {used}")
    return {"uptime_ms": i["uptime_ms"], "stacks": stacks, "shell": out}


def run_threads(fns, secs):
    stop = threading.Event()
    th = [threading.Thread(target=f, args=(stop,), daemon=True) for f in fns]
    for x in th:
        x.start()
    time.sleep(secs)
    stop.set()
    for x in th:
        x.join(15)


def http_worker(t, st, keep, pause=0.0, paths=PATHS):
    def run(stop):
        auth, c, i = Digest(t.user, t.pw), None, secrets.randbelow(len(paths))
        while not stop.is_set():
            path = paths[i % len(paths)]
            i += 1
            t0 = time.monotonic()
            try:
                if c is None:
                    c = t.conn()
                status, _ = t.get(c, auth, path)
                st.ok(status, time.monotonic() - t0)
            except Exception as e:  # noqa: BLE001 - jede Stoerung zaehlen
                st.fail(e)
                if c:
                    c.close()
                c = None
                time.sleep(0.2)
            if not keep and c:
                c.close()
                c = None
            if pause:
                stop.wait(pause)
        if c:
            c.close()
    return run


def ws_worker(t, st, gaps):
    """WebSocket offen halten; Luecken zwischen evse/state-Rahmen sammeln, neu verbinden."""
    def run(stop):
        while not stop.is_set():
            try:
                w = WebSocket(t)
            except Exception as e:  # noqa: BLE001
                st.fail(e)
                stop.wait(1)
                continue
            if w.status != 101:
                st.fail(RuntimeError(f"WS {w.status}"))
                w.close()
                stop.wait(1)
                continue
            last = time.monotonic()
            try:
                while not stop.is_set():
                    m = w.recv(10)
                    now = time.monotonic()
                    if m.get("topic") == "evse/state":
                        st.ok("state", 0)
                        gaps.append(now - last)
                        last = now
            except Exception as e:  # noqa: BLE001
                if not stop.is_set():
                    st.fail(e)
            w.close()
    return run


def churn_worker(t, st):
    """Aufbau ohne Anfrage, Abbruch mit RST, halbe Anfrage; abwechselnd."""
    def run(stop):
        k = 0
        while not stop.is_set():
            k += 1
            try:
                s = socket.create_connection((t.host, t.http), timeout=3)
                if k % 3 == 1:
                    s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
                elif k % 3 == 2:
                    s.sendall(b"GET /evse/state HTTP/1.1\r\nHost: x\r\n")
                s.close()
                st.ok(("rst", "fin", "halb")[k % 3], 0)
            except Exception as e:  # noqa: BLE001
                st.fail(e)
            stop.wait(0.05)
    return run


def stage_http(t, a, keep):
    st = Stats("keep" if keep else "http")
    run_threads([http_worker(t, st, keep) for _ in range(a.n)], a.t)
    print(st.report(a.t))
    return not st.err and set(st.status) <= {200}


def stage_ws(t, a):
    socks, res = [], []
    for k in range(4):
        try:
            w = WebSocket(t)
            res.append(w.status)
            socks.append(w)
        except Exception as e:  # noqa: BLE001
            res.append(type(e).__name__)
    print(f"ws: Aufbau 4 WebSockets -> {res}")
    gaps = {i: [] for i in range(len(socks))}
    end = time.monotonic() + a.t
    last = {i: time.monotonic() for i in gaps}
    while time.monotonic() < end:
        for i, w in enumerate(socks):
            if w.status != 101:
                continue
            try:
                m = w.recv(0.05)
                if m.get("topic") == "evse/state":
                    now = time.monotonic()
                    gaps[i].append(now - last[i])
                    last[i] = now
            except (socket.timeout, TimeoutError):
                pass
            except ConnectionError as e:
                print(f"ws {i}: {e}")
                w.status = -1
    for i, g in gaps.items():
        if g:
            print(f"ws {i}: {len(g)} evse/state, groesste Luecke {max(g):.1f} s")
    for w in socks:
        w.close()
    # 3 Plaetze; ein offener Browser-Tab belegt selbst einen, daher nur: hoechstens 3, Takt haelt
    open_ = [g for g in gaps.values() if g]
    return 1 <= len(open_) <= 3 and all(max(g) < 5 for g in open_)


def reachable_after(t, what, limit=30):
    t0 = time.monotonic()
    while time.monotonic() - t0 < limit:
        try:
            st, _ = t.get_once(Digest(t.user, t.pw), "/evse/state", timeout=3)
            if st == 200:
                print(f"{what}: wieder erreichbar nach {time.monotonic() - t0:.1f} s")
                return True
        except Exception:  # noqa: BLE001
            pass
        time.sleep(0.5)
    print(f"{what}: nach {limit} s nicht erreichbar")
    return False


def stage_churn(t, a):
    st = Stats("churn")
    run_threads([churn_worker(t, st) for _ in range(2)], a.t)
    print(st.report(a.t))
    return reachable_after(t, "churn")


def stage_idle(t, a):
    socks = []
    for _ in range(6):
        try:
            socks.append(socket.create_connection((t.host, t.http), timeout=3))
        except OSError as e:
            print(f"idle: Aufbau {len(socks) + 1}: {e}")
    print(f"idle: {len(socks)} stumme Verbindungen offen")
    t0 = time.monotonic()
    ok = reachable_after(t, "idle (Plaetze belegt)", 40)
    closed = 0
    for s in socks:
        s.settimeout(0.5)
        try:
            if s.recv(1) == b"":
                closed += 1
        except (socket.timeout, TimeoutError):
            pass
        except OSError:
            closed += 1
        s.close()
    print(f"idle: {closed} von {len(socks)} vom Server geschlossen nach {time.monotonic() - t0:.0f} s")
    return ok


def stage_mixed(t, a):
    sh, sw, sc, gaps = Stats("mixed http"), Stats("mixed ws"), Stats("mixed churn"), []
    fns = [http_worker(t, sh, False) for _ in range(a.n)]
    fns += [ws_worker(t, sw, gaps) for _ in range(2)] + [churn_worker(t, sc)]
    run_threads(fns, a.t)
    for s in (sh, sw, sc):
        print(s.report(a.t))
    if gaps:
        print(f"mixed ws: groesste Luecke evse/state {max(gaps):.1f} s")
    return reachable_after(t, "mixed") and not sw.err and gaps and max(gaps) < 5


def stage_long(t, a):
    sh, sp, sw, gaps = Stats("long Abfrage"), Stats("long Seite"), Stats("long ws"), []
    fns = [http_worker(t, sh, True, 2.0, ["/evse/state", "/evse/limits", "/system/info"]),
           http_worker(t, sp, False, 60.0, ["/", "/app.js", "/style.css", "/evse/param_info",
                                            "/evse/params", "/system/log", "/evse/history"])]
    fns += [ws_worker(t, sw, gaps) for _ in range(2)]
    stop = threading.Event()
    th = [threading.Thread(target=f, args=(stop,), daemon=True) for f in fns]
    for x in th:
        x.start()
    t0, ok, last = time.monotonic(), True, None
    while time.monotonic() - t0 < a.t:
        time.sleep(min(300, a.t - (time.monotonic() - t0)))
        snap = snapshot(t, f"long {(time.monotonic() - t0) / 60:.0f} min")
        if last and snap["uptime_ms"] < last["uptime_ms"]:
            print("long: NEUSTART")
            ok = False
        last = snap
        print(sh.report(time.monotonic() - t0))
        print(sw.report(time.monotonic() - t0))
    stop.set()
    for x in th:
        x.join(15)
    print(sp.report(a.t))
    if gaps:
        g = sorted(gaps)
        print(f"long ws: {len(g)} Rahmen, Luecke p99 {g[int(len(g) * .99)]:.1f} s, max {g[-1]:.1f} s")
    return ok and not sh.err and not sp.err


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    p.add_argument("host")
    p.add_argument("stages", nargs="+")
    p.add_argument("-u", "--user", required=True, help="USER:PW")
    p.add_argument("--http", type=int, default=80)
    p.add_argument("--telnet", type=int, default=23)
    p.add_argument("-t", type=float, default=60, help="Dauer je Stufe in s")
    p.add_argument("-n", type=int, default=4, help="Abfrager bei http/keep/mixed")
    a = p.parse_args()
    t = Target(a)
    stages = {"http": lambda: stage_http(t, a, False), "keep": lambda: stage_http(t, a, True),
              "ws": lambda: stage_ws(t, a), "churn": lambda: stage_churn(t, a),
              "idle": lambda: stage_idle(t, a), "mixed": lambda: stage_mixed(t, a),
              "long": lambda: stage_long(t, a), "snap": lambda: True}
    bad = [s for s in a.stages if s not in stages]
    if bad:
        sys.exit(f"unbekannte Stufe {bad}")
    before = snapshot(t, "vorher")
    fails = []
    for s in a.stages:
        print(f"--- {s} ({a.t:.0f} s, n={a.n})")
        if not stages[s]():
            fails.append(s)
        after = snapshot(t, f"nach {s}")
        if after["uptime_ms"] < before["uptime_ms"]:
            print(f"{s}: NEUSTART waehrend der Stufe")
            fails.append(s + " (Neustart)")
        before = after
    print("Ergebnis:", "ok" if not fails else f"auffaellig: {fails}")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
