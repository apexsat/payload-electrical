"use strict";
/* PIB bench interface front end: telemetry on the left, commands (dropdown + sequencer) on the right. */

let S = null;
const $ = id => document.getElementById(id);
const ADC_VREF = 3.3;

const esc = s => String(s).replace(/[&<>"]/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
const isNum = v => v !== null && v !== undefined && !Number.isNaN(v);
const hms = s => { if (!isNum(s)) return "—"; s = Math.max(0, Math.round(s)); const h = Math.floor(s / 3600), m = Math.floor(s / 60) % 60;
  return (h ? h + "h " : "") + String(m).padStart(h ? 2 : 1, "0") + "m " + String(s % 60).padStart(2, "0") + "s"; };
const pinV = c => c / 4095 * ADC_VREF;

// same rounding as pib_cdh.sig(): 4 significant figures in plain notation
function sig(v, n = 4) {
  if (!isNum(v)) return "--";
  if (v === 0) return "0";
  v = parseFloat(v.toPrecision(n));
  const dec = Math.max(0, n - 1 - Math.floor(Math.log10(Math.abs(v))));
  return v.toFixed(dec);
}
const fmt = (v, unit) => isNum(v) ? `${sig(v)} ${unit}` : "--";
const chip = (txt, cls) => `<span class="chip ${cls || ""}">${esc(txt)}</span>`;

async function api(path, body) {
  const r = await fetch(path, { method: body === undefined ? "GET" : "POST", headers: { "Content-Type": "application/json" },
    body: body === undefined ? undefined : JSON.stringify(body) });
  const j = await r.json();
  if (!r.ok) throw new Error(j.error || r.statusText);
  return j;
}

// ------------------------------------------------------------------ commands
const VALVE_NAMES = ["ISO1", "ISO2", "ISO3"];
const CMDS = [
  { name: "ARM", group: "Payload", args: [],
    desc: "Arm the payload (turns on the 12 V, opens the valves, enables the HV supplies) and keep it armed: ARM is re-sent every 2 s until SAFE or STOP and Safe." },
  { name: "SAFE", group: "Payload", args: [],
    desc: "SAFE the payload: igniter and discharge enables off, valves closed, ARM line low. Heaters are left alone." },
  { name: "FIRE_PULSE", group: "Payload", desc: "Fire one igniter pulse (the PIB must be ARMED), then read and plot its waveform.",
    args: [{ name: "WIDTH_US", type: "float", def: 100, min: 0, help: "pulse width, µs (0.1 µs steps)" },
           { name: "CSV", type: "text", opt: true, help: "optional file name; saves the waveform in tests/" }] },
  { name: "FIRE_PULSES", group: "Payload", desc: "Fire a train of pulses at a fixed rate (the PIB must be ARMED). STOP and Safe aborts it.",
    args: [{ name: "COUNT", type: "int", def: 100, min: 1, help: "number of pulses" },
           { name: "WIDTH_US", type: "float", def: 100, min: 0, help: "pulse width, µs" },
           { name: "RATE_HZ", type: "float", def: 1, min: 0.01, help: "pulses per second" }] },
  { name: "SET_VALVE", group: "Valves and heaters", desc: "Open or close a valve (all three are 3-way). ISO3 is the normally-closed solenoid (stays open only while powered); ISO2 and ISO1 latch. Valves need 12 V: ARM first.",
    args: [{ name: "VALVE", type: "enum", opts: VALVE_NAMES.concat(["ALL"]), help: "ISO1 is nearest the thruster" },
           { name: "ACTION", type: "enum", opts: ["OPEN", "CLOSE"] }] },
  { name: "SET_HEATER", group: "Valves and heaters", desc: "Switch the tank heaters. The PIB turns them off by itself 60 s after the last heater-on.",
    args: [{ name: "HEATER", type: "enum", opts: ["BOTH", "1", "2"] }, { name: "STATE", type: "enum", opts: ["ON", "OFF"] }] },
  { name: "TANK_TEMP_CONTROL", group: "Valves and heaters", desc: "Bang-bang tank temperature control: both heaters on below SETPOINT − HYSTERESIS/2, off above SETPOINT + HYSTERESIS/2. Runs in this program, so keep it open.",
    args: [{ name: "ENABLE", type: "enum", opts: ["ON", "OFF"] }, { name: "SETPOINT_C", type: "float", def: 20, min: -20, help: "tank setpoint, °C" },
           { name: "HYSTERESIS_C", type: "float", def: 1, min: 0.1, help: "total band, °C" }] },
  { name: "READ_WAVEFORM", group: "Other", desc: "Read the last pulse's voltage / current waveform again.",
    args: [{ name: "CSV", type: "text", opt: true, help: "optional file name; saves the waveform in tests/" }] },
  { name: "SET_OUTPUT", group: "Other", desc: "Drive one PIB output pin directly (bench use; arm / igniter / discharge time out after 10 s).",
    args: [{ name: "OUTPUT", type: "enum", opts: ["vbat", "arm", "igniter", "discharge", "htr1", "htr2", "pdrst", "wdg"] }, { name: "STATE", type: "enum", opts: ["ON", "OFF"] }] },
  { name: "READ_REGISTER", group: "Other", desc: "Read raw register bytes (debugging); the result goes in the log.",
    args: [{ name: "REG", type: "text", def: "0x50", help: "first register, decimal or hex" }, { name: "N", type: "int", def: 8, min: 1, help: "bytes, 1-255" }] },
];
const BY_NAME = Object.fromEntries(CMDS.map(c => [c.name, c]));

const SAVED = {
  "Cycle all valves": [
    { cmd: "ARM", args: {}, wait: 2 },
    ...["ISO3", "ISO2", "ISO1"].flatMap(v => [{ cmd: "SET_VALVE", args: { VALVE: v, ACTION: "CLOSE" }, wait: 1.5 }, { cmd: "SET_VALVE", args: { VALVE: v, ACTION: "OPEN" }, wait: 1.5 }]),
    { cmd: "SAFE", args: {}, wait: 0 }],
  "Fire 1 pulse": [
    { cmd: "ARM", args: {}, wait: 2 },
    { cmd: "FIRE_PULSE", args: { WIDTH_US: 100, CSV: "" }, wait: 1 },
    { cmd: "SAFE", args: {}, wait: 0 }],
  "Fire 100 pulses (100 µs, 1 Hz)": [
    { cmd: "ARM", args: {}, wait: 2 },
    { cmd: "FIRE_PULSES", args: { COUNT: 100, WIDTH_US: 100, RATE_HZ: 1 }, wait: 1 },
    { cmd: "SAFE", args: {}, wait: 0 }],
};

function describe(step) {
  const a = step.args || {}, c = step.cmd;
  if (c === "FIRE_PULSE") return `FIRE_PULSE ${a.WIDTH_US} µs`;
  if (c === "FIRE_PULSES") return `FIRE_PULSES ${a.COUNT} × ${a.WIDTH_US} µs @ ${a.RATE_HZ} Hz`;
  if (c === "TANK_TEMP_CONTROL") return "TANK_TEMP_CONTROL " + (String(a.ENABLE).toUpperCase() === "OFF" ? "OFF" : `${a.SETPOINT_C} °C ±${a.HYSTERESIS_C}`);
  return c + Object.values(a).filter(v => v !== "" && v !== null && v !== undefined).map(v => " " + v).join("");
}

let SEQ = [];
try { SEQ = JSON.parse(localStorage.getItem("pib_seq") || "[]"); } catch (e) { SEQ = []; }
const saveSeq = () => { try { localStorage.setItem("pib_seq", JSON.stringify(SEQ)); } catch (e) { /* private mode */ } };
const userSeqs = () => { try { return JSON.parse(localStorage.getItem("pib_saved_seqs") || "{}"); } catch (e) { return {}; } };
const allSeqs = () => Object.assign({}, SAVED, userSeqs());

function buildArgs() {
  const c = BY_NAME[$("cmd-sel").value];
  $("cmd-desc").textContent = c.desc;
  $("cmd-args").innerHTML = c.args.map((a, i) => {
    let inp;
    if (a.type === "enum") inp = `<select data-a="${a.name}">${a.opts.map(o => `<option>${esc(o)}</option>`).join("")}</select>`;
    else if (a.type === "text") inp = `<input data-a="${a.name}" value="${esc(a.def || "")}" placeholder="${a.opt ? "optional" : ""}" style="width:150px">`;
    else inp = `<input data-a="${a.name}" type="number" step="${a.type === "int" ? 1 : "any"}" value="${a.def}" ${a.min !== undefined ? `min="${a.min}"` : ""} style="width:110px">`;
    return `<label class="f">${a.name}${inp}${a.help ? `<span class="hintline">${esc(a.help)}</span>` : ""}</label>`;
  }).join("");
  $("cmd-err").textContent = "";
}

function readStep() {
  const c = BY_NAME[$("cmd-sel").value], args = {};
  for (const a of c.args) {
    const el = $("cmd-args").querySelector(`[data-a="${a.name}"]`);
    if (a.type === "int" || a.type === "float") {
      const v = parseFloat(el.value);
      if (!isFinite(v)) throw new Error(`${a.name}: enter a number`);
      if (a.min !== undefined && v < a.min) throw new Error(`${a.name}: must be at least ${a.min}`);
      args[a.name] = v;
    } else args[a.name] = el.value.trim();
  }
  return { cmd: c.name, args, wait: parseFloat($("cmd-wait").value) || 0 };
}

async function run(steps, label, errEl) {
  errEl.textContent = "";
  try { await api("/api/run", { steps, label }); }
  catch (e) { errEl.textContent = e.message; }
}

function drawSeq() {
  $("seq").innerHTML = SEQ.map((s, i) => `<li data-i="${i}"><span class="t">${esc(describe(s))} <small>then wait ${s.wait || 0} s</small></span>` +
    `<button class="btn sm" data-up="${i}">↑</button><button class="btn sm" data-dn="${i}">↓</button><button class="btn sm" data-rm="${i}">✕</button></li>`).join("")
    || `<li style="border:0"><span class="sub">Empty. Choose a command, set its parameters and press “Add to sequence”, or load a saved sequence.</span></li>`;
  $("seq-n").textContent = SEQ.length ? `${SEQ.length} steps` : "";
  $("seq").querySelectorAll("button").forEach(b => b.addEventListener("click", () => {
    const d = b.dataset, i = +(d.up ?? d.dn ?? d.rm);
    if (d.up !== undefined && i > 0) [SEQ[i - 1], SEQ[i]] = [SEQ[i], SEQ[i - 1]];
    else if (d.dn !== undefined && i < SEQ.length - 1) [SEQ[i + 1], SEQ[i]] = [SEQ[i], SEQ[i + 1]];
    else if (d.rm !== undefined) SEQ.splice(i, 1);
    saveSeq(); drawSeq();
  }));
}

function fillSaved() { $("saved").innerHTML = Object.keys(allSeqs()).map(n => `<option>${esc(n)}</option>`).join(""); }

// ------------------------------------------------------------------ drawings
function rrect(g, x, y, w, h, r) {
  const [a, b, c, d] = Array.isArray(r) ? r : [r, r, r, r];
  g.beginPath(); g.moveTo(x + a, y); g.lineTo(x + w - b, y); g.quadraticCurveTo(x + w, y, x + w, y + b); g.lineTo(x + w, y + h - c);
  g.quadraticCurveTo(x + w, y + h, x + w - c, y + h); g.lineTo(x + d, y + h); g.quadraticCurveTo(x, y + h, x, y + h - d); g.lineTo(x, y + a);
  g.quadraticCurveTo(x, y, x + a, y); g.closePath();
}

// Oscilloscope: discharge voltage (white, PA0) and current (red, PA1) as pin volts against time from ignition; counts on the right.
function drawScope(cv, wave) {
  const g = cv.getContext("2d"), W = cv.width, H = cv.height, padL = 70, padR = 84, padT = 14, padB = 46;
  g.fillStyle = "#000"; g.fillRect(0, 0, W, H);
  const rows = wave ? wave.rows : null;
  let t0 = -6, t1 = 32;
  if (rows && rows.length) { t0 = rows[0].t_us; t1 = rows[rows.length - 1].t_us; }
  const X = t => padL + (t - t0) / (t1 - t0) * (W - padL - padR), Y = v => padT + (1 - v / ADC_VREF) * (H - padT - padB);
  g.strokeStyle = "#1f3a1f"; g.lineWidth = 1;
  for (let k = 0; k <= 6; k++) { const y = padT + k * (H - padT - padB) / 6; g.beginPath(); g.moveTo(padL, y); g.lineTo(W - padR, y); g.stroke(); }
  const step = [1, 2, 5, 10, 20, 25, 50].find(s => (t1 - t0) / s <= 8) || 50;
  for (let t = Math.ceil(t0 / step) * step; t <= t1; t += step) { g.beginPath(); g.moveTo(X(t), padT); g.lineTo(X(t), H - padB); g.stroke(); }
  g.strokeStyle = "#5c8a5c"; g.strokeRect(padL, padT, W - padL - padR, H - padT - padB);
  g.font = "17px monospace"; g.fillStyle = "#e8e8e8"; g.textAlign = "right";
  for (const v of [0, 1, 2, 3]) g.fillText(String(v), padL - 8, Y(v) + 6);
  g.fillStyle = "#ff5a5a"; g.textAlign = "left";
  for (const c of [0, 1000, 2000, 3000, 4000]) g.fillText(String(c), W - padR + 8, Y(pinV(c)) + 6);
  g.fillStyle = "#e8e8e8"; g.textAlign = "center";
  for (let t = Math.ceil(t0 / step) * step; t <= t1; t += step) g.fillText(String(t), X(t), H - padB + 20);
  g.fillText("time from ignition, µs", (padL + W - padR) / 2, H - 8);
  g.save(); g.translate(16, (padT + H - padB) / 2); g.rotate(-Math.PI / 2); g.fillText("pin volts, V", 0, 0); g.restore();
  g.fillStyle = "#ff5a5a"; g.save(); g.translate(W - 14, (padT + H - padB) / 2); g.rotate(Math.PI / 2); g.fillText("ADC counts", 0, 0); g.restore();
  if (!rows) { g.fillStyle = "#7f93a8"; g.textAlign = "center"; g.font = "18px monospace"; g.fillText("no waveform yet (fire a pulse, or READ_WAVEFORM)", (padL + W - padR) / 2, H / 2); return; }
  const trace = (key, col) => { g.strokeStyle = col; g.lineWidth = 2.5; g.beginPath(); rows.forEach((r, i) => { const x = X(r.t_us), y = Y(r[key]); if (i) g.lineTo(x, y); else g.moveTo(x, y); }); g.stroke(); };
  trace("v_pin", "#f2f2f2"); trace("i_pin", "#ff4a4a");
  g.strokeStyle = "#7fb2ff"; g.setLineDash([4, 4]); g.beginPath(); g.moveTo(X(0), padT); g.lineTo(X(0), H - padB); g.stroke(); g.setLineDash([]);
  g.font = "16px monospace"; g.textAlign = "left"; g.fillStyle = "#f2f2f2"; g.fillText("voltage PA0", padL + 10, padT + 24); g.fillStyle = "#ff4a4a"; g.fillText("current PA1", padL + 10, padT + 46);
}

// ------------------------------------------------------------------ rendering
const lastKey = {};
const changed = (name, key) => { if (lastKey[name] === key) return false; lastKey[name] = key; return true; };
const rawNtc = c => `-- C   (${c} cnt, ${pinV(c).toFixed(3)} V at pin; part not set yet)`;

function hkRows(hk) {
  const t = k => fmt(hk[k], "C");
  const c1 = [["status", "flags", `0x${hk.status.toString(16).toUpperCase().padStart(2, "0")}  ${hk.flags}`],
    ["pressure", "frit", `${fmt(hk.frit_mbar, "mbar")} @ ${t("frit_degC")}`], ["", "tank", `${fmt(hk.tank_mbar, "mbar")} @ ${t("tank_degC")}`],
    ["flow", "rate", `${fmt(hk.flow_ulmin, "uL/min")} @ ${t("flow_degC")}`]];
  const c2 = [["temperature", "PIB", t("pib_ntc_degC")], ["", "MCU", t("mcu_degC")], ["", "igniter", t("ign_ntc_degC")],
    ["", "tank 1", isNum(hk.tank_ntc1_degC) ? t("tank_ntc1_degC") : rawNtc(hk.ntc_raw[0])],
    ["", "tank 2", isNum(hk.tank_ntc2_degC) ? t("tank_ntc2_degC") : rawNtc(hk.ntc_raw[1])],
    ["", "discharge", isNum(hk.dchg_ntc_degC) ? t("dchg_ntc_degC") : rawNtc(hk.ntc_raw[2])]];
  const p = hk.pulse_view, c3 = [["igniter", "supply", fmt(hk.ign_supply_v, "V")]];
  if (p.index === 0) c3.push(["", "last pulse", "none since boot"]);
  else c3.push(["", "last pulse", "#" + p.index], ["", "peak V", `${fmt(p.peak_v_volts, "V")}   (${p.peak_v_cnt} cnt)`],
    ["", "peak I", `${fmt(p.peak_i_pin_v, "V")} at pin   (${p.peak_i_cnt} cnt, amps scale TBD)`]);
  return [c1, c2, c3];
}
const rowsHTML = rows => rows.map(([g, k, v]) => `<tr><td class="g">${g}</td><td class="k">${esc(k)}</td><td>${esc(v)}</td></tr>`).join("");

function render() {
  const hk = S.hk, conn = S.connected, job = S.job, running = !!(job && job.running);
  const lk = $("link"); lk.textContent = conn ? "CONNECTED" : "DISCONNECTED"; lk.className = "chip big " + (conn ? "ok" : "bad"); lk.title = conn ? S.link : "";
  $("h-up").textContent = hk ? hms(hk.uptime_s) : "—";
  $("h-age").textContent = S.hk_age === null ? "—" : S.hk_age.toFixed(1) + " s ago";
  $("h-job").textContent = running ? job.label + (job.progress ? " · " + job.progress : "") : "—";
  $("h-err").textContent = S.error || "";
  $("pl-age").textContent = S.hk_age === null ? "" : S.hk_age.toFixed(0) + " s";
  $("c-desc").textContent = conn ? S.link : ""; $("c-desc").style.color = "";
  $("b-stop").disabled = !conn;
  for (const id of ["b-send", "b-run"]) $(id).disabled = !conn || running;
  $("b-connect").disabled = false; $("b-disconnect").disabled = !conn;
  // telemetry
  drawPID($("plumb"), { hk, wave: S.wave, fireAge: S.fire_age, tank: S.tank, armedHold: S.armed_hold });
  drawScope($("scope"), S.wave);
  const p = hk ? hk.pulse_view : null;
  $("pk-v").textContent = p && p.index ? sig(p.peak_v_volts) : "—";
  $("pk-i").textContent = p && p.index ? sig(p.peak_i_pin_v) : "—";
  $("wave-age").textContent = S.wave_age === null ? "" : "read " + S.wave_age.toFixed(0) + " s ago";
  if (S.wave && changed("wave", Math.round((S.now - S.wave_age) * 10))) {
    const rows = S.wave.rows, stat = k => { const xs = rows.map(r => r[k]); return `min ${sig(Math.min(...xs))} V  max ${sig(Math.max(...xs))} V  mean ${sig(xs.reduce((a, b) => a + b, 0) / xs.length)} V`; };
    $("wave-note").textContent = `${rows.length} samples, ${sig(S.wave.sample_us)} µs apart · voltage ${stat("v_pin")} · current ${stat("i_pin")}`;
    $("wave-rows").innerHTML = rows.map(r => `<tr><td>${r.i}</td><td>${sig(r.t_us)}</td><td>${sig(r.v_pin)}</td><td>${r.v_cnt}</td><td>${sig(r.i_pin)}</td><td>${r.i_cnt}</td></tr>`).join("");
  }
  if (hk && changed("hk", JSON.stringify(hk))) {
    $("hk-title").textContent = "uptime " + fmt(hk.uptime_s, "s");
    const [a, b, c] = hkRows(hk); $("hk-a").querySelector("tbody").innerHTML = rowsHTML(a); $("hk-b").querySelector("tbody").innerHTML = rowsHTML(b); $("hk-c").querySelector("tbody").innerHTML = rowsHTML(c);
  }
  if (!hk && changed("hk", "none")) { $("hk-a").querySelector("tbody").innerHTML = `<tr><td class="k">${conn ? "waiting for the first read…" : "connect to the PIB to see housekeeping"}</td></tr>`; $("hk-b").querySelector("tbody").innerHTML = ""; $("hk-c").querySelector("tbody").innerHTML = ""; $("hk-title").textContent = ""; }
  // sequence progress
  const lis = $("seq").querySelectorAll("li[data-i]");
  const mine = job && job.label === "Sequence" && job.steps.length === SEQ.length;
  lis.forEach((li, i) => { li.className = mine ? job.steps[i].state : ""; });
  // log
  const top = S.log.length ? S.log[0].n : 0;
  if (changed("log", top)) $("log").innerHTML = S.log.map(l => `<tr><td class="t">${new Date(l.t * 1000).toLocaleTimeString()}</td><td class="m" style="${/^ERROR/.test(l.msg) ? "color:var(--bad)" : /^STOP/.test(l.msg) ? "color:var(--warn)" : ""}">${esc(l.msg)}</td></tr>`).join("");
}

async function tick() {
  try { S = await api("/api/state"); render(); }
  catch (e) { $("link").textContent = "GUI SERVER OFFLINE"; $("link").className = "chip big bad"; }
  setTimeout(tick, 250);
}

// ------------------------------------------------------------------ wiring
function saved() { try { return JSON.parse(localStorage.getItem("pib_gui") || "{}"); } catch (e) { return {}; } }
function save() {
  const o = { link: $("c-link").value, port: $("c-port").value, baud: $("c-baud").value, spibus: $("c-spibus").value, spidev: $("c-spidev").value,
    spihz: $("c-spihz").value, i2cbus: $("c-i2cbus").value, i2caddr: $("c-i2caddr").value };
  try { localStorage.setItem("pib_gui", JSON.stringify(o)); } catch (e) { /* private mode */ }
}
const showLinkFields = () => document.querySelectorAll("[data-for]").forEach(e => { e.style.display = e.dataset.for === $("c-link").value ? "" : "none"; });

async function init() {
  // command dropdown
  const groups = {};
  for (const c of CMDS) (groups[c.group] = groups[c.group] || []).push(c);
  $("cmd-sel").innerHTML = Object.entries(groups).map(([g, cs]) => `<optgroup label="${g}">${cs.map(c => `<option>${c.name}</option>`).join("")}</optgroup>`).join("");
  $("cmd-sel").addEventListener("change", buildArgs);
  buildArgs(); drawSeq(); fillSaved();
  $("b-send").addEventListener("click", () => { try { const s = readStep(); run([Object.assign(s, { wait: 0 })], describe(s), $("cmd-err")); } catch (e) { $("cmd-err").textContent = e.message; } });
  $("b-add").addEventListener("click", () => { try { SEQ.push(readStep()); saveSeq(); drawSeq(); $("cmd-err").textContent = ""; } catch (e) { $("cmd-err").textContent = e.message; } });
  $("b-run").addEventListener("click", () => { if (!SEQ.length) { $("seq-err").textContent = "The sequence is empty."; return; } run(SEQ, "Sequence", $("seq-err")); });
  $("b-clear").addEventListener("click", () => { SEQ = []; saveSeq(); drawSeq(); $("seq-err").textContent = ""; });
  $("b-load").addEventListener("click", () => { const q = allSeqs()[$("saved").value]; if (q) { SEQ = JSON.parse(JSON.stringify(q)); saveSeq(); drawSeq(); $("seq-err").textContent = ""; } });
  $("b-save").addEventListener("click", () => {
    if (!SEQ.length) { $("seq-err").textContent = "The sequence is empty."; return; }
    const name = (prompt("Name for this sequence:") || "").trim();
    if (!name) return;
    const u = userSeqs(); u[name] = JSON.parse(JSON.stringify(SEQ));
    try { localStorage.setItem("pib_saved_seqs", JSON.stringify(u)); } catch (e) { /* private mode */ }
    fillSaved(); $("saved").value = name;
  });
  $("b-stop").addEventListener("click", async () => { try { await api("/api/stop", {}); $("h-err").textContent = ""; } catch (e) { $("h-err").textContent = e.message; } });
  // connection
  let st = null;
  try { st = await api("/api/state"); } catch (e) { /* offline */ }
  $("c-link").innerHTML = ["nucleo", "spi", "i2c"].concat(st && st.sim_available ? ["sim"] : []).map(k => `<option value="${k}">${k === "sim" ? "sim (no hardware)" : k}</option>`).join("");
  const sv = saved();
  if (sv.link && [...$("c-link").options].some(o => o.value === sv.link)) $("c-link").value = sv.link;
  for (const [id, k] of [["c-port", "port"], ["c-baud", "baud"], ["c-spibus", "spibus"], ["c-spidev", "spidev"], ["c-spihz", "spihz"], ["c-i2cbus", "i2cbus"], ["c-i2caddr", "i2caddr"]]) if (sv[k]) $(id).value = sv[k];
  showLinkFields();
  $("c-link").addEventListener("change", () => { showLinkFields(); save(); });
  document.querySelectorAll("details .row input").forEach(i => i.addEventListener("change", save));
  api("/api/ports").then(r => { $("ports").innerHTML = r.ports.map(p => `<option value="${p}">`).join(""); }).catch(() => {});
  $("b-connect").addEventListener("click", async () => {
    save();
    try { await api("/api/connect", { link: $("c-link").value, port: $("c-port").value, baud: +$("c-baud").value, spi_bus: +$("c-spibus").value, spi_dev: +$("c-spidev").value,
      spi_hz: +$("c-spihz").value, i2c_bus: +$("c-i2cbus").value, i2c_addr: $("c-i2caddr").value }); $("h-err").textContent = ""; }
    catch (e) { $("c-desc").textContent = e.message; $("c-desc").style.color = "var(--bad)"; }
  });
  $("b-disconnect").addEventListener("click", () => api("/api/disconnect", {}));
  tick();
}
init();
