#!/usr/bin/env python3
"""Browser GUI for the PIB (sponsor-demo version): telemetry on the left, a command dropdown + sequencer on the right.

    python3 pib_gui.py --link nucleo                  # opens http://127.0.0.1:8765 (Nucleo port auto-detected)
    python3 pib_gui.py --link nucleo --port /dev/cu.usbmodem1103
    python3 pib_gui.py --link spi                     # Raspberry Pi SPI pins
    python3 pib_gui.py --link sim                     # no hardware: register-level fake PIB (pib_sim.py)
    python3 pib_gui.py                                # start disconnected and pick the link in the page

It imports pib_cdh.py for the register map, link classes and decoding, and sends the same register writes.
Differences from the command line tool (by design, for the demo):
  * ARM keeps the PIB armed: the GUI re-sends ARM every 2 s until SAFE or "STOP and Safe" (the PIB's own 10 s
    safety timeout is still there; the GUI just keeps feeding it).  There is no hold time and no automatic disarm.
  * "STOP and Safe" stops any pulse train or sequence, SAFEs the payload and closes all valves.
  * TANK_TEMP_CONTROL runs a bang-bang thermostat on the tank heaters from this program (the PIB firmware has no
    thermostat yet).  If the GUI quits, the PIB turns the heaters off by itself within 60 s.
Valve names (block diagram): ISO3 = valve 2 (NC solenoid), ISO2 = valve 1, ISO1 = valve 3 (flow path tank -> ISO3 -> ISO2 -> FM -> ISO1
-> thruster).  Registers still use 1/2/3.  The PIB only reports commanded valve states; the GUI also shows an inferred actual state.
The page is served on 127.0.0.1 only.  Only the standard library plus what pib_cdh.py needs (pyserial) is required.
"""
import argparse
import glob
import json
import math
import os
import struct
import sys
import threading
import time
import webbrowser
from collections import deque
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

import pib_cdh as P

HERE = os.path.dirname(os.path.abspath(__file__))
WEB_DIR = os.path.join(HERE, "pib_gui")
HOST = "127.0.0.1"

# Valve names follow the block diagram (ISO1 nearest the thruster, ISO3 nearest the tank).  Flow path, tank -> thruster:
#   register 2 (NC solenoid, "LHD1") = ISO3,  register 1 (latching, "LHL2") = ISO2,  register 3 (latching, "LHL3") = ISO1.
# If your plumbing is numbered the other way round, change VALVE_LABEL; everything else follows from it.
VALVE_ORDER = [2, 1, 3]                                   # flow-path order, tank -> thruster, as register numbers
VALVE_LABEL = {2: "ISO3", 1: "ISO2", 3: "ISO1"}
VALVE_KIND = {2: "3-way, NC solenoid", 1: "3-way, latching", 3: "3-way, latching"}
VALVE_BY_LABEL = {v: k for k, v in VALVE_LABEL.items()}
OUT_INDEX = {name: i for i, (name, _, _) in enumerate(P.OUTPUTS)}
REG_SET_TANK_TEMP = 0x0C
TANK_REFRESH_S = 20.0                                     # the PIB turns heaters off 60 s after the last heater-on


class CmdError(Exception):
    """A command that pib_cdh.py would have ended with sys.exit(message)."""


class Stopped(Exception):
    """STOP and Safe was pressed."""


def clean(v):
    """JSON cannot hold NaN / inf."""
    if isinstance(v, float) and (math.isnan(v) or math.isinf(v)):
        return None
    if isinstance(v, dict):
        return {k: clean(x) for k, x in v.items()}
    if isinstance(v, (list, tuple)):
        return [clean(x) for x in v]
    return v


class LockedLink:
    """Serialises every register transaction, so the poller, the heartbeat, the thermostat and a running sequence can share one link."""

    def __init__(self, link, lock):
        self._link, self._lock = link, lock
        self.ser = getattr(link, "ser", None)

    def write(self, reg, data=b""):
        with self._lock:
            self._link.write(reg, data)

    def read(self, reg, n):
        with self._lock:
            return self._link.read(reg, n)


def describe(step):
    a, c = step.get("args", {}), step["cmd"]
    if c == "FIRE_PULSE":
        return "FIRE_PULSE %s µs" % a.get("WIDTH_US")
    if c == "FIRE_PULSES":
        return "FIRE_PULSES %s × %s µs @ %s Hz" % (a.get("COUNT"), a.get("WIDTH_US"), a.get("RATE_HZ"))
    if c == "TANK_TEMP_CONTROL":
        return "TANK_TEMP_CONTROL %s" % ("OFF" if str(a.get("ENABLE", "ON")).upper() == "OFF" else "%s °C ±%s" % (a.get("SETPOINT_C"), a.get("HYSTERESIS_C")))
    return c + "".join(" " + str(v) for v in a.values() if v not in ("", None))


class Bench:
    def __init__(self):
        self.lock = threading.RLock()
        self.link = None
        self.desc = ""
        self.error = ""
        self.hk = None
        self.hk_t = None
        self.wave = None
        self.wave_t = None
        self.fire_t = None
        self.log = deque(maxlen=300)
        self.log_n = 0
        self.stop_evt = threading.Event()
        self.stop = threading.Event()                    # program shutdown
        self.job = None
        self.job_n = 0
        self.job_lock = threading.Lock()
        self.armed_hold = False                          # GUI keeps the PIB armed while True
        self.valve_act = {1: 0, 2: 1, 3: 0}              # inferred ACTUAL valve state: 0 unknown, 1 closed, 2 open
        self._prev_pib_state = None
        self.tank = {"on": False, "sp": 20.0, "hyst": 1.0, "heating": False, "last_cmd": 0.0, "temp": None, "source": "", "warned": False}

    # ------------------------------------------------------------------ plumbing
    def say(self, msg):
        self.log_n += 1
        self.log.appendleft({"n": self.log_n, "t": time.time(), "msg": msg})

    def need_link(self):
        if self.link is None:
            raise CmdError("not connected")
        return self.link

    def connect(self, o):
        self.disconnect(quiet=True)
        kind = o.get("link", "nucleo")
        try:
            if kind == "spi":
                raw = P.SpiLink(int(o.get("spi_bus", 0)), int(o.get("spi_dev", 0)), int(o.get("spi_hz", 1_000_000)))
                desc = "spi bus %s dev %s" % (o.get("spi_bus", 0), o.get("spi_dev", 0))
            elif kind == "i2c":
                addr = o.get("i2c_addr", 0x42)
                addr = int(addr, 0) if isinstance(addr, str) else int(addr)
                raw = P.I2cLink(int(o.get("i2c_bus", 1)), addr)
                desc = "i2c bus %s addr 0x%02X" % (o.get("i2c_bus", 1), addr)
            elif kind == "sim":
                from pib_sim import SimLink
                raw, desc = SimLink(), "simulated PIB (no hardware)"
            else:
                raw = P.NucleoLink(o.get("port") or None, int(o.get("baud", 115200)))
                desc = "nucleo " + raw.ser.port
        except SystemExit as e:                          # NucleoLink exits with a message when it cannot pick a port
            raise CmdError(str(e.code))
        except ImportError as e:
            raise CmdError("missing Python package for this link: %s (pip install pyserial / spidev / smbus2)" % e.name)
        except Exception as e:
            raise CmdError("cannot open the link: %s" % e)
        self.link, self.desc, self.error = LockedLink(raw, self.lock), "%s: %s" % (kind, desc), ""
        self.hk = self.wave = None
        self.say("connected, %s" % self.desc)

    def disconnect(self, quiet=False):
        self.stop_evt.set()
        self.armed_hold = False
        self.tank["on"] = False
        link, self.link = self.link, None
        ser = getattr(link, "ser", None)
        if ser is not None and hasattr(ser, "close"):
            try:
                ser.close()
            except Exception:
                pass
        if link is not None and not quiet:
            self.say("disconnected")
        self.hk = None
        self.stop_evt.clear()

    # ------------------------------------------------------------------ background threads
    def read_hk(self):
        hk = P.read_hk(self.need_link())
        self.track_valves(hk)
        self.hk, self.hk_t = hk, time.time()
        return hk

    def track_valves(self, hk):
        """The PIB reports only the last COMMANDED valve states (it has no position sensor).  Infer the actual state:
        a valve coil only moves when the 12 V rail is good, so
          - solenoid (reg 2, normally closed): open only while commanded open AND 12 V is good, otherwise closed;
          - latching valves: they move to the commanded state when it is commanded with 12 V good, and keep their
            last actual position otherwise (unknown until they have moved under power once);
          - when the PIB goes back to SAFE from ARMED/ARMING/SAFING it closed the valves with the ARM line still up."""
        pg12 = bool(hk["inputs"] & 1)
        state, prev = hk["pib_state"], self._prev_pib_state
        for n in (1, 2, 3):
            cmd = (hk["valves"] >> (2 * (n - 1))) & 3
            if n == 2:
                self.valve_act[2] = 2 if (cmd == 2 and pg12) else 1
            elif pg12 and cmd in (1, 2):
                self.valve_act[n] = cmd
            elif prev not in (None, 0) and state == 0 and cmd == 1:
                self.valve_act[n] = 1
        self._prev_pib_state = state

    def poll_loop(self):
        while not self.stop.wait(1.0):
            if self.link is None:
                continue
            try:
                self.read_hk()
                self.error = ""
            except Exception as e:
                self.error = "read failed: %s" % e

    def heartbeat_loop(self):
        """While the operator has the PIB armed, keep feeding the PIB's 10 s ARM timeout.  Not a hold timer."""
        nxt, misses = 0.0, 0
        while not self.stop.wait(0.2):
            if not (self.armed_hold and self.link) or time.time() < nxt:
                misses = 0 if not self.armed_hold else misses
                continue
            nxt = time.time() + P.HEARTBEAT_S
            try:
                self.link.write(0x07, [P.ARM_CODE])
            except Exception as e:
                self.error = "ARM heartbeat failed: %s" % e
                continue
            hk = self.hk
            if hk is not None and self.hk_t and time.time() - self.hk_t < 2.0 and not hk["status"] & 1:
                misses += 1
                if misses >= 3:
                    self.armed_hold = False
                    self.say("PIB is no longer ARMED (fault?); stopped re-sending ARM")
            else:
                misses = 0

    def tank_temp(self):
        hk = self.hk
        if hk is None:
            return None, ""
        ntc = [v for v in (hk["tank_ntc1_degC"], hk["tank_ntc2_degC"]) if v == v]
        if ntc:
            return sum(ntc) / len(ntc), "tank NTCs"
        if hk["tank_degC"] == hk["tank_degC"]:
            return hk["tank_degC"], "tank pressure sensor"
        return None, ""

    def heaters(self, on):
        link = self.need_link()
        for name in ("htr1", "htr2"):
            link.write(P.REG_SET_OUTPUT, [OUT_INDEX[name], 1 if on else 0])

    def tank_loop(self):
        """Bang-bang tank thermostat: heaters on below setpoint - hysteresis/2, off above setpoint + hysteresis/2."""
        t = self.tank
        while not self.stop.wait(1.0):
            if not (t["on"] and self.link):
                continue
            temp, src = self.tank_temp()
            t["temp"], t["source"] = temp, src
            try:
                if temp is None:
                    if t["heating"]:
                        self.heaters(False)
                        t["heating"] = False
                    if not t["warned"]:
                        self.say("tank temperature control: no tank temperature reading, heaters off")
                        t["warned"] = True
                    continue
                t["warned"] = False
                lo, hi = t["sp"] - t["hyst"] / 2.0, t["sp"] + t["hyst"] / 2.0
                want = True if temp < lo else False if temp > hi else t["heating"]
                now = time.time()
                if want and (not t["heating"] or now - t["last_cmd"] > TANK_REFRESH_S):
                    self.heaters(True)
                    if not t["heating"]:
                        self.say("tank %.1f °C < %.1f: heaters ON" % (temp, lo))
                    t["heating"], t["last_cmd"] = True, now
                elif not want and t["heating"]:
                    self.heaters(False)
                    t["heating"] = False
                    self.say("tank %.1f °C > %.1f: heaters off" % (temp, hi))
            except Exception as e:
                self.error = "tank control: %s" % e

    # ------------------------------------------------------------------ waiting that STOP can interrupt
    def sleep(self, seconds):
        end = time.time() + seconds
        while True:
            if self.stop_evt.is_set():
                raise Stopped()
            left = end - time.time()
            if left <= 0:
                return
            time.sleep(min(0.05, left))

    # ------------------------------------------------------------------ commands (same register sequences as pib_cdh.py)
    def arm(self):
        link = self.need_link()
        link.write(0x07, [P.ARM_CODE])
        t_end = time.time() + P.ARM_TIMEOUT_S
        while time.time() < t_end:
            if P.read_status(link) & 0x01:
                self.armed_hold = True
                self.say("ARMED (re-sending ARM every %g s until SAFE / STOP)" % P.HEARTBEAT_S)
                self.read_hk()
                return
            self.sleep(0.05)
        raise CmdError("PIB did not reach ARMED within %.1f s (check 12 V power-good)" % P.ARM_TIMEOUT_S)

    def safe(self):
        self.armed_hold = False
        self.need_link().write(0x06)
        self.say("SAFE sent")
        time.sleep(0.3)
        self.read_hk()

    def fire(self, width_us, csv=""):
        link = self.need_link()
        steps = round(width_us / P.PULSE_STEP_US)
        if not 0 <= steps <= 0xFFFFFFFF:
            raise CmdError("pulse width must be 0 to %.1f us" % (0xFFFFFFFF * P.PULSE_STEP_US))
        if not P.read_status(link) & 0x01:
            raise CmdError("PIB is not ARMED (run ARM first)")
        link.write(0x08, struct.pack("<I", steps))
        self.sleep(steps * P.PULSE_STEP_US * 1e-6)
        t_end = time.time() + P.DRDY_TIMEOUT_S
        while time.time() < t_end:
            if P.read_status(link) & 0x10:
                self.fire_t = time.time()
                n = P.read_pulse_header(link)
                self.say("fired %.1f us (%d x 100 ns), waveform ready" % (steps * P.PULSE_STEP_US, steps))
                self.say("pulse " + P.pulse_line(n))
                self.read_wave(csv)
                self.read_hk()
                return True
            self.sleep(0.02)
        self.say("fire sent, but DRDY did not come up within %.1f s" % P.DRDY_TIMEOUT_S)
        return False

    def fire_train(self, count, width_us, rate_hz, job):
        if count < 1 or rate_hz <= 0:
            raise CmdError("COUNT must be at least 1 and RATE_HZ above 0")
        period = 1.0 / rate_hz
        self.say("pulse train: %d pulses of %g us at %g Hz" % (count, width_us, rate_hz))
        for k in range(count):
            t0 = time.time()
            job["progress"] = "pulse %d / %d" % (k + 1, count)
            self.sleep(0)
            self.fire(width_us)
            self.sleep(max(0.0, period - (time.time() - t0)))
        self.say("pulse train done (%d pulses)" % count)
        job["progress"] = ""

    def read_wave(self, csv=""):
        samples, hdr = P.read_wave(self.need_link())
        sample_us = hdr["sample_us"]
        if not 0.1 < sample_us < 10.0:
            sample_us = 1e6 / P.NOMINAL_SAMPLE_RATE_HZ
            self.say("warning: board did not report a valid sample period (old firmware?), using nominal %s us" % P.sig(sample_us))
        fire = hdr["fire_sample"]
        rows = [(i, (i - fire) * sample_us, v, c, P.counts_to_pin_v(v), P.counts_to_pin_v(c)) for i, (v, c) in enumerate(samples)]
        self.wave = {"sample_us": sample_us, "fire_sample": fire, "header": hdr,
                     "rows": [{"i": r[0], "t_us": r[1], "v_cnt": r[2], "i_cnt": r[3], "v_pin": r[4], "i_pin": r[5]} for r in rows]}
        self.wave_t = time.time()
        if csv:
            os.makedirs(P.TESTS_DIR, exist_ok=True)
            path = os.path.join(P.TESTS_DIR, os.path.basename(csv))
            with open(path, "w") as f:
                f.write("sample,time_us,voltage_counts,current_counts,voltage_pin_v,current_pin_v\n")
                for r in rows:
                    f.write("%d,%s,%d,%d,%s,%s\n" % (r[0], P.sig(r[1]), r[2], r[3], P.sig(r[4]), P.sig(r[5])))
            self.say("wrote %d samples to %s" % (len(rows), path))

    def valve(self, which, action):
        link = self.need_link()
        which = str(which).upper()
        regs = VALVE_ORDER if which == "ALL" else [VALVE_BY_LABEL[which]]
        for n in regs:
            link.write(P.REG_SET_VALVE, [n, P.OPEN_CLOSE[action.lower()]])
        self.say("valve %s %s" % (which, action.lower()))
        self.sleep(0.3)
        self.read_hk()

    def output(self, names, level):
        link = self.need_link()
        for name in names:
            link.write(P.REG_SET_OUTPUT, [OUT_INDEX[name], P.ON_OFF[level]])
        self.say("out %s %s" % (" ".join(names), level))
        self.sleep(0.2)
        self.read_hk()

    def set_tank_control(self, enable, setpoint, hyst):
        t = self.tank
        if not enable:
            t["on"] = False
            if t["heating"]:
                self.heaters(False)
                t["heating"] = False
            self.say("tank temperature control OFF")
            return
        if hyst <= 0:
            raise CmdError("HYSTERESIS_C must be above 0")
        if not -20 <= setpoint <= 80:
            raise CmdError("SETPOINT_C must be -20 to 80")
        self.need_link().write(REG_SET_TANK_TEMP, struct.pack("<f", setpoint))     # the PIB stores it (register 0x0C)
        t.update({"on": True, "sp": setpoint, "hyst": hyst, "warned": False})
        self.say("tank temperature control ON: %.1f °C, hysteresis %.1f °C (bang-bang, heaters 1 and 2)" % (setpoint, hyst))

    def exec_step(self, step, job):
        c, a = step["cmd"].upper(), step.get("args", {})
        up = lambda k, d="": str(a.get(k, d)).upper()
        if c == "ARM":
            self.arm()
        elif c == "SAFE":
            self.safe()
        elif c == "FIRE_PULSE":
            self.fire(float(a["WIDTH_US"]), str(a.get("CSV") or "").strip())
        elif c == "FIRE_PULSES":
            self.fire_train(int(float(a["COUNT"])), float(a["WIDTH_US"]), float(a["RATE_HZ"]), job)
        elif c == "SET_VALVE":
            if up("VALVE") not in list(VALVE_BY_LABEL) + ["ALL"] or up("ACTION") not in ("OPEN", "CLOSE"):
                raise CmdError("SET_VALVE: VALVE = ISO1|ISO2|ISO3|ALL, ACTION = OPEN|CLOSE")
            self.valve(up("VALVE"), up("ACTION"))
        elif c == "SET_HEATER":
            if up("HEATER") not in ("1", "2", "BOTH") or up("STATE") not in ("ON", "OFF"):
                raise CmdError("SET_HEATER: HEATER = 1|2|BOTH, STATE = ON|OFF")
            self.output(["htr1", "htr2"] if up("HEATER") == "BOTH" else ["htr" + up("HEATER")], up("STATE").lower())
        elif c == "TANK_TEMP_CONTROL":
            self.set_tank_control(up("ENABLE", "ON") == "ON", float(a.get("SETPOINT_C", 20)), float(a.get("HYSTERESIS_C", 1)))
        elif c == "READ_WAVEFORM":
            self.read_wave(str(a.get("CSV") or "").strip())
            self.say("read %d waveform samples" % len(self.wave["rows"]))
        elif c == "SET_OUTPUT":
            name = str(a.get("OUTPUT", "")).lower()
            if name not in OUT_INDEX or up("STATE") not in ("ON", "OFF"):
                raise CmdError("SET_OUTPUT: unknown output or state")
            self.output([name], up("STATE").lower())
        elif c == "READ_REGISTER":
            reg, n = int(str(a["REG"]), 0), int(float(a["N"]))
            if not 1 <= n <= 255:
                raise CmdError("N must be 1-255")
            self.say("raw 0x%02X x%d: %s" % (reg, n, self.need_link().read(reg, n).hex(" ")))
        else:
            raise CmdError("unknown command %r" % c)

    # ------------------------------------------------------------------ jobs (a single command or a sequence)
    def start_job(self, steps, label):
        self.need_link()
        if not steps:
            raise CmdError("nothing to run")
        with self.job_lock:
            if self.job and self.job["running"]:
                raise CmdError("%s is running; press STOP and Safe to abort it" % self.job["label"])
            self.job_n += 1
            job = {"id": self.job_n, "label": label, "running": True, "idx": 0, "progress": "",
                   "steps": [{"label": describe(s), "state": "pending"} for s in steps]}
            self.job = job
            self.stop_evt.clear()
        threading.Thread(target=self.run_job, args=(job, steps), daemon=True).start()

    def run_job(self, job, steps):
        try:
            for i, step in enumerate(steps):
                job["idx"] = i
                job["steps"][i]["state"] = "running"
                self.sleep(0)
                try:
                    self.exec_step(step, job)
                except Stopped:
                    raise
                job["steps"][i]["state"] = "done"
                wait = float(step.get("wait", 0) or 0)
                if wait and i < len(steps) - 1:
                    job["progress"] = "waiting %g s" % wait
                    self.sleep(wait)
                    job["progress"] = ""
            self.say("done: %s" % job["label"])
        except Stopped:
            self.say("STOPPED: %s" % job["label"])
        except CmdError as e:
            self.say("ERROR: %s" % e)
            self.error = str(e)
        except (IOError, OSError) as e:
            self.error = "link error: %s" % e
            self.say("ERROR: %s" % self.error)
        except Exception as e:                           # a bug must not leave the job "running" forever
            self.error = "internal error: %r" % e
            self.say("ERROR: %s" % self.error)
        finally:
            for s in job["steps"]:
                if s["state"] in ("pending", "running"):
                    s["state"] = "skipped" if s["state"] == "pending" else "stopped"
            job["running"], job["progress"] = False, ""

    def stop_and_safe(self):
        """Stop any pulsing or sequence, SAFE the payload, make sure all valves are closed."""
        link = self.need_link()
        self.stop_evt.set()
        self.armed_hold = False
        err = None
        for attempt in range(2):                         # twice: the second catches anything a job thread sent while stopping
            try:
                link.write(0x06)
            except Exception as e:
                err = e
            t_end = time.time() + 5.0
            while attempt == 0 and self.job and self.job["running"] and time.time() < t_end:
                time.sleep(0.05)
            time.sleep(0.3)
        try:
            hk = self.read_hk()
            open_valves = [VALVE_LABEL[n] for n in VALVE_ORDER if (hk["valves"] >> (2 * (n - 1))) & 3 == 2]
            for n in VALVE_ORDER:
                if VALVE_LABEL[n] in open_valves:
                    link.write(P.REG_SET_VALVE, [n, 0])
            if open_valves:
                time.sleep(0.3)
                hk = self.read_hk()
            self.say("STOP and Safe: stopped, SAFE sent, valves %s" % ", ".join(
                "%s %s" % (VALVE_LABEL[n], P.VALVE_STATES[(hk["valves"] >> (2 * (n - 1))) & 3]) for n in VALVE_ORDER))
        except Exception as e:
            err = e
        finally:
            self.stop_evt.clear()
        if err:
            self.error = "STOP and Safe: %s" % err
            raise CmdError(self.error)

    # ------------------------------------------------------------------ what the page shows
    def hk_view(self):
        hk = self.hk
        if hk is None:
            return None
        pk = hk["pulse"]
        valves = []
        for n in VALVE_ORDER:
            st = (hk["valves"] >> (2 * (n - 1))) & 3
            pulsing = bool((n == 1 and hk["valves"] & 0x40) or (n == 3 and hk["valves"] & 0x80))
            valves.append({"n": n, "name": VALVE_LABEL[n], "kind": VALVE_KIND[n], "state": P.VALVE_STATES[st], "cmd": P.VALVE_STATES[st],
                           "actual": P.VALVE_STATES[self.valve_act[n]], "pulsing": pulsing})
        out = dict(hk)
        out.update({
            "flags_list": [name for i, name in enumerate(P.STATUS_BITS) if hk["status"] & (1 << i)],
            "pib_state_name": P.PIB_STATES[hk["pib_state"]] if hk["pib_state"] < len(P.PIB_STATES) else "? (%d)" % hk["pib_state"],
            "valves_view": valves,
            "outputs_view": [{"name": nm, "pin": pin, "desc": d, "on": bool(hk["outputs"] >> i & 1)} for i, (nm, pin, d) in enumerate(P.OUTPUTS)],
            "inputs_view": [{"name": nm, "high": bool(hk["inputs"] >> i & 1)} for i, nm in enumerate(P.INPUTS)],
            "pulse_view": {"index": pk["index"], "peak_v_cnt": pk["peak_v"], "peak_i_cnt": pk["peak_i"],
                           "peak_v_volts": P.counts_to_pin_v(pk["peak_v"]) * P.IGN_V_PER_PIN_V,
                           "peak_i_pin_v": P.counts_to_pin_v(pk["peak_i"])},
        })
        return out

    def state(self):
        """Snapshot for the page.  Lock-free so the page keeps updating while a command runs."""
        now = time.time()
        log = []
        for _ in range(5):
            try:
                log = list(self.log)[:60]
                break
            except RuntimeError:                         # deque changed size while copying; try again
                pass
        t = self.tank
        job = self.job
        return clean({
            "now": now, "connected": self.link is not None, "link": self.desc, "error": self.error,
            "hk": self.hk_view(), "hk_age": None if self.hk_t is None else now - self.hk_t,
            "wave": self.wave, "wave_age": None if self.wave_t is None else now - self.wave_t,
            "fire_age": None if self.fire_t is None else now - self.fire_t,
            "armed_hold": self.armed_hold,
            "tank": {"on": t["on"], "sp": t["sp"], "hyst": t["hyst"], "heating": t["heating"], "temp": t["temp"], "source": t["source"]},
            "job": None if job is None else dict(job),
            "log": log,
            "outputs": [{"name": n, "pin": p, "desc": d} for n, p, d in P.OUTPUTS],
            "valves": [{"n": n, "name": VALVE_LABEL[n], "kind": VALVE_KIND[n]} for n in VALVE_ORDER],
            "hk_age_s": None if self.hk_t is None else now - self.hk_t,
            "sim_available": os.path.exists(os.path.join(HERE, "pib_sim.py")),
        })


def ports():
    return sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/ttyACM*"))


def make_handler(bench):
    class H(SimpleHTTPRequestHandler):
        def __init__(self, *a, **k):
            super().__init__(*a, directory=WEB_DIR, **k)

        def _json(self, obj, code=200):
            body = json.dumps(obj, allow_nan=False).encode()
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            path = self.path.split("?")[0]
            if path == "/api/state":
                self._json(bench.state())
            elif path == "/api/ports":
                self._json({"ports": ports()})
            else:
                super().do_GET()

        def do_POST(self):
            path = self.path.split("?")[0]
            try:
                req = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0))) or b"{}")
                if path == "/api/connect":
                    bench.connect(req)
                elif path == "/api/disconnect":
                    bench.disconnect()
                elif path == "/api/run":
                    bench.start_job(req["steps"], req.get("label") or "sequence")
                elif path == "/api/stop":
                    bench.stop_and_safe()
                else:
                    return self._json({"error": "unknown endpoint"}, 404)
                self._json({"ok": True})
            except CmdError as e:
                bench.say("ERROR: %s" % e)
                self._json({"error": str(e)}, 400)
            except (KeyError, ValueError, TypeError) as e:
                self._json({"error": "bad request: %s" % e}, 400)

        def end_headers(self):
            self.send_header("Cache-Control", "no-store")
            super().end_headers()

        def log_message(self, *a):
            pass

    return H


def main():
    ap = argparse.ArgumentParser(description="Browser GUI for the PIB (sponsor-demo version)")
    g = ap.add_argument_group("connection (same options as pib_cdh.py; you can also choose them in the page)")
    g.add_argument("--link", choices=["spi", "i2c", "nucleo", "sim"], help="how to reach the PIB; sim = fake PIB for trying the GUI. Default: connect from the page")
    g.add_argument("--port", help="Nucleo serial port, e.g. /dev/cu.usbmodem1103")
    g.add_argument("--baud", type=int, default=115200)
    g.add_argument("--spi-bus", type=int, default=0)
    g.add_argument("--spi-dev", type=int, default=0)
    g.add_argument("--spi-hz", type=int, default=1_000_000)
    g.add_argument("--i2c-bus", type=int, default=1)
    g.add_argument("--i2c-addr", type=lambda s: int(s, 0), default=0x42)
    ap.add_argument("--http-port", type=int, default=8765, help="port of the local page. Default: 8765")
    ap.add_argument("--no-browser", action="store_true", help="do not open the page automatically")
    a = ap.parse_args()

    bench = Bench()
    if a.link:
        try:
            bench.connect({"link": a.link, "port": a.port, "baud": a.baud, "spi_bus": a.spi_bus, "spi_dev": a.spi_dev,
                           "spi_hz": a.spi_hz, "i2c_bus": a.i2c_bus, "i2c_addr": a.i2c_addr})
        except CmdError as e:
            sys.exit(str(e))
    for fn in (bench.poll_loop, bench.heartbeat_loop, bench.tank_loop):
        threading.Thread(target=fn, daemon=True).start()
    httpd = ThreadingHTTPServer((HOST, a.http_port), make_handler(bench))
    url = "http://%s:%d" % (HOST, a.http_port)
    print("PIB GUI: %s   (Ctrl+C to quit)" % url)
    if not a.no_browser:
        threading.Timer(0.6, lambda: webbrowser.open(url)).start()
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        bench.stop.set()
        bench.stop_evt.set()
        bench.armed_hold = False
        try:
            if bench.link is not None:
                bench.link.write(0x06)                   # leave the payload SAFE
        except Exception:
            pass
        bench.disconnect(quiet=True)
        httpd.server_close()


if __name__ == "__main__":
    main()
