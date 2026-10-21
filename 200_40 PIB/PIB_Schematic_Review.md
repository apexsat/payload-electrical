# 200_40 PIB — Schematic & Project Review (Living Document)

| | |
|---|---|
| **Board** | 200_40 Payload Interface Board (APEx PMPD thruster, 6U CubeSat) |
| **Reviewed files** | `PIB.kicad_sch`, `Rogowski.kicad_sch`, `Valve Driver.kicad_sch`, `DPT Mux.kicad_sch`, `PIB.kicad_pcb` (parity only), `firmware/firmware.ioc` (pin/clock config) |
| **Scope** | The PIB (200_40) only. The igniter board (200_80) is a boilerplate design and out of scope; only the PIB side of the J5 interface is reviewed. |
| **Reference docs** | `Payload ICD.pdf` (Rev 2.0, CDR), `PIB Block Diagram.drawio.png`, `200_60 DPT Sensor Board`, `Rogowski_Coil_Characterization.md` (coil measurements and integrator sizing) |
| **Datasheets used** | In `200_40 PIB/`: ADM1270 (Rev. A; timer, foldback and ISET figures) and Si7489DP (SOA and thermal impedance). In the repo root: LT3065 (Rev. F). Also LMP202x (TI SNOSAY9G, Rev. G), which is no longer in the repo. |
| **Design state** | branch `pib-split`, `d2c2d1b` (2026-10-20); schematics unchanged in the working copy |
| **PCB** | Layout in progress. Schematic parity is **not** clean (15 issues, 2026-10-20): Q3–Q5 are still BSS138 on the PCB, and D11/D36/D37 are still NSVR1020 in SOD-323. Run Update PCB from Schematic, then re-place the diodes for SOD-123. DRC is mid-route (unconnected items and shorts expected). Before release: regenerate the fab outputs and the iBOM. |
| **Tools** | KiCad 10.0.4 (`kicad-cli` ERC, netlist, DRC w/ schematic parity). To re-run: point `KICAD_CONFIG_HOME` at a scratch directory whose `10.0/` holds only the stock `sym-lib-table`/`fp-lib-table` (from `/usr/share/kicad/template/`), so library warnings are real; run `kicad-cli sch erc --severity-all PIB.kicad_sch`; and compare `kicad-cli sch export netlist` output between revisions (`git archive` the old one) to see exactly which parts and nets changed. |
| **Last updated** | 2026-10-20 |

**Status key:** `OPEN` (needs a design change) · `VERIFY` (needs a datasheet, bench or part check) · `OPTIONAL` (recommended hardening, not required) · `ICD` (schematic is correct; the ICD must change)

**How to use:** This document lists only issues present in the current KiCad design. When an issue is resolved, delete it. Item IDs are stable, so gaps in the numbering are expected. Don't renumber.

---

## Summary

The pin-to-pad check is clean for every footprint. The main risks are:
- The GSE latching-valve pulse needs bench checks: polarity, and pulse width against the valve spec (C11).

The BOM is not production-ready (no MPNs or capacitor ratings). Several ICD items need to be brought in line with the schematic (see [ICD Update Actions](#icd-update-actions)).

**Design context:**
- UNP NS requirements apply. Launch inhibits live in EPS, so the PIB and CDH are fully unpowered until after deployment.
- HV boards (igniter 1 kV, discharge 400 V) scale all signals to 0 to <3.3 V with op amps on ±5 V.
- GSE OPEN/~CLOSE (J1.2) is a 3.3 V digital logic line.
- The NC valve (valve 2) is a Lee LHDB1223518H: non-latching, normally closed, rated for continuous 12 V DC (not spike-and-hold), about 600 mW, so about 50 mA (≈ 240 Ω). It is held open continuously during burns and GSE fills.
- The latching valve (valve 3) is a Lee LHLA1221211H: 12 V DC, face mount, FKM seal, 5.5 mJ per switch, 10–30 ms max response, operating range 4–49 °C, coil max 107 °C. It needs reversed polarity to close (U3 H-bridge). The coil resistance and the required pulse width are still unknown (Q3).
- Each heater is well under 10 W (TBD).
- Power architecture: the EPS supplies `VBAT_HOT` with a 4 A OCP. `VBAT_HOT` (from J3, D3 TVS) → U19 upstream eFuse (3.6 A, set to trip before the EPS) → `VBAT_PROT` → heaters, LT3065 `+12V`, and the discharge (U5) and igniter (U7) branch eFuses. The undervoltage cutoffs are staged and intentional: the branches trip at 12.75 V and the upstream at 12.0 V, both before the EPS cutoff, which protects the pack and sheds the thruster first.
- The CDH connector is the vertical (MS) part.
- Only 2 P/T sensors (tank, frit) are fitted; a third may be added later.
- Op amps with heavy-ion DSEE test heritage (OP27/OP37 family) are preferred over RRIO parts that lack it. Clamp the outputs to the ADC range instead. The heavy-ion-tested LMP2022 is **not** suitable for the Rogowski or other fast paths: it's auto-zero, slew-limited to ≈ 40 mV/µs, and TI recommends closed-loop bandwidth under 20 kHz (LMP202x datasheet §8.1.5). It's fine for slow sensors only.
- `/~{PD_RST}` (PA12) is the shared active-low reset for the Rogowski integrator and the igniter peak detectors. The igniter board inverts it (Q5 → `/IGNITER_nRST`) to drive its peak-detector reset FETs.
- **BOM policy:** every resistor whose value sets a threshold, gain, current limit or timing uses **0.1% tolerance**. That includes UV/OV, ISET, FB_PG and FLB dividers, regulator feedback (R17/R18, R75), IMAX (R2), the Rogowski network and V<sub>REF</sub> divider, NTC bias and pulse-timing resistors. Generic pull-ups, pull-downs and series/gate resistors (e.g. 10k/100k pull-downs, 100 Ω series) can stay at standard tolerance. Current-sense resistors are 1%.
- Discharge branch load (`VBAT_DCHG`, J12): 20 W average, flat, as 0.8 s on / 0.2 s idle per second. That's ≈ 25 W while drawing: 1.5 A at 16.8 V, up to 2.1 A at the 12 V cutoff.
- Rogowski sensor: the flight unit flies only the PEM CWTMini50HF/150 coil head, with the PIB integrating. The complete CWT transducer (0.2 mV/A, ±6 V at 30 kA) is ground calibration equipment.
- Discharge current is a damped ring: 16.26 kA peak, about 24 kHz, about 65 µs long, bidirectional.

| Category | Items |
|---|---|
| Critical | 2 (1 optional, 1 verify) |
| Significant | 4 |
| Footprint / Part | 7 |
| Project setup | 2 |
| ICD / Block diagram | 8 |
| Open questions | 2 |

---

## Critical (safety or function)

### C6 — Igniter-board ADC inputs are unclamped — `OPTIONAL`
- **Where:** `ADC_V_IGN_PK`, `ADC_I_IGN_PK`, `ADC_V_IGN_SUPPLY`, `ADC_NTC_IGN` (J5): each has only a PESD3V3U1UL, with no series R.
- **Risk:** Nominal signals are 0 to <3.3 V. However, the igniter-board op amps run on ±5 V, so a fault, overrange, power-sequencing event or HV arc can push an output to a rail, and the ESD diodes are not continuous clamps.
- **Hardening:** Use the same scheme as PA0/PA1: series R ≈ 1k plus a BAT54 pair to +3.3V/GND at each input. These are slow channels (peak-detect, NTC, supply), so the delay doesn't matter.

### C11 — GSE latching-valve pulse circuit: pulse width and polarity checks — `VERIFY`
- **Where:** Valve Driver sheet (working copy).
  - **Input:** J1.2 `/OPEN/~{CLOSE}` (D5 ESD) → R46 (1k) → R45 (100k to GND) → U18 gate 2 (74LVC2G14) → ~OPEN. Gate 1 re-inverts that to a buffered OPEN.
  - **OPEN edge:** OPEN → C73 (1 µF) → RC node (R47 100k, D41 clamp) → U9 2A (74LVC2G17) → D47 (BAT54C) → U3 **INB**, shared with flight `VALVE3_H`.
  - **CLOSE edge:** ~OPEN → C74 (1 µF) → RC node (R107 100k, D42 clamp) → U9 1A → D48 → U3 **INA** (`VALVE3_L`).
- **Checks:**
  1. **Polarity:** confirm on the bench that INB (`VALVE3_H`) drives the valve open, for both GSE and flight.
  2. **Pulse width:** t ≈ τ·ln(3.3/V<sub>T−</sub>), with τ = 100 ms and V<sub>T−</sub> the 74LVC2G17's negative-going threshold. A Schmitt V<sub>T−</sub> is below V<sub>CC</sub>/2, so even with C −20% the pulse is ≥ 0.8 × 100 ms × ln 2 ≈ **55 ms**. That's above the LHLA1221211H's 10–30 ms max response, so the margin looks adequate. When the Lee datasheet arrives, confirm the minimum pulse width (Q3). The 100 ms pulse spends several times the 5.5 mJ rating per switch, which is trivial at GSE command rates. BAT54J leakage at the RC nodes shortens the pulse when hot, which is negligible at room-temperature GSE use.
  3. **No UVLO gating:** at GSE power-up, U18's ~OPEN output rises with `3.3V_EGSE` and couples through C74, so expect a CLOSE pulse at reduced `+12V`. GSE procedure: send an explicit CLOSE once powered, and space commands ≥ ~2× the pulse width. Overlapping pulses brake the coil, which is safe.

---

## Significant

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
- **Check:** latching-valve operation at about 11.4 V (Q3). The NC valve is rated 12 V and sees ≈ 12.3 V nominal. Cold is a thermal question: the LHLA1221211H is rated to 4 °C minimum operating.

### S16 — DPT MCLK timer not configured in firmware — `OPEN` (firmware)
- `/DPT_MCLK` is driven from PC7, and `firmware.ioc` assigns PC7 to `TIM3_CH2` with TIM3 clocked at 64 MHz (HSE 8 MHz via PLL). The prescaler, period and OC mode are at CubeMX defaults: OC "Frozen" doesn't toggle the pin, and the default period gives about 488 Hz.
- **Fix:** Either set OC mode to Toggle with ARR = 976 (32.75 kHz), or use PWM mode 1 with ARR = 1952 and CCR = 976 (32.77 kHz). Both are within the MS5540C's 30–35 kHz range.

---

## Footprints & Part Numbers

Pin-to-pad check: every symbol pin maps to a footprint pad (custom DFN-8/10_DD, SOT95P280X145-5N, HARWIN M80, ABM3B, PowerPAK SO-8, SOD-323). MIC4424 pads 1/8 are NC, which is correct. SP0502BAJT on SC-70 is correct.

### F1 — No MPNs or capacitor ratings — `OPEN`
- **MPN / Manufacturer:** no part has them. U1 alone carries vendor-import fields (`MP`, `MF`), named differently from the rest. Add `MPN` and `Manufacturer` project-wide.
- **Capacitors:** none has a voltage or dielectric rating (see F2).
- **Tolerance (partly done):** `0.1%, 25ppm/C` is now set on the NTC biases (R3, R4, R78, R87, R109) and on every eFuse UV/OV, FB_PG and FLB divider. Per the BOM policy, these still need it:
  - ISET dividers: R122/R123 (U5), R124/R125 (U19)
  - LT3065: R17/R18 (feedback), R2 (IMAX)
  - LT8580 feedback: R75
  - Rogowski: R79, R98, and the V<sub>REF</sub> divider R96/R97
  - Pulse timing: R47, R107
  - Current-sense: R35, R38, R65 at 1%

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
- Q9/Q10/Q11: symbol `IRF7403`, value IRF7832. The Datasheet field still links the IRF7403.
- U14/U15: symbol `74LS08` (DIP footprint filter), value 74HC08PW. The Datasheet field still links the SN74LS08.
- The pinouts match, but the description and datasheet fields will mislead BOM and assembly.

### F7 — Flight connector retention — `OPEN`
- PicoBlade (J2, J4, J5, J9, J10, J11, J13, J15) is friction-lock only, which is a vibration risk. The DPT board already uses latching Pico-Lock. Consider Pico-Lock, CLIK-Mate, or Gecko.

### F8 — Flight population — `OPEN`
- SW1 (SKRKAHE020 tactile switch) and J8 (debug header): decide flight population. A DNP SW1 on flight units is typical.
- **JP1 (`WD_ENABLE`) must be bridged for flight.** It is a normally-open solder jumper between `/WDG_IN` (PB2, R76) and MAX823 WDI (U12.4). While open, WDI floats and the watchdog is disabled; MAX823 POR supervision and the SW1 reset still reach NRST. Add it to the assembly notes and the flight build checklist.


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
| Q3 | Latching valve datasheet (LHLA1221211H): coil resistance, minimum pulse width, and operation at ~11.4 V. These confirm the C11 pulse and the D36 margin. | Part selected from Lee's catalog: 12 V, 5.5 mJ/switch, 10–30 ms max response. The energy bound gives ≲ 50–230 mA. The symbol library has LHLA1252211H/LHLA1252411H, not the selected part. |
| Q8 | Flight Rogowski coil (PEM CWTMini50HF/150 coil head): confirm the precise open-circuit M (offset-corrected integral at the trough ÷ CT peak). **After the BNC → MMCX re-termination, re-measure the coil's DC resistance and M through the final cable and connector.** The integrator gain depends on R79 + Z<sub>s</sub>. | **Measured 2026-10-19** (380 µF from 200 V via a shorting stick, CT reference): open-circuit **M ≈ 1.7 nH (±10%)** from ∫V<sub>coil</sub>dt vs CT and from the onset step. **Coil resistance 154 Ω DC.** A 50 Ω load attenuated the signal ≈ 3–4×, resistively. The steps in dI/dt at ≈ 41 and 84 µs come from the shorting-stick arc. The transducer trough read ≈ 10–20% above the CT (rough read); check the CT scaling. Full test record: `Rogowski_Coil_Characterization.md`. |

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
- **D37:** NC-valve flyback diode, oriented correctly. Valve closing is slow with a plain freewheel diode; add a series zener only if closing time matters.
- **OR-ing / flyback Schottkys (D11, D36, D37 = MBR0560, 60 V, 0.5 A, SOD-123):**
  - **Voltage:** V<sub>R</sub> ≤ `+12V`. The GSE 12 V is regulated and comes over 15–30 cm of harness, so the only credible transient is hot-plug ringing: a few µs, well damped, peaking in the high teens. 60 V covers that, and so does any D10 (SMF14A) clamp level.
  - **D11:** I<sub>MAX</sub> 300 mA, ≈ 0.14 W.
  - **D36:** NC valve 50 mA continuous, plus latching pulses. 5.5 mJ per switch over a ≥ 10 ms actuation means ≲ 0.55 W, so ≲ 50 mA at 12 V. Even a 2 ms pulse would be ≈ 230 mA, well under 0.5 A.
  - The symbol datasheet is MCC's MBR0560, which isn't AEC-Q101. Pick an automotive-grade source if that matters.
- **Rogowski reset switch (Q3/Q4/Q5 = BSS131):** Infineon specifies I<sub>D(off)</sub> ≤ 10 nA at 240 V, 25 °C (5 µA at 150 °C).
  - **Offset:** × R98 (1 M) that's ≤ 10 mV at 25 °C. Interpolated bounds at 240 V are ~20 mV at 40 °C and ~60 mV at 60 °C. At the real ≈ 1.5 V V<sub>DS</sub> it's far lower, and only one device leaks in the back-to-back pair. The offset is static and removed by the pre-shot baseline.
  - **Gate drive:** off-state V<sub>GS</sub> ≈ −1.65 V. On-state, the gates get ≈ 3.3 V above the V<sub>REF</sub>-side node, vs a 1.8 V max V<sub>th</sub>. Reset settles in ≈ 1 µs (2 × R<sub>DS(on)</sub> · C45).
  - **Q5** from the 3.3 V GPIO pulls the gates to ≈ tens of mV against R99.
  - **Pinout:** SOT-23, G 1 / S 2 / D 3.
  - **Firmware:** hold reset through power-up (C69 charges in ≈ 10 ms), release just before the shot, then take a baseline. That removes leakage × R98, op-amp bias offset and the ≈ 10–20 mV reset-release step.
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
- **DPT harness (J9/J10 ↔ DPT J1):** the DPT board's J1 is now PicoBlade 53261-0671: 1 MCLK, 2 DIN, 3 DOUT, 4 SCLK, 5 GND, 6 3V3. That is the exact mirror of PIB J9/J10 (1 3V3, 2 GND, 3 SCK, 4 DOUT, 5 DIN, 6 MCLK), so a standard mirrored PicoBlade cable (pin n ↔ 7 − n) matches pin-for-pin, the same convention as J5. Directions check out: sensor DOUT → `TANK/FRIT_DOUT` → U16 mux input; U13 → DIN (DPT board at `8c08317`).
- **BAT54C (D47/D48/D50):** the stock `Diode:BAT54C` symbol has anodes on pins 1/2 and the common cathode on pin 3, matching the Diodes Inc. SOT-23 pinout. The OR wiring (anodes from the GSE and flight sources, cathode to INA/INB or R110) is correct.
- **Si7489DP symbol (Q1/Q2/Q6):** now `0_PAY_Symbol_Library:Si7489DP` (−100 V, P-channel): S = 1–3, G = 4, D = 5. The `PowerPAK_SO-8_Single` footprint numbers all five drain pads "5", so every drain pad carries the output net on the PCB.
- **Crystal:** `ABM3B-8.000MHZ-10-D1G-T` is C<sub>L</sub> = 10 pF (distributor listings), so C26/C27 = 12 pF with ≈ 4 pF stray is right. Trim after measuring frequency on the first board.
- **Libraries:** every symbol resolves (no ERC library-mismatch warnings), all 37 PCB 3D-model paths resolve, and PCB ↔ schematic parity is clean.
