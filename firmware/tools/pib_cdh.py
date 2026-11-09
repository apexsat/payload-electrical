#!/usr/bin/env python3
import argparse
import struct
import time

READ = 0x80
ARM_CODE = 0x53
HEARTBEAT_S = 2.0

STATUS_BITS = ["ARMED", "FLOW_FAULT", "IGNITER_FAULT", "DISCHARGE_FAULT", "DRDY", "BATT_UVLO", "TEMP_FAULT"]


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


def read_hk(link):
    b = link.read(0x00, 0x38)
    f = lambda off, n=1: struct.unpack_from("<%df" % n, b, off)
    status = b[0]
    flags = [name for i, name in enumerate(STATUS_BITS) if status & (1 << i)]
    return {
        "status": "0x%02X %s" % (status, " ".join(flags) or "SAFE"),
        "uptime_s": f(0x01)[0],
        "frit_mbar_degC": f(0x10, 2),
        "tank_mbar_degC": f(0x18, 2),
        "tank_ntc_degC": f(0x20, 2),
        "pib_degC": f(0x28, 2),
        "flow_ulmin_degC": f(0x30, 2),
    }


def print_hk(hk):
    for k, v in hk.items():
        if isinstance(v, tuple):
            v = "  ".join("%.2f" % x for x in v)
        elif isinstance(v, float):
            v = "%.1f" % v
        print("%-16s %s" % (k, v))
    print()


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


def main():
    p = argparse.ArgumentParser(description="Talk to the PIB over SPI1 or I2C2 (PIB test 2)")
    p.add_argument("--link", choices=["spi", "i2c"], default="spi")
    p.add_argument("--spi-bus", type=int, default=0)
    p.add_argument("--spi-dev", type=int, default=0)
    p.add_argument("--spi-hz", type=int, default=1_000_000)
    p.add_argument("--i2c-bus", type=int, default=1)
    p.add_argument("--i2c-addr", type=lambda s: int(s, 0), default=0x42)
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("hk", help="read housekeeping once")
    w = sub.add_parser("watch", help="read housekeeping every second")
    w.add_argument("--period", type=float, default=1.0)
    a = sub.add_parser("arm", help="ARM, keep it alive with heartbeats, then SAFE")
    a.add_argument("--hold", type=float, default=5.0)
    a.add_argument("--stay-armed", action="store_true")
    sub.add_parser("safe", help="SAFE")
    h = sub.add_parser("heater", help="heater override")
    h.add_argument("state", choices=["on", "off", "auto"])
    s = sub.add_parser("setpoint", help="tank temperature setpoint")
    s.add_argument("degC", type=float)
    r = sub.add_parser("raw", help="read raw bytes")
    r.add_argument("reg", type=lambda s: int(s, 0))
    r.add_argument("n", type=int)
    args = p.parse_args()

    if args.link == "spi":
        link = SpiLink(args.spi_bus, args.spi_dev, args.spi_hz)
    else:
        link = I2cLink(args.i2c_bus, args.i2c_addr)

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
    elif args.cmd == "heater":
        link.write(0x0A, {"on": [1, 1], "off": [1, 0], "auto": [0, 0]}[args.state])
    elif args.cmd == "setpoint":
        link.write(0x0C, struct.pack("<f", args.degC))
    elif args.cmd == "raw":
        print(link.read(args.reg, args.n).hex(" "))


if __name__ == "__main__":
    main()
