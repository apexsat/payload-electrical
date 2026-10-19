# 200_40 PIB — Schematic & Project Review (Living Document)

| | |
|---|---|
| **Board** | 200_40 Payload Interface Board (APEx PMPD thruster, 6U CubeSat) |
| **Reviewed files** | `PIB.kicad_sch`, `Rogowski.kicad_sch`, `Valve Driver.kicad_sch`, `DPT Mux.kicad_sch`, `PIB.kicad_pcb` (parity only), `firmware/firmware.ioc` (pin/clock config) |
| **Reference docs** | `Payload ICD.pdf` (Rev 2.0, CDR), `PIB Block Diagram.drawio.png`, `200_60 DPT Sensor Board` |
| **Design state** | branch `pib-split`, commit `3aa27f2` |
| **Tools** | KiCad 10.0.4 (`kicad-cli` ERC, netlist, DRC w/ schematic parity) |
| **Last updated** | 2026-10-19 |

**Status key:** `OPEN` (needs a design change) · `VERIFY` (needs a datasheet, bench or part check) · `OPTIONAL` (recommended hardening, not required) · `ICD` (schematic is correct; the ICD must change)

**How to use:** This document lists only issues present in the current KiCad design. When an issue is resolved, delete it. Item IDs are stable, so gaps in the numbering are expected. Don't renumber.

---

## Summary

The pin-to-pad check is clean for every footprint. The main risks are:
- The GSE path holds the latching valve at DC 12 V instead of pulsing it (C11).
- It's unconfirmed whether the NC valve (valve 2) is rated for steady 12 V. The Q10 drive holds full 12 V on it continuously, which is wrong for a spike-and-hold coil (C10).
- The Rogowski integrator is unstable and needs a mid-scale offset for its bidirectional output (C5).
- The heaters have no on-orbit hardware cutoff (C9).
- The SOD-323 OR-ing Schottkys (20 V, 1 A, 200 mW) are marginal for the GSE path (F9).
- GSE mode back-powers the unpowered flight 3.3 V rail by about 5 mA (S13).
- The PCB is out of sync with the schematic (29 parity issues, P6).

The BOM is not production-ready (no MPNs or capacitor ratings), and `Outputs/` holds gerbers from a different project. Several ICD items need to be brought in line with the schematic (see [ICD Update Actions](#icd-update-actions)).

**Design context:**
- UNP NS requirements apply. Launch inhibits live in EPS, so the PIB and CDH are fully unpowered until after deployment.
- HV boards (igniter 1 kV, discharge 400 V) scale all signals to 0 to <3.3 V with op amps on ±5 V.
- GSE OPEN/~CLOSE (J1.2) is a 3.3 V digital logic line.
- The NC valve (valve 2) is a non-latching normally-closed solenoid. It has been bench-run on plain 12 V DC and is held open continuously during burns.
- Each heater is well under 10 W (TBD).
- The CDH connector is the vertical (MS) part.
- Only 2 P/T sensors (tank, frit) are fitted; a third may be added later.
- The Rogowski readout is bidirectional.

| Category | Items |
|---|---|
| Critical | 5 (1 optional) |
| Significant | 12 |
| Footprint / Part | 9 |
| Project setup | 6 |
| ICD / Block diagram | 9 |
| Open questions | 2 |

---

## Critical (safety or function)

### C5 — Rogowski integrator won't work as designed — `OPEN`
- **Where:** Rogowski sheet, U11 (OP37), C45/R79 = TBD, J7.
- **Problems:**
  1. The OP37 is decompensated (stable only at a gain of 5 or more). An integrator reaches unity gain at high frequency, so it will oscillate.
  2. There is no DC feedback path (no resistor across C45) and no reset switch, so the output drifts to a rail. The block diagram shows a reset; the schematic has none.
  3. The ±5 V rails let the output drive PA1 (ADC) far outside its −0.3 V to VDD+0.3 V limits.
  4. There is no coil termination resistor and no ESD protection on J7.
  5. The readout is bidirectional, so the output needs a mid-scale offset.
- **Fix:**
  - Use a unity-gain-stable, rail-to-rail op amp on a single supply (0/3.3 V, or 0/+5 V with a clamp), with GBW ≳ 10 MHz for the 20 µs pulse content. A single supply removes item 3 and the dependence on −5 V.
  - Reference the non-inverting input to a buffered V<sub>DDA</sub>/2 (≈ 1.65 V, divided from VDDA so it tracks the ADC reference). The output then swings about mid-scale.
  - Size the coil termination, R79 and C45 so the expected peak fits in about ±1.5 V.
  - Add a leak resistor or a GPIO-driven reset switch, coil termination, and ESD protection on J7.
  - The coil isn't characterized yet (Q8). Lay out R79/C45 and the leak resistor as adjustable 0603 footprints, plus a DNP reset-switch footprint.

### C6 — ADC inputs from the ±5 V HV boards are unclamped — `OPTIONAL`
- **Where:** `ADC_V_DCHG` (J6 → PA0, only D18 PESD5V0S1UL with no series R); `ADC_V_IGN_PK`, `ADC_I_IGN_PK`, `ADC_V_IGN_SUPPLY` (J5, only PESD3V3U1UL).
- **Risk:** Nominal signals are 0 to <3.3 V. However, the HV-board op amps run on ±5 V, so a fault, overrange, power-sequencing event or HV arc can push an output to a rail, and the ESD diodes are not continuous clamps.
- **Hardening:** Add a series R plus a low-capacitance Schottky pair to VDDA/GND at each PIB ADC input.
  - Fast channels (PA0, and PA1 via C5): R ≈ 47–100 Ω with a local reservoir capacitor, so the ADC's short sampling time is still met.
  - Slow channels (peak-detect, NTC, supply): R ≈ 1k.
  - Alternatively, clamp the output stages on the HV boards.

### C9 — Heaters and BAT branches lack hardware protection / cutoff — `OPEN`
- **Where:** Main sheet, Q9/Q11 (low-side heater switches on `BAT_HOT`); Valve Driver U1 (LT3065 `SHDN` tied to IN).
- **Problems (on orbit; launch inhibits are in EPS):**
  - A heater FET that fails on keeps heating a pressurized tank, with no hardware over-temperature cutoff.
  - There is no PIB-level overcurrent protection on the BAT input, heaters or 12 V regulator.
  - The 12 V rail is always on whenever BAT is present.
  - This contradicts the ICD: "BAT will have overcurrent protection…" and "only pass BAT to … heaters, if 3.3V is also powered and the microcontroller is healthy."
- **Heater sizing:** Worst case is 2 × 10 W at 12 V = 1.67 A, which puts about 0.83 A on each of J2's shared BAT pins (2, 3) and return pins (1, 4). That's within PicoBlade's ~1 A/contact but tight at the 10 W bound. Confirm once wattage is fixed.
- **Minimum fix:**
  - (a) A hardware over-temperature cutoff on the heaters: either a thermostat in series, or a comparator on a tank NTC that pulls the heater gates low.
  - (b) Either a heater/12 V branch eFuse (~2 A) gated by a hardware-healthy signal, or ICD wording that matches the as-built protection.

### C10 — Confirm the NC valve is rated for continuous 12 V, not spike-and-hold — `VERIFY`
- **Where:** Valve Driver sheet. Q10 (IRF7832) is a single low-side switch on J4.6, with the coil to `+12V` (J4.5) and D37 freewheeling across it. Gate drive is `VALVE2_EN_GATED` (via R5) and GSE `OPEN/~CLOSE` (via R47). The valve is non-latching and normally closed.
- **Why it matters:** While open, Q10 holds the full `+12V` (about 11.5 V, S15) across the coil, for the whole burn or GSE fill. That's only correct if the coil is rated for continuous 12 V.
  - Some solenoid valves are **spike-and-hold**: 12 V gives a short pull-in spike, and then the voltage drops to a lower holding level.
  - On a spike-and-hold coil, continuous 12 V overheats the coil. In vacuum that heat can only leave by conduction. It also draws several times the needed holding current from the LT3065 (300 mA limit), D11/D36 (F9) and J4.
  - The bench run on plain 12 V proves the valve actuates, but it doesn't show the coil survives 12 V for a full burn duration.
- **Action:** From the valve datasheet, confirm the continuous-duty voltage rating, coil resistance and steady-state current, then record them in Q3. If it turns out to be spike-and-hold, choose one of these:
  - PWM Q10 after pull-in. `VALVE2_EN` (PB5) would need a timer channel. D37 already freewheels the coil during the PWM off-time.
  - Add a series hold resistor bypassed during pull-in.
  - Use a dedicated spike-and-hold driver.
  - Whichever you pick, the GSE path through R47 is DC-only and needs the same treatment.

### C11 — GSE path holds the latching valve at DC 12 V instead of pulsing it — `OPEN`
- **Where:** Valve Driver sheet.
  - U9 (74LVC2G126, on `3.3V_EGSE`, OE from the R46/R45 UVLO) drives U3 (MIC4424, valve 3, J4.3/J4.4) directly.
  - U9.3 → `Net-(U3-INA)` follows GSE OPEN. This is the **open path**.
  - U9.6 → `Net-(U3-INB)` follows the inverted OPEN from U8, which is the close path.
  - Each input node also has R42/R41 (100k to GND) and R10/R11 (1k from flight U15).
- **Problem:** U9's outputs are DC levels, so while GSE commands OPEN the MIC4424 holds the latching coil at a continuous 12 V. When it commands CLOSE, the coil is held in the opposite polarity. A latching valve only needs a short pulse. Continuous drive overheats the coil and draws continuous GSE current through D36 (F9).
- **Fix:** Add HF-pass (series coupling) capacitors between U9 and the U3 inputs, so each GSE edge produces a pulse that decays through the node's pull-down.
  - **Open path first:** a series C between U9.3 and `Net-(U3-INA)`.
  - **Close path too:** the same treatment on U9.6 → `Net-(U3-INB)`. Otherwise the close side is still held at DC.
- **Sizing:** The pulse ends when the input decays below the MIC4424 V<sub>IH</sub> (2.4 V), so t<sub>pulse</sub> ≈ RC · ln(3.3 / 2.4) ≈ 0.32 · RC. R is whatever the node sees to ground:
  - As built in GSE mode, the dominant load is R10/R11 (1k) into the unpowered, clamped U15. A 50 ms pulse would need about 150 µF, which is impractical.
  - Design this together with S13. With R10/R11 raised to 10k, R ≈ 9.1k and C ≈ 17 µF for 50 ms. With the flight side isolated (an I<sub>off</sub> buffer), R ≈ 100k (R41/R42) and C ≈ 1.6 µF.
  - Set the actual pulse width from the latching valve datasheet (Q3), with margin at cold and at the lowest `+12V`.
- **Other checks:**
  - The falling edge drives the input to about −3.3 V. Check the MIC4424 negative input rating, or add a small Schottky clamp to GND on each input.
  - Enabling U9 (GSE power-up with OPEN already high) creates an edge, and therefore a pulse. Confirm that is acceptable.
  - If the pulse width must be tight across temperature and capacitor tolerance, use a one-shot (74LVC1G123-class) instead of the RC.
  - Flight drive through R10/R11 stays DC-coupled. The coupling capacitor sits only in U9's output path.

---

## Significant

### S1 — CDH IGNITER pin is the actual gated igniter drive — `OPEN`
- `/IGNITER_PULSE_GATED` drives J5.1 (igniter board) and J3.22 through only R29 (100 Ω). A short in the CDH harness loads the real igniter drive, and the 74HC08 output would exceed its 25 mA rating. This contradicts the block diagram note ("not the same net").
- J3.22–24 also have no ESD diodes, unlike every other J3 signal.
- **Fix (agreed):** Put a single-gate 3.3 V buffer (74LVC1G125/1G17 class) between `IGNITER_PULSE_GATED` and R29, with its ESD diode on the J3 side. Add ESD protection on J3.22–24.

### S4 — DPT harness pin order mismatch — `OPEN`
- PIB J9/J10 (PicoBlade 53398-0671): 1 3V3, 2 GND, 3 SCK, 4 DOUT, 5 DIN, 6 MCLK.
- DPT board J1 (Pico-Lock 503480-0640): 1 SCLK, 2 DOUT, 3 DIN, 4 MCLK, 5 VDD, 6 GND.
- A 1:1 harness would put power on SCLK. **Fix:** Align the pin orders or document the cross-wired harness.

### S5 — LT3065 PWRGD cleanup — `OPTIONAL`
- PWRGD (U1.4) is open-drain, pulled up to `BAT_HOT` by R1, and goes nowhere. Either route it to a GPIO with a 3.3 V pull-up, or remove R1 and leave it NC.
- The symbol types the pin as "power input", which causes an ERC error (P4). Change it to open collector.

### S6 — ADM1270 UVLO at 12 V is the minimum battery voltage — `OPEN`
- The R50/R49/R48 and R59/R60/R61 dividers give a 12 V UV and 18 V OV threshold. With threshold tolerance and drops along the path, the eFuses will cut out near end of discharge.
- **Fix:** Lower UV to about 10.5–11 V, or confirm the EPS low-voltage cutoff.

### S7 — ADM1270 FAULT tied to ENABLE — `OPEN`
- R51 and R64 (0 Ω) connect FAULT to ENABLE, so a fault pulls ENABLE low. The result is effectively auto-retry rather than latch-off, and the MCU can't observe FAULT.
- **Confirm this is intended.** Otherwise route FAULT to a GPIO.

### S8 — SWD header has no target voltage reference — `OPEN`
- J8 has no 3.3 V pin, so the ST-Link can't sense the target voltage. Add VTref, or move to a Tag-Connect TC2030 footprint.

### S10 — Slow/unbuffered logic in the GSE path — `OPEN`
- U8 (74LVC1GU04) is an unbuffered inverter fed by an RC input (R43/R44).
- U9's OE sits on a ~2.26 V divider (R46/R45) with no hysteresis.
- **Fix:** Use Schmitt-input parts (e.g. 74LVC1G14, 74LVC2G17-style) on these slow nodes.

### S11 — PIB thermistors missing — `OPEN`
- The ICD lists 2 PIB thermistors. `/ADC_NTC_PIB` (U4.25, PB1) connects to nothing, and no thermistor footprints are present.

### S13 — GSE mode back-powers the unpowered flight 3.3 V rail — `OPEN`
- **Where:** Valve Driver sheet.
  - U9 drives U3 INA/INB directly. Those nodes are also driven through R10/R11 (1k) from U15.
  - GSE OPEN drives the Q10 gate through R47 (100 Ω), and the flight side drives it through R5 (1k) from U14.
- **Problem:** In GSE-only mode, +3.3V is unpowered. Current flows from the GSE-side drivers through R10/R11 (≈ 2.7 mA) and R47→R5 (≈ 2.5 mA) into the 74HC08 output clamp diodes, then into the flight 3.3 V rail. That's about 5 mA in total.
  - It can lift the rail to a partial voltage and half-power the STM32, MAX823 and DPT parts.
  - R88 holds ARM low, so the gated outputs stay low unless the MCU starts and actively drives ARM.
- **Fix options:**
  - Raise R10/R11/R12/R38 to about 10k. The MIC4424 input current is negligible.
  - Scale the Q10 pair up together (e.g. R5 = 10k, R47 = 1k, R6 → 1M). That keeps 10:1 GSE dominance with the flight gate at about 3.2 V, and cuts back-power to about 0.25 mA.
  - Or buffer the flight outputs with an I<sub>off</sub> part (74LVC/AUP) that goes high-Z when unpowered.
- The choice sets the RC in C11, so design the two together. Measure the flight 3.3 V rail in GSE-only mode on the bench.

### S14 — Non-ARM 74HC08 inputs float at reset — `OPTIONAL`
- R88 holds `/ARM/~{SAFE}` low, so all eight gated outputs are low during reset. The other inputs (`IGNITER_EN`, `DISCHARGE_EN`, `IGNITER_PULSE`, `VALVE1_L/H`, `VALVE2_EN`, `VALVE3_L/H`) float while the STM32 pins are high-Z.
- That's safe, but floating HC inputs can draw extra supply current or oscillate. Add 100k pull-downs if layout space allows.

### S15 — `+12V` sits about 0.4–0.5 V below the regulator setpoint — `VERIFY`
- The LT3065 divider (R18/R17) senses at the regulator output, ahead of the OR diode D11. In flight, `+12V` ≈ 12.02 V − V<sub>F</sub> ≈ **11.5–11.6 V**, and it drops further near LT3065 dropout (V<sub>BAT</sub> < ~12.5 V). In GSE mode, `+12V` = `12V_GSE` − V<sub>F</sub>(D36).
- **Check:** valve pull-in and latching operation at about 11 V and at cold. If margin is short, move R18 to the `+12V` side (Kelvin sense, keeping C6 at the OUT pins) or use an ideal-diode OR.

### S16 — DPT MCLK timer not configured in firmware — `OPEN` (firmware)
- `/DPT_MCLK` is driven from PC7, and `firmware.ioc` assigns PC7 to `TIM3_CH2` with TIM3 clocked at 64 MHz (HSE 8 MHz via PLL). The prescaler, period and OC mode are at CubeMX defaults: OC "Frozen" doesn't toggle the pin, and the default period gives about 488 Hz.
- **Fix:** Either set OC mode to Toggle with ARR = 976 (32.75 kHz), or use PWM mode 1 with ARR = 1952 and CCR = 976 (32.77 kHz). Both are within the MS5540C's 30–35 kHz range.

---

## Footprints & Part Numbers

Pin-to-pad check: every symbol pin maps to a footprint pad (custom DFN-8/10_DD, SOT95P280X145-5N, HARWIN M80, ABM3B, PowerPAK SO-8, SOD-323). MIC4424 pads 1/8 are NC, which is correct. SP0502BAJT on SC-70 is correct.

### F1 — No MPNs or capacitor ratings — `OPEN`
- Only U1 carries an MPN field, and only some resistors have a tolerance. No capacitor has a voltage or dielectric rating. **Fix:** Add `MPN` and `Manufacturer` fields project-wide.

### F2 — BAT-rail capacitor voltage rating — `OPEN`
- C1, C28, C33, C62, C66, C67 see up to the SMF18A clamp (~29 V), so they need 50 V parts.
- The 10 µF 0805 capacitors, especially C4 (the coupling capacitor in the −5 V converter, ~10 V across it), lose most of their capacitance under DC bias. Use 25 V+ X7R, and 1206 where needed.

### F3 — R35 sense resistor power/footprint — `OPEN`
- 15 mΩ at 3.33 A ≈ 0.17 W in 0805. Use a current-sense part rated ≥ 0.5 W with a Kelvin footprint.

### F4 — FB1 footprint/value — `OPEN`
- FB1 uses `R_0603` and its value is "120". Specify a ferrite part number (impedance at 100 MHz, DCR, current) and use an inductor/ferrite footprint.

### F5 — Symbol/value mismatches — `OPEN`
- Q1/Q2: symbol `Si7141DP` (20 V part), value Si7489DP (100 V).
- Q9/Q10/Q11: symbol `IRF7403`, value IRF7832.
- U14/U15: symbol `74LS08`, value 74HC08PW.
- The pinouts match, but the description and datasheet fields will mislead BOM and assembly.

### F7 — Flight connector retention — `OPEN`
- PicoBlade (J2, J4, J5, J9, J10, J11, J13, J15) is friction-lock only, which is a vibration risk. The DPT board already uses latching Pico-Lock. Consider Pico-Lock, CLIK-Mate, or Gecko.

### F8 — Flight population — `OPEN`
- SW1 (B3F-1000 through-hole tactile switch) and J8 (debug header): mark as DNP for flight units.
- **JP1 (`WD_ENABLE`) must be bridged for flight.** It is a normally-open solder jumper between the MAX823 ~RESET output and NRST. While open, the MAX823 watchdog, power-on/brown-out supervision and the SW1 manual reset never reach the MCU. During development, reset through J8.5 or the STM32's internal POR. Keep R77 (the NRST pull-up while JP1 is open). Add JP1 to the assembly notes and the flight build checklist.

### F9 — OR-ing / flyback Schottky ratings (NSVR1020MW2T1G) — `VERIFY`
- **Parts:** D11, D36, D37 are onsemi NSVR1020MW2T1G: 20 V V<sub>R</sub>, 1 A, V<sub>F</sub> 0.54 V max at 1 A, **200 mW P<sub>D</sub>**, SOD-323, AEC-Q101. The symbol `Datasheet` field is empty.
- **D11 (LT3065 → `+12V`):** I<sub>MAX</sub> caps the current at 300 mA, giving ≈ 0.12 W. In GSE-only mode its V<sub>R</sub> equals `12V_GSE`. D10 (SMF14A) clamps GSE transients at roughly 23 V, which **exceeds the 20 V V<sub>R</sub>**.
- **D36 (`12V_GSE` → `+12V`):** Carries the NC valve's holding current continuously during a fill, plus the latching-valve current (continuous until C11 is fixed). At about 0.4 A, V<sub>F</sub> × I ≈ 0.18 W, which is at the 200 mW limit.
- **D37 (NC valve flyback):** Conducts the coil current briefly at turn-off. It's OK if the coil current is ≤ 1 A. Valve closing is slow with a plain freewheel diode; add a series zener if closing time matters.
- **Recommendation:** Once the coil currents are known (Q3), use a 2–3 A, 30–40 V Schottky in SOD-123FL or SMA for D11/D36. Give them copper for heat in vacuum.

### F10 — Crystal load capacitance — `VERIFY`
- C26/C27 = 12 pF assumes `ABM3B-8.000MHZ-10-D1G-T` has C<sub>L</sub> = 10 pF, with about 4 pF stray. Confirm the "-10-" code in Abracon's part-numbering guide, and trim after measuring frequency on the first board.

---

## Project Setup

### P1 — `Outputs/` contains another project's gerbers — `OPEN`
- `PIB-job.gbrjob` reports the "lm5022-flyback" project (KiCad 9.0.6, 2-layer, 82.05 × 44.85 mm). The PIB is a 4-layer board. `lm5022-flyback.zip` is also present. Delete and regenerate before any fab order. Check `bom/ibom.html` too.

### P2 — Broken symbol library links — `OPEN`
- U1 uses `LT3065EDDPBF:LT3065EDDPBF`, and U2/U3 use `MIC4424YM:MIC4424YM`. Those nicknames aren't in the project's `sym-lib-table`. The symbols exist in `0_PAY_Symbol_Library`; remap them there.

### P3 — Empty title blocks — `OPEN`
- None of the 4 sheets has a title, revision, date or company.

### P4 — ERC cleanup — `OPEN`
- With stock libraries loaded: 17 messages (16 errors, 1 warning):
  - 11× `power_pin_not_driven`: needs PWR_FLAGs; also LT3065 PWRGD is typed as a power input (S5).
  - 5× `pin_not_connected`: U4 PA2, PA10, PA11, PC15, PD2. Add no-connect flags or use them.
  - 1× isolated label `ADC_NTC_PIB` (S11).

### P5 — KiCad version — `OPEN`
- The files are KiCad 10 format (`version 20260306`), so the whole team needs KiCad 10.

### P6 — PCB out of sync with schematic — `OPEN`
- DRC with schematic parity reports **29 issues**:
  - 11 missing footprints: D11, D36, D37, R88–R94, JP1.
  - Value mismatches: C26/C27 (PCB 27 pF, schematic 12 pF), R47 (PCB 1k, schematic 100 Ω).
  - Net conflicts: U1 OUT / C6 / C22 / R18 on `Net-(D11-A)`, Q10 drain / J4.6 on `Net-(D37-A)`, and PA2 unconnected.
- **Fix:** Run Update PCB from Schematic (F8), place the new parts and re-route. D11/D36 need copper for heat (F9).

---

## ICD / Block Diagram Discrepancies

| ID | Item | ICD / Block Diagram | Schematic | Status |
|---|---|---|---|---|
| D1 | CDH connector PN | G125-MH12605M4P | G125-MS12605M2P (vertical, correct) | `ICD` — update to the MS PN; also confirm the "M2P" vs "M4P" suffix against the CDH harness and mating part |
| D2 | EGSE pinout | 12V_1, 12V_2 on pins 1/2, GND on 14/15; "both pairs of 12V" to open | J1: 1 = 12V_GSE, 2 = OPEN/~CLOSE (3.3 V logic), 3/4 = GND (4-pin part) | `ICD` |
| D3 | 5 V rail | "TBD" | Required: −5 V supply, LG16, HV boards, integrator | `ICD` |
| D4 | P/T sensors | 3 (frit, tank refrigerant, tank propellant) | 2 DPT channels (tank, frit) | `ICD` — list 2, noting a possible future third |
| D5 | Igniter channels | Integral V, integral I, total pulse energy | J5: peak V, peak I, supply V | `ICD` — trim to as-built channels |
| D6 | GSE architecture | Separate H-bridge + 12 V NMOS, diode-OR'd, "completely separated" | Shares flight drivers. `+12V` diode-OR'd from LT3065 and `12V_GSE` (D11/D36). The Q10 gate uses a 10:1 resistor ratio (R47 100 Ω vs R5 1k), so the GSE line overrides the flight command whenever it is driven; J1.2 sits at ≈ 3.2 V when flight opens valve 2 with GSE unmated | `ICD` |
| D7 | Rogowski reset | Block diagram shows reset input | None (C5) | `OPEN` |
| D8 | IGNITER vs Igniter_Pulse | Separate nets | Same net (S1) | `OPEN` — buffer to be added (S1) |
| D9 | BAT OCP / MCU-healthy gating | Claimed | Not implemented (C9) | `OPEN` |

### ICD Update Actions

Checklist of ICD edits the review has generated. Tick these off as the ICD is revised.

- [ ] **GSE section and Table 3 (D2, D6):** OPEN/~CLOSE is a 3.3 V digital logic line on J1.2; J1.1 = 12V_GSE; J1.3/4 = GND. Remove the "both pairs of 12V" wording and the 14/15 pin numbers. Describe the as-built scheme: shared valve drivers, `+12V` diode-OR'd from LT3065 and `12V_GSE`, the GSE line overriding the flight NC-valve command while driven, and the latching valve pulsed from GSE edges (C11). Replace "diode OR'd … completely separated".
- [ ] **Electrical connectors (D1):** G125-MS12605M2P (vertical), not MH12605M4P.
- [ ] **Sensor channels (D4, D5):** 2 P/T sensors (tank, frit; third optional/future). Igniter channels trimmed to peak V, peak I, supply V (plus whatever is derived in firmware).
- [ ] **Power interfaces (D3):** 5 V is required, not TBD (−5 V converter, LG16 flow sensor, HV board op amps, integrator).
- [ ] **BAT protection paragraph (C9, D9):** match the as-built protection (per-HV-branch ADM1270 eFuses; heater and 12 V branch protection once decided). State that launch inhibits are provided by EPS under UNP NS requirements.
- [ ] **Data interfaces (S1, D8):** IGNITER is a buffered copy of the gated igniter pulse.

---

## Open Questions

| ID | Question | Known so far |
|---|---|---|
| Q3 | Valve part numbers and coil data:<br>• Is the NC valve rated for continuous 12 V, or is it spike-and-hold (C10)?<br>• What is each valve's coil current?<br>• What pulse width does the latching valve need?<br>These set the C11 pulse RC, the F9 diode sizing, D37's rating, the continuous LT3065 load (300 mA limit) and J4 pin current. | Library has LHLA1252x11H and LHDB0442245D. The NC valve is non-latching and ran on plain 12 V DC on the bench. |
| Q8 | Rogowski: peak current and pulse width? | Not characterized yet; readout is bidirectional (C5). |

---

## Verified OK (for reference)

- **MCU pin assignments:**
  - SPI1 slave PA4–7, I2C2 PA8/PA9, I2C3 PC8/PC9, SPI3 PC10–12 (DPT mux), LPUART1 PC0/PC1, SWD PA13/PA14.
  - DPT MCLK on PC7 (TIM3_CH2, from HSE via PLL).
  - Discharge V/I on PA0/PA1 (ADC12, simultaneous dual-mode capable).
- **ARM/SAFE:** R88 pull-down holds all eight 74HC08 gated outputs low during reset and watchdog reset.
- **GSE power path:**
  - D11/D36 diode-OR into `+12V`. The LT3065 loop (C6, C22, R18) stays on the regulator side.
  - `12V_GSE` (D10, R46/R45, U6) is on the source side of D36, so flight power can't back-feed the GSE UVLO or EGSE pin.
- **Q10 gate:** In every valid flight/GSE state the gate is ≥ 3.0 V (on) or ≤ 0.3 V (off), never in the 1.35–2.35 V threshold band. IRF7832 Fig. 3 shows tens of A typical at about 3 V, so 3.3 V-class drive is adequate for the heater and valve FETs.
- **D37:** NC-valve flyback diode, oriented correctly.
- **DPT mux:** R89–R94 pull-downs keep the deselected sensor's SCK/DIN and the unselected DOUT input defined.
- **J3:** pinout matches ICD Table 2. GSE OPEN/~CLOSE input protection (D5 PESD3V3U1UL, R43/R44, LVC logic with I<sub>off</sub>) suits 3.3 V logic.
- **LG16 flow sensor J15:** pinout (1 SDA, 2 SCL, 3 VDD, 4 GND, 5 NC), 5 V supply and 3.3 V I²C all match the datasheet. Sensor operating range is +10…+50 °C.
- **LT8580 −5 V:** R75 60.4k → −5.03 V. D1 orientation is correct.
- **LT3065 (U1):**
  - DFN-10 pinout matches the Rev. F datasheet.
  - Output 0.6 × (1 + 21.5k/1.13k) = 12.02 V. I<sub>MAX</sub> = 300 mA (R2 = 1k), with C10 22 nF on IMAX, C11 10 nF REF/BYP, C6 10 µF and C22 10 nF feedforward. Foldback is not engaged.
  - Thermal: about 1.44 W at 300 mA and 4.8 V headroom, roughly a 50 °C rise. That needs the board ≲ 70 °C.
- **MAX823:** RESET source current is 800 µA max, so the STM32 and ST-Link can overdrive it. SOT-23-5 pin mapping is correct.
- **ADM1270:** current limits 50 mV / 15 mΩ = 3.33 A and 50 mV / 100 mΩ = 0.5 A. PG threshold ≈ 11 V.
- **MIC4424:** SOIC-8 mapping is correct.
