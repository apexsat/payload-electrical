#!/usr/bin/env python3
import argparse
import difflib
import glob
import math
import os
import struct
import sys
import time

READ = 0x80
ARM_CODE = 0x53
HEARTBEAT_S = 2.0
ARM_TIMEOUT_S = 1.0
DRDY_TIMEOUT_S = 0.5
WAVE_FIRST = 0x60
WAVE_SAMPLES = 80
WAVE_CHUNK_REGS = 10
NOMINAL_SAMPLE_RATE_HZ = 32e6 / 15
PULSE_STEP_US = 0.1
ADC_VREF_V = 3.3
IGN_V_PER_PIN_V = -455.0
SIG_FIGS = 4  # significant figures for every measured value printed or saved
TESTS_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "tests")

STATUS_BITS = ["ARMED", "FLOW_FAULT", "IGNITER_FAULT", "DISCHARGE_FAULT", "DRDY", "BATT_UVLO", "TEMP_FAULT"]
PIB_STATES = ["SAFE", "ARMING", "ARMED", "SAFING"]
OUTPUTS = [
    ("vbat", "VBAT_EN", "battery bus enable"),
    ("arm", "ARM_SAFE", "ARM line (turns on the 12 V that drives the valves)"),
    ("igniter", "IGNITER_EN", "igniter HV supply enable"),
    ("discharge", "DISCHARGE_EN", "discharge HV supply enable"),
    ("htr1", "HTR1_EN", "heater 1 (also turns on vbat)"),
    ("htr2", "HTR2_EN", "heater 2 (also turns on vbat)"),
    ("pdrst", "PD_RST", "igniter peak-detector reset"),
    ("wdg", "WDG_IN", "watchdog input pin"),
]
INPUTS = ["PGOOD_12V", "VBAT_PGOOD", "IGNITER_PGOOD", "DISCHARGE_PGOOD"]
VALVE_STATES = ["unknown", "closed", "open", "?"]
VALVE_NAMES = {1: "V1 latching", 2: "V2 solenoid (NC)", 3: "V3 latching"}
REG_SET_VALVE = 0x09
REG_SET_OUTPUT = 0x0B


class SpiLink:
    def __init__(self, bus, dev, hz):
        import spidev
        self.spi = spidev.SpiDev()
        self.spi.open(bus, dev)
        self.spi.mode = 0
        self.spi.max_speed_hz = hz

    def write(self, reg, data=b""):
        self.spi.xfer2([reg, *data])

    def read(self, reg, n):
        self.spi.xfer2([READ | reg, n])
        time.sleep(0.0002)
        return bytes(self.spi.xfer2([0] * n))


class I2cLink:
    def __init__(self, bus, addr):
        from smbus2 import SMBus, i2c_msg
        self.bus = SMBus(bus)
        self.addr = addr
        self.msg = i2c_msg

    def write(self, reg, data=b""):
        self.bus.i2c_rdwr(self.msg.write(self.addr, [reg, *data]))

    def read(self, reg, n):
        w = self.msg.write(self.addr, [READ | reg])
        r = self.msg.read(self.addr, n)
        self.bus.i2c_rdwr(w, r)
        return bytes(r)


class NucleoLink:
    SYNC_REQ = 0xA5
    SYNC_RESP = 0x5A

    def __init__(self, port, baud):
        import serial
        if port is None:
            ports = sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/ttyACM*"))
            if len(ports) != 1:
                sys.exit("give --port; found %s\nexample: python3 pib_cdh.py --link nucleo --port %s hk"
                         % (ports or "no /dev/cu.usbmodem* or /dev/ttyACM* ports",
                            ports[0] if ports else "/dev/cu.usbmodem1103"))
            port = ports[0]
        self.ser = serial.Serial(port, baud, timeout=1.0)
        self.ser.reset_input_buffer()

    def xfer(self, data):
        data = bytes(data)
        self.ser.write(bytes([self.SYNC_REQ, len(data) & 0xFF, len(data) >> 8]) + data)
        hdr = self.ser.read(4)
        if len(hdr) != 4 or hdr[0] != self.SYNC_RESP:
            raise IOError("no reply from the Nucleo bridge (got %r)" % hdr)
        n = hdr[2] | (hdr[3] << 8)
        body = self.ser.read(n)
        if hdr[1] != 0 or len(body) != n:
            raise IOError("SPI transfer failed on the Nucleo (HAL status %d)" % hdr[1])
        return body

    def write(self, reg, data=b""):
        self.xfer([reg, *data])

    def read(self, reg, n):
        self.xfer([READ | reg, n])
        time.sleep(0.002)
        return self.xfer([0] * n)


def read_status(link):
    return link.read(0x00, 1)[0]


def arm_once(link):
    link.write(0x07, [ARM_CODE])
    t_end = time.time() + ARM_TIMEOUT_S
    while time.time() < t_end:
        if read_status(link) & 0x01:
            print("ARMED")
            return
        time.sleep(0.05)
    sys.exit("PIB did not reach ARMED within %.1f s (check 12 V power-good)" % ARM_TIMEOUT_S)


def fire(link, width_us):
    steps = round(width_us / PULSE_STEP_US)
    if not 0 <= steps <= 0xFFFFFFFF:
        sys.exit("pulse width must be 0 to %.1f us\nexample: %sarm fire 5" % (0xFFFFFFFF * PULSE_STEP_US, EXAMPLE))
    if not read_status(link) & 0x01:
        sys.exit("PIB is not ARMED; put 'arm' in front to arm first\nexample: %sarm fire %g" % (EXAMPLE, width_us))
    link.write(0x08, struct.pack("<I", steps))
    time.sleep(steps * PULSE_STEP_US * 1e-6)
    t_end = time.time() + DRDY_TIMEOUT_S
    while time.time() < t_end:
        if read_status(link) & 0x10:
            print("fired %.1f us (%d x 100 ns), waveform ready" % (steps * PULSE_STEP_US, steps))
            print("pulse " + pulse_line(read_pulse_header(link)))
            return True
        time.sleep(0.02)
    print("fire sent, but DRDY did not come up within %.1f s" % DRDY_TIMEOUT_S)
    return False


def seq(link, count, width_us, hz, csv_name):
    if count < 1 or hz <= 0:
        sys.exit("count must be at least 1 and rate above 0 Hz\nexample: %sarm seq 10 5 2" % EXAMPLE)
    period = 1.0 / hz
    if csv_name:
        base, ext = os.path.splitext(csv_name)
        ext = ext or ".csv"
    late = 0
    t0 = time.time()
    for k in range(count):
        wait = t0 + k * period - time.time()
        if wait > 0:
            time.sleep(wait)
        elif k:
            late += 1
        t_last = time.time()
        print("pulse %d of %d" % (k + 1, count))
        if fire(link, width_us) and csv_name:
            print_wave(read_wave(link), "%s_%04d%s" % (base, k + 1, ext))
    elapsed = t_last - t0
    rate = (count - 1) / elapsed if count > 1 and elapsed > 0 else hz
    print("seq done: %d pulses of %s us in %s s (asked %s Hz, got %s Hz)" % (
        count, sig(width_us), sig(elapsed), sig(hz), sig(rate)))
    if late:
        print("warning: %d pulses started late; the link can't keep up with %s Hz%s" % (
            late, sig(hz), " while saving waveforms" if csv_name else ""))


def read_wave(link):
    data = b""
    for reg in range(WAVE_FIRST, WAVE_FIRST + WAVE_SAMPLES // 4, WAVE_CHUNK_REGS):
        data += link.read(reg, WAVE_CHUNK_REGS * 16)
    return list(struct.iter_unpack("<HH", data)), read_pulse_header(link)


def print_wave(wave, csv_path):
    samples, hdr = wave
    sample_us = hdr["sample_us"]
    if not 0.1 < sample_us < 10.0:
        sample_us = 1e6 / NOMINAL_SAMPLE_RATE_HZ
        print("warning: board did not report a valid sample period (old firmware?), using nominal %s us" % sig(sample_us))
    fire = hdr["fire_sample"]
    rows = [(i, (i - fire) * sample_us, v, c, counts_to_pin_v(v), counts_to_pin_v(c))
            for i, (v, c) in enumerate(samples)]
    if csv_path:
        os.makedirs(TESTS_DIR, exist_ok=True)
        csv_path = os.path.join(TESTS_DIR, os.path.basename(csv_path))
        with open(csv_path, "w") as f:
            f.write("sample,time_us,voltage_counts,current_counts,voltage_pin_v,current_pin_v\n")
            for r in rows:
                f.write("%d,%s,%d,%d,%s,%s\n" % (r[0], sig(r[1]), r[2], r[3], sig(r[4]), sig(r[5])))
        print("wrote %d samples to %s" % (len(rows), csv_path))
        return
    v_pin = [r[4] for r in rows]
    i_pin = [r[5] for r in rows]
    title = "Discharge capture   %d samples   %s us apart   pin volts, %s V full scale" % (
        len(rows), sig(sample_us), sig(ADC_VREF_V))
    body = [["%d" % r[0], sig(r[1]),
             "%s V" % sig(r[4]), "%4d" % r[2], bar(r[4], ADC_VREF_V),
             "%s V" % sig(r[5]), "%4d" % r[3], bar(r[5], ADC_VREF_V)] for r in rows]
    print_table(title,
                ["#", "t (us)", "pin", "counts", "0 ────── 3.3", "pin", "counts", "0 ────── 3.3"],
                body, "rrrrlrrl", groups=[(2, 3, "voltage  PA0"), (5, 3, "current  PA1")])
    for name, xs in (("voltage", v_pin), ("current", i_pin)):
        print("  %s  min %s V  max %s V  mean %s V" % (name, sig(min(xs)), sig(max(xs)), sig(sum(xs) / len(xs))))
    print()


BAR_WIDTH = 12
BAR_PARTS = " ▏▎▍▌▋▊▉█"


def bar(value, full_scale):
    eighths = max(0, min(BAR_WIDTH * 8, round(value / full_scale * BAR_WIDTH * 8)))
    full, part = divmod(eighths, 8)
    s = "█" * full + (BAR_PARTS[part] if part else "")
    return s.ljust(BAR_WIDTH, "·")


def print_table(title, headers, rows, align, groups=None):
    widths = [max([len(h)] + [len(r[c]) for r in rows]) for c, h in enumerate(headers)]
    spans = {}
    for start, n, label in groups or []:
        need = len(label) - (sum(widths[start:start + n]) + 3 * (n - 1))
        if need > 0:
            widths[start + n - 1] += need
        spans[start] = (n, label)

    def cell(text, w, a):
        return text.rjust(w) if a == "r" else text.ljust(w)

    def rule(left, mid, right, merged=False):
        parts, c = [], 0
        while c < len(widths):
            n = spans[c][0] if merged and c in spans else 1
            parts.append("─" * (sum(widths[c:c + n]) + 3 * (n - 1) + 2))
            c += n
        return left + mid.join(parts) + right

    print()
    print("  " + title)
    if groups:
        print(rule("┌", "┬", "┐", merged=True))
        line, c = [], 0
        while c < len(widths):
            if c in spans:
                n, label = spans[c]
                line.append(" " + label.ljust(sum(widths[c:c + n]) + 3 * (n - 1)) + " ")
                c += n
            else:
                line.append(" " * (widths[c] + 2))
                c += 1
        print("│" + "│".join(line) + "│")
        inner = set()
        for start, n, _ in groups:
            inner.update(range(start + 1, start + n))
        print("├" + "".join(("┬" if c in inner else "┼") * (c > 0) + "─" * (widths[c] + 2)
                            for c in range(len(widths))) + "┤")
    else:
        print(rule("┌", "┬", "┐"))
    print("│" + "│".join(" " + cell(h, widths[c], align[c]) + " " for c, h in enumerate(headers)) + "│")
    print(rule("├", "┼", "┤"))
    for r in rows:
        print("│" + "│".join(" " + cell(t, widths[c], align[c]) + " " for c, t in enumerate(r)) + "│")
    print(rule("└", "┴", "┘"))


def read_pulse_header(link):
    index, peak_i, peak_v, sample_us, fire_sample = struct.unpack("<IHHfI", link.read(0x50, 16))
    return {"index": index, "peak_i": peak_i, "peak_v": peak_v,
            "sample_us": sample_us, "fire_sample": fire_sample}


def read_hk(link):
    b = link.read(0x00, 0x58)
    f = lambda off: struct.unpack_from("<f", b, off)[0]
    status = b[0]
    flags = [name for i, name in enumerate(STATUS_BITS) if status & (1 << i)]
    return {
        "status": status,
        "flags": " ".join(flags) or "SAFE",
        "uptime_s": f(0x01),
        "frit_mbar": f(0x10), "frit_degC": f(0x14),
        "tank_mbar": f(0x18), "tank_degC": f(0x1C),
        "tank_ntc1_degC": f(0x20), "tank_ntc2_degC": f(0x24),
        "pib_ntc_degC": f(0x28), "mcu_degC": f(0x2C),
        "flow_ulmin": f(0x30), "flow_degC": f(0x34),
        "ign_supply_v": f(0x38), "ign_ntc_degC": f(0x3C), "dchg_ntc_degC": f(0x40),
        "pulse": dict(zip(("index", "peak_i", "peak_v"), struct.unpack_from("<IHH", b, 0x50))),
        "pib_state": b[0x44], "outputs": b[0x45], "inputs": b[0x46], "valves": b[0x47],
        "ntc_raw": struct.unpack_from("<HHH", b, 0x48),
    }


def sig(v, n=SIG_FIGS):
    """v rounded to n significant figures, in plain (non-exponent) notation."""
    if v != v or v in (float("inf"), float("-inf")):
        return str(v)
    if v == 0:
        return "0"
    v = float("%.*g" % (n, v))  # round first, so 9.9996 -> 10.00, not 10.000
    decimals = max(0, n - 1 - math.floor(math.log10(abs(v))))
    return "%.*f" % (decimals, v)


def fmt(v, unit):
    return "--" if v != v else "%s %s" % (sig(v), unit)


def counts_to_pin_v(counts):
    return counts / 4095 * ADC_VREF_V


def pulse_line(p):
    if p["index"] == 0:
        return "none since boot"
    return "#%d   peak V %d cnt (~%s)   peak I %d cnt (%s at pin, amps scale TBD)" % (
        p["index"],
        p["peak_v"], fmt(counts_to_pin_v(p["peak_v"]) * IGN_V_PER_PIN_V, "V"),
        p["peak_i"], fmt(counts_to_pin_v(p["peak_i"]), "V"))


def raw_ntc(counts):
    return "-- C   (%d cnt, %.3f V at pin; part not set yet)" % (counts, counts_to_pin_v(counts))


def valve_text(hk, n):
    st = VALVE_STATES[(hk["valves"] >> (2 * (n - 1))) & 0x03]
    pulsing = (n == 1 and hk["valves"] & 0x40) or (n == 3 and hk["valves"] & 0x80)
    return st + ("   (coil pulsing)" if pulsing else "")


def io_rows(hk):
    state = hk["pib_state"]
    rows = [["state", "PIB", PIB_STATES[state] if state < len(PIB_STATES) else "? (%d)" % state]]
    rows += [["valves" if n == 1 else "", VALVE_NAMES[n], valve_text(hk, n)] for n in (1, 2, 3)]
    rows += [["outputs" if i == 0 else "", "%s  (%s)" % (pin, name), "ON" if hk["outputs"] >> i & 1 else "off"]
             for i, (name, pin, _) in enumerate(OUTPUTS)]
    rows += [["inputs" if i == 0 else "", name, "high" if hk["inputs"] >> i & 1 else "low"]
             for i, name in enumerate(INPUTS)]
    return rows


def print_io(hk):
    print_table("PIB outputs and inputs   (valve state = last command; there is no position sensor)",
                ["group", "item", "state"], io_rows(hk), "lll")


def print_hk(hk):
    t = lambda key: fmt(hk[key], "C")
    rows = [
        ["status", "flags", "0x%02X  %s" % (hk["status"], hk["flags"])],
        ["pressure", "frit", "%s @ %s" % (fmt(hk["frit_mbar"], "mbar"), t("frit_degC"))],
        ["", "tank", "%s @ %s" % (fmt(hk["tank_mbar"], "mbar"), t("tank_degC"))],
        ["temperature", "PIB", t("pib_ntc_degC")],
        ["", "MCU", t("mcu_degC")],
        ["", "igniter", t("ign_ntc_degC")],
        ["", "tank 1", t("tank_ntc1_degC") if hk["tank_ntc1_degC"] == hk["tank_ntc1_degC"] else raw_ntc(hk["ntc_raw"][0])],
        ["", "tank 2", t("tank_ntc2_degC") if hk["tank_ntc2_degC"] == hk["tank_ntc2_degC"] else raw_ntc(hk["ntc_raw"][1])],
        ["", "discharge", t("dchg_ntc_degC") if hk["dchg_ntc_degC"] == hk["dchg_ntc_degC"] else raw_ntc(hk["ntc_raw"][2])],
        ["igniter", "supply", fmt(hk["ign_supply_v"], "V", 0)],
    ]
    pk = hk["pulse"]
    if pk["index"] == 0:
        rows.append(["", "last pulse", "none since boot"])
    else:
        rows += [
            ["", "last pulse", "#%d" % pk["index"]],
            ["", "peak V", "%s   (%d cnt)" % (fmt(counts_to_pin_v(pk["peak_v"]) * IGN_V_PER_PIN_V, "V"), pk["peak_v"])],
            ["", "peak I", "%s at pin   (%d cnt, amps scale TBD)" % (fmt(counts_to_pin_v(pk["peak_i"]), "V"), pk["peak_i"])],
        ]
    rows += [
        ["flow", "rate", "%s @ %s" % (fmt(hk["flow_ulmin"], "uL/min"), t("flow_degC"))],
    ]
    rows += io_rows(hk)
    print_table("PIB housekeeping   uptime %s   (-- = no sensor or part unknown)" % fmt(hk["uptime_s"], "s"),
                ["group", "sensor", "value"], rows, "lll")


def arm(link, hold_s, stay_armed):
    t_end = time.time() + hold_s
    while True:
        link.write(0x07, [ARM_CODE])
        time.sleep(0.3)
        print_hk(read_hk(link))
        if time.time() >= t_end:
            break
        time.sleep(HEARTBEAT_S)
    if not stay_armed:
        link.write(0x06)
        time.sleep(0.3)
        print_hk(read_hk(link))


ON_OFF = {"on": 1, "off": 0}
OPEN_CLOSE = {"open": 1, "close": 0}


def set_valve(link, which, action):
    for n in ([1, 2, 3] if which == "all" else [int(which)]):
        link.write(REG_SET_VALVE, [n, OPEN_CLOSE[action]])
    time.sleep(0.3)
    print_io(read_hk(link))


def set_output(link, names, level):
    for i, (name, _, _) in enumerate(OUTPUTS):
        if name in names:
            link.write(REG_SET_OUTPUT, [i, ON_OFF[level]])
    time.sleep(0.2)
    print_io(read_hk(link))


HELP_DESCRIPTION = """\
Control the PIB from a laptop or Raspberry Pi, acting as the spacecraft computer (CDH).
Each run sends one command and exits.

usage pattern:
  pib_cdh.py [options] <command> [command options]
  pib_cdh.py [options] arm <command> [command options]

On the bench, always pass --link nucleo: the laptop talks to the Nucleo over USB,
and the Nucleo passes each SPI transaction to the PIB."""

HELP_EPILOG = """\
SAFE and ARMED:
  The PIB starts SAFE (valves closed, ARM line low). 'safe' works from any state:
  it drops the igniter / discharge enables, closes all three valves and lowers
  the ARM line. It leaves heaters, vbat, pdrst and wdg alone.
  ARM turns on the battery bus, raises the ARM line, waits up to 200 ms for the
  12 V supply, opens the valves and sets the ARMED status bit. It needs battery
  voltage on J3 pins 1-3; with only 3.3 V / 5 V the PIB can't arm. While ARMED,
  the PIB SAFEs itself if no ARM or FIRE command arrives for 10 s. Nothing
  re-sends ARM in the background.

putting 'arm' in front of a command:
  'arm <command>' sends ARM once, waits up to 1 s for ARMED, then runs the command.
  It stops with an error, without running the command, if the PIB doesn't arm.

bench control ('io', 'valve', 'heater', 'out'):
  These work in any state, without arming. 'out' names:
    vbat       VBAT_EN       battery bus enable
    arm        ARM_SAFE      ARM line (turns on the 12 V that drives the valves)
    igniter    IGNITER_EN    igniter HV supply enable
    discharge  DISCHARGE_EN  discharge HV supply enable
    htr1, htr2 HTR1/2_EN     heaters (also turn on vbat)
    pdrst      PD_RST        igniter peak-detector reset
    wdg        WDG_IN        watchdog input pin
  Valves need 12 V: in SAFE, run 'out arm on' before 'valve'.
  Timeouts: arm / igniter / discharge turned on with 'out' SAFE the PIB 10 s
  after the last such command (closing the valves); heaters turn off 60 s after
  the last heater-on. vbat, pdrst and wdg have no timeout.

examples (bench, from tools/):
  python3 pib_cdh.py --link nucleo hk
  python3 pib_cdh.py --link nucleo watch
  python3 pib_cdh.py --link nucleo arm fire 5 --csv shot1.csv
  python3 pib_cdh.py --link nucleo fire 2.5 --wave
  python3 pib_cdh.py --link nucleo arm seq 10 5 2 --csv run1
  python3 pib_cdh.py --link nucleo wave
  python3 pib_cdh.py --link nucleo safe
  python3 pib_cdh.py --link nucleo raw 0x50 8

bench control examples:
  python3 pib_cdh.py --link nucleo io
  python3 pib_cdh.py --link nucleo out arm on
  python3 pib_cdh.py --link nucleo valve 2 open
  python3 pib_cdh.py --link nucleo valve all close
  python3 pib_cdh.py --link nucleo heater 1 on
  python3 pib_cdh.py --link nucleo heater both off
  python3 pib_cdh.py --link nucleo out discharge off

more help on one command:
  python3 pib_cdh.py fire --help

first-time setup:
  python3 -m pip install pyserial
  Flash the Nucleo with RUN_SPI_BRIDGE = 1 (Core/Src/main.c).
  ls /dev/cu.usbmodem* (macOS) or ls /dev/ttyACM* (Linux) lists serial ports;
  pass the Nucleo's with --port if more than one shows up."""


EXAMPLE = "python3 pib_cdh.py --link nucleo "


class Parser(argparse.ArgumentParser):
    def __init__(self, *args, example=None, **kwargs):
        super().__init__(*args, **kwargs)
        self.example = example

    def error(self, message):
        example = self.example or "hk"
        subs = next((a.choices for a in self._actions if isinstance(a, argparse._SubParsersAction)), None)
        typed = [w for w in sys.argv[1:] if not w.startswith("-")]
        for word in typed if subs else []:
            near = difflib.get_close_matches(word, subs, n=1)
            if near:
                example = subs[near[0]].example
                break
        self.print_usage(sys.stderr)
        self.exit(2, "%s: error: %s\nexample: %s%s\n" % (self.prog, message, EXAMPLE, example))


def main():
    raw_fmt = argparse.RawDescriptionHelpFormatter
    p = Parser(prog="pib_cdh.py", example="hk", description=HELP_DESCRIPTION, epilog=HELP_EPILOG,
                                formatter_class=raw_fmt)
    g = p.add_argument_group("connection (options go before the command)")
    g.add_argument("--link", choices=["spi", "i2c", "nucleo"], default="spi",
                   help="how to reach the PIB: nucleo = through the Nucleo bridge over USB (use this on the bench); "
                        "spi / i2c = straight from a Raspberry Pi's own pins. Default: spi")
    g.add_argument("--port", help="Nucleo serial port, e.g. /dev/cu.usbmodem1103 (macOS) or /dev/ttyACM0 (Linux). "
                                  "Default: the only /dev/cu.usbmodem* or /dev/ttyACM* port, if exactly one exists")
    g.add_argument("--baud", type=int, default=115200, help="Nucleo serial speed; must match its firmware. Default: 115200")
    g.add_argument("--spi-bus", type=int, default=0, help="Raspberry Pi SPI bus (--link spi). Default: 0")
    g.add_argument("--spi-dev", type=int, default=0, help="Raspberry Pi SPI chip select (--link spi). Default: 0")
    g.add_argument("--spi-hz", type=int, default=1_000_000, help="Raspberry Pi SPI clock in Hz (--link spi). Default: 1000000")
    g.add_argument("--i2c-bus", type=int, default=1, help="Raspberry Pi I2C bus (--link i2c). Default: 1")
    g.add_argument("--i2c-addr", type=lambda s: int(s, 0), default=0x42, help="PIB I2C address (--link i2c). Default: 0x42")
    sub = p.add_subparsers(dest="cmd", title="commands", metavar="<command>")
    sub.add_parser("hk", example="hk", formatter_class=raw_fmt, help="read and print housekeeping once",
                   description="Read registers 0x00-0x57 once and print them with units: status flags, uptime,\n"
                               "frit and tank pressure/temperature, PIB / MCU / igniter / tank / discharge\n"
                               "temperatures, igniter supply voltage, flow, and the last pulse header.\n"
                               "'--' means no reading (no sensor fitted, or its part isn't known yet).")
    w = sub.add_parser("watch", example="watch --period 2", formatter_class=raw_fmt, help="print housekeeping repeatedly until Ctrl+C",
                       description="Same as 'hk', repeated until you press Ctrl+C.")
    w.add_argument("--period", type=float, default=1.0, help="seconds between reads. Default: 1")
    a = sub.add_parser("arm", example="arm --hold 10", formatter_class=raw_fmt, help="ARM, re-send ARM every 2 s for --hold seconds, then SAFE",
                       description="Used alone: ARM, print housekeeping, re-send ARM every 2 s until --hold seconds\n"
                                   "have passed, then SAFE (unless --stay-armed).\n"
                                   "Used in front of another command ('arm fire 5'): ARM once, wait for ARMED,\n"
                                   "then run that command. See 'pib_cdh.py --help'.")
    a.add_argument("--hold", type=float, default=5.0, help="seconds to keep re-sending ARM. Default: 5")
    a.add_argument("--stay-armed", action="store_true",
                   help="don't SAFE at the end; the PIB still SAFEs itself 10 s after the last ARM")
    sub.add_parser("safe", example="safe", formatter_class=raw_fmt, help="SAFE the payload: valves closed, ARM line low",
                   description="Send SAFE (register 0x06). Valves close and the ARM line drops.")
    fp = sub.add_parser("fire", example="arm fire 5", formatter_class=raw_fmt, help="fire one igniter pulse (PIB must be ARMED)",
                        description="Send FIRE_PULSE (register 0x08) with the pulse width, wait out the pulse, then wait up to 0.5 s\n"
                                    "for DRDY and print the pulse number and igniter peak V and I.\n"
                                    "Refuses if the PIB isn't ARMED; use 'arm fire <us>' to arm first.",
                        epilog="examples:\n  pib_cdh.py --link nucleo arm fire 5\n"
                               "  pib_cdh.py --link nucleo fire 2.5 --csv shot2.csv\n"
                               "  pib_cdh.py --link nucleo arm fire 500000")
    fp.add_argument("width_us", type=float, help="igniter pulse width in microseconds, in 0.1 us steps (e.g. 5 or 2.5)")
    fp.add_argument("--wave", action="store_true", help="print the waveform after the pulse")
    fp.add_argument("--csv", metavar="FILE", help="save the waveform to tests/FILE instead of printing it")
    tp = sub.add_parser("seq", example="arm seq 10 5 2", formatter_class=raw_fmt,
                        help="<count> <width_us> <hz>   fire a sequence of igniter pulses (PIB must be ARMED)",
                        description="Fire <count> igniter pulses of <width_us> each, one every 1/<hz> seconds, timed\n"
                                    "from the laptop. Each pulse is a normal FIRE (see 'fire --help'), so each one\n"
                                    "also keeps the PIB ARMED. Stops if the PIB SAFEs part way through.\n"
                                    "With --csv NAME, each pulse's waveform is saved to tests/NAME_0001.csv,\n"
                                    "tests/NAME_0002.csv, and so on. Reading a waveform takes time over the link,\n"
                                    "so high rates may run slower than asked; the summary prints the real rate.",
                        epilog="examples:\n  pib_cdh.py --link nucleo arm seq 10 5 2\n"
                               "  pib_cdh.py --link nucleo arm seq 50 2.5 1 --csv run1")
    tp.add_argument("count", type=int, help="number of pulses")
    tp.add_argument("width_us", type=float, help="pulse width in microseconds, in 0.1 us steps")
    tp.add_argument("hz", type=float, help="pulses per second")
    tp.add_argument("--csv", metavar="NAME", help="save each pulse's waveform to tests/NAME_0001.csv, NAME_0002.csv, ...")
    wv = sub.add_parser("wave", example="wave --csv shot1.csv", formatter_class=raw_fmt, help="read the last pulse's voltage/current waveform",
                        description="Read the 80 voltage/current sample pairs from registers 0x60-0x73: 12-bit ADC\n"
                                    "counts, about 0.47 us apart, starting at ignition. Reading them lowers DRDY.\n"
                                    "The data stays until the next FIRE, so you can read it more than once.")
    wv.add_argument("--csv", metavar="FILE", help="save to tests/FILE (columns: sample, time_us, voltage_counts, current_counts)")
    sub.add_parser("io", example="io", formatter_class=raw_fmt, help="print state, valves, output pins and power-good inputs",
                   description="Read registers 0x44-0x47 and print the state machine state, each valve's last\n"
                               "commanded state, every output pin the PIB drives, and the four power-good inputs.\n"
                               "'hk' prints the same rows at the bottom of its table.")
    vp = sub.add_parser("valve", example="valve 1 open", formatter_class=raw_fmt, help="<1|2|3|all> <open|close>   open or close a valve",
                        description="Send SET_VALVE (register 0x09). Works in any state.\n"
                                    "V1 and V3 are latching: the PIB pulses the coil for 100 ms and the valve stays put.\n"
                                    "V2 is the normally-closed solenoid: it stays open only while powered.\n"
                                    "The valve drivers run from the 12 V supply, which comes up with the ARM line.\n"
                                    "In SAFE, run 'out arm on' first or the coil gets no power (the PIB still\n"
                                    "records the new state, since it has no position sensor). 'out arm on' times\n"
                                    "out after 10 s, which SAFEs the PIB and closes all three valves.",
                        epilog="examples:\n  pib_cdh.py --link nucleo out arm on\n"
                               "  pib_cdh.py --link nucleo valve 1 open\n"
                               "  pib_cdh.py --link nucleo valve all close")
    vp.add_argument("which", choices=["1", "2", "3", "all"], help="valve number, or all")
    vp.add_argument("action", choices=["open", "close"])
    hp = sub.add_parser("heater", example="heater both on", formatter_class=raw_fmt, help="<1|2|both> <on|off>   switch heaters (60 s auto-off)",
                        description="Send SET_OUTPUT (register 0x0B) for HTR1_EN / HTR2_EN. Turning a heater on also\n"
                                    "turns on the battery bus (vbat). The PIB turns all heaters off 60 s after the\n"
                                    "last heater-on command.")
    hp.add_argument("which", choices=["1", "2", "both"])
    hp.add_argument("level", choices=["on", "off"])
    op = sub.add_parser("out", example="out arm on", formatter_class=raw_fmt, help="<name> <on|off>   drive one PIB output pin; names below",
                        description="Send SET_OUTPUT (register 0x0B). Works in any state and does not go through\n"
                                    "the ARM sequence or its 12 V check. 'safe' turns arm / igniter / discharge back off.\n"
                                    "Like ARMED, these three time out: the PIB SAFEs itself 10 s after the last\n"
                                    "'out arm|igniter|discharge on' (or ARM) command. Re-send it to keep them up.\n\n"
                                    "outputs:\n" + "\n".join("  %-10s %-13s %s" % o for o in OUTPUTS))
    op.add_argument("name", choices=[o[0] for o in OUTPUTS], metavar="name",
                    help="one of: " + ", ".join(o[0] for o in OUTPUTS))
    op.add_argument("level", choices=["on", "off"])
    r = sub.add_parser("raw", example="raw 0x50 8", formatter_class=raw_fmt, help="read raw register bytes, printed in hex (debugging)",
                       description="Read n bytes starting at a register and print them in hex.",
                       epilog="example:\n  pib_cdh.py --link nucleo raw 0x50 8    (the pulse header)")
    r.add_argument("reg", type=lambda s: int(s, 0), help="first register, decimal or hex (e.g. 0x50)")
    r.add_argument("n", type=int, help="number of bytes to read (1-255)")

    argv = sys.argv[1:]
    if not argv:
        p.print_help()
        return
    arm_first = False
    for i, a in enumerate(argv):
        if a in sub.choices:
            if a == "arm" and i + 1 < len(argv) and argv[i + 1] in sub.choices and argv[i + 1] != "arm":
                arm_first = True
                del argv[i]
            break
    args, extra = p.parse_known_args(argv)
    if extra:
        (sub.choices[args.cmd] if args.cmd else p).error("unrecognized arguments: %s" % " ".join(extra))
    if args.cmd is None:
        p.print_help()
        return

    if args.link == "spi":
        link = SpiLink(args.spi_bus, args.spi_dev, args.spi_hz)
    elif args.link == "i2c":
        link = I2cLink(args.i2c_bus, args.i2c_addr)
    else:
        link = NucleoLink(args.port, args.baud)

    if arm_first:
        arm_once(link)

    if args.cmd == "hk":
        print_hk(read_hk(link))
    elif args.cmd == "watch":
        while True:
            print_hk(read_hk(link))
            time.sleep(args.period)
    elif args.cmd == "arm":
        arm(link, args.hold, args.stay_armed)
    elif args.cmd == "safe":
        link.write(0x06)
    elif args.cmd == "io":
        print_io(read_hk(link))
    elif args.cmd == "valve":
        set_valve(link, args.which, args.action)
    elif args.cmd == "heater":
        set_output(link, ["htr1", "htr2"] if args.which == "both" else ["htr" + args.which], args.level)
    elif args.cmd == "out":
        set_output(link, [args.name], args.level)
    elif args.cmd == "raw":
        print(link.read(args.reg, args.n).hex(" "))
    elif args.cmd == "fire":
        if fire(link, args.width_us) and (args.wave or args.csv):
            print_wave(read_wave(link), args.csv)
    elif args.cmd == "seq":
        seq(link, args.count, args.width_us, args.hz, args.csv)
    elif args.cmd == "wave":
        print_wave(read_wave(link), args.csv)


if __name__ == "__main__":
    main()
