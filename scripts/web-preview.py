#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Vorschau der Weboberflaeche ohne Hardware.

Liefert die Dateien aus web/ und beantwortet die API mit Attrappen (kein
Digest, kein WebSocket - die Oberflaeche faellt dann auf Abfrage alle 2 s
zurueck). Nur zum Ansehen und Ausprobieren der Oberflaeche; sagt nichts ueber
das Verhalten der Firmware aus.

    scripts/web-preview.py [--port 8080] [--factory]
"""
import argparse
import json
import pathlib
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

WEB = pathlib.Path(__file__).resolve().parent.parent / "web"
FILES = {
    "/": ("index.html", "text/html; charset=utf-8"),
    "/app.js": ("app.js", "text/javascript"),
    "/style.css": ("style.css", "text/css"),
    "/vendor.js": ("vendor/preact-htm.js", "text/javascript"),
}

INFO = [
    {"name": "lock_pulse", "kind": "config", "unit": "ms", "min": 100, "max": 3000,
     "default": 500, "field": 525, "help": "Impuls Verriegeln"},
    {"name": "wakeup_delay", "kind": "config", "unit": "ms", "min": 0, "max": 300000,
     "default": 30000, "field": 0, "help": "Wartezeit in B bis zur Weckfolge, 0 = aus"},
    {"name": "external_current", "kind": "runtime", "unit": "mA", "min": 0, "max": 80000,
     "default": 80000, "field": 528, "help": "Sollstrom der Schnittstelle (evcc)"},
    {"name": "state", "kind": "status", "unit": "", "field": 0, "help": "Automatenzustand S0..S10"},
    {"name": "errors", "kind": "status", "unit": "", "field": 0, "help": "aktive Fehler (Bitmaske)"},
]
SYSINFO = [
    {"name": "net_dhcp", "type": "bool", "help": "Adresse per DHCP beziehen (F25)"},
    {"name": "net_ip", "type": "ipv4", "help": "feste IPv4-Adresse (ohne DHCP)"},
    {"name": "net_hostname", "type": "hostname", "help": "Hostname, leer = wallfree-<MAC-Ende>"},
    {"name": "telnet_enable", "type": "bool", "help": "Telnet-Shell ohne Anmeldung (F20)"},
]


class Sim:
    def __init__(self, factory):
        self.must_change = factory
        self.ext = 16000
        self.params = {"lock_pulse": 500, "wakeup_delay": 30000, "external_current": 16000,
                       "state": 1, "errors": 0}
        self.sys = {"net_dhcp": True, "net_ip": "192.168.0.8", "net_hostname": "",
                    "telnet_enable": True}
        self.active = dict(self.sys)
        self.t0 = time.time()

    def state(self):
        # Zyklus: kein Fahrzeug -> verbunden -> laedt, je 10 s
        phase = int((time.time() - self.t0) / 10) % 3
        charging = phase == 2 and self.ext > 0
        return {"iec61851_state": phase if self.ext or phase < 2 else 1,
                "charger_state": [0, 2, 3][phase] if self.ext else [0, 1, 1][phase],
                "contactor_state": 3 if charging else 0, "contactor_error": 0,
                "allowed_charging_current": min(self.ext, 16000) if phase else 0,
                "error_state": 0, "lock_state": 1}


class Handler(BaseHTTPRequestHandler):
    sim = None

    def send_json(self, obj, status=200):
        b = json.dumps(obj).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(b)))
        self.end_headers()
        self.wfile.write(b)

    def body(self):
        n = int(self.headers.get("Content-Length", 0))
        try:
            return json.loads(self.rfile.read(n) or b"null")
        except ValueError:
            return None

    def do_GET(self):
        s = self.sim
        if self.path in FILES:
            name, ctype = FILES[self.path]
            b = (WEB / name).read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(b)))
            self.end_headers()
            self.wfile.write(b)
        elif self.path == "/credentials":
            self.send_json({"must_change": s.must_change})
        elif s.must_change:
            self.send_json({"error": "Werkszugang: zuerst Zugangsdaten aendern"}, 403)
        elif self.path == "/evse/state":
            self.send_json(s.state())
        elif self.path == "/evse/external_current":
            self.send_json({"current": s.ext})
        elif self.path == "/evse/params":
            s.params["external_current"] = s.ext
            s.params["state"] = [1, 5, 6][int((time.time() - s.t0) / 10) % 3]
            self.send_json(s.params)
        elif self.path == "/evse/param_info":
            self.send_json(INFO)
        elif self.path == "/system/config":
            self.send_json({"active": s.active, "saved": s.sys,
                            "restart_required": s.active != s.sys})
        elif self.path == "/system/config_info":
            self.send_json(SYSINFO)
        elif self.path == "/system/info":
            self.send_json({"firmware": "vorschau", "reset_reason": "power_on", "reset_flags": 80,
                            "uptime_ms": int((time.time() - s.t0) * 1000)})
        elif self.path == "/evse/errors":
            self.send_json({"active": [], "seen": ["CP-INVALID"], "history": [
                {"age_ms": 42000, "state": 1, "set": [], "cleared": ["CP-INVALID"]},
                {"age_ms": 45000, "state": 1, "set": ["CP-INVALID"], "cleared": []}]})
        else:
            self.send_json({"error": "nicht gefunden"}, 404)

    def do_POST(self):
        s = self.sim
        b = self.body()
        if self.path == "/credentials":
            if not isinstance(b, dict) or len(b.get("password", "")) < 8:
                return self.send_json({"error": "Passwort: 8..64 Zeichen"}, 400)
            s.must_change = False
            return self.send_json({"must_change": False})
        if s.must_change:
            return self.send_json({"error": "Werkszugang"}, 403)
        if self.path == "/evse/external_current":
            cur = b.get("current") if isinstance(b, dict) else b
            if not isinstance(cur, int) or not (cur == 0 or 6000 <= cur <= 80000):
                return self.send_json({"error": "Wert ausserhalb des zulaessigen Bereichs"}, 400)
            s.ext = cur
            return self.send_json({"current": s.ext})
        if self.path == "/evse/params":
            errors = {}
            for k, v in (b or {}).items():
                p = next((p for p in INFO if p["name"] == k), None)
                if p is None:
                    errors[k] = "unbekannter Parameter"
                elif p["kind"] == "status":
                    errors[k] = "nur lesbar"
                elif not isinstance(v, int) or not p["min"] <= v <= p["max"]:
                    errors[k] = "Wert ausserhalb des zulaessigen Bereichs"
                else:
                    s.params[k] = v
                    if k == "external_current":
                        s.ext = v
            return self.send_json({"errors": errors} if errors else {}, 400 if errors else 200)
        if self.path == "/system/config":
            s.sys.update(b or {})
            return self.send_json({"restart_required": s.active != s.sys})
        if self.path == "/system/restart":
            s.active = dict(s.sys)
            return self.send_json({}, 202)
        if self.path == "/evse/errors/ack":
            return self.send_json({"acknowledged": False})
        self.send_json({"error": "nicht gefunden"}, 404)

    def log_message(self, fmt, *args):
        pass


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--factory", action="store_true", help="mit Werkszugang starten")
    a = ap.parse_args()
    Handler.sim = Sim(a.factory)
    print(f"Vorschau: http://127.0.0.1:{a.port}/  (nur lokal)")
    ThreadingHTTPServer(("127.0.0.1", a.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
