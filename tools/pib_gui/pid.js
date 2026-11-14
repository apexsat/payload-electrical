"use strict";
/* P&ID of the payload, following the block diagram: PIB (MCU, drivers, flyback converters, fast ADC, sensors),
 * propellant feed system (tank, heater, ISO3 -> ISO2 -> flow meter -> ISO1), and the PMPD thruster (capacitor bank, exhaust to the right).
 * Colours as in the diagram: fluid red, power yellow, analog purple, digital white.  Live values come from housekeeping.
 * Valves show the COMMANDED state (label) and the inferred ACTUAL state (symbol colour: green open, red closed, grey unknown). */

const PID_W = 1250, PID_H = 730;
const FLUID = "#e0563f", FLUID_DIM = "#6b3a34", PWR = "#e0b13c", PWR_DIM = "#5c4d24", ANA = "#a98bea", DIG = "#c4ccd6", GREY = "#7f8b99";

function drawPID(cv, d) {
  const g = cv.getContext("2d"), hk = d.hk;
  g.clearRect(0, 0, PID_W, PID_H);
  g.fillStyle = "#0a1017"; g.fillRect(0, 0, PID_W, PID_H);
  g.lineCap = "round"; g.lineJoin = "round";
  const out = n => { const o = hk && hk.outputs_view.find(x => x.name === n); return o ? o.on : null; };
  const inp = n => { const o = hk && hk.inputs_view.find(x => x.name === n); return o ? o.high : null; };
  const num = (v, n) => isNum(v) ? sig(v, n || 4) : "—";
  const T = (txt, x, y, o = {}) => { g.font = `${o.b ? "bold " : ""}${o.s || 15}px monospace`; g.textAlign = o.al || "center"; g.fillStyle = o.c || "#cfd9e4"; g.fillText(txt, x, y); };
  const R = (x, y, w, h, o = {}) => { g.fillStyle = o.fill || "#1a232e"; g.strokeStyle = o.stroke || "#8aa0b6"; g.lineWidth = o.lw || 2; g.setLineDash(o.dash || []);
    rrect(g, x, y, w, h, o.r === undefined ? 6 : o.r); if (o.fill !== null) g.fill(); g.stroke(); g.setLineDash([]); };
  const L = (pts, col, w, o = {}) => { g.strokeStyle = col; g.lineWidth = w || 2; g.setLineDash(o.dash || []); g.beginPath(); pts.forEach(([x, y], i) => i ? g.lineTo(x, y) : g.moveTo(x, y)); g.stroke(); g.setLineDash([]);
    if (o.arrow) { const [x1, y1] = pts[pts.length - 1], [x0, y0] = pts[pts.length - 2], a = Math.atan2(y1 - y0, x1 - x0), s = o.arrow === true ? 8 : o.arrow;
      g.fillStyle = col; g.beginPath(); g.moveTo(x1, y1); g.lineTo(x1 - s * Math.cos(a - 0.45), y1 - s * Math.sin(a - 0.45)); g.lineTo(x1 - s * Math.cos(a + 0.45), y1 - s * Math.sin(a + 0.45)); g.closePath(); g.fill(); } };
  const LED = (x, y, on) => { g.beginPath(); g.arc(x, y, 5.5, 0, 6.3); g.fillStyle = on === null ? "#0a1017" : on ? "#3ddc84" : "#26323f"; g.fill();
    g.strokeStyle = on === null ? "#5c6b7a" : on ? "#8dffb8" : "#4a5a6c"; g.lineWidth = 1.5; g.stroke(); };
  const SENSOR = (x, y, label) => { g.beginPath(); g.arc(x, y, 19, 0, 6.3); g.fillStyle = "#121a24"; g.fill(); g.strokeStyle = "#cfd9e4"; g.lineWidth = 2; g.stroke(); T(label, x, y + 5, { s: 14, b: true }); };

  // ---- live flags
  const vbat = out("vbat"), vbatPg = inp("VBAT_PGOOD"), arm = out("arm"), pg12 = inp("PGOOD_12V"), igEn = out("igniter"), igPg = inp("IGNITER_PGOOD");
  const dcEn = out("discharge"), dcPg = inp("DISCHARGE_PGOOD"), h1 = out("htr1"), h2 = out("htr2");
  const wave = d.wave, rows = wave ? wave.rows : null;
  const vdPre = rows ? (() => { const pre = rows.filter(r => r.t_us < 0); return pre.length ? pre.reduce((a, r) => a + r.v_pin, 0) / pre.length : null; })() : null;
  const idPeak = rows ? Math.max(...rows.map(r => r.i_pin)) : null;
  const pulse = hk ? hk.pulse_view : null;
  const flowFault = !!(hk && (hk.status & 2));
  const pwrOn = (x) => x ? PWR : PWR_DIM;

  // ---- frames
  R(4, 4, 1242, 722, { fill: null, stroke: "#4a5a6c", dash: [10, 7], lw: 2, r: 8 }); T("Payload Subsystem", 16, 24, { al: "left", s: 15, c: "#9fb3c8" });
  R(540, 36, 696, 444, { fill: null, stroke: "#4a5a6c", dash: [8, 6], lw: 1.5 }); T("Propellant Feed System", 556, 56, { al: "left", s: 15, c: "#9fb3c8" });
  R(100, 500, 1136, 222, { fill: null, stroke: "#4a5a6c", dash: [8, 6], lw: 1.5 }); T("PMPD Thruster", 116, 521, { al: "left", s: 15, c: "#9fb3c8" });

  // ================= PIB
  R(100, 36, 400, 404, { fill: "#12301f", stroke: "#3f9460", lw: 2.5 });
  T("Payload Interface Board (PIB)", 112, 58, { al: "left", s: 15, c: "#9fe0b4" });
  const st = hk ? hk.pib_state_name : "—", stCol = st === "SAFE" ? "#3ddc84" : st === "—" ? "#7f93a8" : "#ffb84d";
  R(112, 68, 150, 30, { fill: "#0c1f15", stroke: stCol, lw: 2, r: 15 }); T("PIB " + st, 187, 89, { b: true, s: 17, c: stCol });
  if (d.armedHold) T("ARM held", 272, 89, { al: "left", s: 13, c: "#ffb84d" });
  T(hk ? hk.flags : "", 112, 116, { al: "left", s: 13, c: hk && /FAULT|UVLO/.test(hk.flags) ? "#ff7b7b" : "#9fb3c8" });
  // connectors and outside world
  [[88, 64, 24, 50, "J2"], [88, 150, 24, 50, "J1"], [494, 80, 22, 130, "J5"], [196, 430, 36, 18, "J3"], [398, 430, 36, 18, "J4"]].forEach(([x, y, w, h, t]) => {
    R(x, y, w, h, { fill: "#e6edf3", stroke: "#8aa0b6", lw: 1.5, r: 3 }); T(t, x + w / 2, y + h / 2 + 4, { s: 11, c: "#0b1017", b: true }); });
  T("CDH", 8, 160, { al: "left", s: 14, b: true }); T("SPI / I2C", 8, 190, { al: "left", s: 12, c: "#9fb3c8" });
  L([[52, 172], [88, 172]], DIG, 2, { arrow: true });
  T("V_BAT", 8, 80, { al: "left", s: 13, b: true }); T("3V3", 8, 104, { al: "left", s: 13, b: true }); L([[52, 76], [88, 76]], pwrOn(vbat), 3, { arrow: true }); L([[40, 100], [88, 100]], PWR, 3, { arrow: true });
  T("VBAT_EN", 6, 126, { al: "left", s: 11, c: "#9fb3c8" }); LED(76, 122, vbat); T("PG", 6, 142, { al: "left", s: 11, c: "#9fb3c8" }); LED(76, 138, vbatPg);
  T("Power Distribution", 6, 214, { al: "left", s: 11, c: "#7f93a8" }); T("Board", 6, 228, { al: "left", s: 11, c: "#7f93a8" });

  // blocks
  const ORANGE = "#b9831f", GREYB = "#4a5260";
  R(130, 136, 150, 100, { fill: GREYB, stroke: "#9aa5b3" }); T("MCU", 205, 160, { b: true, s: 18 });
  T(hk ? "up " + hms(hk.uptime_s) : "up —", 205, 184, { s: 13, c: "#e6edf3" }); T("WDG_IN", 196, 214, { s: 12, c: "#e6edf3" }); LED(160, 210, out("wdg"));
  R(350, 72, 140, 52, { fill: ORANGE, stroke: "#f0c36a" }); T("Heater Driver", 420, 93, { b: true, s: 14, c: "#1a1203" });
  T("HTR1", 364, 114, { al: "left", s: 12, c: "#1a1203" }); LED(408, 110, h1); T("HTR2", 420, 114, { al: "left", s: 12, c: "#1a1203" }); LED(464, 110, h2);
  R(350, 138, 140, 52, { fill: ORANGE, stroke: "#f0c36a" }); T("Valve Drivers", 420, 159, { b: true, s: 14, c: "#1a1203" });
  T("ARM", 364, 180, { al: "left", s: 12, c: "#1a1203" }); LED(398, 176, arm); T("12V", 420, 180, { al: "left", s: 12, c: "#1a1203" }); LED(452, 176, pg12);
  R(124, 268, 124, 76, { fill: ORANGE, stroke: "#f0c36a" }); T("Main Flyback", 186, 288, { b: true, s: 13, c: "#1a1203" }); T("Converter", 186, 303, { b: true, s: 13, c: "#1a1203" });
  T("EN", 134, 330, { al: "left", s: 12, c: "#1a1203" }); LED(160, 326, dcEn); T("PGOOD", 172, 330, { al: "left", s: 11, c: "#1a1203" }); LED(228, 326, dcPg);
  R(262, 268, 100, 76, { fill: GREYB, stroke: "#9aa5b3" }); T("Fast ADC", 312, 292, { b: true, s: 14 }); T("PD_RST", 292, 330, { s: 12, c: "#e6edf3" }); LED(342, 326, out("pdrst"));
  R(378, 268, 114, 76, { fill: ORANGE, stroke: "#f0c36a" }); T("Igniter", 435, 288, { b: true, s: 13, c: "#1a1203" }); T("Flyback Conv.", 435, 303, { b: true, s: 13, c: "#1a1203" });
  T("EN", 386, 330, { al: "left", s: 12, c: "#1a1203" }); LED(412, 326, igEn); T("PG", 424, 330, { al: "left", s: 12, c: "#1a1203" }); LED(452, 326, igPg);
  // measurements: V_D, V_ig, I_ig
  SENSOR(178, 392, "V_D"); SENSOR(330, 392, "V_ig"); SENSOR(435, 392, "I_ig");
  T(isNum(vdPre) ? num(vdPre, 3) + " V pin" : "—", 178, 428, { s: 13 }); T(pulse && pulse.index ? num(pulse.peak_v_volts) + " V pk" : "—", 330, 428, { s: 13 }); T(pulse && pulse.index ? num(pulse.peak_i_pin_v, 3) + " V pk" : "—", 435, 428, { s: 13 });
  T("charge (pre-pulse)", 178, 346 + 38, { s: 11, c: "#7f93a8" });
  // PIB internal wiring
  L([[114, 89], [114, 260], [140, 260], [140, 268]], pwrOn(vbat), 3, { arrow: 7 }); L([[114, 89], [350, 89]], pwrOn(vbat), 3, { arrow: 7 });
  L([[332, 89], [332, 156], [350, 156]], pwrOn(vbat), 3, { arrow: 7 }); L([[114, 260], [470, 260], [470, 268]], pwrOn(vbat), 3, { arrow: 7 });
  L([[112, 175], [130, 175]], DIG, 2, { arrow: 6 });
  L([[280, 170], [318, 170], [318, 98], [350, 98]], DIG, 2, { arrow: 6 }); L([[318, 164], [350, 164]], DIG, 2, { arrow: 6 });
  L([[205, 236], [205, 252], [186, 252], [186, 268]], DIG, 2, { arrow: 6 }); L([[205, 252], [435, 252], [435, 268]], DIG, 2, { arrow: 6 });
  L([[300, 268], [300, 244], [255, 244], [255, 236]], DIG, 2, { arrow: 6 });
  L([[178, 373], [178, 358], [285, 358], [285, 344]], ANA, 2, { arrow: 6 }); L([[330, 373], [330, 344]], ANA, 2, { arrow: 6 }); L([[435, 373], [435, 358], [348, 358], [348, 344]], ANA, 2, { arrow: 6 });
  // outputs to the heater and the valves
  L([[490, 98], [516, 98], [564, 98], [564, 110]], pwrOn(h1 || h2), 3);
  L([[490, 164], [528, 164], [528, 466], [1000, 466]], pwrOn(pg12), 3); T("12 V (pulsed)", 552, 460, { al: "left", s: 13, c: pwrOn(pg12) });

  // ================= propellant feed system
  // heater control readout, right where the heater is
  const tk = d.tank, heating = !!(h1 || h2);
  T("HEATER", 556, 80, { al: "left", s: 14, b: true, c: heating ? "#ffb84d" : "#9fb3c8" });
  T("1", 636, 80, { al: "left", s: 13 }); LED(650, 76, h1); T("2", 664, 80, { al: "left", s: 13 }); LED(678, 76, h2);
  T(heating ? "ON" : "off", 696, 80, { al: "left", s: 14, b: true, c: heating ? "#ffb84d" : "#7f93a8" });
  if (tk && tk.on) T(`control ${num(tk.sp, 3)} °C ±${num(tk.hyst / 2, 2)}  ·  tank ${isNum(tk.temp) ? num(tk.temp, 3) + " °C" : "no reading"}`, 556, 98, { al: "left", s: 14, c: isNum(tk.temp) ? "#cfd9e4" : "#ff9a9a" });
  else T("control: off (no thermostat)", 556, 98, { al: "left", s: 14, c: "#7f93a8" });
  if (heating && vbatPg === false) T("heater enabled but V_BAT is not good: no heater power", 556, 116, { al: "left", s: 13, c: "#ffb84d" });
  // tank, coil, sensors
  g.strokeStyle = heating ? "#ffb84d" : "#5c6b7a"; g.lineWidth = 3; g.beginPath();
  for (let k = 0; k <= 10; k++) { const x = 564 + (k % 2 ? 9 : -9), y = 128 + k * 11; if (k) g.lineTo(x, y); else g.moveTo(x, y); } g.stroke();
  R(588, 130, 150, 126, { fill: "#1a232e", stroke: "#8aa0b6", lw: 3, r: 12 }); R(594, 160, 138, 90, { fill: "#e8a0a8", stroke: null, r: [30, 30, 6, 6] });
  T("Propellant Tank", 663, 150, { b: true, s: 14, c: "#e6eef7" });
  T(`P ${hk ? num(hk.tank_mbar) : "—"} mbar`, 663, 198, { s: 15, b: true, c: "#2a1216" }); T(`T ${hk ? num(hk.tank_degC) : "—"} °C`, 663, 224, { s: 15, b: true, c: "#2a1216" });
  const n1 = hk && isNum(hk.tank_ntc1_degC), n2 = hk && isNum(hk.tank_ntc2_degC);
  if (n1 || n2) T(`NTC ${n1 ? num(hk.tank_ntc1_degC) : "--"} / ${n2 ? num(hk.tank_ntc2_degC) : "--"} °C`, 663, 244, { s: 12, c: "#2a1216" });
  SENSOR(618, 296, "T_t"); SENSOR(708, 296, "P_t");
  L([[618, 277], [618, 256]], ANA, 2); L([[708, 277], [708, 256]], ANA, 2);
  // (tank T/P values are shown inside the tank)
  L([[600, 296], [532, 296], [532, 200]], ANA, 2); L([[532, 140], [506, 140]], DIG, 2, { arrow: 6 });   // sensors and I2C to the MCU side (J5)
  // fluid path: tank -> ISO3 -> ISO2 -> FM -> ISO1 -> thruster
  const yP = 392, xV = { ISO3: 735, ISO2: 825, FM: 905, ISO1: 985 }, xEnd = 1015;
  const valves = hk ? hk.valves_view : [{ name: "ISO3" }, { name: "ISO2" }, { name: "ISO1" }];
  const vbyName = Object.fromEntries(valves.map(v => [v.name, v]));
  const allOpen = valves.every(v => v.actual === "open"), flowing = hk && allOpen && isNum(hk.flow_ulmin) && hk.flow_ulmin > 0.5;
  const pipe = allOpen ? (flowing ? "#ff8a6a" : FLUID) : FLUID_DIM, pw = flowing ? 7 : 5;
  L([[663, 256], [663, yP], [xEnd, yP], [xEnd, 570]], pipe, pw, { arrow: 9 });
  [xV.ISO3 + 42, xV.ISO2 + 42, xV.FM + 38].forEach(x => { g.fillStyle = pipe; g.beginPath(); g.moveTo(x + 7, yP); g.lineTo(x - 6, yP - 7); g.lineTo(x - 6, yP + 7); g.closePath(); g.fill(); });
  // 3-way valves
  ["ISO3", "ISO2", "ISO1"].forEach(name => {
    const v = vbyName[name] || {}, x = xV[name], s = 26, act = v.actual || "unknown", cmd = v.cmd || "unknown";
    const fill = act === "open" ? "#2fbf6a" : act === "closed" ? "#1c2530" : "#59636f", stroke = act === "open" ? "#8dffb8" : act === "closed" ? "#ff6b6b" : "#aab4c0";
    // third port (3-way valve): ISO2 goes to the capped propellant fill port, the others are plugged
    L([[x - 12, yP + 14], [x - 12, yP + (name === "ISO2" ? 32 : 20)]], pipe === FLUID_DIM ? FLUID_DIM : FLUID, 4);
    if (name === "ISO2") { R(x - 24, yP + 32, 24, 13, { fill: "#2a3440", stroke: "#8aa0b6", lw: 2, r: 3 }); T("fill port", x - 12, yP + 60, { s: 12, c: "#9fb3c8", al: "right" }); T("(capped)", x - 12, yP + 73, { s: 12, c: "#9fb3c8", al: "right" }); }
    else { g.fillStyle = "#8aa0b6"; g.fillRect(x - 17, yP + 20, 10, 4); }
    g.fillStyle = fill; g.strokeStyle = stroke; g.lineWidth = 3;
    for (const dx of [-1, 1]) { g.beginPath(); g.moveTo(x + dx * s, yP - s * 0.7); g.lineTo(x, yP); g.lineTo(x + dx * s, yP + s * 0.7); g.closePath(); g.fill(); g.stroke(); }
    L([[x + 14, 466], [x + 14, yP + 20]], pwrOn(pg12), 3);                        // 12 V to the actuator
    const mism = hk && act !== "unknown" && act !== cmd;
    const short = s => s === "unknown" ? "?" : s.toUpperCase();
    T(name, x, yP - 66, { b: true, s: 19, c: "#e6eef7" });
    T("CMD " + (hk ? short(cmd) : "—"), x, yP - 46, { s: 13, b: true, c: cmd === "open" ? "#6ff5a2" : cmd === "closed" ? "#ff8c8c" : "#9aa5b3" });
    T("ACT " + (hk ? short(act) : "—"), x, yP - 28, { s: 13, c: act === "unknown" ? "#9aa5b3" : mism ? "#ffd27a" : act === "open" ? "#6ff5a2" : "#ff8c8c" });
    if (v.pulsing) { g.fillStyle = "#ffe066"; g.beginPath(); g.moveTo(x + 36, yP - 62); g.lineTo(x + 28, yP - 46); g.lineTo(x + 33, yP - 46); g.lineTo(x + 29, yP - 32); g.lineTo(x + 40, yP - 50); g.lineTo(x + 35, yP - 50); g.closePath(); g.fill(); }
  });
  // flow meter (in line)
  g.fillStyle = "#1a232e"; g.strokeStyle = "#cfd9e4"; g.lineWidth = 2.5; g.beginPath(); g.arc(xV.FM, yP, 23, 0, 6.3); g.fill(); g.stroke(); T("FM", xV.FM, yP + 6, { b: true, s: 16 });
  T(flowFault ? "I2C FAULT" : hk ? num(hk.flow_ulmin, 3) : "—", xV.FM, yP + 36, { s: 14, b: true, c: flowFault ? "#ff7b7b" : "#e6eef7" });
  T(flowFault ? "no link" : "µL/min", xV.FM, yP + 50, { s: 12, c: flowFault ? "#ff7b7b" : "#9fb3c8" });
  T(!flowFault && hk ? num(hk.flow_degC) + " °C" : "", xV.FM, yP + 63, { s: 12, c: "#9fb3c8" });
  L([[xV.FM, yP - 24], [xV.FM, 336]], flowFault ? "#ff7b7b" : DIG, 2, { dash: [4, 3] });
  T("I2C", xV.FM + 8, 346, { al: "left", s: 11, c: "#9fb3c8" });

  // ================= thruster and its electrics (exhaust to the right)
  // discharge capacitor bank
  R(124, 548, 200, 160, { fill: "#1a232e", stroke: "#cfd9e4", lw: 2.5, r: 4 }); T("Discharge", 224, 570, { b: true, s: 14 }); T("Capacitor Bank", 224, 588, { b: true, s: 14 });
  for (const cx of [170, 224, 278]) { g.strokeStyle = "#cfd9e4"; g.lineWidth = 3; g.beginPath(); g.moveTo(cx - 14, 622); g.lineTo(cx + 14, 622); g.moveTo(cx - 14, 634); g.lineTo(cx + 14, 634); g.stroke(); }
  T("V_D (pre-pulse, pin)", 224, 668, { s: 12, c: "#9fb3c8" }); T(isNum(vdPre) ? num(vdPre, 3) + " V" : "—", 224, 690, { b: true, s: 17, c: isNum(vdPre) && vdPre > 0.3 ? "#6ff5a2" : "#e6eef7" });
  T("charge", 224, 704, { s: 11, c: "#7f93a8" });
  L([[214, 448], [214, 548]], pwrOn(dcEn && dcPg), 4, { arrow: 8 }); T("−400 V", 224, 500, { al: "left", s: 13, c: pwrOn(dcEn && dcPg) });
  // igniter supply
  L([[416, 448], [416, 590], [960, 590]], pwrOn(igEn && igPg), 3, { arrow: 8 }); T("−1 kV (pulsed)", 428, 500, { al: "left", s: 13, c: pwrOn(igEn && igPg) });
  T("igniter supply", 428, 520, { al: "left", s: 12, c: "#9fb3c8" }); T(hk ? num(hk.ign_supply_v) + " V" : "—", 428, 540, { al: "left", b: true, s: 17, c: hk && Math.abs(hk.ign_supply_v) > 50 ? "#6ff5a2" : "#e6eef7" });
  // main discharge path through I_D
  L([[324, 630], [960, 630]], PWR, 8); SENSOR(560, 630, "I_D"); T(isNum(idPeak) ? num(idPeak, 3) + " V pk" : "—", 560, 668, { s: 12 });
  L([[560, 611], [560, 560], [300, 560], [300, 344]], ANA, 2, { arrow: 6 });
  // thruster body
  const firing = isNum(d.fireAge) && d.fireAge < 2;
  if (firing) { g.fillStyle = "rgba(176,72,255,.5)"; g.beginPath(); g.moveTo(1140, 552); g.lineTo(1236, 506); g.lineTo(1236, 716); g.lineTo(1140, 668); g.closePath(); g.fill();
    g.fillStyle = "rgba(225,170,255,.7)"; g.beginPath(); g.moveTo(1140, 592); g.lineTo(1215, 580); g.lineTo(1215, 640); g.lineTo(1140, 628); g.closePath(); g.fill(); }
  g.fillStyle = "#4a5666"; g.strokeStyle = "#9fb3c8"; g.lineWidth = 3;
  g.beginPath(); g.moveTo(960, 572); g.lineTo(1050, 572); g.lineTo(1140, 534); g.lineTo(1140, 686); g.lineTo(1050, 648); g.lineTo(960, 648); g.closePath(); g.fill(); g.stroke();
  g.fillStyle = "#1a232e"; g.fillRect(960, 590, 70, 40); g.strokeRect(960, 590, 70, 40);
  g.strokeStyle = "#cfd9e4"; g.lineWidth = 6; g.beginPath(); g.moveTo(985, 610); g.lineTo(1130, 610); g.stroke();
  T("PMPD", 1100, 706, { b: true, s: 15 });
  T(hk ? `frit ${num(hk.frit_mbar)} mbar · ${num(hk.frit_degC)} °C` : "frit —", 1020, 548, { al: "left", s: 13 });
  T(pulse && pulse.index ? `last pulse #${pulse.index}` : "no pulse since boot", 1010, 526, { al: "left", s: 13, c: "#9fb3c8" });
}
