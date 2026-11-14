"""Register-level stand-in for the PIB, so pib_gui.py (and pib_cdh.py code) can be tried without hardware.

It speaks the same register protocol as SpiLink / I2cLink / NucleoLink in pib_cdh.py:
    link.write(reg, data)      link.read(reg, n) -> bytes
and implements only the registers pib_cdh.py uses (0x00-0x5F housekeeping and pulse header, 0x06 SAFE,
0x07 ARM, 0x08 FIRE, 0x09 SET_VALVE, 0x0B SET_OUTPUT, 0x60-0x73 waveform).  Values are made up.
Use it with:  python3 pib_gui.py --link sim
"""
import math
import random
import struct
import threading
import time

ARM_CODE = 0x53
SAMPLE_US = 1e6 / (32e6 / 15)       # 0.46875 us
N_SAMPLES = 80
FIRE_SAMPLE = 12
TIMEOUT_HV_S, TIMEOUT_HTR_S = 10.0, 60.0
NAN = float("nan")


class SimLink:
    def __init__(self):
        self.lock = threading.RLock()
        self.t0 = time.time()
        self.rng = random.Random(3)
        self.state = 0                      # 0 SAFE, 1 ARMING, 2 ARMED, 3 SAFING
        self.outputs = 0                    # bit i = OUTPUTS[i] in pib_cdh.py
        self.valves = [1, 1, 1]             # 1 closed, 2 open (last commanded)
        self.coil_until = {1: 0.0, 3: 0.0}
        self.last_hv = 0.0                  # last ARM / arm|igniter|discharge on command
        self.last_htr = 0.0
        self.drdy = False
        self.pulse = (0, 0, 0)              # index, peak_i counts, peak_v counts
        self.wave = bytes(N_SAMPLES * 4)
        self.last_fire = 0.0
        self.tank_t = 17.5                  # tank temperature, degC: warms with the heaters, cools toward 18 degC
        self._img_t = time.time()

    # ------------------------------------------------------------------ behaviour
    def _safe(self):
        self.state = 0
        self.outputs &= ~((1 << 1) | (1 << 2) | (1 << 3))
        self.valves = [1, 1, 1]

    def _tick(self):
        now = time.time()
        if (self.state == 2 or self.outputs & 0b1110) and now - self.last_hv > TIMEOUT_HV_S:
            self._safe()
        if self.outputs & 0b110000 and now - self.last_htr > TIMEOUT_HTR_S:
            self.outputs &= ~0b110000

    def write(self, reg, data=b""):
        with self.lock:
            self._tick()
            data = bytes(data)
            now = time.time()
            if reg == 0x06:
                self._safe()
            elif reg == 0x07 and data[:1] == bytes([ARM_CODE]):
                self.outputs |= (1 << 0) | (1 << 1)
                self.state = 2
                self.valves = [2, 2, 2]
                self.last_hv = now
            elif reg == 0x08 and self.state == 2:
                steps = struct.unpack("<I", data[:4])[0]
                self._capture(steps)
                self.last_hv = now
            elif reg == 0x09 and len(data) >= 2 and data[0] in (1, 2, 3):
                n, opn = data[0], data[1]
                self.valves[n - 1] = 2 if opn else 1
                if n in self.coil_until:
                    self.coil_until[n] = now + 0.1
            elif reg == 0x0B and len(data) >= 2 and data[0] < 8:
                i, on = data[0], data[1]
                self.outputs = (self.outputs | (1 << i)) if on else (self.outputs & ~(1 << i))
                if on and i in (1, 2, 3):
                    self.last_hv = now
                if on and i in (4, 5):
                    self.outputs |= 1
                    self.last_htr = now

    def _capture(self, steps):
        rnd = self.rng
        width_us = steps * 0.1
        vs, cs = [], []
        for k in range(N_SAMPLES):
            t = (k - FIRE_SAMPLE) * SAMPLE_US
            v = 3000.0 if t < 0 else 3000.0 * math.exp(-t / 5.0) + 380.0 * math.exp(-((t - 14.0) / 6.0) ** 2)
            c = 80.0 if t < 0 else 80.0 + 3300.0 * math.exp(-((t - 7.0) / 3.2) ** 2) - 150.0 * math.exp(-((t - 22.0) / 5.0) ** 2)
            vs.append(int(max(0, min(4095, v + rnd.gauss(0, 12)))))
            cs.append(int(max(0, min(4095, c + rnd.gauss(0, 12)))))
        self.wave = b"".join(struct.pack("<HH", v, c) for v, c in zip(vs, cs))
        idx = self.pulse[0] + 1
        self.pulse = (idx, int(1480 + rnd.gauss(0, 25)), int(2730 + rnd.gauss(0, 30)))
        self.drdy = True
        self.last_fire = time.time()

    # ------------------------------------------------------------------ register image
    def _status(self):
        s = 0
        if self.state == 2:
            s |= 0x01
        if self.drdy:
            s |= 0x10
        return s

    def _valve_bits(self):
        b = 0
        for i, st in enumerate(self.valves):
            b |= (st & 3) << (2 * i)
        now = time.time()
        if now < self.coil_until[1]:
            b |= 0x40
        if now < self.coil_until[3]:
            b |= 0x80
        return b

    def _inputs(self):
        o = self.outputs
        return (1 if o & 2 else 0) | (2 if o & 1 else 0) | (4 if o & 4 else 0) | (8 if o & 8 else 0)

    def _image(self):
        now = time.time()
        r = self.rng
        img = bytearray(0x60)
        img[0] = self._status()
        struct.pack_into("<f", img, 0x01, now - self.t0)
        dt, self._img_t = min(now - self._img_t, 5.0), now
        heat = 0.12 if self.outputs & 0b110000 else 0.0
        self.tank_t += heat * dt - (self.tank_t - 18.0) / 150.0 * dt
        flowing = self.state == 2 and now - self.last_fire < 3.0
        pairs = {0x10: 690.0 + r.gauss(0, 1.5), 0x14: 20.2 + r.gauss(0, .05), 0x18: 701.0 + r.gauss(0, 1.5), 0x1C: self.tank_t + r.gauss(0, .03),
                 0x20: NAN, 0x24: NAN, 0x28: 24.1 + r.gauss(0, .05), 0x2C: 31.5 + r.gauss(0, .05),
                 0x30: (15.0 + r.gauss(0, .3)) if flowing else abs(r.gauss(0, .02)), 0x34: 20.4 + r.gauss(0, .05),
                 0x38: (-1000.0 + r.gauss(0, 2)) if self.outputs & 4 else 0.0, 0x3C: 25.3 + r.gauss(0, .05), 0x40: NAN}
        for off, v in pairs.items():
            struct.pack_into("<f", img, off, v)
        img[0x44], img[0x45], img[0x46], img[0x47] = self.state, self.outputs, self._inputs(), self._valve_bits()
        struct.pack_into("<HHH", img, 0x48, 1810, 1822, 1790)
        idx, pi, pv = self.pulse
        struct.pack_into("<IHHfI", img, 0x50, idx, pi, pv, SAMPLE_US, FIRE_SAMPLE)
        return bytes(img)

    def read(self, reg, n):
        with self.lock:
            self._tick()
            if reg >= 0x60:
                self.drdy = False
                a = (reg - 0x60) * 16
                return (self.wave[a:a + n] + bytes(n))[:n]
            img = self._image()
            return (img[reg:reg + n] + bytes(n))[:n]
