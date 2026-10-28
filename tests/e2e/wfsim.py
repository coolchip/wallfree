# SPDX-License-Identifier: Apache-2.0
"""Hilfen fuer den Ende-zu-Ende-Test gegen die Firmware in native_sim.

Nur Python-Standardbibliothek. Startet zephyr.exe mit eigener Flash-Datei,
spricht HTTP mit Digest-Anmeldung (wie evcc), die Telnet-Shell (Kommando
"sim" steuert das Hardwaremodell) und den WebSocket /ws.
"""

import base64
import hashlib
import http.client
import json
import os
import re
import secrets
import signal
import socket
import struct
import subprocess
import time

REALM = "wallfree"
FACTORY = ("admin", "wallfree")
# CONFIG_SHELL_TELNET_PORT in app/boards/native_sim_native_64.conf; abweichend z. B. fuer einen
# zweiten Build, waehrend eine andere Simulation 2323 belegt
TELNET_PORT = int(os.environ.get("WALLFREE_TELNET_PORT", "2323"))


def md5(s):
    return hashlib.md5(s.encode()).hexdigest()


class Digest:
    """HTTP-Digest (RFC 2617, MD5, qop=auth) mit wiederverwendeter Nonce."""

    def __init__(self, user, password):
        self.user = user
        self.password = password
        self.nonce = None
        self.nc = 0

    def take_challenge(self, header):
        m = re.search(r'nonce="([^"]+)"', header or "")
        if not m:
            raise AssertionError(f"Challenge ohne Nonce: {header!r}")
        self.nonce = m.group(1)
        self.nc = 0

    def header(self, method, uri):
        self.nc += 1
        nc = f"{self.nc:08x}"
        cnonce = secrets.token_hex(8)
        ha1 = md5(f"{self.user}:{REALM}:{self.password}")
        ha2 = md5(f"{method}:{uri}")
        resp = md5(f"{ha1}:{self.nonce}:{nc}:{cnonce}:auth:{ha2}")
        return (f'Digest username="{self.user}", realm="{REALM}", nonce="{self.nonce}", '
                f'uri="{uri}", algorithm=MD5, qop=auth, nc={nc}, cnonce="{cnonce}", '
                f'response="{resp}"')


class Response:
    def __init__(self, status, headers, body):
        self.status = status
        self.headers = headers
        self.body = body

    def json(self):
        return json.loads(self.body.decode())

    def __repr__(self):
        return f"<{self.status} {self.body[:200]!r}>"


class Sim:
    """Ein laufender native_sim-Prozess mit eigener Flash-Datei."""

    def __init__(self, exe, workdir, http_port, extra_args=(), env=None, telnet=True):
        self.exe = os.path.abspath(exe)
        self.telnet = telnet  # ohne: Bau wie die Steuerung (test_e2e.T9WieSteuerung)
        self.env = dict(env or {})
        self.workdir = workdir
        self.http_port = http_port
        self.flash = os.path.join(workdir, "flash.bin")
        self.log_path = os.path.join(workdir, "sim.log")
        self.extra_args = list(extra_args)
        self.proc = None
        self.log = None
        self.tn = None

    # ---------------------------------------------------------- Prozess

    def start(self, extra_args=None, timeout=15):
        if extra_args is not None:
            self.extra_args = list(extra_args)
        self.log = open(self.log_path, "ab")
        self.proc = subprocess.Popen(
            [self.exe, f"--flash={self.flash}", f"--http-port={self.http_port}",
             *self.extra_args],
            stdout=self.log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
            cwd=self.workdir, env={**os.environ, **self.env})
        try:
            self.wait_up(timeout)
        except AssertionError:
            self.stop()
            raise

    def stop(self):
        self.close_shell()
        if self.proc is not None and self.proc.poll() is None:
            self.proc.send_signal(signal.SIGINT)  # sauberes Ende, Flash-Datei geschrieben
            try:
                self.proc.wait(10)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
        self.proc = None
        if self.log:
            self.log.close()
            self.log = None

    def alive(self):
        return self.proc is not None and self.proc.poll() is None

    def port_open(self, port):
        try:
            socket.create_connection(("127.0.0.1", port), timeout=0.5).close()
            return True
        except OSError:
            return False

    def wait_up(self, timeout=15):
        t0 = time.monotonic()
        while time.monotonic() - t0 < timeout:
            if not self.alive():
                raise AssertionError(f"Simulation beendet, siehe {self.log_path}")
            if self.port_open(self.http_port) and (not self.telnet or self.port_open(TELNET_PORT)):
                return
            time.sleep(0.1)
        raise AssertionError(f"HTTP/Telnet nicht erreichbar, siehe {self.log_path}")

    def boots(self):
        """Anzahl der Starts im Log (jeder Neustart schreibt die Bootmeldung)."""
        return self.log_text().count("*** Booting Zephyr")

    def wait_restart(self, boots_before, timeout=30):
        """Neustart abwarten (Prozess startet sich per execv neu, gleiche PID)."""
        self.close_shell()
        t0 = time.monotonic()
        while self.boots() <= boots_before:
            if time.monotonic() - t0 > timeout or not self.alive():
                raise AssertionError(f"kein Neustart, siehe {self.log_path}")
            time.sleep(0.05)
        self.wait_up(timeout)

    def log_text(self):
        if self.log:
            self.log.flush()
        with open(self.log_path, "rb") as f:
            return f.read().decode(errors="replace")

    # ---------------------------------------------------------- HTTP

    def request(self, method, path, body=None, auth=None, timeout=5, retry=True):
        """Anfrage; mit auth (Digest) wie evcc: nach 401 einmal mit neuer Challenge."""
        data = None
        if body is not None:
            data = body if isinstance(body, (bytes, str)) else json.dumps(body)
            if isinstance(data, str):
                data = data.encode()
        for attempt in range(2):
            headers = {}
            if data is not None:
                headers["Content-Type"] = "application/json"
            if auth is not None and auth.nonce is not None:
                headers["Authorization"] = auth.header(method, path)
            c = http.client.HTTPConnection("127.0.0.1", self.http_port, timeout=timeout)
            try:
                c.request(method, path, body=data, headers=headers)
                r = c.getresponse()
                resp = Response(r.status, {k.lower(): v for k, v in r.getheaders()}, r.read())
            finally:
                c.close()
            if resp.status == 401 and auth is not None and attempt == 0 and retry:
                auth.take_challenge(resp.headers.get("www-authenticate"))
                continue
            return resp
        return resp

    def get_json(self, path, auth):
        r = self.request("GET", path, auth=auth)
        if r.status != 200:
            raise AssertionError(f"GET {path}: {r}")
        return r.json()

    def state(self, auth):
        return self.get_json("/evse/state", auth)

    def wait_state(self, auth, pred, timeout=20, what="Zustand"):
        t0 = time.monotonic()
        last = None
        while time.monotonic() - t0 < timeout:
            last = self.state(auth)
            if pred(last):
                return last
            time.sleep(0.1)
        raise AssertionError(f"{what} nicht erreicht, zuletzt {last}")

    # ---------------------------------------------------------- Telnet-Shell

    def close_shell(self):
        """Telnet schonend trennen: ausstehende Ausgabe lesen, dann FIN.

        Ein harter Abbruch (RST bei ungelesenen Daten) loest in Zephyr 4.4.2
        die Blockade der Systemworkqueue aus (siehe test_e2e.T3ZephyrTelnet)."""
        if self.tn is None:
            return
        try:
            self.tn.settimeout(0.2)
            while self.tn.recv(4096):
                pass
        except OSError:
            pass
        try:
            self.tn.shutdown(socket.SHUT_WR)
            self.tn.settimeout(1)
            while self.tn.recv(4096):
                pass
        except OSError:
            pass
        self.tn.close()
        self.tn = None

    def _read(self, done, timeout):
        buf = b""
        t0 = time.monotonic()
        self.tn.settimeout(0.1)
        while time.monotonic() - t0 < timeout:
            try:
                d = self.tn.recv(4096)
                if not d:
                    break
                buf += d
            except socket.timeout:
                pass
            if done(clean_telnet(buf)):
                break
        return clean_telnet(buf)

    def shell(self, cmd, timeout=5):
        """Shell-Kommando ausfuehren, Ausgabe ohne Echo, Prompt und Logzeilen."""
        if self.tn is None:
            self.tn = socket.create_connection(("127.0.0.1", TELNET_PORT), timeout=3)
            self.tn.sendall(b"\r\n")
            self._read(lambda t: t.rstrip().endswith("~$"), 3)
        self._read(lambda t: False, 0.2)  # Reste (Logzeilen, alte Prompts) abholen
        # Endmarke: Logzeilen zeichnen das Prompt jederzeit neu, ein Prompt allein
        # zeigt das Ende der Ausgabe nicht sicher an
        self.tn.sendall(cmd.encode() + b"\r\nevse uptime\r\n")
        out = self._read(lambda t: re.search(r"Laufzeit \d+ ms", t) is not None, timeout)
        out = re.split(r"Laufzeit \d+ ms", out)[0]
        # Echo des Kommandos kommt nicht immer zurueck
        i = out.find(cmd)
        rest = out[i + len(cmd):] if i >= 0 else out
        rest = rest.replace("evse uptime", "")
        # Logmeldungen laufen ebenfalls ueber die Telnet-Shell
        lines = [ln for ln in rest.splitlines()
                 if ln.strip() not in ("", "~$") and not LOG_LINE.match(ln.lstrip("~$ "))]
        return "\n".join(lines)

    # ---------------------------------------------------------- WebSocket

    def ws_connect(self, auth=None, timeout=5):
        return WebSocket(self.http_port, auth, timeout)


LOG_LINE = re.compile(r"^\[\d\d:\d\d:\d\d\.\d{3},\d{3}\] ")


def clean_telnet(b):
    b = re.sub(rb"\xff[\xfb-\xfe].", b"", b)    # Telnet-Optionen
    b = re.sub(rb"\xff.", b"", b)
    t = b.decode(errors="replace")
    t = re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", t)  # ANSI-Steuerfolgen
    return t.replace("\r", "")


class WebSocket:
    """Minimaler WebSocket-Client (RFC 6455), Anmeldung vorab wie evcc."""

    def __init__(self, port, auth, timeout):
        self.s = socket.create_connection(("127.0.0.1", port), timeout=timeout)
        key = base64.b64encode(os.urandom(16)).decode()
        req = (f"GET /ws HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\nUpgrade: websocket\r\n"
               f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\n"
               f"Sec-WebSocket-Version: 13\r\n")
        if auth is not None:
            req += f"Authorization: {auth.header('GET', '/ws')}\r\n"
        self.s.sendall((req + "\r\n").encode())
        head = b""
        while b"\r\n\r\n" not in head:
            d = self.s.recv(1024)
            if not d:
                raise ConnectionError("Verbindung beim Aufbau geschlossen")
            head += d
        self.status_line, _, self.rest = head.partition(b"\r\n")
        _, _, self.buf = head.partition(b"\r\n\r\n")
        self.status = int(self.status_line.split()[1])

    def _need(self, n, deadline):
        while len(self.buf) < n:
            self.s.settimeout(max(0.01, deadline - time.monotonic()))
            d = self.s.recv(4096)
            if not d:
                raise ConnectionError("geschlossen")
            self.buf += d

    def recv(self, timeout=3):
        """Naechster Textrahmen als JSON; ConnectionError bei Abbau."""
        deadline = time.monotonic() + timeout
        while True:
            self._need(2, deadline)
            b0, b1 = self.buf[0], self.buf[1]
            n, off = b1 & 0x7F, 2
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

    def recv_topic(self, topic, pred=lambda p: True, timeout=5):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            m = self.recv(deadline - time.monotonic())
            if m.get("topic") == topic and pred(m["payload"]):
                return m["payload"]
        raise AssertionError(f"kein Rahmen {topic}")

    def close(self):
        try:
            mask = os.urandom(4)
            self.s.sendall(bytes([0x88, 0x80]) + mask)
        except OSError:
            pass
        self.s.close()
