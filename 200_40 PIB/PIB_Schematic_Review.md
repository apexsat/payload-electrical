# 200_40 PIB — Schematic & Project Review (Living Document)

| | |
|---|---|
| **Board** | 200_40 Payload Interface Board (APEx PMPD thruster, 6U CubeSat) |
| **Reviewed files** | `PIB.kicad_sch`, `Rogowski.kicad_sch`, `Valve Driver.kicad_sch`, `DPT Mux.kicad_sch`, `PIB.kicad_pcb` (parity only), `firmware/firmware.ioc` (pin/clock config) |
| **Reference docs** | `Payload ICD.pdf` (Rev 2.0, CDR), `PIB Block Diagram.drawio.png`, `200_60 DPT Sensor Board` |
| **Design state** | branch `pib-split`, `2d73650` + uncommitted changes, including the upstream eFuse (U19) and the new load-switch sheets (2026-10-19) |
| **PCB** | Not yet updated from the schematic; layout follows the schematic changes. Parity isn't tracked here. When updating, give D11/D36 copper for heat (F9), then regenerate the fab outputs and the iBOM. |
| **Tools** | KiCad 10.0.4 (`kicad-cli` ERC, netlist, DRC w/ schematic parity) |
| **Last updated** | 2026-10-19 |

**Status key:** `OPEN` (needs a design change) · `VERIFY` (needs a datasheet, bench or part check) · `OPTIONAL` (recommended hardening, not required) · `ICD` (schematic is correct; the ICD must change)

**How to use:** This document lists only issues present in the current KiCad design. When an issue is resolved, delete it. Item IDs are stable, so gaps in the numbering are expected. Don't renumber.

---

## Summary

The pin-to-pad check is clean for every footprint. The main risks are:
- The GSE latching-valve pulse needs bench checks: polarity, and pulse width against the valve spec (C11).
- The Rogowski front end is complete. What's left is checking the reset-switch leakage against R98 and the coil's flight suitability (C5).
- D11 (20 V OR-ing Schottky) is under-rated for ~23 V GSE transients, and D36 still needs checking against the latching-valve pulse current (F9).

The BOM is not production-ready (no MPNs or capacitor ratings). Several ICD items need to be brought in line with the schematic (see [ICD Update Actions](#icd-update-actions)).

**Design context:**
- UNP NS requirements apply. Launch inhibits live in EPS, so the PIB and CDH are fully unpowered until after deployment.
- HV boards (igniter 1 kV, discharge 400 V) scale all signals to 0 to <3.3 V with op amps on ±5 V.
- GSE OPEN/~CLOSE (J1.2) is a 3.3 V digital logic line.
- The NC valve (valve 2) is a Lee LHDB1223518H: non-latching, normally closed, rated for continuous 12 V DC (not spike-and-hold), about 600 mW, so about 50 mA (≈ 240 Ω). It is held open continuously during burns and GSE fills.
- Each heater is well under 10 W (TBD).
- Power architecture: the EPS supplies `VBAT_HOT` with a 4 A OCP. `VBAT_HOT` (from J3, D3 TVS) → U19 upstream eFuse (3.6 A, set to trip before the EPS) → `VBAT_PROT` → heaters, LT3065 `+12V`, and the discharge (U5) and igniter (U7) branch eFuses. The undervoltage cutoffs are staged and intentional: the branches trip at 12.75 V and the upstream at 12.0 V, both before the EPS cutoff, which protects the pack and sheds the thruster first.
- The CDH connector is the vertical (MS) part.
- Only 2 P/T sensors (tank, frit) are fitted; a third may be added later.
- Op amps with heavy-ion DSEE test heritage (OP27/OP37 family) are preferred over RRIO parts that lack it. Clamp the outputs to the ADC range instead.
- `/~{PD_RST}` (PA12) is the shared active-low reset for the Rogowski integrator and the igniter peak detectors. The igniter board inverts it (Q5 → `/IGNITER_nRST`) to drive its peak-detector reset FETs.
- **BOM policy:** every resistor whose value sets a threshold, gain, current limit or timing uses **0.1% tolerance**. That includes UV/OV, ISET, FB_PG and FLB dividers, regulator feedback (R17/R18, R75), IMAX (R2), the Rogowski network and V<sub>REF</sub> divider, NTC bias and pulse-timing resistors. Generic pull-ups, pull-downs and series/gate resistors (e.g. 10k/100k pull-downs, 100 Ω series) can stay at standard tolerance. Current-sense resistors are 1%.
- Discharge branch load (`VBAT_DCHG`, J12): 20 W average, flat, as 0.8 s on / 0.2 s idle per second. That's ≈ 25 W while drawing: 1.5 A at 16.8 V, up to 2.1 A at the 12 V cutoff.
- Rogowski sensor: the flight unit flies only the PEM CWTMini50HF/150 coil head, with the PIB integrating. The complete CWT transducer (0.2 mV/A, ±6 V at 30 kA) is ground calibration equipment.
- Discharge current is a damped ring: 16.26 kA peak, about 24 kHz, about 65 µs long, bidirectional.

| Category | Items |
|---|---|
| Critical | 4 (2 optional, 2 verify) |
| Significant | 5 |
| Footprint / Part | 9 |
| Project setup | 2 |
| ICD / Block diagram | 8 |
| Open questions | 2 |

---

## Critical (safety or function)

### C5 — Rogowski front end: switch leakage and coil flight suitability — `VERIFY`
- **Sensor:** the flight unit flies only the coil head of a PEM CWTMini50HF/150. The PIB does the integration. The complete CWT transducer (with PEM's integrator: 0.2 mV/A, ±6 V at 30 kA) is ground equipment and can serve as the calibration reference.
- **Where:** Rogowski sheet (working copy).
  - Signal chain: J7.1 (MMCX, D38 SD12C-01F bidirectional 12 V TVS, R95 100k bleeder to GND) → C69 (4.7 µF) → R79 (2.00k) → U11.2 (OP27), with C45 (10 nF C0G) ∥ R98 (1 M) ∥ Q3/Q4 (BSS138 reset switch) from U11.2 to U11.6.
  - U11.3 (+) sits at V<sub>REF</sub> = VDDA/2 (R96/R97 10k/10k, C70 1 µF). Supplies are ±5 V.
  - Reset: `~{ROGOWSKI_RST}` (main-sheet net `/~{PD_RST}`, PA12, shared with the igniter peak-detector reset on J5.2) → R100 → Q5 (BSS138, R101 gate pull-down). Q5's drain, pulled up to +5 V by R99, drives the Q3/Q4 gates. The integrator is held in reset whenever the GPIO is low or high-Z.
  - Output: U11.6 → R102 (100 Ω) → clamp D40 (BAT54J, to +3.3V) / D39 (BAT54J, to GND) → R104 (1k) → PA1 with C71 (470 pF), matched to the PA0 network.
- **Problems:**
  1. **Reset-switch leakage (check):** while integrating, Q3/Q4 off-state leakage × R98 (1 M) appears as offset. That's typically nA, a few mV, at the ≈ 1.5 V V<sub>DS</sub> here, but the datasheet max (often 0.5–1 µA at 50 V, more when hot) would be 0.5–1 V. Check BSS138 leakage at low V<sub>DS</sub> and temperature.
  2. **Coil flight suitability is unconfirmed:** whether the coil detaches from the CWT integrator, its cable and connector to J7, its temperature rating and its outgassing.
- **Fix:**
  - **Leakage:** if the BSS138 leakage is significant, reduce R98 (adjusting C45 / relying on firmware droop correction) or use a lower-leakage switch.
  - **Firmware sequencing:** hold reset during power-up (C69 charges with τ ≈ (R79 + 154 Ω coil)·C69 ≈ 10 ms). Release it shortly before the shot, then sample a baseline after release. That removes switch leakage × R98, op-amp offset and charge injection (≈ C<sub>gd</sub>·ΔV<sub>g</sub>/C45, a few mV).
  - **Gain headroom:** size the gain so the expected peak stays within about 0.15–3.15 V, so the D39/D40 clamp only acts on faults.
- **Value sizing (M ≈ 1.7 nH):** the flight shot's peak onset dI/dt is ≈ 3.1 kA/µs (16.26 kA, ~24 kHz, damped ~0.6× per half-cycle), so the peak coil voltage is ≈ 5.3 V (≤ ~6 V with M uncertainty). Gain target: R79·C45 = M·16 260/1.5 V ≈ 18 µs.

  | Part | Proposed | Result |
  |---|---|---|
  | R79 | 2.00k (0.1%) | Peak summing current ≈ 2.7 mA; ≥ 10× R95 |
  | C45 | 10 nF C0G | Output peak = M·I<sub>pk</sub>/((R79 + Z<sub>s</sub>)·C45) ≈ 1.28 V with the coil's Z<sub>s</sub> = 154 Ω (0.37–2.93 V around 1.65 V); ≈ 1.41 V at M + 10% |
  | R98 | 1.00 M (0.1%) | τ<sub>leak</sub> = 10 ms; OP27 bias × R98 ≈ 40–60 mV offset, removed by the pre-shot baseline |
  | C69 | 4.7 µF X7R, 0603, ≥ 10 V (16 V preferred) | It sees ≈ 1.65 V DC and negligible AC (≈ 1.4 Ω at 24 kHz vs ≈ 2.15 kΩ). τ<sub>AC</sub> = (R79 + Z<sub>s</sub>)·C69 ≈ 10 ms nominal, ≈ 7–9 ms after DC-bias derating; total droop ≈ 1.3–1.6% at 65 µs |
  | R95 | 100k bleeder (not a termination) | The coil has 154 Ω source resistance (DC multimeter), and a 50 Ω load cut the signal ≈ 3–4×, so the coil must see only high impedance (PEM's integrator is 3 MΩ). 100k only defines DC and bleeds static. |

  - **C69:** X7R in 0603 is acceptable. Its bias-driven piezo error is about 10⁻⁴–10⁻³ of the coil signal, its tolerance and DC-bias loss only shift droop slightly (calibrated), and 0603 is less prone to flex cracking than larger cases. Avoid tantalum.
  - **Coil source resistance (measured 2026-10-19):** 154 Ω DC. The 50 Ω-load shot showed the same resistive behaviour: the step and the slow portion were attenuated alike. With R79 = 2k the loss is ≈ 7% (calibrated out). If the 154 Ω is copper, ±40 °C moves the gain ≈ ±1.1%. Correct with the PIB thermistor, or use the option below.
  - **Lower-tempco option:** R79 4.99k, C45 4.7 nF C0G, R98 ≈ 2.2 M, C69 2.2 µF. That gives ≈ 1.14 V peak and ≈ ±0.5% tempco, at the cost of ≈ 90–130 mV of bias offset (removed by the baseline).
  - **ADC rate and LT8580 ripple:** The planned rate is 2–4 MSPS (80–160 samples per period). The −5 V LT8580 switches at 846 kHz, which is below Nyquist at both rates, so it appears as an in-band tone that a digital low-pass removes.
    - Its harmonics alias to 78 kHz (7th) at 2 or 3 MSPS, and to 230 kHz at worst (5th) at 4 MSPS. Prefer 4 MSPS.
    - The LT8580 free-runs (SYNC = GND), so its tolerance and drift move these aliases. If the bench PA1 noise floor shows the ripple matters, drive SYNC from an HSE-derived MCU timer, e.g. f<sub>sw</sub> = fs × 2/9 ≈ 889 kHz at 4 MSPS (check the SYNC range).
    - Expected ripple at the output is sub-LSB (≈ 0.3 mV, via the OP27's V− PSRR). Use 1 nF rather than 470 pF in the PA0/PA1 RC if more rejection is needed: −14.6 dB vs −8.6 dB at 846 kHz.
  - **Bandwidth:** The measured current is a damped ring at about 24 kHz (41 µs period, ~65 µs long). The OP27's 8 MHz GBW gives about 330× loop gain at 24 kHz (≈ 0.3% error), and its 2.8 V/µs slew is well above the ~0.25 V/µs needed.

### C6 — Igniter-board ADC inputs are unclamped — `OPTIONAL`
- **Where:** `ADC_V_IGN_PK`, `ADC_I_IGN_PK`, `ADC_V_IGN_SUPPLY`, `ADC_NTC_IGN` (J5): each has only a PESD3V3U1UL, with no series R.
- **Risk:** Nominal signals are 0 to <3.3 V. However, the igniter-board op amps run on ±5 V, so a fault, overrange, power-sequencing event or HV arc can push an output to a rail, and the ESD diodes are not continuous clamps.
- **Hardening:** Use the same scheme as PA0/PA1: series R ≈ 1k plus a BAT54 pair to +3.3V/GND at each input. These are slow channels (peak-detect, NTC, supply), so the delay doesn't matter.

### C9 — No hardware heater over-temperature cutoff — `OPTIONAL`
- **Where:** Main sheet, Q9/Q11 (low-side heater switches on `VBAT_PROT`).
- U19 now gives the BAT bus overcurrent protection and MCU-controlled gating (`/BAT_EN`, PA10, pulled off at reset). A heater FET that fails on can therefore be cut by firmware through U19, but that also drops the `+12V` valve rail and depends on firmware noticing the over-temperature.
- **Option:** add an independent hardware cutoff, either a series thermostat or a comparator on a tank NTC that pulls the heater gates low.
- **Heater sizing:** Worst case is 2 × 10 W at 12 V = 1.67 A, ≈ 0.83 A on each of J2's shared BAT pins (2, 3) and return pins (1, 4). That's within PicoBlade's ~1 A/contact but tight at the 10 W bound. Confirm once wattage is fixed.

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

---

## Significant

### S4 — DPT harness pin order mismatch — `OPEN`
- PIB J9/J10 (PicoBlade 53398-0671): 1 3V3, 2 GND, 3 SCK, 4 DOUT, 5 DIN, 6 MCLK.
- DPT board J1 (Pico-Lock 503480-0640): 1 SCLK, 2 DOUT, 3 DIN, 4 MCLK, 5 VDD, 6 GND.
- A 1:1 harness would put power on SCLK. **Fix:** Align the pin orders or document the cross-wired harness.

### S7 — ADM1270 auto-retry: start-up capacitance and PWRGD fault logic — `VERIFY`
- **As built (intentional):** R51/R64/R116 (0 Ω) tie ~FAULT to ENABLE, so all three eFuses (U5 discharge, U7 igniter, U19 upstream) auto-retry. U19 uses C81 = 22 nF (≈ 2.2 ms on-time, so U5 clears a branch fault first) and C80 = 100 nF. Fault state is read through PWRGD: `/DISCHARGE_PGOOD` on PC13, `/IGNITER_PGOOD` on PC14 and `/BAT_PGOOD` on PA11, each with a 10k pull-up.
- **Timing (ADM1270 Rev. A):** with TIMER = 10 nF (C30/C35) and TIMER_OFF = 100 nF (C29/C34/C80):
  - On time = 2.0 V × C<sub>TIMER</sub>/20 µA ≈ **1.0 ms** (0.8–1.25 ms).
  - Off time = 2.0 V × C<sub>TIMER_OFF</sub>/1 µA ≈ **200 ms** (150–265 ms).
  - Retry duty ≈ **0.5%** (≤ 0.8%). Initial power-on delay ≈ 10 ms.
- **Foldback:** the current-limit reference is the lower of V<sub>ISET</sub> and V<sub>FLB</sub>, giving V<sub>SENSE</sub> ≈ 50 mV/V × V<sub>FLB</sub> with a 10 mV floor. U5/U19 use 100k/10k dividers (V<sub>FLB</sub> = V<sub>OUT</sub>/11; full current above ≈ 7–8.6 V out), and U7 uses 33k/10k.
- **Checks:**
  1. **Start-up into load capacitance:** The current limit is also the inrush limit, and the 1 ms timer runs throughout. Starting from 0 V through foldback, the branch can charge only about **≈ 130 µF on `VBAT_DCHG`** (≈ 100 µF at worst-case tolerances) and **21 µF on `VBAT_IGN`** (no load current) before timing out.
     - Confirm the input capacitance of the HV discharge converter on J12. It isn't in the repo; `200_70` holds only the sense board. If the capacitance exceeds that limit, the branch never starts and retries every ~200 ms.
     - **Fix if needed:** raise C30 (each 10 nF adds ≈ 1 ms of on time; re-check Q1 SOA beyond ~3 ms) or soft-start the load.
  2. **Firmware fault logic** (also applies to U19's `/BAT_PGOOD` on PA11): PWRGD (≈ 11 V threshold) is also low when the branch is commanded off (ENABLE low or ARM safe) or when BAT is below the 12 V UV. Flag a fault only when the branch is commanded on, BAT is in range, and PWRGD stays low or keeps toggling past a debounce window. PWRGD pulses during retries.

### S14 — Non-ARM 74HC08 inputs float at reset — `OPTIONAL`
- R88 holds `/ARM/~{SAFE}` low, so all eight gated outputs are low during reset. The other inputs (`IGNITER_EN`, `DISCHARGE_EN`, `IGNITER_PULSE`, `VALVE1_L/H`, `VALVE2_EN`, `VALVE3_L/H`) float while the STM32 pins are high-Z.
- That's safe, but floating HC inputs can draw extra supply current or oscillate. Add 100k pull-downs if layout space allows.

### S15 — `+12V` in LT3065 dropout at low battery — `VERIFY`
- The LT3065 regulates 12.6 V (R18 20k / R17 1k), so `+12V` ≈ 12.3 V after D11 while BAT ≳ 12.9 V. Below that the regulator is in dropout and `+12V` ≈ BAT − ~0.6 V, about 11.4 V just before U19's 12.0 V undervoltage cutoff (the LT3065 is on `VBAT_PROT`). In GSE mode, `+12V` = `12V_GSE` − V<sub>F</sub>(D36).
- **Check:** latching-valve operation at about 11.4 V and at cold (Q3). The NC valve is rated 12 V and sees ≈ 12.3 V nominal.

### S16 — DPT MCLK timer not configured in firmware — `OPEN` (firmware)
- `/DPT_MCLK` is driven from PC7, and `firmware.ioc` assigns PC7 to `TIM3_CH2` with TIM3 clocked at 64 MHz (HSE 8 MHz via PLL). The prescaler, period and OC mode are at CubeMX defaults: OC "Frozen" doesn't toggle the pin, and the default period gives about 488 Hz.
- **Fix:** Either set OC mode to Toggle with ARR = 976 (32.75 kHz), or use PWM mode 1 with ARR = 1952 and CCR = 976 (32.77 kHz). Both are within the MS5540C's 30–35 kHz range.

---

## Footprints & Part Numbers

Pin-to-pad check: every symbol pin maps to a footprint pad (custom DFN-8/10_DD, SOT95P280X145-5N, HARWIN M80, ABM3B, PowerPAK SO-8, SOD-323). MIC4424 pads 1/8 are NC, which is correct. SP0502BAJT on SC-70 is correct.

### F1 — No MPNs or capacitor ratings — `OPEN`
- Only U1 carries an MPN field, and only some resistors have a tolerance. No capacitor has a voltage or dielectric rating. **Fix:** Add `MPN` and `Manufacturer` fields project-wide, and a `Tolerance` field that marks the 0.1% resistors per the BOM policy in the design context.

### F2 — BAT-rail capacitor voltage rating — `OPEN`
- C79 (`VBAT_HOT`), C1, C28, C33, C62, C82 (`VBAT_PROT`) and C66, C67 see up to the SMF18A clamp (~29 V), so they need 50 V parts.
- The 10 µF 0805 capacitors, especially C4 (the coupling capacitor in the −5 V converter, ~10 V across it), lose most of their capacitance under DC bias. Use 25 V+ X7R, and 1206 where needed.

### F3 — Sense-resistor rating and accuracy (R38, R35) — `OPEN`
- **R38** (upstream, 10 mΩ, 0805) carries the whole bus: up to ≈ 3–4 A, so ≈ 0.09–0.16 W, at or above a standard 0805's 1/8 W. Use a ≥ 0.25–0.5 W current-sense part.
- **R35** (discharge, 10 mΩ, 0805) dissipates ≈ 44 mW at 2.1 A, which is fine.
- At 10 mΩ, 1 mΩ of solder and trace resistance shifts each current limit by ≈ 10%. Use low-TCR current-sense parts with Kelvin footprints, routing SENSE+/SENSE− from the pads.

### F4 — FB1 footprint/value — `OPEN`
- FB1 uses `R_0603` and its value is "120". Specify a ferrite part number (impedance at 100 MHz, DCR, current) and use an inductor/ferrite footprint.

### F5 — Symbol/value mismatches — `OPEN`
- Q1/Q2/Q6: symbol `Si7141DP` (20 V part), value Si7489DP (100 V).
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
- **D36 (`12V_GSE` → `+12V`):** Carries the NC valve's ~50 mA continuously during a fill (≈ 15 mW, fine), plus ~100 ms latching-valve pulses at a current that's still unknown (Q3). Check the latching-valve pulse current against the 1 A rating and the pulse-power limit.
- **D37 (NC valve flyback):** Conducts the ~50 mA coil current briefly at turn-off, well within rating. Valve closing is slow with a plain freewheel diode; add a series zener if closing time matters.
- **Recommendation:** D11 needs a ≥ 30–40 V part for the GSE transient (e.g. a 1–2 A, 40 V Schottky in SOD-123FL). Re-check D36 once the latching-valve pulse current is known (Q3).

### F10 — Crystal load capacitance — `VERIFY`
- C26/C27 = 12 pF assumes `ABM3B-8.000MHZ-10-D1G-T` has C<sub>L</sub> = 10 pF, with about 4 pF stray. Confirm the "-10-" code in Abracon's part-numbering guide, and trim after measuring frequency on the first board.

---

## Project Setup

### P3 — Empty title blocks — `OPEN`
- None of the 6 sheets (including the two load-switch sheets) has a title, revision, date or company.

### P4 — ERC cleanup — `OPEN`
- With stock libraries loaded: 12 messages:
  - 11× `power_pin_not_driven`: needs PWR_FLAGs.
  - 1× `pin_not_connected`: U4 PC15. Add a no-connect flag or use it.

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
| D9 | BAT OCP / MCU-healthy gating | Claimed | Upstream eFuse U19 on all BAT loads, enabled by the MCU (`/BAT_EN`), with OV 18 V and staged UV | `ICD` — describe as built |
| D10 | PIB thermistors | 2 | 1 (TH1 on PB1) | `ICD` — change to 1 |

### ICD Update Actions

Checklist of ICD edits the review has generated. Tick these off as the ICD is revised.

- [ ] **GSE section and Table 3 (D2, D6):** OPEN/~CLOSE is a 3.3 V digital logic line on J1.2; J1.1 = 12V_GSE; J1.3/4 = GND. Remove the "both pairs of 12V" wording and the 14/15 pin numbers. Describe the as-built scheme: shared valve drivers, `+12V` diode-OR'd from LT3065 and `12V_GSE`, the NC valve diode-OR'd between flight and GSE (either opens it, neither forces it closed), and the latching valve pulsed (~100 ms) on each GSE OPEN/CLOSE edge (C11). Replace "diode OR'd … completely separated".
- [ ] **Electrical connectors (D1):** G125-MS12605M2P (vertical), not MH12605M4P.
- [ ] **Sensor channels (D4, D5):** 2 P/T sensors (tank, frit; third optional/future). Igniter channels trimmed to peak V, peak I, supply V (plus whatever is derived in firmware).
- [ ] **Power interfaces (D3):** 5 V is required, not TBD (−5 V converter, LG16 flow sensor, HV board op amps, integrator).
- [ ] **BAT protection paragraph (D9):** describe the as-built protection: an upstream ADM1270 eFuse (U19) on the whole BAT bus, enabled by the MCU and reporting `BAT_PGOOD`, with OV 18 V and UV 12.0 V. Per-branch ADM1270s on the discharge and igniter supplies, with UV 12.75 V for staged shedding. State that launch inhibits are provided by EPS under UNP NS requirements.
- [ ] **Data interfaces:** IGNITER (J3.22) is a buffered copy (U8) of the gated igniter pulse.
- [ ] **Thermistors (D10):** 1 PIB thermistor, not 2.

---

## Open Questions

| ID | Question | Known so far |
|---|---|---|
| Q3 | Latching valve data: part number, coil current, and required pulse width. These set the C11 pulse RC and the D36 pulse rating (F9). | NC valve: LHDB1223518H, 12 V DC continuous, ~600 mW (~50 mA). Library also has LHLA1252x11H (likely the latching valve; confirm). |
| Q8 | Flight Rogowski coil (PEM CWTMini50HF/150 coil head): confirm the precise open-circuit M (offset-corrected integral at the trough ÷ CT peak). | **Measured 2026-10-19** (380 µF from 200 V via a shorting stick, CT reference): open-circuit **M ≈ 1.7 nH (±10%)** from ∫V<sub>coil</sub>dt vs CT and from the onset step. **Coil resistance 154 Ω DC.** A 50 Ω load attenuated the signal ≈ 3–4×, resistively. The steps in dI/dt at ≈ 41 and 84 µs come from the shorting-stick arc. The transducer trough read ≈ 10–20% above the CT (rough read); check the CT scaling. |

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
- **J7 protection:** D38 (SD12C-01F, bidirectional 12 V standoff) at J7.1 ahead of C69 stays off at the ≈ 5–6 V coil peak. R95 (100k) defines DC with the coil unplugged and costs ≈ 0.15% against the coil's 154 Ω.
- **PA0/PA1 front ends:** Each channel has 100 Ω (R106 / R102) → BAT54J pair to +3.3V/GND (D43/D44, D39/D40) → 1k (R105 / R104) → pin with 470 pF (C72 / C71). Orientation is correct, and the matched RCs (f<sub>c</sub> ≈ 339 kHz, ≈ 0.47 µs, −0.02 dB / −4° at 24 kHz) keep the V×I timing aligned. Injection in a fault is ≪ 0.1 mA.
- **GSE pulse circuit:** J1.2 is isolated by R46 (1k) with R45 pull-down. U18 buffers both phases, so no RC cap loads the GSE line. U18 (74LVC2G14) and U9 (74LVC2G17) are Schmitt parts with I<sub>off</sub>. D41/D42 clamp the RC nodes to GND (orientation correct). D47/D48 (BAT54C) diode-OR flight and GSE into U3 INA/INB with R41/R42 pull-downs, so there's no back-power in either direction on valve 3. U9 and U18 are decoupled (C31, C75).
- **IGNITER to CDH:** U8 (74AHC1G125, OE tied to GND, decoupled by C77) buffers `IGNITER_PULSE_GATED` onto J3.22 through R29.
- **PIB thermistor:** TH1 10k NTC / R109 10k from VDDA / C76 100 nF on PB1 gives ≈ VDDA/2 at 25 °C, ratiometric with the ADC reference.
- **J8 / reset:** J8.5 = +3.3V VTref through R108 (1k), which is short-protected. MAX823 ~RESET drives NRST directly (R77 pull-up), so SW1 is always a hardware reset; use a manual connect-under-reset for recovery. JP1 in the WDI line disables only the watchdog.
- **PIB J5 ↔ igniter J3:** through a standard mirrored PicoBlade cable (pin n ↔ 14 − n), all signals, grounds and ±5 V match pin-for-pin (igniter board at `acfe0a1`).
- **J3 ESD:** J3.22–24 are protected by D45/D46/D49 (PESD3V3U1UL) on the connector side of R29/R30/R31.
- **Symbol libraries:** U1/U2/U3 come from `0_PAY_Symbol_Library`, which is in the project's `sym-lib-table`.
- **Valve 1 (U2):** U15 drives INA/INB directly, with R39/R40 pull-downs (no GSE path).
- **DPT mux:** R89–R94 pull-downs keep the deselected sensor's SCK/DIN and the unselected DOUT input defined.
- **J3:** pinout matches ICD Table 2. GSE OPEN/~CLOSE input protection (D5 PESD3V3U1UL, R46 1k series, R45 pull-down, LVC Schmitt logic with I<sub>off</sub>) suits 3.3 V logic.
- **LG16 flow sensor J15:** pinout (1 SDA, 2 SCL, 3 VDD, 4 GND, 5 NC), 5 V supply and 3.3 V I²C all match the datasheet. Sensor operating range is +10…+50 °C.
- **LT8580 −5 V:** R75 60.4k → −5.03 V. D1 orientation is correct.
- **LT3065 (U1):**
  - DFN-10 pinout matches the Rev. F datasheet.
  - Output 0.6 × (1 + 20k/1k) = 12.6 V. I<sub>MAX</sub> = 300 mA (R2 = 1k), with C10 22 nF on IMAX, C11 10 nF REF/BYP, C6 10 µF and C22 10 nF feedforward. Foldback is not engaged.
  - Thermal: the continuous load is the NC valve (~50 mA), so about 0.21 W at 4.2 V headroom (16.8 V in). The 300 mA current-limit worst case is ≈ 1.26 W (≈ 45 °C rise).
- **MAX823:** RESET source current is 800 µA max, so the STM32 and ST-Link can overdrive it. SOT-23-5 pin mapping is correct.
- **Pass-FET SOA (Si7489DP):** With the 100k/10k foldback, the worst retry pulse (18 V in, max limit, V<sub>OUT</sub> at end of foldback) is ≈ 36 W on Q1 for ≤ 1.25 ms and ≈ 37 W on Q6 for ≤ 2.75 ms. Z<sub>thJC</sub> gives ≈ 5–7 °C rise per pulse, with ≈ 2.5× margin to a rough derated SOA-chart reading. The sustained-fault average is ≤ 0.3 W (Q1) and ≤ 0.7 W (Q6). Q2 (0.5 A) peaks at ≈ 7 W.
- **Selectivity:** U19's on-time (≥ 1.76 ms) exceeds U5's (≤ 1.25 ms), so a discharge-branch fault is cleared by U5 before U19 can trip.
- **Discharge branch in normal operation:** Q1 conduction loss at the worst-case 2.1 A on-current is ≈ 2.1² × 0.041 Ω × 1.5 (hot) × 0.8 duty ≈ 0.2 W. J12 (Harwin M80, two VBAT_DCHG contacts and three GND contacts, ~3 A/contact class) carries ≈ 1 A per contact.
- **ADM1270 thresholds (0.1% divider resistors recommended):**
  - U19 (R111/R112/R113 = 33k/1k/2k): UV 12.0 V falling / 12.72 V rising, OV 18.0 V rising / 17.46 V falling.
  - U5 and U7 (158k/3.92k/9.53k): UV 12.75 V falling / 13.52 V rising, OV 18.0 V rising / ≈ 17.5 V falling.
  - PG thresholds ≈ 11 V. Igniter limit 50 mV / 100 mΩ = 0.5 A.
  - Auto-retry ENABLE levels: ≈ 3.0 V via the 10k/100k dividers (V<sub>IH</sub> 1.2 V), pulled to ≲ 0.2 V by ~FAULT (V<sub>IL</sub> 0.4 V).
  - ISET dividers sit well clear of the 2.55–2.65 V reference-select threshold (U19 1.44 V, U5 1.28 V), and their VCAP loads (≈ 15 µA, ≈ 22 µA) are far under the 100 µA limit.
- **Bus control and telemetry:** `/BAT_EN` (PA10, R115 pull-down, so the bus is off while the MCU is in reset), `/BAT_PGOOD` (PA11), `/12V_PG` (PA2, LT3065 PWRGD with R1 pull-up to +3.3V).
- **MIC4424:** SOIC-8 mapping is correct.
