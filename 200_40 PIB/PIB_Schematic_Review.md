# 200_40 PIB — Schematic & Project Review (Living Document)

| | |
|---|---|
| **Board** | 200_40 Payload Interface Board (APEx PMPD thruster, 6U CubeSat) |
| **Reviewed files** | `PIB.kicad_sch`, `Rogowski.kicad_sch`, `Valve Driver.kicad_sch`, `DPT Mux.kicad_sch`, `PIB.kicad_pcb` (parity only), `firmware/firmware.ioc` (pin/clock config) |
| **Reference docs** | `Payload ICD.pdf` (Rev 2.0, CDR), `PIB Block Diagram.drawio.png`, `200_60 DPT Sensor Board` |
| **Design state** | branch `pib-split`, `153cef1` + uncommitted main-sheet, Rogowski and Valve Driver changes (2026-10-19) |
| **Tools** | KiCad 10.0.4 (`kicad-cli` ERC, netlist, DRC w/ schematic parity) |
| **Last updated** | 2026-10-19 |

**Status key:** `OPEN` (needs a design change) · `VERIFY` (needs a datasheet, bench or part check) · `OPTIONAL` (recommended hardening, not required) · `ICD` (schematic is correct; the ICD must change)

**How to use:** This document lists only issues present in the current KiCad design. When an issue is resolved, delete it. Item IDs are stable, so gaps in the numbering are expected. Don't renumber.

---

## Summary

The pin-to-pad check is clean for every footprint. The main risks are:
- **The PIB J5 ↔ igniter board J3 pinouts are incompatible.** With the mirrored mapping that lines up the signals, the ±5 V rails cross, and the PIB's VBAT_IGN pins land on unconnected igniter pins (C13).
- The Rogowski reset shares PA12 with the igniter peak-detector reset, at opposite polarity, so the two can never both be armed (C12).
- The GSE latching-valve pulse needs bench checks: polarity, and pulse width against the valve spec (C11).
- It's unconfirmed whether the NC valve (valve 2) is rated for steady 12 V. The Q10 drive holds full 12 V on it continuously, which is wrong for a spike-and-hold coil (C10).
- The Rogowski front end still needs ESD on J7 and its TBD values sized once the coil is characterized (C5).
- The heaters have no on-orbit hardware cutoff (C9).
- The SOD-323 OR-ing Schottkys (20 V, 1 A, 200 mW) are marginal for the GSE path (F9).
- The PCB is well out of sync with the schematic (142 parity issues, P6).

The BOM is not production-ready (no MPNs or capacitor ratings), and `Outputs/` holds gerbers from a different project. Several ICD items need to be brought in line with the schematic (see [ICD Update Actions](#icd-update-actions)).

**Design context:**
- UNP NS requirements apply. Launch inhibits live in EPS, so the PIB and CDH are fully unpowered until after deployment.
- HV boards (igniter 1 kV, discharge 400 V) scale all signals to 0 to <3.3 V with op amps on ±5 V.
- GSE OPEN/~CLOSE (J1.2) is a 3.3 V digital logic line.
- The NC valve (valve 2) is a non-latching normally-closed solenoid. It has been bench-run on plain 12 V DC and is held open continuously during burns.
- Each heater is well under 10 W (TBD).
- The CDH connector is the vertical (MS) part.
- Only 2 P/T sensors (tank, frit) are fitted; a third may be added later.
- Op amps with heavy-ion DSEE test heritage (OP27/OP37 family) are preferred over RRIO parts that lack it. Clamp the outputs to the ADC range instead.
- Discharge current is a damped ring: 16.26 kA peak, about 24 kHz, about 65 µs long, bidirectional.

| Category | Items |
|---|---|
| Critical | 7 (1 optional, 2 verify) |
| Significant | 8 |
| Footprint / Part | 9 |
| Project setup | 5 |
| ICD / Block diagram | 9 |
| Open questions | 2 |

---

## Critical (safety or function)

### C5 — Rogowski integrator: shared reset, J7 ESD, value sizing — `OPEN`
- **Where:** Rogowski sheet (working copy, uncommitted).
  - The signal chain: J7.1 (MMCX) → R95 (TBD, termination) → C69 (1 µF) → R79 (TBD) → U11.2 (OP27), with C45 (TBD) ∥ R98 (TBD, leak) ∥ Q3/Q4 (BSS138 reset switch) from U11.2 to U11.6.
  - U11.3 (+) sits at V<sub>REF</sub> = VDDA/2 (R96/R97 10k/10k, C70 1 µF). Supplies are ±5 V.
  - Reset: `ROGOWSKI_RST` (main-sheet net `/PD_RST`, PA12, shared with J5.2; see C12) → R100 → Q5 (BSS138, R101 gate pull-down). Q5's drain, pulled up to +5 V by R99, drives the Q3/Q4 gates. The integrator is held in reset whenever the GPIO is low or high-Z.
  - Output: U11.6 → R102 (100 Ω) → clamp D40 (BAT54J, to +3.3V) / D39 (BAT54J, to GND) → R104 (1k) → PA1 with C71 (470 pF). This is matched to the PA0 network (C6).
- **Problems:**
  1. **The reset shares PA12 with the igniter peak-detector reset, at opposite polarity** (C12).
  2. **No ESD protection on J7.1.**
  3. **R79, C45, R98 and R95 are TBD.** They're waiting on M (Q8).
  4. *Minor:* The reset net is now active-low (GPIO high = integrate), but it's still named `ROGOWSKI_RST`. The `I_ROGOWSKI` sheet pin and hierarchical label are typed as input; make them output.
- **Fix:**
  - **Reset:** Give it its own GPIO (C12) and rename it `~{ROGOWSKI_RST}`.
  - **Firmware sequencing:** Hold reset during power-up (C69 charges with τ ≈ (R79 + R95)·C69). Release it shortly before the shot, then sample a baseline after release. That removes switch leakage × R98, op-amp offset and charge injection (≈ C<sub>gd</sub>·ΔV<sub>g</sub>/C45, a few mV).
  - **Gain headroom:** size the gain so the expected peak stays within about 0.15–3.15 V, so the D39/D40 clamp only acts on faults.
  - **J7:** Add ESD/TVS protection at J7.1, ahead of C69. Rate it for the peak coil voltage M × ~2.45 kA/µs.
- **Value sizing (once M is known):**
  - **Gain:** V<sub>out,pk</sub> = M · I<sub>pk</sub> / (R79 · C45). For 16.26 kA → 1.5 V, R79 · C45 = M · 16 260 / 1.5.
  - **Droop:** R98 · C45 and R79 · C69 each ≈ 10 ms. Total droop ≈ T/τ<sub>AC</sub> + T/τ<sub>leak</sub> ≈ 1.3% at T ≈ 65 µs.
    - Keep R98 ≤ ~100k (OP27 bias current × R98 = a few mV), giving C45 ≥ ~60 nF in C0G.
    - With a pre-shot baseline, R98 can go to about 1–2 MΩ and C45 to about 5–10 nF.
    - Firmware droop correction (I ∝ y + (1/τ)∫y dt) would allow shorter time constants.
  - **C69 (1 µF):** X7R is acceptable. Its bias-driven piezo error is about 10⁻⁴–10⁻³ of the coil signal, and its tolerance only shifts droop by about ±0.1%. Specify 1206 or larger, 25–50 V, AEC-Q200, flexible termination. R79 ≈ 10k gives 10 ms; avoid tantalum.
  - **R95:**
    - Start at 50 Ω and keep R95/(2πL<sub>coil</sub>) ≥ 240 kHz (L<sub>coil</sub> ≤ ~33 µH at 50 Ω).
    - Tune on the bench for no ringing at the coil's self-resonance (≈ ½·√(L/C<sub>coil</sub>)). Keep R79 ≥ 10× R95.
    - **Size and rating:** set by V<sub>pk</sub> = M × ~2.45 kA/µs, both for working voltage (0603 ≈ 50–75 V, 0805 ≈ 150 V, 1206 ≈ 200 V typical) and for single-pulse energy (≈ V<sub>pk</sub>²/R × 16 µs, about 0.2 mJ at M = 10 nH into 50 Ω). Average power is negligible.
      - **0603 is fine if M ≲ 20 nH** (V<sub>pk</sub> ≲ 50 V); use an anti-surge type (ERJ-P03 / CRCW-HP class) and check its pulse-load curve.
      - If layout happens before M is known, use 0805: you can't upsize from an 0603 footprint later.
      - R79 also sees about the full coil voltage, so the same voltage limits apply to it.
  - **ADC rate and LT8580 ripple:** The planned rate is 2–4 MSPS (80–160 samples per period). The −5 V LT8580 switches at 846 kHz, which is below Nyquist at both rates, so it appears as an in-band tone that a digital low-pass removes.
    - Its harmonics alias to 78 kHz (7th) at 2 or 3 MSPS, and to 230 kHz at worst (5th) at 4 MSPS. Prefer 4 MSPS.
    - The LT8580 free-runs (SYNC = GND), so its tolerance and drift move these aliases. If the bench PA1 noise floor shows the ripple matters, drive SYNC from an HSE-derived MCU timer, e.g. f<sub>sw</sub> = fs × 2/9 ≈ 889 kHz at 4 MSPS (check the SYNC range).
    - Expected ripple at the output is sub-LSB (≈ 0.3 mV, via the OP27's V− PSRR). Use 1 nF rather than 470 pF in the PA0/PA1 RC if more rejection is needed: −14.6 dB vs −8.6 dB at 846 kHz.
  - **Bandwidth:** The measured current is a damped ring at about 24 kHz (41 µs period, ~65 µs long). The OP27's 8 MHz GBW gives about 330× loop gain at 24 kHz (≈ 0.3% error), and its 2.8 V/µs slew is well above the ~0.25 V/µs needed.

### C6 — Igniter-board ADC inputs are unclamped — `OPTIONAL`
- **Where:** `ADC_V_IGN_PK`, `ADC_I_IGN_PK`, `ADC_V_IGN_SUPPLY`, `ADC_NTC_IGN` (J5): each has only a PESD3V3U1UL, with no series R.
- **Risk:** Nominal signals are 0 to <3.3 V. However, the igniter-board op amps run on ±5 V, so a fault, overrange, power-sequencing event or HV arc can push an output to a rail, and the ESD diodes are not continuous clamps.
- **Hardening:** Use the same scheme as PA0/PA1: series R ≈ 1k plus a BAT54 pair to +3.3V/GND at each input. These are slow channels (peak-detect, NTC, supply), so the delay doesn't matter.

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
- **Where:** Valve Driver sheet. Q10 (IRF7832) is a single low-side switch on J4.6, with the coil to `+12V` (J4.5) and D37 freewheeling across it. Gate drive is `VALVE2_EN_GATED` diode-OR'd with GSE `OPEN/~CLOSE` through D50 (BAT54C), with R6 as the pull-down. The valve is non-latching and normally closed.
- **Why it matters:** While open, Q10 holds the full `+12V` (about 11.5 V, S15) across the coil, for the whole burn or GSE fill. That's only correct if the coil is rated for continuous 12 V.
  - Some solenoid valves are **spike-and-hold**: 12 V gives a short pull-in spike, and then the voltage drops to a lower holding level.
  - On a spike-and-hold coil, continuous 12 V overheats the coil. In vacuum that heat can only leave by conduction. It also draws several times the needed holding current from the LT3065 (300 mA limit), D11/D36 (F9) and J4.
  - The bench run on plain 12 V proves the valve actuates, but it doesn't show the coil survives 12 V for a full burn duration.
- **Action:** From the valve datasheet, confirm the continuous-duty voltage rating, coil resistance and steady-state current, then record them in Q3. If it turns out to be spike-and-hold, choose one of these:
  - PWM Q10 after pull-in. `VALVE2_EN` (PB5) would need a timer channel. D37 already freewheels the coil during the PWM off-time.
  - Add a series hold resistor bypassed during pull-in.
  - Use a dedicated spike-and-hold driver.
  - Whichever you pick, the GSE path through D50 is DC-only and needs the same treatment.

### C11 — GSE latching-valve pulse circuit: pulse width and polarity checks — `VERIFY`
- **Where:** Valve Driver sheet (working copy).
  - **Input:** J1.2 `/OPEN/~{CLOSE}` (D5 ESD) → R46 (1k) → R45 (100k to GND) → U18 gate 2 (74LVC2G14) → ~OPEN. Gate 1 re-inverts that to a buffered OPEN.
  - **OPEN edge:** OPEN → C73 (1 µF) → RC node (R47 100k, D41 clamp) → U9 2A (74LVC2G17) → D47 (BAT54C) → U3 **INB**, shared with flight `VALVE3_H`.
  - **CLOSE edge:** ~OPEN → C74 (1 µF) → RC node (R107 100k, D42 clamp) → U9 1A → D48 → U3 **INA** (`VALVE3_L`).
- **Checks:**
  1. **Polarity:** confirm on the bench that INB (`VALVE3_H`) drives the valve open, for both GSE and flight.
  2. **Pulse width:** t ≈ τ·ln(3.3/V<sub>T−</sub>), with τ = 100 ms and V<sub>T−</sub> the 74LVC2G17's negative-going threshold (datasheet). Check this against the latching valve's required pulse width (Q3), with X7R tolerance. BAT54J leakage at the RC nodes shortens the pulse when hot, which is negligible at room-temperature GSE use.
  3. **No UVLO gating:** at GSE power-up, U18's ~OPEN output rises with `3.3V_EGSE` and couples through C74, so expect a CLOSE pulse at reduced `+12V`. GSE procedure: send an explicit CLOSE once powered, and space commands ≥ ~2× the pulse width. Overlapping pulses brake the coil, which is safe.
  4. **BAT54C symbol:** confirm pins 1/2 are the anodes and pin 3 the common cathode (the symbol's pins are unnamed).

### C12 — Rogowski reset and igniter peak-detector reset share PA12 at opposite polarity — `OPEN`
- **Where:** Main-sheet net `/PD_RST` (U4.46 PA12) goes to J5.2 and to R100 (Rogowski sheet).
  - On the igniter board, J3.12 `IGNITER_RST` drives the gates of the BSS123 Q2/Q4, which short the peak-detect caps C34/C35. That reset is **active-high**.
  - The Rogowski reset (Q5 → Q3/Q4) is **active-low**: GPIO low or high-Z holds the integrator in reset.
- **Problem:**
  - GPIO high: the integrator runs, but the igniter peak detectors are held discharged.
  - GPIO low: the peak detectors are armed, but the integrator is held in reset.
  - No state arms both, so the igniter peaks and the discharge current can't be captured in the same shot. The two resets also can't be timed independently.
- **Fix:** Move the Rogowski reset to its own GPIO: PA2, PA10 or PA11 are free; avoid PC15.
  - Keep a pull-down on `/PD_RST` (e.g. 100k at J5.2). Today the only DC path to GND is through R100/R101, so after the split the igniter BSS123 gates would float while the MCU is in reset.

### C13 — PIB J5 and igniter board J3 pinouts are incompatible — `OPEN`
- **Where:** PIB J5 (53398-1371) ↔ igniter board J3 (53261-1371, `200_80 Igniter Supply Board`).
- **Problem:** A 1:1 mapping lines up nothing. The mirrored mapping (PIB pin n ↔ igniter pin 14 − n) lines up the signals (PULSE, RST, NTC, I_PK, V_PK, V_SUPPLY, GNDs), but then:

  | PIB J5 | PIB net | Igniter J3 | Igniter net |
  |---|---|---|---|
  | 7 | **+5V** | 7 | **−5V** |
  | 8 | **−5V** | 6 | **+5V** |
  | 12, 13 | VBAT_IGN | 2, 1 | unconnected |

  - **The ±5 V rails cross**, shorting the PIB +5 V into the igniter −5 V and vice versa. That's damaging on first power-up.
  - The PIB's ADM1270-protected VBAT_IGN branch feeds nothing. The igniter flyback (LT8301, VIN/EN on 5V) runs from J3.6 instead, which would load the PIB +5 V rail.
- **Harness:** Off-the-shelf PicoBlade double-ended assemblies are **mirrored** (pin n ↔ N + 1 − n, per the supplier drawing; confirm for the exact part bought). So the crossing above is what a standard cable produces.
- **Fix:**
  - **Swap PIB J5.7 and J5.8** to J5.7 = −5V and J5.8 = +5V. That matches igniter J3.7 (−5V) and J3.6 (+5V) through a mirrored cable.
  - Resolve VBAT_IGN. Either route it to igniter J3.1/J3.2 and run the flyback from it (the ADM1270 eFuse branch, as the PIB intends), or drop the PIB VBAT_IGN branch and check the PIB +5 V budget for the igniter flyback load.

---

## Significant

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

### S14 — Non-ARM 74HC08 inputs float at reset — `OPTIONAL`
- R88 holds `/ARM/~{SAFE}` low, so all eight gated outputs are low during reset. The other inputs (`IGNITER_EN`, `DISCHARGE_EN`, `IGNITER_PULSE`, `VALVE1_L/H`, `VALVE2_EN`, `VALVE3_L/H`) float while the STM32 pins are high-Z.
- That's safe, but floating HC inputs can draw extra supply current or oscillate. Add 100k pull-downs if layout space allows.

### S15 — `+12V` sits about 0.4–0.5 V below the regulator setpoint — `VERIFY`
- The LT3065 divider (R18/R17) senses at the regulator output, ahead of the OR diode D11. In flight, `+12V` ≈ 12.02 V − V<sub>F</sub> ≈ **11.5–11.6 V**, and it drops further near LT3065 dropout (V<sub>BAT</sub> < ~12.5 V). In GSE mode, `+12V` = `12V_GSE` − V<sub>F</sub>(D36).
- **Check:** valve pull-in and latching operation at about 11 V and at cold. If margin is short, move R18 to the `+12V` side (Kelvin sense, keeping C6 at the OUT pins) or use an ideal-diode OR.

### S16 — DPT MCLK timer not configured in firmware — `OPEN` (firmware)
- `/DPT_MCLK` is driven from PC7, and `firmware.ioc` assigns PC7 to `TIM3_CH2` with TIM3 clocked at 64 MHz (HSE 8 MHz via PLL). The prescaler, period and OC mode are at CubeMX defaults: OC "Frozen" doesn't toggle the pin, and the default period gives about 488 Hz.
- **Fix:** Either set OC mode to Toggle with ARR = 976 (32.75 kHz), or use PWM mode 1 with ARR = 1952 and CCR = 976 (32.77 kHz). Both are within the MS5540C's 30–35 kHz range.

### S17 — J5.2 lost its ESD diode — `OPEN`
- D27 (PESD3V3U1UL) is still on the old `IGNITER_RST` label, which now connects to nothing (ERC isolated label). The live J5.2 net `/PD_RST` has no ESD protection, unlike the other J5 signals.
- **Fix:** Move D27 to `/PD_RST` and delete the stale label.

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
- SW1 (SKRKAHE020 tactile switch) and J8 (debug header): decide flight population. A DNP SW1 on flight units is typical.
- **JP1 (`WD_ENABLE`) must be bridged for flight.** It is a normally-open solder jumper between `/WDG_IN` (PB2, R76) and MAX823 WDI (U12.4). While open, WDI floats and the watchdog is disabled; MAX823 POR supervision and the SW1 reset still reach NRST. Add it to the assembly notes and the flight build checklist.

### F9 — OR-ing / flyback Schottky ratings (NSVR1020MW2T1G) — `VERIFY`
- **Parts:** D11, D36, D37 are onsemi NSVR1020MW2T1G: 20 V V<sub>R</sub>, 1 A, V<sub>F</sub> 0.54 V max at 1 A, **200 mW P<sub>D</sub>**, SOD-323, AEC-Q101. The symbol `Datasheet` field is empty.
- **D11 (LT3065 → `+12V`):** I<sub>MAX</sub> caps the current at 300 mA, giving ≈ 0.12 W. In GSE-only mode its V<sub>R</sub> equals `12V_GSE`. D10 (SMF14A) clamps GSE transients at roughly 23 V, which **exceeds the 20 V V<sub>R</sub>**.
- **D36 (`12V_GSE` → `+12V`):** Carries the NC valve's holding current continuously during a fill, plus ~100 ms latching-valve pulses. At about 0.4 A, V<sub>F</sub> × I ≈ 0.18 W, which is at the 200 mW limit.
- **D37 (NC valve flyback):** Conducts the coil current briefly at turn-off. It's OK if the coil current is ≤ 1 A. Valve closing is slow with a plain freewheel diode; add a series zener if closing time matters.
- **Recommendation:** Once the coil currents are known (Q3), use a 2–3 A, 30–40 V Schottky in SOD-123FL or SMA for D11/D36. Give them copper for heat in vacuum.

### F10 — Crystal load capacitance — `VERIFY`
- C26/C27 = 12 pF assumes `ABM3B-8.000MHZ-10-D1G-T` has C<sub>L</sub> = 10 pF, with about 4 pF stray. Confirm the "-10-" code in Abracon's part-numbering guide, and trim after measuring frequency on the first board.

---

## Project Setup

### P1 — `Outputs/` contains another project's gerbers — `OPEN`
- `PIB-job.gbrjob` still reports the "lm5022-flyback" project (2-layer). The PIB is a 4-layer board. Delete `Outputs/` and regenerate before any fab order. Check `bom/ibom.html` too.

### P3 — Empty title blocks — `OPEN`
- None of the 4 sheets has a title, revision, date or company.

### P4 — ERC cleanup — `OPEN`
- With stock libraries loaded: 16 messages:
  - 11× `power_pin_not_driven`: needs PWR_FLAGs; also LT3065 PWRGD is typed as a power input (S5).
  - 4× `pin_not_connected`: U4 PA2, PA10, PA11, PC15. Add no-connect flags or use them.
  - 1× isolated label `IGNITER_RST` (S17).

### P5 — KiCad version — `OPEN`
- The files are KiCad 10 format (`version 20260306`), so the whole team needs KiCad 10.

### P6 — PCB out of sync with schematic — `OPEN`
- DRC with schematic parity reports **142 footprint issues**, covering the diode-OR / flyback / pull-down / JP1 parts, the Rogowski, PA0 and GSE pulse rework, the IGNITER buffer, TH1, J8 VTref and the removed parts.
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
| D6 | GSE architecture | Separate H-bridge + 12 V NMOS, diode-OR'd, "completely separated" | Shares flight drivers. `+12V` diode-OR'd from LT3065 and `12V_GSE` (D11/D36). The Q10 gate is diode-OR'd (D50, BAT54C) between GSE and flight, so either can open valve 2 and neither can force it closed. The latching valve gets an RC-timed pulse (~100 ms) on each GSE OPEN/CLOSE edge, diode-OR'd with flight (D47/D48), so either side can drive a MIC4424 input high. Conflicting commands put both inputs high, which brakes the coil | `ICD` |
| D7 | Rogowski reset | Block diagram shows reset input | Q3/Q4 reset switch with +5 V level shifter (Q5), wired to PA12, which it shares with the igniter peak-detector reset at opposite polarity (C12) | `OPEN` |
| D9 | BAT OCP / MCU-healthy gating | Claimed | Not implemented (C9) | `OPEN` |
| D10 | PIB thermistors | 2 | 1 (TH1 on PB1) | `ICD` — change to 1 |

### ICD Update Actions

Checklist of ICD edits the review has generated. Tick these off as the ICD is revised.

- [ ] **GSE section and Table 3 (D2, D6):** OPEN/~CLOSE is a 3.3 V digital logic line on J1.2; J1.1 = 12V_GSE; J1.3/4 = GND. Remove the "both pairs of 12V" wording and the 14/15 pin numbers. Describe the as-built scheme: shared valve drivers, `+12V` diode-OR'd from LT3065 and `12V_GSE`, the NC valve diode-OR'd between flight and GSE (either opens it, neither forces it closed), and the latching valve pulsed (~100 ms) on each GSE OPEN/CLOSE edge (C11). Replace "diode OR'd … completely separated".
- [ ] **Electrical connectors (D1):** G125-MS12605M2P (vertical), not MH12605M4P.
- [ ] **Sensor channels (D4, D5):** 2 P/T sensors (tank, frit; third optional/future). Igniter channels trimmed to peak V, peak I, supply V (plus whatever is derived in firmware).
- [ ] **Power interfaces (D3):** 5 V is required, not TBD (−5 V converter, LG16 flow sensor, HV board op amps, integrator).
- [ ] **BAT protection paragraph (C9, D9):** match the as-built protection (per-HV-branch ADM1270 eFuses; heater and 12 V branch protection once decided). State that launch inhibits are provided by EPS under UNP NS requirements.
- [ ] **Data interfaces:** IGNITER (J3.22) is a buffered copy (U8) of the gated igniter pulse.
- [ ] **Thermistors (D10):** 1 PIB thermistor, not 2.

---

## Open Questions

| ID | Question | Known so far |
|---|---|---|
| Q3 | Valve part numbers and coil data:<br>• Is the NC valve rated for continuous 12 V, or is it spike-and-hold (C10)?<br>• What is each valve's coil current?<br>• What pulse width does the latching valve need?<br>These set the C11 pulse RC, the F9 diode sizing, D37's rating, the continuous LT3065 load (300 mA limit) and J4 pin current. | Library has LHLA1252x11H and LHDB0442245D. The NC valve is non-latching and ran on plain 12 V DC on the bench. |
| Q8 | Rogowski coil sensitivity: mutual inductance, in V per A/µs? This is needed to size the coil termination, R79 and C45 for about ±1.5 V full scale (C5). | Discharge current measured 2025-09-03: 16.26 kA peak, damped ring at about 24 kHz (41 µs period), about 3 half-cycles over ~65 µs, bidirectional. |

---

## Verified OK (for reference)

- **MCU pin assignments:**
  - SPI1 slave PA4–7, I2C2 PA8/PA9, I2C3 PC8/PC9, SPI3 PC10–12 (DPT mux), LPUART1 PC0/PC1, SWD PA13/PA14.
  - DPT MCLK on PC7 (TIM3_CH2, from HSE via PLL).
  - Discharge V/I on PA0/PA1 (ADC12, simultaneous dual-mode capable).
- **ARM/SAFE:** R88 pull-down holds all eight 74HC08 gated outputs low during reset and watchdog reset.
- **GSE power path:**
  - D11/D36 diode-OR into `+12V`. The LT3065 loop (C6, C22, R18) stays on the regulator side.
  - `12V_GSE` (D10, U6) is on the source side of D36, so flight power can't back-feed the EGSE regulator or the EGSE pin.
- **Q10 gate:** D50 (BAT54C) diode-ORs flight and GSE (GSE anode on `EGSE_OPEN_F`, behind R46), through R110 (100 Ω), giving ≈ 3.1 V from either side; R6 pulls the gate off to ≈ 0 V, never in the 1.35–2.35 V threshold band. Neither side can back-power the other. IRF7832 Fig. 3 shows tens of A typical at about 3 V, so 3.3 V-class drive is adequate for the heater and valve FETs.
- **D37:** NC-valve flyback diode, oriented correctly.
- **PA0/PA1 front ends:** Each channel has 100 Ω (R106 / R102) → BAT54J pair to +3.3V/GND (D43/D44, D39/D40) → 1k (R105 / R104) → pin with 470 pF (C72 / C71). Orientation is correct, and the matched RCs (f<sub>c</sub> ≈ 339 kHz, ≈ 0.47 µs, −0.02 dB / −4° at 24 kHz) keep the V×I timing aligned. Injection in a fault is ≪ 0.1 mA.
- **GSE pulse circuit:** J1.2 is isolated by R46 (1k) with R45 pull-down. U18 buffers both phases, so no RC cap loads the GSE line. U18 (74LVC2G14) and U9 (74LVC2G17) are Schmitt parts with I<sub>off</sub>. D41/D42 clamp the RC nodes to GND (orientation correct). D47/D48 (BAT54C) diode-OR flight and GSE into U3 INA/INB with R41/R42 pull-downs, so there's no back-power in either direction on valve 3. U9 and U18 are decoupled (C31, C75).
- **IGNITER to CDH:** U8 (74AHC1G125, OE tied to GND, decoupled by C77) buffers `IGNITER_PULSE_GATED` onto J3.22 through R29.
- **PIB thermistor:** TH1 10k NTC / R109 10k from VDDA / C76 100 nF on PB1 gives ≈ VDDA/2 at 25 °C, ratiometric with the ADC reference.
- **J8 / reset:** J8.5 = +3.3V VTref through R108 (1k), which is short-protected. MAX823 ~RESET drives NRST directly (R77 pull-up), so SW1 is always a hardware reset; use a manual connect-under-reset for recovery. JP1 in the WDI line disables only the watchdog.
- **J3 ESD:** J3.22–24 are protected by D45/D46/D49 (PESD3V3U1UL) on the connector side of R29/R30/R31.
- **Symbol libraries:** U1/U2/U3 come from `0_PAY_Symbol_Library`, which is in the project's `sym-lib-table`.
- **Valve 1 (U2):** U15 drives INA/INB directly, with R39/R40 pull-downs (no GSE path).
- **DPT mux:** R89–R94 pull-downs keep the deselected sensor's SCK/DIN and the unselected DOUT input defined.
- **J3:** pinout matches ICD Table 2. GSE OPEN/~CLOSE input protection (D5 PESD3V3U1UL, R46 1k series, R45 pull-down, LVC Schmitt logic with I<sub>off</sub>) suits 3.3 V logic.
- **LG16 flow sensor J15:** pinout (1 SDA, 2 SCL, 3 VDD, 4 GND, 5 NC), 5 V supply and 3.3 V I²C all match the datasheet. Sensor operating range is +10…+50 °C.
- **LT8580 −5 V:** R75 60.4k → −5.03 V. D1 orientation is correct.
- **LT3065 (U1):**
  - DFN-10 pinout matches the Rev. F datasheet.
  - Output 0.6 × (1 + 21.5k/1.13k) = 12.02 V. I<sub>MAX</sub> = 300 mA (R2 = 1k), with C10 22 nF on IMAX, C11 10 nF REF/BYP, C6 10 µF and C22 10 nF feedforward. Foldback is not engaged.
  - Thermal: about 1.44 W at 300 mA and 4.8 V headroom, roughly a 50 °C rise. That needs the board ≲ 70 °C.
- **MAX823:** RESET source current is 800 µA max, so the STM32 and ST-Link can overdrive it. SOT-23-5 pin mapping is correct.
- **ADM1270:** current limits 50 mV / 15 mΩ = 3.33 A and 50 mV / 100 mΩ = 0.5 A. PG threshold ≈ 11 V.
- **MIC4424:** SOIC-8 mapping is correct.
