#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Nachbildung eines Energiezaehlers als Modbus-RTU-Slave am USB-RS-485-Adapter.

Prueft die Zaehleranbindung der Steuerung ohne echten Zaehler: --model sdm wie Eastron
SDM72D-M V2 / SDM630 (meter_type = 3 bzw. 2), --model abb wie ABB B23 (meter_type = 1).
  scripts/meter-emu.py [--model sdm] [--port /dev/ttyUSB0] [--addr 1] [--baud 9600] [--parity N]
                       [--ctl datei] [--power W] [--check]
Ohne --port geht der Zugriff ueber libftdi (FT232R, kein Kernel-Modul noetig).
--ctl: Datei, die zur Laufzeit die Betriebsart enthaelt: ok, silent, badcrc, exception, nan.
--check: nur Protokollteil pruefen, ohne Adapter.
"""
import argparse
import math
import struct
import sys
import time

FRAME_LEN = 8  # Anfrage Funktion 03/04: Adresse, Funktion, Start, Anzahl, CRC


def crc16(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return struct.pack("<H", crc)


def put32(out, reg, u):
    out[reg] = u >> 16
    out[reg + 1] = u & 0xFFFF


class Meter:
    """Messwerte; die Energie laeuft mit der Leistung. Unterklassen bilden die Register."""

    def __init__(self, power_w):
        self.u = 230.0
        self.f = 50.0
        self.p = power_w
        self.kwh = 1234.5
        self.t = time.monotonic()

    def step(self):
        now = time.monotonic()
        self.kwh += self.p * (now - self.t) / 3.6e6
        self.t = now


class Sdm(Meter):
    """Funktion 04, Float32, ungueltig = NaN (wie sdm_blk/sdm_val in lib/meter/meter.c)."""

    fc = 4
    blocks = [(0x0000, 18), (0x0034, 24)]

    def regs(self, nan=False):
        self.step()
        i = self.p / (3 * self.u)
        vals = {0x00: self.u, 0x02: self.u + 1, 0x04: self.u - 1, 0x06: i, 0x08: i, 0x0A: i,
                0x0C: self.p / 3, 0x0E: self.p / 3, 0x10: self.p / 3, 0x34: self.p,
                0x46: self.f, 0x48: self.kwh, 0x4A: 0.0}
        out = {}
        for reg, v in vals.items():
            put32(out, reg, struct.unpack(">I", struct.pack(">f", math.nan if nan else v))[0])
        return out


class Abb(Meter):
    """Funktion 03, ganzzahlig, ungueltig = Hoechstwert (wie abb_blk/abb_val in lib/meter/meter.c)."""

    fc = 3
    blocks = [(0x5B00, 28), (0x5000, 8), (0x5B2C, 1)]

    def regs(self, nan=False):
        self.step()
        i = self.p / (3 * self.u)
        out = {}
        for k, v in enumerate((self.u, self.u + 1, self.u - 1)):
            put32(out, 0x5B00 + 2 * k, 0xFFFFFFFF if nan else round(v * 10))
        for k in range(3):
            put32(out, 0x5B0C + 2 * k, 0xFFFFFFFF if nan else round(i * 100))
            put32(out, 0x5B16 + 2 * k, 0x7FFFFFFF if nan else round(self.p / 3 * 100))
        put32(out, 0x5B14, 0x7FFFFFFF if nan else round(self.p * 100))
        for k, kwh in enumerate((self.kwh, 0.0)):
            put32(out, 0x5000 + 4 * k, 0xFFFFFFFF if nan else 0)
            put32(out, 0x5002 + 4 * k, 0xFFFFFFFF if nan else round(kwh * 100))
        out[0x5B2C] = 0xFFFF if nan else round(self.f * 100)
        return out


def handle(req, addr, meter, mode):
    """Antwort auf eine CRC-gepruefte Anfrage oder None (keine Antwort)."""
    if req[0] != addr or mode == "silent":
        return None
    fc = req[1]
    start, n = struct.unpack(">HH", req[2:6])
    if mode == "exception" or fc != meter.fc or (start, n) not in meter.blocks:
        body = bytes([addr, fc | 0x80, 2])
    else:
        regs = meter.regs(mode == "nan")
        data = b"".join(struct.pack(">H", regs.get(start + i, 0)) for i in range(n))
        body = bytes([addr, fc, len(data)]) + data
    crc = crc16(body)
    if mode == "badcrc":
        crc = bytes([crc[0] ^ 0xFF, crc[1]])
    return body + crc


class Parser:
    """Sucht im Bytestrom gueltige Anfragen; alles andere zaehlt als Muell."""

    def __init__(self):
        self.buf = bytearray()
        self.junk = 0

    def feed(self, data):
        self.buf += data
        frames = []
        while len(self.buf) >= FRAME_LEN:
            if crc16(self.buf[:6]) == bytes(self.buf[6:8]):
                frames.append(bytes(self.buf[:FRAME_LEN]))
                del self.buf[:FRAME_LEN]
            else:
                del self.buf[0]
                self.junk += 1
        return frames


class FtdiPort:
    def __init__(self, baud, parity):
        import ftdi1 as ftdi

        self.ftdi = ftdi
        self.c = ftdi.new()
        par = {"N": ftdi.NONE, "E": ftdi.EVEN, "O": ftdi.ODD}[parity]
        for ret in (ftdi.usb_open(self.c, 0x0403, 0x6001),
                    ftdi.set_baudrate(self.c, baud),
                    ftdi.set_line_property(self.c, ftdi.BITS_8, ftdi.STOP_BIT_1, par),
                    ftdi.set_latency_timer(self.c, 1),
                    ftdi.tcioflush(self.c)):
            if ret < 0:
                sys.exit("FTDI: " + ftdi.get_error_string(self.c))

    def read(self):
        ret, data = self.ftdi.read_data(self.c, 256)
        if ret < 0:
            sys.exit("FTDI: " + self.ftdi.get_error_string(self.c))
        return data[:ret]

    def write(self, data):
        self.ftdi.write_data(self.c, data)


class SerialPort:
    def __init__(self, port, baud, parity):
        import serial

        self.s = serial.Serial(port, baud, parity=parity, timeout=0.002)

    def read(self):
        return self.s.read(256)

    def write(self, data):
        self.s.write(data)


def read_mode(path, last):
    if not path:
        return last
    try:
        with open(path, encoding="ascii") as f:
            return f.read().strip() or last
    except OSError:
        return last


def check():
    assert crc16(bytes.fromhex("010400000002")) == bytes.fromhex("71CB")
    meter = Sdm(3450.0)
    req = bytes.fromhex("010400000012")
    req += crc16(req)
    p = Parser()
    assert p.feed(b"\x00\xff" + req[:3]) == [] and p.feed(req[3:]) == [req] and p.junk == 2
    rsp = handle(req, 1, meter, "ok")
    assert rsp[:3] == bytes([1, 4, 36]) and crc16(rsp[:-2]) == rsp[-2:]
    assert struct.unpack(">f", rsp[3:7])[0] == 230.0
    assert handle(req, 2, meter, "ok") is None and handle(req, 1, meter, "silent") is None
    assert handle(req, 1, meter, "exception")[1:3] == bytes([0x84, 2])
    assert crc16(handle(req, 1, meter, "badcrc")[:-2]) != handle(req, 1, meter, "badcrc")[-2:]
    assert math.isnan(struct.unpack(">f", handle(req, 1, meter, "nan")[3:7])[0])
    req2 = bytes.fromhex("010400340018")
    rsp = handle(req2 + crc16(req2), 1, meter, "ok")
    assert struct.unpack(">f", rsp[3:7])[0] == 3450.0
    freq = 2 * (0x0046 - 0x0034) + 3
    assert struct.unpack(">f", rsp[freq:freq + 4])[0] == 50.0
    abb = Abb(3450.0)
    reqs = [bytes.fromhex(h) for h in ("01035B00001C", "010350000008", "01035B2C0001")]
    rsps = [handle(r + crc16(r), 1, abb, "ok") for r in reqs]
    assert [r[2] for r in rsps] == [56, 16, 2]
    assert struct.unpack(">I", rsps[0][3:7])[0] == 2300  # 0,1 V
    assert struct.unpack(">I", rsps[0][3 + 24:7 + 24])[0] == 500  # 0x5B0C, 0,01 A
    assert struct.unpack(">i", rsps[0][3 + 40:7 + 40])[0] == 345000  # 0x5B14, 0,01 W
    assert struct.unpack(">Q", rsps[1][3:11])[0] == 123450  # 0,01 kWh
    assert struct.unpack(">H", rsps[2][3:5])[0] == 5000  # 0,01 Hz
    assert handle(req, 1, abb, "ok")[1:3] == bytes([0x84, 2])  # falsche Funktion
    print("Protokoll in Ordnung")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--model", choices=("sdm", "abb"), default="sdm")
    ap.add_argument("--port")
    ap.add_argument("--addr", type=int, default=1)
    ap.add_argument("--baud", type=int, default=9600)
    ap.add_argument("--parity", choices="NEO", default="N")
    ap.add_argument("--ctl")
    ap.add_argument("--power", type=float, default=3450.0)
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    if a.check:
        check()
        return

    port = SerialPort(a.port, a.baud, a.parity) if a.port else FtdiPort(a.baud, a.parity)
    meter = (Abb if a.model == "abb" else Sdm)(a.power)
    parser = Parser()
    mode = "ok"
    count = {"anfragen": 0, "antworten": 0}
    last_print = time.monotonic()
    print("wartet auf Anfragen", flush=True)
    while True:
        data = port.read()
        for req in parser.feed(data):
            count["anfragen"] += 1
            mode = read_mode(a.ctl, mode)
            rsp = handle(req, a.addr, meter, mode)
            if rsp is not None:
                port.write(rsp)
                count["antworten"] += 1
        if not data:
            time.sleep(0.001)
        now = time.monotonic()
        if now - last_print >= 10:
            last_print = now
            print(f"{time.strftime('%H:%M:%S')} {mode} {count} rohbytes_verworfen={parser.junk}",
                  flush=True)


if __name__ == "__main__":
    main()
