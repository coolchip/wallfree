# SPDX-License-Identifier: Apache-2.0
"""Ende-zu-Ende-Test: echte Firmware (native_sim) ueber HTTP, WebSocket und Shell.

Aufruf ueber scripts/run-e2e.sh (baut vorher). Direkt:
    WALLFREE_SIM_EXE=<build>/zephyr/zephyr.exe python3 -m unittest -v tests/e2e/test_e2e.py

Die Testfaelle einer Klasse laufen in Namensreihenfolge gegen denselben
Prozess und bauen aufeinander auf. Die Hardware ist ein Modell (app/src/hw_sim.c);
die Tests belegen nichts ueber die reale Steuerung.
"""

import gzip
import os
import re
import shutil
import socket
import tempfile
import time
import unittest

from wfsim import FACTORY, Digest, Sim

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
EXE = os.environ.get("WALLFREE_SIM_EXE", os.path.join(REPO, "build/sim/zephyr/zephyr.exe"))
USER, PASSWORD = "evcc", "geheim-123"


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class SimCase(unittest.TestCase):
    """Ein Prozess je Klasse, frische Flash-Datei."""

    extra_args = ()

    @classmethod
    def setUpClass(cls):
        if not os.path.exists(EXE):
            raise unittest.SkipTest(f"{EXE} fehlt (scripts/run-e2e.sh baut es)")
        cls.tmp = tempfile.mkdtemp(prefix="wallfree-e2e-")
        cls.sim = Sim(EXE, cls.tmp, free_port(), cls.extra_args)
        cls.sim.start()

    @classmethod
    def tearDownClass(cls):
        cls.sim.stop()
        if os.environ.get("WALLFREE_E2E_KEEP"):
            print(f"\nLog: {cls.sim.log_path}")
        else:
            shutil.rmtree(cls.tmp, ignore_errors=True)

    def shell(self, cmd):
        out = self.sim.shell(cmd)
        self.assertNotIn("erwartet", out, f"{cmd}: {out}")  # Bedienfehler im sim-Kommando
        return out

    def set_credentials(self):
        auth = Digest(*FACTORY)
        r = self.sim.request("POST", "/credentials", {"user": USER, "password": PASSWORD},
                             auth=auth)
        self.assertEqual(r.status, 200, r)
        return Digest(USER, PASSWORD)


class T1Api(SimCase):
    """Anmeldung, Ressourcen, Laden, WebSocket, Persistenz, Neustart, Bremse."""

    auth = None

    def test_01_ohne_anmeldung_401(self):
        r = self.sim.request("GET", "/evse/state")
        self.assertEqual(r.status, 401)
        ch = r.headers["www-authenticate"]
        self.assertIn('realm="wallfree"', ch)
        self.assertIn('qop="auth"', ch)

    def test_02_werkszugang_nur_credentials(self):
        auth = Digest(*FACTORY)
        self.assertEqual(self.sim.request("GET", "/evse/state", auth=auth).status, 403)
        self.assertEqual(self.sim.request("GET", "/system/info", auth=auth).status, 403)
        r = self.sim.request("GET", "/credentials", auth=auth)
        self.assertEqual(r.status, 200, r)
        self.assertTrue(r.json()["must_change"])
        # Oberflaeche auch im Werkszugang (Passwortaenderung)
        r = self.sim.request("GET", "/", auth=auth)
        self.assertEqual(r.status, 200)
        self.assertEqual(r.headers.get("content-encoding"), "gzip")
        self.assertIn(b"<html", gzip.decompress(r.body).lower())

    def test_03_passwort_regeln(self):
        auth = Digest(*FACTORY)
        for body in ({"user": USER, "password": "kurz"},
                     {"user": USER, "password": FACTORY[1]},
                     {"user": "a b", "password": PASSWORD},
                     {"user": USER}):
            r = self.sim.request("POST", "/credentials", body, auth=auth)
            self.assertEqual(r.status, 400, f"{body}: {r}")

    def test_04_zugangsdaten_aendern(self):
        T1Api.auth = self.set_credentials()
        self.assertEqual(self.sim.request("GET", "/evse/state", auth=Digest(*FACTORY)).status,
                         401)
        self.assertEqual(self.sim.request("GET", "/evse/state", auth=self.auth).status, 200)

    def test_05_info_und_system(self):
        name = self.sim.get_json("/info/name", self.auth)
        self.assertEqual(name["type"], "warp2")
        self.assertEqual(self.sim.get_json("/info/features", self.auth), [])
        info = self.sim.get_json("/system/info", self.auth)
        self.assertEqual(info["reset_reason"], "power_on")
        cfg = self.sim.get_json("/system/config", self.auth)
        self.assertFalse(cfg["restart_required"])
        self.assertIsInstance(self.sim.get_json("/system/config_info", self.auth), (list, dict))
        for path in ("/app.js", "/style.css", "/vendor.js"):
            r = self.sim.request("GET", path, auth=self.auth)
            self.assertEqual(r.status, 200, path)
            self.assertGreater(len(gzip.decompress(r.body)), 100, path)

    def test_06_ruhezustand(self):
        st = self.sim.wait_state(self.auth, lambda s: s["iec61851_state"] == 0)
        self.assertEqual(st["charger_state"], 0)
        self.assertEqual(st["contactor_state"], 0)
        self.assertEqual(st["error_state"], 0)
        self.assertEqual(st["allowed_charging_current"], 0)

    def test_07_parameter(self):
        params = self.sim.get_json("/evse/params", self.auth)
        info = self.sim.get_json("/evse/param_info", self.auth)
        self.assertEqual(sorted(p["name"] for p in info), sorted(params))
        lp = next(p for p in info if p["name"] == "lock_pulse")
        r = self.sim.request("POST", "/evse/params", {"lock_pulse": lp["max"] + 1},
                             auth=self.auth)
        self.assertEqual(r.status, 400, r)
        r = self.sim.request("POST", "/evse/params", {"unbekannt": 1}, auth=self.auth)
        self.assertEqual(r.status, 400, r)
        r = self.sim.request("POST", "/evse/params", b"{kaputt", auth=self.auth)
        self.assertEqual(r.status, 400, r)
        new = lp["default"] + 100 if lp["default"] + 100 <= lp["max"] else lp["default"] - 100
        T1Api.lock_pulse = new
        r = self.sim.request("POST", "/evse/params", {"lock_pulse": new}, auth=self.auth)
        self.assertEqual(r.status, 200, r)
        self.assertEqual(self.sim.get_json("/evse/params", self.auth)["lock_pulse"], new)

    def test_08_ladevorgang(self):
        self.shell("sim car B")
        # S4 (Selbsttest) meldet ebenfalls 2, aber ohne PWM
        st = self.sim.wait_state(self.auth, lambda s: s["charger_state"] == 2 and
                                 s["allowed_charging_current"] > 0, what="PWM an")
        self.assertEqual(st["iec61851_state"], 1)
        # Strom des Tastgrads (SOLL-CUR-21): 32 A -> 533 Promille -> 31980 mA
        self.assertEqual(st["allowed_charging_current"], 31980)
        self.shell("sim car C")
        st = self.sim.wait_state(self.auth, lambda s: s["charger_state"] == 3, what="Laden")
        self.assertEqual(st["contactor_state"], 3)
        self.assertIn("contactor=1", self.shell("sim status"))
        # Sollwert 80 A (F12, keine Einschraenkung), Drehschalter 6 = 32 A begrenzt
        lim = self.sim.get_json("/evse/limits", self.auth)
        self.assertEqual((lim["external"], lim["hardware"], lim["allowed"], lim["limited_by"]),
                         (80000, 32000, 31980, "hardware"))

    def test_09_sollstrom(self):
        r = self.sim.request("POST", "/evse/external_current", {"current": 10000},
                             auth=self.auth)
        self.assertEqual(r.status, 200, r)
        self.assertEqual(r.json()["current"], 10000)
        # 10 A -> Tastgrad 166 Promille -> 9960 mA (SOLL-CUR-21)
        self.sim.wait_state(self.auth, lambda s: s["allowed_charging_current"] == 9960)
        self.assertIn("duty=166 ", self.shell("sim status"))
        self.assertEqual(self.sim.get_json("/evse/limits", self.auth)["limited_by"], "external")
        r = self.sim.request("POST", "/evse/external_current", 3000, auth=self.auth)
        self.assertEqual(r.status, 400, r)  # unter 6 A und nicht 0
        r = self.sim.request("POST", "/evse/external_current", 0, auth=self.auth)
        self.assertEqual(r.status, 200, r)
        self.sim.wait_state(self.auth, lambda s: s["contactor_state"] == 0, what="Schuetz aus")
        r = self.sim.request("POST", "/evse/external_current", 16000, auth=self.auth)
        self.sim.wait_state(self.auth, lambda s: s["charger_state"] == 3 and
                            s["allowed_charging_current"] == 15960, what="wieder Laden")

    def test_10_websocket(self):
        ws = self.sim.ws_connect(self.auth)
        try:
            self.assertEqual(ws.status, 101)
            st = ws.recv_topic("evse/state")
            self.assertEqual(st["charger_state"], 3)
            self.assertEqual(ws.recv_topic("evse/external_current")["current"], 16000)
            t0 = time.monotonic()
            self.sim.request("POST", "/evse/external_current", 12000, auth=self.auth)
            ws.recv_topic("evse/external_current", lambda p: p["current"] == 12000)
            self.assertLess(time.monotonic() - t0, 1.0)
            ws.recv_topic("evse/state", lambda p: p["allowed_charging_current"] == 12000)
        finally:
            ws.close()

    def test_10b_websocket_plaetze(self):
        # drei gleichzeitig (SOLL-API-07: mindestens 2), Plaetze werden wieder frei
        for _ in range(3):
            socks = [self.sim.ws_connect(self.auth) for _ in range(3)]
            try:
                for w in socks:
                    self.assertEqual(w.recv_topic("evse/state")["charger_state"], 3)
            finally:
                for w in socks:
                    w.close()
            time.sleep(0.3)
        w = self.sim.ws_connect(self.auth)
        w.recv_topic("evse/state")
        w.s.close()  # Abbruch ohne Close-Rahmen
        time.sleep(0.3)
        ws = self.sim.ws_connect(self.auth)
        try:
            ws.recv_topic("evse/state")
        finally:
            ws.close()

    def test_10c_ereignisse(self):
        # SOLL-ERR-10: Schuetz und Zustandswechsel als evse/event, fortlaufende Nummern
        ws = self.sim.ws_connect(self.auth)
        try:
            ws.recv_topic("evse/state")
            self.sim.request("POST", "/evse/external_current", 0, auth=self.auth)
            # S6 Laden -> geordnetes Ende (S7), Schuetz aus spaetestens nach 5 s
            ws.recv_topic("evse/event", lambda p: p["type"] == "state" and p["from"] == 6)
            off = ws.recv_topic("evse/event", lambda p: p["type"] == "contactor", timeout=8)
            self.assertFalse(off["on"])
            self.sim.request("POST", "/evse/external_current", 16000, auth=self.auth)
            on = ws.recv_topic("evse/event", lambda p: p["type"] == "contactor", timeout=10)
            self.assertTrue(on["on"])
            self.assertGreater(on["seq"], off["seq"])
            self.assertGreater(on["t_ms"], off["t_ms"])
        finally:
            ws.close()
        self.assertIn('"type":"contactor","on":true', self.sim.log_text())  # Protokoll

    def test_11_websocket_ohne_anmeldung(self):
        ws = self.sim.ws_connect(None)
        try:
            with self.assertRaises((ConnectionError, OSError)):
                ws.recv(timeout=2)
        finally:
            ws.close()
        bad = Digest(USER, "falsch-falsch")
        bad.nonce = self.auth.nonce
        ws = self.sim.ws_connect(bad)
        try:
            with self.assertRaises((ConnectionError, OSError)):
                ws.recv(timeout=2)
        finally:
            ws.close()

    def test_11a_zugriffsprotokoll(self):
        # SOLL-ERR-11: falsche Anmeldung mit Absender und Ereignisse in GET /system/log
        self.assertEqual(self.sim.request("GET", "/system/log").status, 401)
        bad = Digest(USER, "falsch-falsch")
        bad.nonce = self.auth.nonce
        t0 = self.sim.get_json("/system/info", self.auth)["uptime_ms"]
        r = self.sim.request("GET", "/evse/state", auth=bad, retry=False)
        self.assertEqual(r.status, 401)
        r = self.sim.request("GET", "/system/log", auth=self.auth)
        self.assertEqual(r.status, 200, r)
        entries = r.json()["entries"]
        self.assertGreater(len(r.body), 1536, "Antwort in mehreren Teilen")
        seqs = [e["seq"] for e in entries]
        self.assertEqual(seqs, sorted(seqs))
        self.assertLessEqual(len(entries), 32)
        self.assertEqual({e["kind"] for e in entries}, {"auth_fail", "event"})
        fail = [e for e in entries if e["kind"] == "auth_fail"][-1]
        self.assertEqual(fail["addr"], "127.0.0.1")
        self.assertGreaterEqual(fail["count"], 1)
        self.assertGreaterEqual(fail["t_ms"], t0)
        ev = [e["event"] for e in entries if e["kind"] == "event"]
        self.assertTrue(any(e["type"] == "contactor" and e["on"] for e in ev), ev)
        self.assertTrue(any(e["type"] == "state" for e in ev), ev)

    def test_11b_netzwerk_led(self):
        # SOLL-LED-06: an bei Verbindung, sonst 1 Hz
        self.assertIn("N=1", self.shell("sim status"))
        self.shell("sim link 0")
        seen = set()
        for _ in range(8):
            seen.add(re.search(r"N=(\d)", self.shell("sim status")).group(1))
            time.sleep(0.2)
        self.assertEqual(seen, {"0", "1"})
        self.shell("sim link 1")
        time.sleep(0.6)
        for _ in range(4):
            self.assertIn("N=1", self.shell("sim status"))
            time.sleep(0.2)

    def test_12_abstecken(self):
        self.shell("sim car A")
        st = self.sim.wait_state(self.auth, lambda s: s["charger_state"] == 0, what="frei")
        self.assertEqual(st["contactor_state"], 0)
        self.assertEqual(st["error_state"], 0)

    def test_13_neustart_persistenz(self):
        cfg = self.sim.request("POST", "/system/config", {"net_hostname": "e2e-box"},
                               auth=self.auth)
        self.assertEqual(cfg.status, 200, cfg)
        self.assertTrue(cfg.json()["restart_required"])
        boots = self.sim.boots()
        r = self.sim.request("POST", "/system/restart", b"", auth=self.auth)
        self.assertEqual(r.status, 202, r)
        self.sim.wait_restart(boots)
        info = self.sim.get_json("/system/info", self.auth)  # Zugangsdaten gespeichert
        self.assertEqual(info["reset_reason"], "user_restart")
        self.assertEqual(self.sim.get_json("/evse/params", self.auth)["lock_pulse"],
                         self.lock_pulse)
        self.assertEqual(self.sim.get_json("/info/name", self.auth)["name"], "e2e-box")
        self.assertFalse(self.sim.get_json("/system/config", self.auth)["restart_required"])
        # Sollstrom liegt nur im RAM (SOLL-NVM-03): wieder Startwert
        self.assertEqual(self.sim.get_json("/evse/external_current", self.auth)["current"],
                         80000)

    def test_14_neustart_waehrend_laden(self):
        self.shell("sim car C")
        self.sim.wait_state(self.auth, lambda s: s["charger_state"] == 3, what="Laden")
        boots = self.sim.boots()
        r = self.sim.request("POST", "/system/restart", b"", auth=self.auth)
        self.assertEqual(r.status, 202, r)
        # geordnetes Ende (SOLL-RT-22): Schuetz aus vor dem Reset, nicht erst nach der Frist
        self.sim.wait_restart(boots)
        self.assertNotIn("nach Fristablauf", self.sim.log_text())
        # Fahrzeug bleibt angesteckt: nach dem Start wieder Laden
        self.sim.wait_state(self.auth, lambda s: s["charger_state"] == 3, timeout=20,
                            what="Laden nach Neustart")
        self.shell("sim car A")
        self.sim.wait_state(self.auth, lambda s: s["charger_state"] == 0, what="frei")

    def test_99_bremse(self):
        bad = Digest(USER, "falsch-falsch")
        bad.nonce = self.auth.nonce
        codes = [self.sim.request("GET", "/evse/state", auth=bad, retry=False).status
                 for _ in range(6)]
        self.assertEqual(codes, [401] * 5 + [429])
        r = self.sim.request("GET", "/evse/state", auth=self.auth)
        self.assertEqual(r.status, 429, r)
        self.assertGreaterEqual(int(r.headers["retry-after"]), 1)


class T2Faults(SimCase):
    """Fehlerpfade: Fehlerstrom, Ueberwachung, Werksreset."""

    def test_01_fehlerstrom_beim_laden(self):
        T2Faults.auth = self.set_credentials()
        self.shell("sim car C")
        self.sim.wait_state(self.auth, lambda s: s["charger_state"] == 3, what="Laden")
        ws = self.sim.ws_connect(self.auth)
        ws.recv_topic("evse/state")
        self.shell("sim rcm fault")
        try:
            err = ws.recv_topic("evse/event", lambda p: p["type"] == "error")
            self.assertTrue(any(n.startswith("RCM") for n in err["set"]), err)
            ws.recv_topic("evse/event", lambda p: p["type"] == "contactor" and not p["on"])
            ws.recv_topic("evse/event", lambda p: p["type"] == "state" and p["to"] == 9)
        finally:
            ws.close()
        # WARP-Code 3 = Fehlerstromueberwachung (Spezifikation 12.1)
        st = self.sim.wait_state(self.auth, lambda s: s["error_state"] == 3, what="RCM-Fehler",
                                 timeout=3)
        self.assertEqual(st["contactor_state"], 0)
        self.assertEqual(st["charger_state"], 4)
        self.assertIn("contactor=0", self.shell("sim status"))
        errs = self.sim.get_json("/evse/errors", self.auth)
        self.assertTrue(any(n.startswith("RCM") for n in errs["active"]), errs)
        self.shell("sim rcm clear")
        self.shell("sim car A")
        time.sleep(1)
        # Fehlerstrom bleibt gesperrt, bis ein Selbsttest besteht (Neustart, SOLL-PROT)
        st = self.sim.state(self.auth)
        self.assertEqual(st["contactor_state"], 0)

    def test_02_messung_haengt(self):
        boots = self.sim.boots()
        self.shell("sim hang")
        self.sim.wait_restart(boots)
        info = self.sim.get_json("/system/info", self.auth)
        self.assertEqual(info["reset_reason"], "supervision_limit")
        self.assertIn("SIM: Reset (supervision_limit)", self.sim.log_text())

    def test_03_werksreset_taster(self):
        self.sim.stop()
        self.sim.start(["--reset-button=6000"], timeout=20)
        r = self.sim.request("GET", "/credentials", auth=Digest(*FACTORY))
        self.assertEqual(r.status, 200, r)
        self.assertTrue(r.json()["must_change"])
        self.assertIn("Werksreset", self.sim.log_text())


class T3ZephyrTelnet(SimCase):
    """Telnet-Client bricht hart ab (RST), waehrend Shell-Ausgabe ansteht.

    Betrieb und Logausgabe muessen weiterlaufen.
    """

    def abort_telnet(self):
        tn = socket.create_connection(("127.0.0.1", 2323))
        tn.sendall(b"kernel thread list\r\n")  # viel Ausgabe ...
        time.sleep(0.05)
        tn.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, b"\x01\0\0\0\0\0\0\0")
        tn.close()                            # ... und harter Abbruch (RST)
        time.sleep(0.5)

    def test_01_betrieb_nach_telnet_abbruch(self):
        T3ZephyrTelnet.auth = auth = self.set_credentials()
        self.abort_telnet()
        self.assertEqual(self.sim.state(auth)["charger_state"], 0)
        ws = self.sim.ws_connect(auth)
        try:
            ws.recv_topic("evse/state")
        finally:
            ws.close()
        self.assertIn("car=A", self.sim.shell("sim status"))  # neue Telnet-Sitzung
        self.sim.close_shell()
        boots = self.sim.boots()
        self.assertEqual(self.sim.request("POST", "/system/restart", b"", auth=auth).status,
                         202)
        self.sim.wait_restart(boots, timeout=10)
        self.assertEqual(self.sim.get_json("/system/info", auth)["reset_reason"],
                         "user_restart")

    def test_02_log_nach_telnet_abbruch(self):
        self.abort_telnet()
        for _ in range(3):
            ws = self.sim.ws_connect(None)  # erzeugt je eine Warnung im Log
            ws.close()
        time.sleep(1)
        tail = self.sim.log_text().rsplit("Telnet socket", 1)[-1]
        self.assertEqual(tail.count("WebSocket ohne gueltige Anmeldung"), 3)


if __name__ == "__main__":
    unittest.main(verbosity=2)
