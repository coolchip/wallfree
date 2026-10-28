# SPDX-License-Identifier: Apache-2.0
"""Ende-zu-Ende-Test: echte Firmware (native_sim) ueber HTTP, WebSocket und Shell.

Aufruf ueber scripts/run-e2e.sh (baut vorher). Direkt:
    WALLFREE_SIM_EXE=<build>/zephyr/zephyr.exe python3 -m unittest -v tests/e2e/test_e2e.py

Die Testfaelle einer Klasse laufen in Namensreihenfolge gegen denselben
Prozess und bauen aufeinander auf. Die Hardware ist ein Modell (app/src/hw_sim.c);
die Tests belegen nichts ueber die reale Steuerung.
"""

import gzip
import json
import os
import re
import shutil
import socket
import struct
import subprocess
import tempfile
import threading
import time
import unittest
import urllib.request
import urllib.error
import zlib

import wfsim
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
    env = {}

    @classmethod
    def setUpClass(cls):
        if not os.path.exists(EXE):
            raise unittest.SkipTest(f"{EXE} fehlt (scripts/run-e2e.sh baut es)")
        cls.tmp = tempfile.mkdtemp(prefix="wallfree-e2e-")
        cls.sim = Sim(EXE, cls.tmp, free_port(), cls.extra_args, cls.env)
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
        self.assertEqual(r.json(), {"must_change": True, "user": "admin"})
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
        self.assertEqual(self.sim.get_json("/credentials", self.auth),
                         {"must_change": False, "user": USER})
        self.assertEqual(self.sim.request("GET", "/evse/state", auth=Digest(*FACTORY)).status,
                         401)
        self.assertEqual(self.sim.request("GET", "/evse/state", auth=self.auth).status, 200)

    def test_05_info_und_system(self):
        name = self.sim.get_json("/info/name", self.auth)
        self.assertEqual(name["type"], "warp2")
        self.assertEqual(self.sim.get_json("/info/features", self.auth), [])
        info = self.sim.get_json("/system/info", self.auth)
        self.assertEqual(info["reset_reason"], "power_on")
        with open(os.path.join(REPO, "VERSION")) as f:
            self.assertEqual(info["firmware"], f.readline().strip())
        self.assertRegex(info["build"], r"^[0-9a-f]{7,}(-dirty)?$")
        self.assertEqual(info["mcu_temp_dc"], 350)  # SOLL-API-35, feste Werte der Simulation
        self.assertEqual(info["adc_vref_mv"], 2495)
        cfg = self.sim.get_json("/system/config", self.auth)
        self.assertFalse(cfg["restart_required"])
        # Abgleich der Temperatur wirkt sofort, ohne Neustart
        for offset, temp in ((-25, 325), (0, 350)):
            r = self.sim.request("POST", "/system/config", {"mcu_temp_offset": offset},
                                 auth=self.auth)
            self.assertEqual(r.status, 200, r.body)
            self.assertEqual(self.sim.get_json("/system/info", self.auth)["mcu_temp_dc"], temp)
        self.assertFalse(self.sim.get_json("/system/config", self.auth)["restart_required"])
        self.assertIsInstance(self.sim.get_json("/system/config_info", self.auth), (list, dict))
        for path in ("/app.js", "/style.css", "/vendor.js"):
            r = self.sim.request("GET", path, auth=self.auth)
            self.assertEqual(r.status, 200, path)
            self.assertGreater(len(gzip.decompress(r.body)), 100, path)
        r = self.sim.request("GET", "/lizenzen.txt", auth=self.auth)
        self.assertEqual(r.status, 200)
        text = gzip.decompress(r.body).decode()
        for name in ("Apache License", "Zephyr", "Preact", "STMicroelectronics", "picolibc"):
            self.assertIn(name, text)

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
        # Schuetz Soll (C1/C2) und Ist (Spannung an CT): beide aus
        p = self.sim.get_json("/evse/params", self.auth)
        self.assertEqual((p["contactor"], p["ct_sense"]), (0, 0))
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
        p = self.sim.get_json("/evse/params", self.auth)
        self.assertEqual((p["contactor"], p["ct_sense"]), (1, 1))
        # Sollwert 80 A (F12, keine Einschraenkung), Drehschalter 6 = 32 A begrenzt
        lim = self.sim.get_json("/evse/limits", self.auth)
        self.assertEqual((lim["external"], lim["hardware"], lim["allowed"], lim["limited_by"]),
                         (80000, 32000, 31980, "hardware"))
        # SOLL-API-33: laufendes Intervall (Simulation < 10 min), ohne Zaehler keine Leistung
        time.sleep(1.5)
        h = self.sim.get_json("/evse/history", self.auth)
        self.assertEqual((h["interval_ms"], h["rows"]), (600000, []))
        power, allowed, flags = h["current"]
        self.assertIsNone(power)
        self.assertGreater(allowed, 0)
        self.assertEqual(flags & 3, 3, "angesteckt und geladen")

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

    def test_11a_ereignisprotokoll(self):
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

    def test_11a_ereignisprotokoll_loeschen(self):
        # SOLL-ERR-13: nur angemeldet; danach nur der Loescheintrag mit Absender, Nummern weiter
        self.assertEqual(self.sim.request("POST", "/system/log/clear", b"").status, 401)
        before = self.sim.get_json("/system/log", self.auth)["entries"]
        r = self.sim.request("POST", "/system/log/clear", b"", auth=self.auth)
        self.assertEqual(r.status, 200, r)
        entries = self.sim.get_json("/system/log", self.auth)["entries"]
        self.assertEqual(entries[0]["kind"], "cleared")
        self.assertEqual(entries[0]["addr"], "127.0.0.1")
        self.assertGreater(entries[0]["seq"], before[-1]["seq"])
        self.assertFalse(any(e["seq"] <= before[-1]["seq"] for e in entries))

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

    def test_11b_cp_aufnahme(self):
        # SOLL-API-34: Modell laedt (C), Plateau 6 V, negativ -12 V, Tastgrad wie im Zustand
        st = self.sim.state(self.auth)
        self.assertEqual(st["iec61851_state"], 2)
        d = self.sim.get_json("/evse/cp_scope", self.auth)
        self.assertEqual((d["t0_us"], d["dt_us"]), (970, -4))
        self.assertEqual(len(d["pos_mv"]), 111)
        self.assertEqual(len(d["neg_mv"]), 111)
        self.assertGreater(d["duty"], 0)
        self.assertLess(d["duty"], 1000)
        self.assertIn(6000, d["pos_mv"])
        self.assertIn(12000, d["neg_mv"])

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
        # SOLL-ERR-12: Protokoll uebersteht den Neustart, Startereignis mit Grund
        log = self.sim.get_json("/system/log", self.auth)
        self.assertGreaterEqual(log["boot"], 2)
        boots_seen = {e["boot"] for e in log["entries"]}
        self.assertIn(log["boot"] - 1, boots_seen, "Eintraege des vorigen Starts")
        starts = [e["event"] for e in log["entries"]
                  if e["kind"] == "event" and e["event"]["type"] == "boot"]
        self.assertEqual(starts[-1]["reason"], "user_restart")
        self.assertNotIn("detail", starts[-1])
        seqs = [e["seq"] for e in log["entries"]]
        self.assertEqual(seqs, sorted(seqs), "Nummern laufen weiter")
        self.assertIsNone(info["reset_detail"])

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
        # SOLL-RT-14/ERR-12: welche Ueberwachung, auch im Protokoll nach dem Neustart
        self.assertEqual(info["reset_detail"], "measurement_blocks")
        log = self.sim.get_json("/system/log", self.auth)["entries"]
        boot = [e["event"] for e in log if e["kind"] == "event" and e["event"]["type"] == "boot"]
        self.assertEqual((boot[-1]["reason"], boot[-1]["detail"]),
                         ("supervision_limit", "measurement_blocks"))

    def test_03_werksreset_taster(self):
        self.sim.stop()
        self.sim.start(["--reset-button=6000"], timeout=20)
        r = self.sim.request("GET", "/credentials", auth=Digest(*FACTORY))
        self.assertEqual(r.status, 200, r)
        self.assertTrue(r.json()["must_change"])
        self.assertIn("Werksreset", self.sim.log_text())


class T4Meter(SimCase):
    """Energiezaehler nur zur Anzeige (Soll-Spezifikation 12.5) mit simuliertem Zaehler."""

    def meter(self):
        ids = self.sim.get_json("/meters/0/value_ids", self.auth)
        vals = self.sim.get_json("/meters/0/values", self.auth)
        return dict(zip(ids, vals))

    def wait_meter(self, pred, timeout=25, what="Zaehlerzustand"):
        t0 = time.monotonic()
        last = None
        while time.monotonic() - t0 < timeout:
            last = self.sim.get_json("/meters/0/state", self.auth)
            if pred(last):
                return last
            time.sleep(0.2)
        raise AssertionError(f"{what} nicht erreicht, zuletzt {last}")

    def test_01_ohne_zaehler(self):
        T4Meter.auth = self.set_credentials()
        self.assertEqual(self.sim.get_json("/info/features", self.auth), [])
        self.assertEqual(self.sim.request("GET", "/meters/0/values", auth=self.auth).status, 404)
        self.assertIn("kein Zaehler", self.shell("evse meter"))
        self.assertEqual(self.sim.get_json("/system/config", self.auth)["active"]["meter_type"], 0)

    def test_02_konfigurieren(self):
        r = self.sim.request("POST", "/system/config", {"meter_type": 9}, auth=self.auth)
        self.assertEqual(r.status, 400, r)
        self.assertIn("meter_type", r.json()["errors"])
        r = self.sim.request("POST", "/system/config",
                             {"meter_type": 2, "meter_address": 5, "meter_baud": 19200},
                             auth=self.auth)
        self.assertEqual(r.status, 200, r)
        self.assertTrue(r.json()["restart_required"])
        self.assertIn("gespeichert", self.shell("evse sys set meter_framing 0"))
        boots = self.sim.boots()
        self.assertEqual(self.sim.request("POST", "/system/restart", b"", auth=self.auth).status,
                         202)
        self.sim.wait_restart(boots)
        self.assertEqual(self.sim.get_json("/info/features", self.auth), ["meters"])
        self.assertEqual(self.sim.get_json("/meters/0/value_ids", self.auth),
                         [1, 2, 3, 13, 17, 21, 39, 48, 57, 74, 364, 209, 211])
        st = self.wait_meter(lambda s: s["status"] == "ok")
        self.assertEqual(st["state"], 2)
        self.assertEqual(st["type"], 2)
        m = self.meter()
        self.assertAlmostEqual(m[1], 230.0, places=1)
        self.assertEqual(m[13], 0.0)
        self.assertEqual(m[74], 0.0)
        self.assertAlmostEqual(m[364], 50.0, places=2)
        self.assertGreater(m[209], 1000.0)

    def test_03_laden_mit_istwert(self):
        self.shell("sim car C")
        st = self.sim.wait_state(self.auth, lambda s: s["charger_state"] == 3, what="Laden")
        allowed = st["allowed_charging_current"] / 1000
        time.sleep(2.5)
        m = self.meter()
        e0 = m[209]
        self.assertAlmostEqual(m[13], allowed, delta=0.1)
        self.assertAlmostEqual(m[74], m[39] + m[48] + m[57], delta=0.01)
        self.assertGreater(m[74], 3 * 230 * allowed * 0.95)
        time.sleep(3)
        self.assertGreater(self.meter()[209], e0, "Zaehlerstand steigt beim Laden")
        out = self.shell("evse meter")
        self.assertIn("Eastron SDM630", out)
        self.assertIn("Zustand ok", out)
        # SOLL-API-33: Mittel enthaelt die Zeit vor dem Laden, daher nur > 0
        power = self.sim.get_json("/evse/history", self.auth)["current"][0]
        self.assertGreater(power, 0)
        self.assertLess(power, m[74] * 1.05)

    def test_04_websocket(self):
        ws = self.sim.ws_connect(self.auth)
        try:
            self.assertEqual(ws.recv_topic("meters/0/value_ids")[0], 1)
            v1 = ws.recv_topic("meters/0/values")
            st = ws.recv_topic("meters/0/state")
            # nach jedem Zyklus erneut, auch ohne Aenderung (SOLL-MTR-30)
            ws.recv_topic("meters/0/state", lambda p: p["sets"] > st["sets"], timeout=3)
            self.assertEqual(len(v1), 13)
        finally:
            ws.close()

    def test_05_zaehlerausfall_laden_unberuehrt(self):
        ws = self.sim.ws_connect(self.auth)
        try:
            ws.recv_topic("evse/state")
            self.shell("sim meter off")
            ev = ws.recv_topic("evse/event", lambda p: p["type"] == "meter", timeout=25)
            self.assertEqual(ev["status"], "error")
        finally:
            ws.close()
        st = self.sim.get_json("/meters/0/state", self.auth)
        self.assertEqual(st["state"], 1)
        self.assertGreaterEqual(st["timeouts"], 9)
        # SOLL-MTR-03: kein Einfluss auf die Ladelogik
        s = self.sim.state(self.auth)
        self.assertEqual(s["charger_state"], 3)
        self.assertEqual(s["error_state"], 0)
        self.assertEqual(self.sim.get_json("/evse/errors", self.auth)["active"], [])
        # nach 10 s ohne Satz ungueltig (SOLL-MTR-12)
        self.wait_meter(lambda s: s["age_ms"] > 10000, timeout=10)
        self.assertTrue(all(v is None for v in self.meter().values()))
        self.shell("sim meter ok")
        self.wait_meter(lambda s: s["status"] == "ok", timeout=15)
        self.assertIsNotNone(self.meter()[74])

    def test_06_stoerungen_gezaehlt(self):
        st0 = self.sim.get_json("/meters/0/state", self.auth)
        self.shell("sim meter frame")
        self.wait_meter(lambda s: s["frame_errors"] >= st0["frame_errors"] + 3, timeout=5)
        self.shell("sim meter bad")
        self.wait_meter(lambda s: s["implausible"] >= st0["implausible"] + 1, timeout=5)
        self.shell("sim meter flaky")
        self.wait_meter(lambda s: s["timeouts"] > st0["timeouts"] and s["sets"] > st0["sets"],
                        timeout=10, what="Saetze trotz jeder zweiten Antwort")
        self.shell("sim meter ok")
        self.wait_meter(lambda s: s["status"] == "ok", timeout=15)

    def test_07_zaehlerstand_sinkt(self):
        ws = self.sim.ws_connect(self.auth)
        try:
            ws.recv_topic("evse/state")
            self.shell("sim meter energy 5000")
            ev = ws.recv_topic("evse/event", lambda p: p["type"] == "meter", timeout=5)
            self.assertTrue(ev.get("energy_decreased"), ev)
        finally:
            ws.close()
        self.assertLess(self.meter()[209], 6.0)
        self.shell("sim car A")

    def test_08_wieder_ohne_zaehler(self):
        r = self.sim.request("POST", "/system/config", {"meter_type": 0}, auth=self.auth)
        self.assertEqual(r.status, 200, r)
        boots = self.sim.boots()
        self.sim.request("POST", "/system/restart", b"", auth=self.auth)
        self.sim.wait_restart(boots)
        self.assertEqual(self.sim.get_json("/info/features", self.auth), [])
        self.assertEqual(self.sim.request("GET", "/meters/0/state", auth=self.auth).status, 404)


class T3ZephyrTelnet(SimCase):
    """Telnet-Client bricht hart ab (RST), waehrend Shell-Ausgabe ansteht.

    Betrieb und Logausgabe muessen weiterlaufen.
    """

    def abort_telnet(self):
        tn = socket.create_connection(("127.0.0.1", wfsim.TELNET_PORT))
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

    def test_03_telnet_aus_port_zu(self):
        # SOLL-SH-19: telnet_enable 0 schliesst Port 23 nach dem Neustart, 1 oeffnet ihn wieder
        auth = T3ZephyrTelnet.auth
        r = self.sim.request("POST", "/system/config", {"telnet_enable": False}, auth=auth)
        self.assertEqual(r.status, 200, r)
        boots = self.sim.boots()
        self.sim.close_shell()
        self.assertEqual(self.sim.request("POST", "/system/restart", b"", auth=auth).status, 202)
        t0 = time.monotonic()
        while self.sim.boots() <= boots or not self.sim.port_open(self.sim.http_port):
            self.assertLess(time.monotonic() - t0, 15, "kein Neustart")
            time.sleep(0.1)
        t0 = time.monotonic()
        while self.sim.port_open(wfsim.TELNET_PORT):
            self.assertLess(time.monotonic() - t0, 5, "Telnet-Port bleibt offen")
            time.sleep(0.1)
        self.assertIn("geschlossen (telnet_enable = 0)", self.sim.log_text().rsplit("*** Booting Zephyr", 1)[-1])
        self.assertEqual(self.sim.state(auth)["charger_state"], 0)  # HTTP laeuft weiter
        r = self.sim.request("POST", "/system/config", {"telnet_enable": True}, auth=auth)
        self.assertEqual(r.status, 200, r)
        boots = self.sim.boots()
        self.assertEqual(self.sim.request("POST", "/system/restart", b"", auth=auth).status, 202)
        self.sim.wait_restart(boots, timeout=15)  # wartet auch auf den Telnet-Port


class Broker:
    """Mosquitto auf 127.0.0.1 und ein Abonnent fuer <prefix>/#; Zeilen "<retain> <topic> <payload>"."""

    def __init__(self, port):
        self.port = port
        self.proc = None
        self.lines = []
        self.lock = threading.Lock()

    def start(self):
        self.proc = subprocess.Popen(["mosquitto", "-p", str(self.port)],
                                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        t0 = time.monotonic()
        while time.monotonic() - t0 < 5:
            try:
                socket.create_connection(("127.0.0.1", self.port), timeout=0.2).close()
                return
            except OSError:
                time.sleep(0.05)
        raise AssertionError("Mosquitto startet nicht")

    def stop(self):
        if self.proc is not None:
            self.proc.terminate()
            self.proc.wait(5)
            self.proc = None

    def subscribe(self, topic):
        """Neuer Abonnent; liefert gespeicherte Nachrichten mit retain = 1."""
        with self.lock:
            self.lines = []
        p = subprocess.Popen(["mosquitto_sub", "-p", str(self.port), "-t", topic, "-F",
                              "%r %t %p"], stdout=subprocess.PIPE, text=True)

        def reader():
            for line in p.stdout:
                with self.lock:
                    self.lines.append(line.rstrip("\n").split(" ", 2))

        threading.Thread(target=reader, daemon=True).start()
        return p

    def wait(self, topic, pred=lambda r, p: True, timeout=10):
        """Erste passende Nachricht seit dem Abonnieren: (retain, payload)."""
        t0 = time.monotonic()
        seen = 0
        while time.monotonic() - t0 < timeout:
            with self.lock:
                new = self.lines[seen:]
                seen = len(self.lines)
            for r, t, p in new:
                if t == topic and pred(r == "1", p):
                    return r == "1", p
            time.sleep(0.05)
        raise AssertionError(f"{topic} nicht empfangen, zuletzt {self.lines[-5:]}")

    def publish(self, topic, payload, retain=False):
        subprocess.run(["mosquitto_pub", "-p", str(self.port), "-t", topic, "-m", payload] +
                       (["-r"] if retain else []), check=True)


@unittest.skipUnless(shutil.which("mosquitto") and shutil.which("mosquitto_sub"),
                     "Mosquitto fehlt")
class T5Mqtt(SimCase):
    """MQTT-Client (Soll-Spezifikation 12.6) gegen einen lokalen Mosquitto."""

    PREFIX = "e2e/wb"

    @classmethod
    def setUpClass(cls):
        cls.broker = Broker(free_port())
        cls.broker.start()
        cls.sub = None
        super().setUpClass()

    @classmethod
    def tearDownClass(cls):
        super().tearDownClass()
        if cls.sub is not None:
            cls.sub.terminate()
        cls.broker.stop()

    def resubscribe(self):
        if T5Mqtt.sub is not None:
            T5Mqtt.sub.terminate()
        T5Mqtt.sub = self.broker.subscribe(self.PREFIX + "/#")

    def mqtt_state(self, pred, timeout=20, what="MQTT-Zustand"):
        t0 = time.monotonic()
        last = None
        while time.monotonic() - t0 < timeout:
            last = self.sim.get_json("/mqtt/state", self.auth)
            if pred(last):
                return last
            time.sleep(0.1)
        raise AssertionError(f"{what} nicht erreicht, zuletzt {last}")

    def config(self, body, status=200):
        r = self.sim.request("POST", "/system/config", body, auth=self.auth)
        self.assertEqual(r.status, status, r)
        return r.json()

    def test_01_aus(self):
        T5Mqtt.auth = self.set_credentials()
        st = self.sim.get_json("/mqtt/state", self.auth)
        self.assertEqual(st["connection_state"], 0)  # SOLL-MQTT-01
        self.assertTrue(re.fullmatch(r"wallfree/[0-9a-f]{6}", st["prefix"]), st)
        self.assertIn("aus", self.shell("evse mqtt"))

    def test_02_nur_mit_broker(self):
        self.assertIn("Broker", self.config({"mqtt_enable": True}, 400)["error"])
        self.assertIn("mqtt_prefix", self.config({"mqtt_prefix": "a/#"}, 400)["errors"])

    def test_03_verbinden_sofort(self):
        # SOLL-MQTT-31: ohne Neustart; Passwort nie lesbar (SOLL-MQTT-02)
        r = self.config({"mqtt_enable": True, "mqtt_broker": "127.0.0.1",
                         "mqtt_port": self.broker.port, "mqtt_prefix": self.PREFIX,
                         "mqtt_password": "geheim"})
        self.assertFalse(r["restart_required"])
        self.assertIsNone(self.sim.get_json("/system/config", self.auth)["saved"]["mqtt_password"])
        st = self.mqtt_state(lambda s: s["connection_state"] == 2)
        self.assertEqual(st["prefix"], self.PREFIX)
        time.sleep(0.5)  # Vollversand im ersten Takt nach dem Verbinden
        # neuer Abonnent sieht die gespeicherten Zustaende (SOLL-MQTT-04/10)
        self.resubscribe()
        self.assertEqual(self.broker.wait(self.PREFIX + "/online"), (True, "true"))
        # zuerst kann eine Live-Meldung kommen (Anlauf S0 -> S1)
        _, p = self.broker.wait(self.PREFIX + "/evse/state", lambda r, p: r)
        self.assertEqual(self.broker.wait(self.PREFIX + "/evse/external_current"),
                         (True, '{"current":80000}'))
        self.assertEqual(json.loads(p).keys(), self.sim.state(self.auth).keys())
        self.broker.wait(self.PREFIX + "/evse/limits", lambda r, p: r)

    def test_04_aenderung_und_ereignis(self):
        self.shell("sim car B")
        _, p = self.broker.wait(self.PREFIX + "/evse/state",
                                lambda r, p: json.loads(p)["iec61851_state"] == 1)
        retained, p = self.broker.wait(self.PREFIX + "/evse/event")
        self.assertFalse(retained)
        self.assertIn("seq", json.loads(p))

    def test_05_befehle_aus(self):
        self.broker.publish(self.PREFIX + "/evse/external_current_update", "10000")
        time.sleep(1.5)
        self.assertEqual(self.sim.get_json("/evse/external_current", self.auth)["current"], 80000)
        self.assertEqual(self.sim.get_json("/mqtt/state", self.auth)["commands"], 0)

    def test_06_befehle(self):
        self.assertIn("wirkt sofort", self.shell("evse sys set mqtt_commands 1"))
        self.mqtt_state(lambda s: s["connection_state"] == 2 and s["connection_start"] > 0)
        time.sleep(0.5)  # Abo bestaetigt
        topic = self.PREFIX + "/evse/external_current_update"
        self.broker.publish(topic, "10000")
        self.broker.wait(self.PREFIX + "/evse/external_current",
                         lambda r, p: json.loads(p)["current"] == 10000)
        self.broker.publish(topic, '{"current": 12000}')
        self.broker.wait(self.PREFIX + "/evse/external_current",
                         lambda r, p: json.loads(p)["current"] == 12000)
        self.broker.publish(topic, "3000")  # SOLL-API-05: 1..5999 abgelehnt
        st = self.mqtt_state(lambda s: s["rejected"] == 1)
        self.assertIn("Bereich", st["last_reject"])
        self.assertEqual(self.sim.get_json("/evse/external_current", self.auth)["current"], 12000)

    def test_06b_home_assistant(self):
        # SOLL-MQTT-40/41: Erkennung retained unter homeassistant/, Ladestrom nur mit Befehlen
        node = "wallfree_000000"  # Simulation ohne MAC
        T5Mqtt.sub.terminate()
        T5Mqtt.sub = self.broker.subscribe("homeassistant/#")
        retained, p = self.broker.wait(f"homeassistant/sensor/{node}/state/config")
        self.assertTrue(retained)
        cfg = json.loads(p)
        self.assertEqual(cfg["~"], self.PREFIX)
        self.assertEqual(cfg["stat_t"], "~/evse/state")
        self.assertEqual(cfg["avty_t"], "~/online")
        self.assertEqual(cfg["dev"]["ids"], [node])
        _, p = self.broker.wait(f"homeassistant/number/{node}/charge_current/config")
        self.assertEqual(json.loads(p)["cmd_t"], "~/evse/external_current_update")
        time.sleep(0.5)
        topics = {t for _, t, _ in self.broker.lines}
        self.assertNotIn(f"homeassistant/sensor/{node}/power/config", topics, "ohne Zaehler")
        # ausgeschaltet: leere retained Nachrichten entfernen die Elemente
        self.config({"mqtt_discovery": False})
        self.mqtt_state(lambda s: s["connection_state"] == 2)
        time.sleep(1)
        T5Mqtt.sub.terminate()
        T5Mqtt.sub = self.broker.subscribe("homeassistant/#")
        time.sleep(1)
        self.assertEqual([x for x in self.broker.lines if x[2:] and x[2]], [], "nichts mehr gespeichert")
        self.config({"mqtt_discovery": True})
        self.mqtt_state(lambda s: s["connection_state"] == 2)
        self.resubscribe()

    def test_07_gespeicherter_befehl_ignoriert(self):
        # SOLL-MQTT-22: retained beim Abonnieren ausgeliefert -> ignoriert
        topic = self.PREFIX + "/evse/external_current_update"
        self.broker.publish(topic, "14000", retain=True)  # laufendes Abo: ohne Retain-Kennung
        self.broker.wait(self.PREFIX + "/evse/external_current",
                         lambda r, p: json.loads(p)["current"] == 14000)
        self.sim.request("POST", "/evse/external_current", 9000, auth=self.auth)
        n = self.sim.get_json("/mqtt/state", self.auth)["rejected"]
        self.config({"mqtt_interval": 2})  # neu verbinden, Broker liefert die gespeicherte Nachricht
        st = self.mqtt_state(lambda s: s["rejected"] == n + 1, what="retained ignoriert")
        self.assertIn("retained", st["last_reject"])
        self.assertEqual(self.sim.get_json("/evse/external_current", self.auth)["current"], 9000)
        self.broker.publish(topic, "", retain=True)  # gespeicherte Nachricht loeschen

    def test_08_broker_neustart(self):
        # SOLL-MQTT-01/03: Ausfall beruehrt das Laden nicht, Neuverbindung
        self.broker.stop()
        self.mqtt_state(lambda s: s["connection_state"] == 1 and s["connection_end"] > 0)
        self.assertEqual(self.sim.state(self.auth)["iec61851_state"], 1)
        self.broker.start()
        self.mqtt_state(lambda s: s["connection_state"] == 2, timeout=30, what="neu verbunden")
        self.resubscribe()
        self.assertEqual(self.broker.wait(self.PREFIX + "/online"), (True, "true"))

    def test_09_aus_und_last_will(self):
        # geordnet aus: online = false gesendet (SOLL-MQTT-04), Zustand 0
        self.config({"mqtt_enable": False})
        self.broker.wait(self.PREFIX + "/online", lambda r, p: p == "false")
        self.mqtt_state(lambda s: s["connection_state"] == 0)
        self.config({"mqtt_enable": True})
        self.broker.wait(self.PREFIX + "/online", lambda r, p: p == "true")
        # harter Abbruch: der Broker sendet den Last Will
        self.sim.proc.kill()
        self.sim.proc.wait()
        self.broker.wait(self.PREFIX + "/online", lambda r, p: p == "false")


class T6Update(SimCase):
    """Update-Uebergabe an den Bootloader (SOLL-BOOT-30..33, SOLL-SH-10) mit dem
    Ersatz-Bootloader scripts/bootloader-sim.py."""

    env = {"WALLFREE_SIM_BOOTLOADER": os.path.join(REPO, "scripts/bootloader-sim.py"),
           "WALLFREE_SIM_BOOTLOADER_WAIT": "8"}

    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls.out = os.path.join(cls.tmp, "upload.bin")
        cls.sim.env["WALLFREE_SIM_BOOTLOADER_OUT"] = cls.out
        cls.sim.stop()
        cls.sim.start()

    def image(self, sp=0x20020000, size=100000):
        """Testabbild mit Anhang wie scripts/image-trailer.py (SOLL-BOOT-35)"""
        body = bytes((i * 7 + 3) & 0xFF for i in range(size)) + b"WALLFREE-IMAGE:test\0"
        data = struct.pack("<II", sp, 0x08010201) + body
        return data + struct.pack("<IIII", 0x474D4957, len(data), zlib.crc32(data),
                                  ~0x474D4957 & 0xFFFFFFFF)

    def update(self, data, *args):
        path = os.path.join(self.tmp, "image.bin")
        with open(path, "wb") as f:
            f.write(data)
        return subprocess.run(
            ["sh", os.path.join(REPO, "scripts/update.sh"), *args,
             f"127.0.0.1:{self.sim.http_port}", path],
            capture_output=True, text=True, timeout=150)

    def update_events(self):
        log = self.sim.get_json("/system/log", self.auth)["entries"]
        return [x["event"] for x in log if x["kind"] == "event" and x["event"]["type"] == "update"]

    def test_01_ueber_api(self):
        T6Update.auth = self.set_credentials()
        data = self.image()
        r = self.update(data, "-u", f"{USER}:{PASSWORD}")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        with open(self.out, "rb") as f:
            self.assertEqual(f.read(), data, "Bootloader erhielt genau die Datei")
        info = self.sim.get_json("/system/info", self.auth)
        self.assertEqual(info["reset_reason"], "update")
        self.assertEqual(info["update_result"], {"mode": 2, "ok": True})
        self.assertEqual(info["image"], {"crc": f"{zlib.crc32(data[:-16]):08x}", "ok": True})
        self.assertEqual(len(self.update_events()), 1)
        self.assertTrue(self.update_events()[0]["ok"])
        self.assertIn("bootloader-sim: 100044 Byte empfangen, gueltig", self.sim.log_text())

    def test_02_shell_und_ungueltiges_abbild(self):
        boots = self.sim.boots()
        out = self.shell("evse bootloader")
        self.assertIn("binnen 2 min", out)
        r = self.update(self.image(sp=0xFFFFFFFF), "--upload-only")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.sim.wait_restart(boots)
        info = self.sim.get_json("/system/info", self.auth)
        self.assertEqual(info["update_result"], {"mode": 2, "ok": False})
        self.assertFalse(self.update_events()[0]["ok"])

    def test_03_ohne_upload_und_doppelt(self):
        boots = self.sim.boots()
        r = self.sim.request("POST", "/system/update", b"", auth=self.auth)
        self.assertEqual(r.status, 202, r)
        self.assertIn("wait_s", r.json())
        self.assertEqual(self.sim.request("POST", "/system/update", b"", auth=self.auth).status,
                         409)
        self.sim.wait_restart(boots, timeout=40)  # Zeitablauf im Ersatz-Bootloader: 8 s
        info = self.sim.get_json("/system/info", self.auth)
        self.assertEqual(info["update_result"], {"mode": 2, "ok": False})

    def test_04_waehrend_laden(self):
        self.shell("sim car C")
        self.sim.wait_state(self.auth, lambda s: s["charger_state"] == 3, what="Laden")
        boots = self.sim.boots()
        r = self.update(self.image(), "-u", f"{USER}:{PASSWORD}")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.sim.wait_restart(boots)
        log = self.sim.log_text()
        # geordnetes Ende (SOLL-BOOT-30): Schuetz aus vor der Uebergabe
        self.assertIn("Uebergabe an den Bootloader", log)
        self.assertNotIn("nach Fristablauf", log)
        self.sim.wait_up()
        time.sleep(1)  # Telnet-Shell direkt nach dem Neustart noch nicht bereit
        self.shell("sim car A")

    def raw_upload(self, data, filler=700):
        """Upload wie ein Browser: viele Kopfzeilen nach Content-Length (filler Byte)."""
        b = b"----WebKitFormBoundaryTEST"
        body = (b"--" + b + b'\r\nContent-Disposition: form-data; name="datafile"; '
                b'filename="x.bin"\r\nContent-Type: application/octet-stream\r\n\r\n' + data +
                b"\r\n--" + b + b"--\r\n")
        head = (f"POST /upload.cgi HTTP/1.1\r\nHost: x\r\nContent-Length: {len(body)}\r\n"
                f"X-Filler: {'x' * filler}\r\nContent-Type: multipart/form-data; "
                f"boundary={b.decode()}\r\n\r\n").encode()
        return head + body

    def wait_bootloader(self, timeout=30):
        t0 = time.monotonic()
        while time.monotonic() - t0 < timeout:
            try:
                with urllib.request.urlopen(f"http://127.0.0.1:{self.sim.http_port}/system/info",
                                            timeout=1) as r:
                    if not r.read(1).startswith(b"{"):
                        return
            except urllib.error.HTTPError:
                pass  # 401 = Wallfree
            except OSError:
                pass
            time.sleep(0.3)
        self.fail("Bootloader antwortet nicht")

    def send(self, req):
        with socket.create_connection(("127.0.0.1", self.sim.http_port), timeout=30) as c:
            c.sendall(req)
            try:
                c.recv(1000)
            except OSError:
                pass

    def test_05_abgeschnitten(self):
        """Erstes Paket mit Daten und Content-Length nicht zuletzt: der Bootloader endet zu frueh
        und meldet trotzdem Erfolg; Wallfree erkennt das Abbild als ungueltig und sperrt das
        Laden (SOLL-BOOT-35)."""
        self.sim.stop()
        self.sim.env["WALLFREE_SIM_BOOTLOADER_SEG"] = "mss"
        self.sim.start()
        size = 100000
        while True:  # Endmarke ganz im letzten Paket, das kuerzer ist als die zu viel
            data = self.image(size=size)  # gezaehlten Kopfzeilen
            if 60 < len(self.raw_upload(data)) % 1426 < 300:
                break
            size += 4
        boots = self.sim.boots()
        self.assertEqual(self.sim.request("POST", "/system/update", b"", auth=self.auth).status,
                         202)
        self.wait_bootloader()
        self.send(self.raw_upload(data))
        self.sim.wait_restart(boots, timeout=40)
        with open(self.out, "rb") as f:
            got = f.read()
        self.assertLess(len(got), len(data), "abgeschnitten wie im Original")
        self.assertEqual(got, data[:len(got)])
        info = self.sim.get_json("/system/info", self.auth)
        self.assertEqual(info["update_result"], {"mode": 2, "ok": True}, "Bootloader: Erfolg")
        self.assertFalse(info["image"]["ok"])
        self.assertIn("INTERNAL", self.sim.get_json("/evse/errors", self.auth)["active"])
        self.sim.wait_up()
        time.sleep(1)  # Telnet-Shell direkt nach dem Neustart noch nicht bereit
        self.shell("sim car C")
        time.sleep(3)
        self.assertEqual(self.sim.state(self.auth)["contactor_state"], 0, "kein Laden")
        self.shell("sim car A")
        self.sim.env.pop("WALLFREE_SIM_BOOTLOADER_SEG")

    def test_06_get_bricht_upload_ab(self):
        """Wie am Testgeraet: eine andere Anfrage waehrend des Uploads bricht ihn ab, der Bereich ist
        geloescht, der Bootloader bleibt im Update-Betrieb; ein neuer Upload stellt her."""
        self.sim.stop()
        self.sim.env["WALLFREE_SIM_BOOTLOADER_RATE"] = "20000"  # 100 KB etwa 5 s
        self.sim.start()
        data = self.image()
        boots = self.sim.boots()
        self.assertEqual(self.sim.request("POST", "/system/update", b"", auth=self.auth).status,
                         202)
        self.wait_bootloader()
        up = threading.Thread(target=self.send, args=(self.raw_upload(data, filler=0),))
        up.start()
        time.sleep(4)  # Loeschen 2,3 s, dann laufen Daten
        urllib.request.urlopen(f"http://127.0.0.1:{self.sim.http_port}/system/info",
                               timeout=5).read()
        up.join()
        self.assertIn("Upload abgebrochen", self.sim.log_text())
        time.sleep(1)
        self.assertEqual(self.sim.boots(), boots, "keine Firmware gestartet")
        r = self.update(data, "--upload-only")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.sim.wait_restart(boots, timeout=40)
        info = self.sim.get_json("/system/info", self.auth)
        self.assertEqual(info["update_result"], {"mode": 2, "ok": True})
        self.assertTrue(info["image"]["ok"])
        self.sim.env.pop("WALLFREE_SIM_BOOTLOADER_RATE")

    def test_07_ohne_bootloader(self):
        self.sim.stop()
        self.sim.env.pop("WALLFREE_SIM_BOOTLOADER")
        self.sim.start()
        boots = self.sim.boots()
        r = self.sim.request("POST", "/system/update", b"", auth=self.auth)
        self.assertEqual(r.status, 202, r)
        self.sim.wait_restart(boots)
        info = self.sim.get_json("/system/info", self.auth)
        self.assertEqual(info["reset_reason"], "update")
        self.assertEqual(info["update_result"], {"mode": 2, "ok": False})


if __name__ == "__main__":
    unittest.main(verbosity=2)


class FakeNtp(threading.Thread):
    """Minimaler SNTP-Server (RFC 4330) auf 127.0.0.1, antwortet mit fester Uhrzeit."""

    NTP_UNIX = 2208988800

    def __init__(self, unix_s):
        super().__init__(daemon=True)
        self.unix_s = unix_s
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", 0))
        self.port = self.sock.getsockname()[1]
        self.queries = 0

    def run(self):
        while True:
            try:
                req, peer = self.sock.recvfrom(512)
            except OSError:
                return
            if len(req) < 48:
                continue
            self.queries += 1
            ts = struct.pack(">II", self.unix_s + self.NTP_UNIX, 0)
            # LI 0, VN 4, Modus 4 (Server); Stratum 1; Originate = Transmit der Anfrage
            resp = bytes([0x24, 1, 6, 0xEC]) + bytes(20) + req[40:48] + ts + ts
            self.sock.sendto(resp, peer)

    def stop(self):
        self.sock.close()


class T7Zeit(SimCase):
    """Uhrzeit per SNTP (SOLL-TIME-01..04) gegen einen lokalen Zeitserver."""

    UNIX = 1800000000  # 2027-01-15

    @classmethod
    def setUpClass(cls):
        cls.ntp = FakeNtp(cls.UNIX)
        cls.ntp.start()
        super().setUpClass()

    @classmethod
    def tearDownClass(cls):
        super().tearDownClass()
        cls.ntp.stop()

    def test_01_ohne_zeitserver(self):
        T7Zeit.auth = self.set_credentials()
        info = self.sim.get_json("/system/info", self.auth)
        self.assertIsNone(info["time"], "pool.ntp.org ist in der Simulation nicht aufloesbar")
        cfg = self.sim.get_json("/system/config", self.auth)["active"]
        self.assertTrue(cfg["ntp_enable"])
        self.assertEqual(cfg["ntp_server"], "")

    def test_02_zeitserver_einstellen(self):
        r = self.sim.request("POST", "/system/config",
                             {"ntp_server": f"127.0.0.1:{self.ntp.port}"}, auth=self.auth)
        self.assertEqual(r.status, 200, r)
        self.assertFalse(r.json()["restart_required"], "wirkt sofort")
        t0 = time.monotonic()
        info = None
        while time.monotonic() - t0 < 10:
            info = self.sim.get_json("/system/info", self.auth)
            if info["time"]:
                break
            time.sleep(0.2)
        self.assertIsNotNone(info["time"], "kein Abgleich")
        self.assertAlmostEqual(info["time"]["time_ms"] / 1000, self.UNIX, delta=30)
        self.assertEqual(info["time"]["server"], "127.0.0.1")
        # neue Protokolleintraege tragen die Uhrzeit (SOLL-ERR-12)
        bad = Digest(USER, "falsch-falsch")
        bad.nonce = self.auth.nonce
        self.assertEqual(self.sim.request("GET", "/evse/state", auth=bad, retry=False).status, 401)
        log = self.sim.get_json("/system/log", self.auth)["entries"]
        self.assertEqual(log[0]["event"]["type"], "boot")
        # vor dem Abgleich entstanden, nachgetragen: Uhrzeit = Abgleich - Laufzeit + t_ms
        self.assertAlmostEqual(log[0]["wall_ms"] / 1000, self.UNIX, delta=30,
                               msg="Eintrag vor dem Abgleich nachgetragen")
        self.assertEqual(log[-1]["kind"], "auth_fail")
        self.assertGreater(log[-1].get("wall_ms", 0) / 1000, self.UNIX - 30)

    def test_03_abschalten(self):
        r = self.sim.request("POST", "/system/config", {"ntp_enable": False}, auth=self.auth)
        self.assertEqual(r.status, 200, r)
        t0 = time.monotonic()
        while time.monotonic() - t0 < 5 and self.sim.get_json("/system/info", self.auth)["time"]:
            time.sleep(0.2)
        self.assertIsNone(self.sim.get_json("/system/info", self.auth)["time"])

    def test_04_ungueltiger_server(self):
        r = self.sim.request("POST", "/system/config", {"ntp_server": "a b"}, auth=self.auth)
        self.assertEqual(r.status, 400, r)
