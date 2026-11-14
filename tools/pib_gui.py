import collections
import math
import queue
import struct
import sys
import time

from PySide6 import QtCore, QtGui, QtWidgets

import pib_cdh as cdh

W, H = 1920, 1080
INK = "#000000"
TXT2 = "#2B2B2B"
GREY = "#7A7A7A"
RULE = "#8C8C8C"
FAINT = "#D0D0D0"
BG = "#B4B4B4"
PANEL = "#E4E4E4"
WHITE = "#FFFFFF"
RED = "#E00000"
YELLOW = "#FFD400"
GREEN = "#00A651"
FLUID = "#D40000"
POWER = "#F29200"
OFF = "#808080"
LIQUID = "#A9CCEE"
BLUE = "#0050E6"
ORANGE = "#E86A00"
SCOPE = "#0A0A0A"
GRAT = "#3C3C3C"
CH1 = "#FFD500"
CH2 = "#00C8F0"
SANS = ["Helvetica", "Arial"]
MONO = ["Menlo", "Courier New"]
WEIGHTS = {400: QtGui.QFont.Weight.Normal, 500: QtGui.QFont.Weight.Medium,
           600: QtGui.QFont.Weight.DemiBold, 700: QtGui.QFont.Weight.Bold}

FAULTS = ["FLOW_FAULT", "IGNITER_FAULT", "DISCHARGE_FAULT", "BATT_UVLO", "TEMP_FAULT"]
VALVES = [(1, "ISO1", "V1 tank, latching", 1230), (2, "ISO2", "V2 NC solenoid", 1120), (3, "ISO3", "V3 frit, latching", 1010)]
OUT_LABELS = ["Battery bus", "Valve 12 V", "Igniter HV", "Discharge HV", "Heater 1", "Heater 2", "Peak-detect reset", "Watchdog pin"]
TREND_S = 600.0
POLL_S = 1.0


def num(v, unit="", n=None):
    if v is None or v != v:
        return "--"
    return cdh.sig(v, n or cdh.SIG_FIGS) + (" " + unit if unit else "")


def press(v):
    if v is None or v != v:
        return "--"
    return "%.0f" % v if abs(v) >= 100 else cdh.sig(v, 3)


def axis(v, span):
    return "%.0f" % v if span >= 50 else ("%.1f" % v if span >= 5 else "%.2f" % v)


def describe(link):
    ser = getattr(link, "ser", None)
    if ser is not None:
        return "%s, %d baud" % (ser.port, ser.baudrate)
    if hasattr(link, "spi"):
        return "Raspberry Pi SPI"
    return "Raspberry Pi I2C 0x%02X" % getattr(link, "addr", 0)


class Worker(QtCore.QObject):
    hk = QtCore.Signal(object, float)
    wave = QtCore.Signal(object, object)
    log = QtCore.Signal(str)
    link = QtCore.Signal(bool, str)
    heartbeat = QtCore.Signal(bool, float)
    busy = QtCore.Signal(str)

    def __init__(self, make_link):
        super().__init__()
        self.make_link = make_link
        self.q = queue.Queue()
        self.abort = False
        self.lnk = None
        self.hb = False
        self.hb_next = 0.0
        self.arm_sent = 0.0
        self.last_wave = None
        self.last_width = None

    def send(self, *cmd):
        self.q.put(cmd)

    def set_hb(self, on):
        self.hb = on
        self.hb_next = time.monotonic() + cdh.HEARTBEAT_S
        self.heartbeat.emit(on, self.hb_next)

    def keepalive(self):
        if self.hb and time.monotonic() >= self.hb_next:
            self.lnk.write(0x07, [cdh.ARM_CODE])
            self.hb_next = time.monotonic() + cdh.HEARTBEAT_S
            self.heartbeat.emit(True, self.hb_next)

    def run(self):
        next_poll = 0.0
        next_connect = 0.0
        while True:
            if self.lnk is None:
                if time.monotonic() >= next_connect:
                    self.open_link()
                    next_connect = time.monotonic() + 2.0
                    next_poll = 0.0
                try:
                    cmd = self.q.get(timeout=0.2)
                except queue.Empty:
                    continue
                if cmd[0] == "quit":
                    return
                if self.lnk is None:
                    self.log.emit("no link to the PIB: %s not sent" % cmd[0])
                    continue
                self.q.put(cmd)
                continue
            try:
                self.keepalive()
                if time.monotonic() >= next_poll:
                    self.poll()
                    next_poll = time.monotonic() + POLL_S
                wake = min(next_poll, self.hb_next) if self.hb else next_poll
                try:
                    cmd = self.q.get(timeout=max(0.0, wake - time.monotonic()))
                except queue.Empty:
                    continue
                if cmd[0] == "quit":
                    if self.hb:
                        self.lnk.write(0x06)
                    return
                self.handle(cmd)
                next_poll = min(next_poll, time.monotonic() + 0.3)
            except Exception as e:
                self.drop(e)

    def open_link(self):
        try:
            self.lnk = self.make_link()
        except (Exception, SystemExit) as e:
            self.lnk = None
            msg = str(e).splitlines()[0] if str(e) else type(e).__name__
            self.link.emit(False, msg)
            return
        self.link.emit(True, describe(self.lnk))
        self.log.emit("link up: " + describe(self.lnk))
        try:
            hk = cdh.read_hk(self.lnk)
            if hk["pulse"]["index"]:
                self.last_wave = cdh.read_wave(self.lnk)
                self.wave.emit(self.last_wave, None)
        except Exception as e:
            self.drop(e)

    def drop(self, e):
        ser = getattr(self.lnk, "ser", None)
        if ser is not None:
            try:
                ser.close()
            except Exception:
                pass
        self.lnk = None
        if self.hb:
            self.set_hb(False)
        self.link.emit(False, str(e) or type(e).__name__)
        self.log.emit("link lost: %s" % (str(e) or type(e).__name__))

    def poll(self):
        hk = cdh.read_hk(self.lnk)
        self.hk.emit(hk, time.time())
        if self.hb and hk["pib_state"] == 0 and time.monotonic() - self.arm_sent > 2.0:
            self.set_hb(False)
            self.log.emit("PIB is SAFE, heartbeat stopped")

    def handle(self, cmd):
        kind = cmd[0]
        if kind == "safe":
            self.lnk.write(0x06)
            if self.hb:
                self.set_hb(False)
            self.log.emit("SAFE sent")
        elif kind == "arm":
            self.lnk.write(0x07, [cdh.ARM_CODE])
            self.arm_sent = time.monotonic()
            self.set_hb(cmd[1])
            self.log.emit("ARM sent" + (", heartbeat on" if cmd[1] else ", no heartbeat (SAFE in 10 s)"))
        elif kind == "hb":
            if cmd[1] != self.hb:
                self.set_hb(cmd[1])
                self.log.emit("heartbeat " + ("on" if cmd[1] else "off (PIB SAFEs 10 s after the last ARM)"))
        elif kind == "valve":
            for n in ([1, 2, 3] if cmd[1] == 0 else [cmd[1]]):
                self.lnk.write(cdh.REG_SET_VALVE, [n, 1 if cmd[2] else 0])
            self.log.emit("%s %s" % ("all valves" if cmd[1] == 0 else "ISO%d" % cmd[1], "open" if cmd[2] else "close"))
        elif kind == "out":
            self.lnk.write(cdh.REG_SET_OUTPUT, [cmd[1], 1 if cmd[2] else 0])
            self.log.emit("%s %s" % (cdh.OUTPUTS[cmd[1]][1], "on" if cmd[2] else "off"))
        elif kind == "fire":
            self.abort = False
            self.busy.emit("FIRING…")
            self.fire_one(cmd[1], cmd[2], True)
            self.busy.emit("")
        elif kind == "seq":
            self.seq(*cmd[1:])
        elif kind == "savewave":
            if self.last_wave:
                self.save(self.last_wave, cmd[1])
            else:
                self.log.emit("no waveform to save yet")

    def fire_one(self, width_us, save_name, want_wave):
        steps = round(width_us / cdh.PULSE_STEP_US)
        if not cdh.read_status(self.lnk) & 0x01:
            self.log.emit("not fired: PIB is not ARMED")
            return False
        self.lnk.write(0x08, struct.pack("<I", steps))
        time.sleep(steps * cdh.PULSE_STEP_US * 1e-6)
        t_end = time.monotonic() + cdh.DRDY_TIMEOUT_S
        while not cdh.read_status(self.lnk) & 0x10:
            if time.monotonic() > t_end:
                self.log.emit("fire sent, but DRDY did not come up within %s s" % cdh.sig(cdh.DRDY_TIMEOUT_S))
                return False
            time.sleep(0.02)
        self.last_width = steps * cdh.PULSE_STEP_US
        if want_wave or save_name:
            self.last_wave = cdh.read_wave(self.lnk)
            self.wave.emit(self.last_wave, self.last_width)
            index = self.last_wave[1]["index"]
        else:
            index = cdh.read_pulse_header(self.lnk)["index"]
        self.log.emit("fired %.1f µs, pulse #%d" % (self.last_width, index))
        if save_name:
            self.save(self.last_wave, save_name)
        return True

    def save(self, wave, name):
        path = "%s_%04d.csv" % (name, wave[1]["index"])
        cdh.print_wave(wave, path)
        self.log.emit("wrote tests/" + path)

    def seq(self, count, width_us, hz, save_name):
        self.abort = False
        period = 1.0 / hz
        late = 0
        done = 0
        t0 = time.monotonic()
        t_last = t0
        for k in range(count):
            while not self.abort and t0 + k * period - time.monotonic() > 0:
                self.keepalive()
                time.sleep(min(0.05, max(0.0, t0 + k * period - time.monotonic())))
            if self.abort:
                self.log.emit("sequence stopped after %d of %d" % (done, count))
                break
            if k and time.monotonic() - (t0 + k * period) > 0.01:
                late += 1
            t_last = time.monotonic()
            self.busy.emit("PULSE %d / %d" % (k + 1, count))
            if not self.fire_one(width_us, save_name, k == count - 1):
                self.log.emit("sequence stopped after %d of %d" % (done, count))
                break
            done += 1
            if k < count - 1 and t0 + (k + 1) * period - time.monotonic() > 0.25:
                self.poll()
        else:
            elapsed = t_last - t0
            rate = (count - 1) / elapsed if count > 1 and elapsed > 0 else hz
            self.log.emit("sequence done: %d pulses at %s Hz" % (count, cdh.sig(rate, 3)))
            if late:
                self.log.emit("%d pulses started late" % late)
        self.busy.emit("")


class Console(QtWidgets.QWidget):
    def __init__(self, make_link, link_name):
        super().__init__()
        self.setWindowTitle("APEX Bench Interface")
        self.setMouseTracking(True)
        SANS[:0] = [QtGui.QFontDatabase.systemFont(QtGui.QFontDatabase.SystemFont.GeneralFont).family()]
        MONO[:0] = [QtGui.QFontDatabase.systemFont(QtGui.QFontDatabase.SystemFont.FixedFont).family()]
        self.link_name = link_name
        self.hk = None
        self.hk_time = 0.0
        self.wave = None
        self.wave_width = None
        self.link_ok = False
        self.link_msg = "connecting…"
        self.hb = False
        self.hb_next = 0.0
        self.busy = ""
        self.stay_armed = True
        self.save_csv = False
        self.trend = collections.deque()
        self.logs = collections.deque(maxlen=8)
        self.hits = []
        self.s, self.ox, self.oy = 1.0, 0.0, 0.0

        self.edits = {}
        for key, rect, val in [("width", (1448, 702, 84, 38), "5.0"), ("count", (1448, 768, 60, 38), "10"),
                               ("swidth", (1518, 768, 72, 38), "5.0"), ("hz", (1600, 768, 60, 38), "2"),
                               ("name", (1668, 816, 150, 30), "demo_run")]:
            e = QtWidgets.QLineEdit(val, self)
            e.setStyleSheet("QLineEdit{border:1px solid %s;border-radius:0;background:%s;color:%s;padding:0 6px}" % (INK, WHITE, INK))
            self.edits[key] = (e, rect)

        self.worker = Worker(make_link)
        self.qthread = QtCore.QThread(self)
        self.worker.moveToThread(self.qthread)
        self.qthread.started.connect(self.worker.run)
        self.worker.hk.connect(self.on_hk)
        self.worker.wave.connect(self.on_wave)
        self.worker.log.connect(self.on_log)
        self.worker.link.connect(self.on_link)
        self.worker.heartbeat.connect(self.on_hb)
        self.worker.busy.connect(self.on_busy)
        self.qthread.start()

        self.timer = QtCore.QTimer(self)
        self.timer.timeout.connect(self.update)
        self.timer.start(200)

        screen = QtGui.QGuiApplication.primaryScreen().availableGeometry()
        s = min(screen.width() * 0.95 / W, screen.height() * 0.92 / H, 1.0)
        self.resize(int(W * s), int(H * s))
        self.setMinimumSize(960, 540)

    def on_hk(self, hk, t):
        self.hk, self.hk_time = hk, t
        self.trend.append((t, hk["tank_mbar"], hk["frit_mbar"], hk["tank_ntc1_degC"], hk["tank_ntc2_degC"], hk["frit_degC"]))
        while self.trend and self.trend[0][0] < t - TREND_S:
            self.trend.popleft()

    def on_wave(self, wave, width):
        self.wave, self.wave_width = wave, width

    def on_log(self, msg):
        self.logs.append(time.strftime("%H:%M:%S") + "  " + msg)

    def on_link(self, ok, msg):
        self.link_ok, self.link_msg = ok, msg

    def on_hb(self, on, nxt):
        self.hb, self.hb_next = on, nxt

    def on_busy(self, text):
        self.busy = text

    def closeEvent(self, ev):
        self.worker.abort = True
        self.worker.send("quit")
        self.qthread.quit()
        self.qthread.wait(3000)
        ev.accept()

    def resizeEvent(self, ev):
        self.s = min(self.width() / W, self.height() / H)
        self.ox = (self.width() - W * self.s) / 2
        self.oy = (self.height() - H * self.s) / 2
        for e, (x, y, w, h) in self.edits.values():
            e.setGeometry(int(self.ox + x * self.s), int(self.oy + y * self.s), int(w * self.s), int(h * self.s))
            e.setFont(self.font_(14 if h > 32 else 13, 400, True, self.s))

    def logical(self, pos):
        return QtCore.QPointF((pos.x() - self.ox) / self.s, (pos.y() - self.oy) / self.s)

    def mousePressEvent(self, ev):
        pt = self.logical(ev.position())
        for r, cb in reversed(self.hits):
            if r.contains(pt):
                cb()
                self.update()
                return

    def mouseMoveEvent(self, ev):
        pt = self.logical(ev.position())
        hand = any(r.contains(pt) for r, _ in self.hits)
        self.setCursor(QtCore.Qt.CursorShape.PointingHandCursor if hand else QtCore.Qt.CursorShape.ArrowCursor)

    def value(self, key, kind, lo):
        try:
            v = kind(self.edits[key][0].text())
        except ValueError:
            v = None
        if v is None or not v > lo or (kind is float and math.isinf(v)):
            self.on_log("enter a valid number in the highlighted field")
            self.edits[key][0].setFocus()
            self.edits[key][0].selectAll()
            return None
        return v

    def csv_name(self):
        if not self.save_csv:
            return None
        name = self.edits["name"][0].text().strip()
        return name or "demo_run"

    def armed(self):
        return bool(self.hk and self.hk["status"] & 0x01)

    def confirm(self, text):
        box = QtWidgets.QMessageBox(self)
        box.setIcon(QtWidgets.QMessageBox.Icon.Warning)
        box.setWindowTitle("Confirm")
        box.setText(text)
        box.setStandardButtons(QtWidgets.QMessageBox.StandardButton.Yes | QtWidgets.QMessageBox.StandardButton.Cancel)
        box.setDefaultButton(QtWidgets.QMessageBox.StandardButton.Cancel)
        return box.exec() == QtWidgets.QMessageBox.StandardButton.Yes

    def do_safe(self):
        self.worker.abort = True
        self.worker.send("safe")

    def do_arm(self):
        self.worker.send("arm", self.stay_armed)

    def do_stay(self):
        self.stay_armed = not self.stay_armed
        if self.hb and not self.stay_armed:
            self.worker.send("hb", False)
        elif self.stay_armed and self.armed() and not self.hb:
            self.worker.send("hb", True)

    def do_fire(self):
        w = self.value("width", float, 0)
        if w is None:
            return
        if self.confirm("Fire one %.1f µs pulse?" % w):
            self.worker.send("fire", w, self.csv_name())

    def do_seq(self):
        n = self.value("count", int, 0)
        w = self.value("swidth", float, 0) if n else None
        hz = self.value("hz", float, 0) if w else None
        if hz is None:
            return
        if self.confirm("Fire %d pulses of %s µs at %s Hz?\nSAFE stops the sequence." % (n, "%.1f" % w, cdh.sig(hz, 3))):
            self.worker.send("seq", n, w, hz, self.csv_name())

    def font_(self, size, weight=400, mono=False, scale=1.0):
        f = QtGui.QFont()
        f.setFamilies(MONO if mono else SANS)
        f.setPixelSize(max(1, int(round(size * scale))))
        f.setWeight(WEIGHTS[weight])
        return f

    def paintEvent(self, ev):
        p = QtGui.QPainter(self)
        p.setRenderHint(QtGui.QPainter.RenderHint.Antialiasing)
        p.setRenderHint(QtGui.QPainter.RenderHint.TextAntialiasing)
        p.fillRect(self.rect(), QtGui.QColor(BG))
        p.translate(self.ox, self.oy)
        p.scale(self.s, self.s)
        self.p = p
        self.hits = []
        self.draw_top()
        self.draw_diagram()
        self.draw_wave()
        self.draw_trends()
        self.draw_controls()
        p.end()

    def pen(self, color, width=1.0, style="solid"):
        pen = QtGui.QPen(QtGui.QColor(color))
        pen.setWidthF(width)
        pen.setCapStyle(QtCore.Qt.PenCapStyle.FlatCap)
        pen.setJoinStyle(QtCore.Qt.PenJoinStyle.MiterJoin)
        if style == "dash":
            pen.setDashPattern([2.4, 1.2] if width >= 2 else [4 / width, 3 / width])
        elif style == "dot":
            pen.setCapStyle(QtCore.Qt.PenCapStyle.RoundCap)
            pen.setDashPattern([0.01, 1.8])
        return pen

    def box(self, x, y, w, h, fill=None, edge=INK, width=1.0, style="solid"):
        p = self.p
        p.setPen(self.pen(edge, width, style) if edge else QtCore.Qt.PenStyle.NoPen)
        p.setBrush(QtGui.QColor(fill) if fill else QtCore.Qt.BrushStyle.NoBrush)
        p.drawRect(QtCore.QRectF(x, y, w, h))

    def circle(self, x, y, d, edge=INK, width=1.5):
        p = self.p
        p.setPen(self.pen(edge, width))
        p.setBrush(QtGui.QColor(WHITE))
        p.drawEllipse(QtCore.QRectF(x + width / 2, y + width / 2, d - width, d - width))

    def line(self, x1, y1, x2, y2, color=INK, width=1.0, style="solid"):
        self.p.setPen(self.pen(color, width, style))
        self.p.drawLine(QtCore.QPointF(x1, y1), QtCore.QPointF(x2, y2))

    def run_width(self, parts):
        return sum(QtGui.QFontMetricsF(self.font_(*pt[1:4])).horizontalAdvance(pt[0]) for pt in parts)

    def runs(self, x, y, parts, color=INK):
        p = self.p
        asc = max(QtGui.QFontMetricsF(self.font_(*pt[1:4])).ascent() for pt in parts)
        for pt in parts:
            text, size, weight, mono = pt[:4]
            col = pt[4] if len(pt) > 4 else color
            f = self.font_(size, weight, mono)
            p.setFont(f)
            p.setPen(QtGui.QColor(col))
            p.drawText(QtCore.QPointF(x, y + asc), text)
            x += QtGui.QFontMetricsF(f).horizontalAdvance(text)
        return x

    def text(self, x, y, s, size=13, weight=400, color=INK, mono=False, w=None, align="l"):
        parts = [(s, size, weight, mono, color)]
        if w is not None and align != "l":
            tw = self.run_width(parts)
            x = x + (w - tw if align == "r" else (w - tw) / 2)
        return self.runs(x, y, parts)

    def rows(self, x, y, w, h, rows, center=False, pad=12, valign="c"):
        heights = [max(pt[1] for pt in r) * 1.45 for r in rows]
        top = y + (h - sum(heights)) / 2 if valign == "c" else y + pad - 2
        for r, rh in zip(rows, heights):
            rx = x + (w - self.run_width(r)) / 2 if center else x + pad
            self.runs(rx, top + (rh - max(pt[1] for pt in r) * 1.25) / 2, r)
            top += rh

    def label(self, x, y, s):
        self.text(x, y, s, 13, 700, INK)

    def button(self, x, y, w, h, label, cb, kind="plain", enabled=True, size=14, weight=600):
        fill, edge, col, width = {"plain": (WHITE, INK, INK, 1.0), "sel": (INK, INK, WHITE, 1.0),
                                  "on": (POWER, INK, INK, 1.0), "danger": (WHITE, RED, RED, 2.0),
                                  "solid": (RED, None, WHITE, 1.0)}[kind]
        if not enabled:
            fill, edge, col = (WHITE, GREY, GREY) if kind != "solid" else (GREY, None, WHITE)
        self.box(x, y, w, h, fill, edge, width)
        fm = QtGui.QFontMetricsF(self.font_(size, weight))
        self.text(x, y + (h - fm.height()) / 2, label, size, weight, col, w=w, align="c")
        if enabled and cb:
            self.hits.append((QtCore.QRectF(x, y, w, h), cb))

    def bit(self, field, i):
        return bool(self.hk and self.hk[field] >> i & 1)

    def valve_state(self, n):
        if not self.hk:
            return "unknown", False
        st = cdh.VALVE_STATES[(self.hk["valves"] >> (2 * (n - 1))) & 3]
        pulsing = bool((n == 1 and self.hk["valves"] & 0x40) or (n == 3 and self.hk["valves"] & 0x80))
        return st, pulsing

    def g(self, key):
        return self.hk[key] if self.hk else float("nan")

    def draw_top(self):
        self.box(0, 0, W, 80, PANEL, None)
        self.line(0, 80, W, 80, INK)
        self.text(24, 14, "APEX Bench Interface", 22, 600)
        self.text(24, 48, "link: " + self.link_name, 13, 400, TXT2)

        for x in (488, 860, 1060, 1400):
            self.line(x, 0, x, 80, RULE)
        self.label(508, 12, "Link")
        self.box(508, 36, 10, 10, GREEN if self.link_ok else RED, INK)
        self.text(526, 30, "Connected" if self.link_ok else "No link", 15, 600)
        age = time.time() - self.hk_time if self.hk else None
        detail = self.link_msg if not self.link_ok else "%s   read %s s ago" % (self.link_msg, cdh.sig(age, 2) if age is not None else "--")
        if len(detail) > 52:
            detail = detail[:51] + "…"
        self.text(508, 54, detail, 12, 400, TXT2 if self.link_ok else RED, True)

        self.label(880, 12, "Uptime")
        up = self.g("uptime_s")
        upt = "--:--:--" if up != up else "%02d:%02d:%02d" % (int(up) // 3600, int(up) // 60 % 60, int(up) % 60)
        self.text(880, 32, upt, 22, 500, INK, True)

        self.label(1080, 12, "PIB state")
        st = self.hk["pib_state"] if self.hk else None
        name = cdh.PIB_STATES[st] if st is not None and st < len(cdh.PIB_STATES) else "--"
        fill = {"ARMED": RED, "ARMING": YELLOW, "SAFING": YELLOW, "SAFE": GREEN}.get(name, PANEL)
        self.box(1080, 30, 150, 38, fill, INK, 1.0)
        self.text(1080, 37, name, 20, 700, WHITE if name == "ARMED" else (INK if name != "--" else GREY), w=150, align="c")
        if self.hb:
            self.text(1244, 32, "heartbeat on", 13, 400, TXT2)
            self.runs(1244, 50, [("next in ", 13, 400, False, TXT2),
                                 ("%s s" % cdh.sig(max(0.0, self.hb_next - time.monotonic()), 2), 13, 600, True)])
        else:
            self.text(1244, 40, "heartbeat off", 13, 400, TXT2)

        self.label(1420, 12, "Faults")
        faults = [f for f in FAULTS if self.hk and f in self.hk["flags"].split()]
        if faults:
            self.text(1420, 32, ", ".join(f.replace("_FAULT", "") for f in faults), 15, 700, RED)
        else:
            self.text(1420, 32, "None" if self.hk else "--", 16, 500)

        self.button(1720, 14, 176, 52, "SAFE", self.do_safe, "solid", self.link_ok, 18, 700)

    def draw_diagram(self):
        hk = self.hk
        out = lambda i: self.bit("outputs", i)
        inp = lambda i: self.bit("inputs", i)
        pw = lambda on: (POWER, 3.0, "solid") if on else (OFF, 1.5, "dash")
        mono = lambda s: (s, 13, 600, True)
        grey = lambda s, size=13: (s, size, 400, False, TXT2)
        onoff = lambda on: ("ON", 13, 600, False) if on else ("OFF", 13, 400, False, TXT2)
        high = lambda on: ("HIGH", 13, 600, False) if on else ("LOW", 13, 400, False, TXT2)

        self.box(24, 96, 1392, 640, PANEL, INK)
        self.label(40, 106, "System")

        self.line(200, 196, 256, 196, INK if self.link_ok else GREY, 1.0, "solid" if self.link_ok else "dash")
        self.line(200, 340, 256, 340, *pw(out(0)))
        self.line(200, 412, 232, 412, *pw(inp(0)))
        self.line(696, 196, 980, 196, *pw(out(4) or out(5)))
        vb = pw(out(1))
        self.line(696, 246, 728, 246, *vb)
        self.line(728, 246, 728, 496, *vb)
        self.line(728, 496, 1231, 496, *vb)
        for _, _, _, cx in VALVES:
            self.line(cx, 496, cx, 536, *vb)
        self.line(320, 384, 320, 520, *pw(out(3)))
        self.line(384, 584, 600, 584, *pw(out(3)))
        self.line(652, 384, 652, 504, *pw(out(2)))

        v1, v2, v3 = (self.valve_state(n)[0] == "open" for n in (1, 2, 3))
        fl = lambda on: (FLUID, 4.0, "solid") if on else (OFF, 2.0, "dash")
        self.line(1300, 368, 1300, 602, *fl(True))
        self.line(1262, 600, 1302, 600, *fl(True))
        self.line(1152, 600, 1198, 600, *fl(v1))
        self.line(1042, 600, 1088, 600, *fl(v1 and v2))
        for a, b in ((944, 978), (836, 856), (704, 748)):
            self.line(a, 600, b, 600, *fl(v1 and v2 and v3))
        self.line(1120, 620, 1120, 664, OFF, 2.0, "dash")

        self.box(48, 160, 152, 72, WHITE, INK)
        self.rows(48, 160, 152, 72, [[("CDH link", 13, 600, False)], [grey("Nucleo bridge" if self.link_name == "nucleo" else "Laptop")]])
        self.text(210, 178, "SPI" if self.link_name != "i2c" else "I2C", 11, 400, TXT2, True)
        self.box(48, 304, 152, 72, WHITE, INK)
        self.rows(48, 304, 152, 72, [[("V_BAT", 13, 600, False)], [grey("EN "), onoff(out(0))], [grey("PGOOD "), high(inp(1))]])
        self.box(48, 384, 152, 56, WHITE, INK)
        self.rows(48, 384, 152, 56, [[("12 V rail", 13, 600, False)], [grey("PGOOD "), high(inp(0))]])

        self.box(48, 468, 168, 252, WHITE, INK)
        self.label(60, 478, "Key")
        keys = [(FLUID, 4.0, "solid", "Fluid, pressurised"), (OFF, 2.0, "dash", "Fluid, no flow"),
                (POWER, 3.0, "solid", "Power on"), (OFF, 1.5, "dash", "Power off"), (INK, 1.0, "solid", "Data")]
        for i, (c, wd, stl, name) in enumerate(keys):
            self.line(60, 508 + i * 22, 92, 508 + i * 22, c, wd, stl)
            self.text(102, 500 + i * 22, name, 12)
        self.bowtie(60, 612, 32, 20, False, 1.5)
        self.text(102, 614, "Valve open", 12)
        self.bowtie(60, 638, 32, 20, True, 1.5)
        self.text(102, 640, "Valve closed", 12)
        self.circle(66, 664, 20)
        self.text(102, 666, "Sensor", 12)

        self.box(232, 112, 488, 336, None, INK, 1.5)
        self.text(244, 120, "Payload interface board", 13, 700)

        status = lambda en, pg: ("OFF", 13, 400, False, TXT2) if not en else (("ON", 13, 600, False) if pg else ("ON, NO PGOOD", 13, 700, False, RED))
        self.box(256, 152, 200, 120, WHITE, INK)
        self.rows(256, 152, 200, 120, [[("MCU", 13, 600, False)],
                                       [grey("Die "), mono(num(self.g("mcu_degC"), "°C", 3))],
                                       [grey("Board "), mono(num(self.g("pib_ntc_degC"), "°C", 3))]])
        self.box(496, 152, 200, 56, WHITE, INK)
        self.rows(496, 152, 200, 56, [[("Heater driver", 13, 600, False)]])
        self.box(496, 220, 200, 52, WHITE, INK)
        self.rows(496, 220, 200, 52, [[("Valve drivers", 13, 600, False)], [status(out(1), inp(0))]])
        self.box(256, 296, 200, 88, WHITE, INK)
        self.rows(256, 296, 200, 88, [[("Discharge flyback", 13, 600, False)], [status(out(3), inp(3))],
                                      [grey("NTC "), mono(self.ntc("dchg_ntc_degC", 2))]])
        self.box(496, 296, 200, 88, WHITE, INK)
        self.rows(496, 296, 200, 88, [[("Igniter flyback", 13, 600, False)], [status(out(2), inp(2))],
                                      [grey("Supply "), mono(num(self.g("ign_supply_v"), "V", 3)), grey("   NTC "), mono(num(self.g("ign_ntc_degC"), "°C", 3))]])

        self.box(232, 472, 488, 248, None, TXT2, 1.0, "dash")
        self.box(240, 478, 102, 18, PANEL, None)
        self.text(244, 480, "PMPD thruster", 13, 700)
        self.box(256, 520, 128, 120, WHITE, INK)
        self.rows(256, 520, 128, 120, [[("Discharge", 13, 400, False)], [("capacitor bank", 13, 400, False)]], center=True)
        vd, idd = self.wave_peaks()
        pk = hk["pulse"] if hk else {"index": 0, "peak_v": 0, "peak_i": 0}
        has = pk["index"] > 0
        vig = num(cdh.counts_to_pin_v(pk["peak_v"]) * cdh.IGN_V_PER_PIN_V, "V", 3) if has else "--"
        iig = num(cdh.counts_to_pin_v(pk["peak_i"]), "V", 3) if has else "--"
        for x, y, name, val in ((400, 496, "V_D max", vd), (400, 600, "I_D max", idd), (488, 496, "V_ig pk", vig), (488, 600, "I_ig pk", iig)):
            self.circle(x, y, 72)
            self.rows(x, y, 72, 72, [[grey(name, 11)], [(val, 13, 600, True)]], center=True)
        self.box(600, 504, 104, 192, WHITE, INK)
        self.rows(600, 504, 104, 192, [[("Thruster", 13, 600, False)], [grey("last fire")], [mono("#%d" % pk["index"] if has else "none")]], center=True)
        self.text(244, 694, "V_D, I_D, I_ig in pin volts", 11, 400, TXT2)

        self.box(744, 112, 648, 608, None, TXT2, 1.0, "dash")
        self.text(756, 120, "Propellant tank and feed", 13, 700)
        for i, (y, idx) in enumerate(((214, 4), (300, 5))):
            on = out(idx)
            self.runs(860 + 108 - self.run_width([("H%d " % (i + 1), 13, 400, False), onoff(on)]), y, [("H%d " % (i + 1), 13, 400, False), onoff(on)])
            self.zigzag(980 + i * 26, 184, POWER if on else OFF, 3.0 if on else 1.5)

        self.box(1033, 169, 298, 198, WHITE, INK, 2.0)
        self.box(1034, 268, 296, 98, LIQUID, None)
        self.line(1034, 268, 1330, 268, INK, 1.0)
        for x, key, raw, nm in ((1044, "tank_ntc1_degC", 0, "NTC1"), (1232, "tank_ntc2_degC", 1, "NTC2")):
            self.box(x, 178, 88, 34, WHITE, INK)
            self.rows(x, 178, 88, 34, [[grey(nm, 11)], [(self.ntc(key, raw), 12, 600, True)]], center=True)
        self.circle(1124, 214, 116)
        self.rows(1124, 214, 116, 116, [[grey("Tank", 12)], [(press(self.g("tank_mbar")), 20, 600, True), grey(" mbar", 12)],
                                        [(num(self.g("tank_degC"), "°C", 3), 13, 600, True)]], center=True)

        for n, tag, _, cx in VALVES:
            state, pulsing = self.valve_state(n)
            self.rows(cx - 60, 536, 120, 40, [[(tag, 12, 600, False), grey(" V%d" % n, 12)],
                                             [(state.upper() + (", PULSING" if pulsing else ""), 12, 700 if state == "closed" else 500, False)]], center=True)
            self.bowtie(cx - 32, 580, 64, 40, state == "closed", 2.0, state == "unknown")
        self.text(1060, 668, "ASCENT fill / drain", 12, 400, TXT2, w=120, align="c")

        self.circle(856, 556, 88)
        self.rows(856, 556, 88, 88, [[grey("Flow", 12)], [(num(self.g("flow_ulmin"), "", 3), 15, 600, True)], [grey("µL/min", 11)]], center=True)
        self.text(856, 650, num(self.g("flow_degC"), "°C", 3), 12, 600, TXT2, True, 88, "c")
        self.circle(748, 556, 88)
        self.rows(748, 556, 88, 88, [[grey("Frit", 12)], [(press(self.g("frit_mbar")), 15, 600, True), grey(" mbar", 11)],
                                     [(num(self.g("frit_degC"), "°C", 3), 12, 600, True)]], center=True)

    def ntc(self, key, raw_i):
        v = self.g(key)
        if v == v:
            return num(v, "°C", 3)
        if self.hk:
            return "%d cnt" % self.hk["ntc_raw"][raw_i]
        return "--"

    def bowtie(self, x, y, w, h, filled, width, dashed=False):
        p = self.p
        p.setPen(self.pen(INK, width, "dash" if dashed else "solid"))
        p.setBrush(QtGui.QColor(INK if filled else WHITE))
        m = width / 2
        for pts in (((x + m, y + h * 0.1), (x + w / 2, y + h / 2), (x + m, y + h * 0.9)),
                    ((x + w - m, y + h * 0.1), (x + w / 2, y + h / 2), (x + w - m, y + h * 0.9))):
            p.drawPolygon(QtGui.QPolygonF([QtCore.QPointF(a, b) for a, b in pts]))

    def zigzag(self, x, y, color, width):
        ys = [0, 12] + [20 + 12 * k for k in range(11)] + [148, 168]
        xs = [10, 10] + [2 if k % 2 == 0 else 18 for k in range(11)] + [10, 10]
        self.p.setPen(self.pen(color, width))
        self.p.setBrush(QtCore.Qt.BrushStyle.NoBrush)
        self.p.drawPolyline(QtGui.QPolygonF([QtCore.QPointF(x + a, y + b) for a, b in zip(xs, ys)]))

    def wave_data(self):
        if not self.wave:
            return None
        samples, hdr = self.wave
        sample_us = hdr["sample_us"] if 0.1 < hdr["sample_us"] < 10.0 else 1e6 / cdh.NOMINAL_SAMPLE_RATE_HZ
        v = [cdh.counts_to_pin_v(a) for a, _ in samples]
        i = [cdh.counts_to_pin_v(b) for _, b in samples]
        return v, i, hdr, sample_us

    def wave_peaks(self):
        d = self.wave_data()
        if not d:
            return "--", "--"
        return num(max(d[0]), "V", 3), num(max(d[1]), "V", 3)

    def series(self, x, y, w, h, xs, ys, x0, x1, y0, y1, color, style):
        p = self.p
        p.setPen(self.pen(color, 2.5, style))
        p.setBrush(QtCore.Qt.BrushStyle.NoBrush)
        seg = []
        for a, b in list(zip(xs, ys)) + [(None, None)]:
            if a is None or b is None or b != b:
                if len(seg) > 1:
                    p.drawPolyline(QtGui.QPolygonF(seg))
                elif len(seg) == 1:
                    p.drawPoint(seg[0])
                seg = []
                continue
            seg.append(QtCore.QPointF(x + (a - x0) / (x1 - x0) * w, y + h - (b - y0) / (y1 - y0) * h))

    def frame(self, x, y, w, h):
        self.box(x, y, w, h, WHITE, INK)
        for k in (1, 2, 3):
            self.line(x, y + h * k / 4, x + w, y + h * k / 4, FAINT)

    def draw_wave(self):
        self.box(24, 752, 688, 304, PANEL, INK)
        self.label(40, 762, "Discharge capture")
        x, y, w, h = 92, 816, 600, 168
        d = self.wave_data()
        self.box(x, y, w, h, SCOPE, INK)
        for k in range(1, 10):
            self.line(x + w * k / 10, y, x + w * k / 10, y + h, GRAT)
        for k in range(1, 4):
            self.line(x, y + h * k / 4, x + w, y + h * k / 4, GRAT)
        self.text(40, 812, "3.3 V", 12, 400, TXT2, True, 44, "r")
        self.text(40, 972, "0", 12, 400, TXT2, True, 44, "r")
        if not d:
            self.text(40, 780, "No pulse yet", 14, 600)
            self.text(x, y + h / 2 - 8, "Fire a pulse to see the discharge waveform", 13, 400, "#BBBBBB", w=w, align="c")
            return
        v, i, hdr, sample_us = d
        n = len(v)
        fire = hdr["fire_sample"]
        title = "Pulse #%d" % hdr["index"] + (", %.1f µs" % self.wave_width if self.wave_width else "")
        self.text(40, 780, title, 14, 600)
        self.text(380, 782, "%d samples, %s µs apart" % (n, cdh.sig(sample_us, 3)), 12, 400, TXT2, w=312, align="r")
        xs = list(range(n))
        if 0 <= fire < n:
            fx = x + fire / (n - 1) * w
            self.line(fx, y, fx, y + h, "#9A9A9A", 1.0, "dash")
            self.text(fx - 16, 990, "fire", 11, 400, INK, True, 32, "c")
        self.series(x, y, w, h, xs, i, 0, n - 1, 0, cdh.ADC_VREF_V, CH2, "solid")
        self.series(x, y, w, h, xs, v, 0, n - 1, 0, cdh.ADC_VREF_V, CH1, "solid")
        self.text(72, 990, "%s µs" % cdh.sig(-fire * sample_us, 2), 11, 400, TXT2, True, 40, "c")
        self.text(652, 990, "+%s µs" % cdh.sig((n - 1 - fire) * sample_us, 2), 11, 400, TXT2, True, 56, "c")
        self.box(40, 1021, 24, 14, SCOPE, None)
        self.line(43, 1028, 61, 1028, CH1, 3.0)
        self.runs(70, 1018, [("V_D (PA0) max ", 12, 400, False), (num(max(v), "V", 3), 12, 600, True)])
        self.box(232, 1021, 24, 14, SCOPE, None)
        self.line(235, 1028, 253, 1028, CH2, 3.0)
        self.runs(262, 1018, [("I_D (PA1) max ", 12, 400, False), (num(max(i), "V", 3), 12, 600, True)])
        self.text(520, 1018, "Save CSV", 12, 600, INK, w=172, align="r")
        self.line(640, 1034, 692, 1034, INK)
        self.hits.append((QtCore.QRectF(632, 1012, 64, 26), lambda: self.worker.send("savewave", self.edits["name"][0].text().strip() or "demo_run")))

    def draw_trends(self):
        self.box(728, 752, 688, 304, PANEL, INK)
        self.label(744, 762, "Trends")
        now = time.time()
        span = min(TREND_S, max(60.0, now - self.trend[0][0])) if self.trend else 60.0
        self.text(1196, 762, "last %d min" % math.ceil(span / 60 - 0.01), 11, 500, TXT2, w=200, align="r")
        ts = [r[0] for r in self.trend]
        for y0, title, cols, minspan, _ in ((788, "Pressure, mbar", [(1, BLUE, "solid", "Tank"), (2, ORANGE, "dot", "Frit")], 10.0, 4),
                                               (918, "Temperature, °C", [(3, BLUE, "solid", "Tank NTC1"), (4, INK, "dash", "Tank NTC2"),
                                                                         (5, ORANGE, "dot", "Frit")], 2.0, 3)):
            self.text(796, y0, title, 13, 600)
            kx = 1396
            for _, c, stl, name in reversed(cols):
                kx -= self.run_width([(name, 12, 500, False)])
                self.text(kx, y0 + 1, name, 12, 500)
                kx -= 34
                self.line(kx, y0 + 9, kx + 28, y0 + 9, c, 2.5, stl)
                kx -= 18
            x, y, w, h = 796, y0 + 26, 600, 96
            self.frame(x, y, w, h)
            vals = [r[k] for r in self.trend for k, *_ in cols if r[k] == r[k]]
            if not vals:
                self.text(x, y + h / 2 - 8, "no readings yet", 12, 400, TXT2, w=w, align="c")
                continue
            lo, hi = min(vals), max(vals)
            if hi - lo < minspan:
                mid = (hi + lo) / 2
                lo, hi = mid - minspan / 2, mid + minspan / 2
            pad = (hi - lo) * 0.1
            lo, hi = lo - pad, hi + pad
            self.text(736, y - 4, axis(hi, hi - lo), 12, 400, TXT2, True, 52, "r")
            self.text(736, y + h - 12, axis(lo, hi - lo), 12, 400, TXT2, True, 52, "r")
            for k, c, stl, _ in cols:
                self.series(x, y, w, h, ts, [r[k] for r in self.trend], now - span, now, lo, hi, c, stl)

    def draw_controls(self):
        x0, w0 = 1432, 464
        self.box(x0, 96, w0, 960, PANEL, INK)
        link = self.link_ok
        busy = bool(self.busy)

        self.label(1448, 108, "Arm")
        self.button(1448, 130, 96, 40, "Arm", self.do_arm, "plain", link and not busy)
        self.check(1560, 142, self.stay_armed, "Stay armed (heartbeat every 2 s)", self.do_stay)
        self.line(x0, 186, x0 + w0, 186, INK)

        self.label(1448, 198, "Valves")
        self.text(1824, 196, "Close all", 13, 400, INK if link else GREY, w=56, align="r")
        self.line(1824, 214, 1880, 214, INK if link else GREY)
        if link:
            self.hits.append((QtCore.QRectF(1818, 192, 66, 26), lambda: self.worker.send("valve", 0, False)))
        for k, (n, tag, desc, _) in enumerate(VALVES):
            y = 220 + k * 42
            state, pulsing = self.valve_state(n)
            self.runs(1448, y + 12, [(tag, 13, 600, False), ("   " + desc, 13, 400, False, TXT2)])
            self.button(1744, y + 5, 68, 30, "OPEN", lambda n=n: self.worker.send("valve", n, True),
                        "sel" if state == "open" else "plain", link, 12, 600 if state == "open" else 400)
            self.button(1812, y + 5, 68, 30, "CLOSE", lambda n=n: self.worker.send("valve", n, False),
                        "sel" if state == "closed" else "plain", link, 12, 600 if state == "closed" else 400)
            if k < 2:
                self.line(1448, y + 41, 1880, y + 41, FAINT)
        self.line(x0, 352, x0 + w0, 352, INK)

        self.label(1448, 364, "Outputs")
        for i, (label, (_, net, _)) in enumerate(zip(OUT_LABELS, cdh.OUTPUTS)):
            y = 384 + i * 32
            on = self.bit("outputs", i)
            self.text(1448, y + 8, label, 13)
            self.text(1694, y + 9, net, 12, 400, TXT2, True)
            self.button(1824, y + 3, 56, 26, "ON" if on else "OFF", lambda i=i, on=on: self.worker.send("out", i, not on),
                        "on" if on else "plain", link, 11, 600 if on else 400)
            if i < 7:
                self.line(1448, y + 32, 1880, y + 32, FAINT)
        self.line(x0, 652, x0 + w0, 652, INK)

        self.label(1448, 664, "Fire")
        can = link and not busy and self.armed()
        self.text(1448, 684, "Width, µs", 12, 400, TXT2)
        self.button(1542, 702, 338, 38, "Fire pulse…", self.do_fire, "danger", can, 14, 700)
        for x, t in ((1448, "Count"), (1518, "Width, µs"), (1600, "Rate, Hz")):
            self.text(x, 750, t, 12, 400, TXT2)
        self.button(1670, 768, 210, 38, self.busy if busy else "Run sequence…", self.do_seq, "danger", can, 14, 700)
        self.check(1448, 822, self.save_csv, "Save waveforms as tests/", self.toggle_csv)
        self.text(1824, 822, "_NNNN.csv", 12, 400, TXT2, True)
        if not self.armed():
            self.text(1448, 846, "Arm the PIB to fire.", 12, 400, TXT2)
        self.line(x0, 866, x0 + w0, 866, INK)

        self.label(1448, 876, "Log")
        for k, msg in enumerate(self.logs):
            if len(msg) > 52:
                msg = msg[:51] + "…"
            self.text(1448, 896 + k * 19, msg, 12, 400, INK, True)

    def toggle_csv(self):
        self.save_csv = not self.save_csv

    def check(self, x, y, on, label, cb):
        self.box(x, y, 16, 16, INK if on else WHITE, INK)
        if on:
            self.p.setPen(self.pen(WHITE, 2.0))
            self.p.drawPolyline(QtGui.QPolygonF([QtCore.QPointF(x + 3.5, y + 8.5), QtCore.QPointF(x + 7, y + 12), QtCore.QPointF(x + 12.5, y + 4.5)]))
        end = self.text(x + 24, y, label, 13)
        self.hits.append((QtCore.QRectF(x - 4, y - 6, end - x + 8, 28), cb))


def run(make_link, link_name):
    app = QtWidgets.QApplication.instance() or QtWidgets.QApplication(sys.argv[:1])
    w = Console(make_link, link_name)
    w.show()
    return app.exec()
