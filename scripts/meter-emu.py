#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Nachbildung eines Eastron SDM72D-M V2 / SDM630 als Modbus-RTU-Slave am USB-RS-485-Adapter.

Prueft die Zaehleranbindung der Steuerung ohne echten Zaehler (meter_type = 3).
  scripts/meter-emu.py [--port /dev/ttyUSB0] [--addr 1] [--baud 9600] [--parity N]
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

BLOCKS = [(0x0000, 18), (0x0034, 24)]  # wie lib/meter/meter.c (sdm_blk)
FRAME_LEN = 8  # Anfrage Funktion 03/04: Adresse, Funktion, Start, Anzahl, CRC


def crc16(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return struct.pack("<H", crc)


class Meter:
    """Messwerte und Registerbild; die Energie laeuft mit der Leistung."""

    def __init__(self, power_w):
        self.u = 230.0
        self.f = 50.0
        self.p = power_w
        self.kwh = 1234.5
        self.t = time.monotonic()

    def regs(self, nan=False):
        now = time.monotonic()
        self.kwh += self.p * (now - self.t) / 3.6e6
        self.t = now
        i = self.p / (3 * self.u)
        vals = {0x00: self.u, 0x02: self.u + 1, 0x04: self.u - 1, 0x06: i, 0x08: i, 0x0A: i,
                0x0C: self.p / 3, 0x0E: self.p / 3, 0x10: self.p / 3, 0x34: self.p,
                0x46: self.f, 0x48: self.kwh, 0x4A: 0.0}
        out = {}
        for reg, v in vals.items():
            hi, lo = struct.unpack(">HH", struct.pack(">f", math.nan if nan else v))
            out[reg] = hi
            out[reg + 1] = lo
        return out


def handle(req, addr, meter, mode):
    """Antwort auf eine CRC-gepruefte Anfrage oder None (keine Antwort)."""
    if req[0] != addr or mode == "silent":
        return None
    fc = req[1]
    start, n = struct.unpack(">HH", req[2:6])
    if mode == "exception" or fc != 4 or (start, n) not in BLOCKS:
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
    meter = Meter(3450.0)
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
    print("Protokoll in Ordnung")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
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
    meter = Meter(a.power)
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
