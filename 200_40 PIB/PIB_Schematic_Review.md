# 200_40 PIB — Schematic & Project Review (Living Document)

| | |
|---|---|
| **Board** | 200_40 Payload Interface Board (APEx PMPD thruster, 6U CubeSat) |
| **Reviewed files** | `PIB.kicad_sch`, `Rogowski.kicad_sch`, `Valve Driver.kicad_sch`, `DPT Mux.kicad_sch`, `PIB.kicad_pcb` (parity only), `firmware/firmware.ioc` (pin/clock config) |
| **Scope** | The PIB (200_40) only. The igniter board (200_80) is a boilerplate design and out of scope; only the PIB side of the J5 interface is reviewed. |
| **Reference docs** | `Payload ICD.pdf` (Rev 2.1, 10/22/2026, working copy), `PIB Block Diagram.drawio.png`, `200_60 DPT Sensor Board`, `Rogowski_Coil_Characterization.md` (coil measurements and integrator sizing) |
| **Datasheets used** | In `200_40 PIB/`: ADM1270 (Rev. A; timer, foldback and ISET figures) and Si7489DP (SOA and thermal impedance). In the repo root: LT3065 (Rev. F). Also LMP202x (TI SNOSAY9G, Rev. G) and STM32G431 (DS12589 Rev 6; ADC clock, R<sub>AIN</sub> and injection limits), which are not in the repo. |
| **Design state** | branch `pib-split`, `2fd37c5` plus working-copy PCB routing of U6/R43/R44 (2026-10-21) |
| **PCB** | Routed. Schematic parity is clean, with 0 unconnected pads (2026-10-21, zones refilled). DRC shows 61 silkscreen warnings only: 58 `silk_overlap`, mostly the B.Silk outlines of the ESD diodes, and 3 `silk_over_copper` at U5, U7 and U9. Before release: clean up the silkscreen, then regenerate the fab outputs and the iBOM. |
| **Tools** | KiCad 10.0 (`kicad-cli` ERC, netlist, DRC w/ schematic parity). To re-run: point `KICAD_CONFIG_HOME` at a scratch directory whose `10.0/` holds only the stock `sym-lib-table`/`fp-lib-table` (from KiCad's `share/kicad/template/`), so library warnings are real; run `kicad-cli sch erc --severity-all PIB.kicad_sch`; and compare `kicad-cli sch export netlist` output between revisions (`git archive` the old one) to see exactly which parts and nets changed. |
| **Last updated** | 2026-10-21 |

**Status key:** `OPEN` (needs a design change) · `VERIFY` (needs a datasheet, bench or part check) · `OPTIONAL` (recommended hardening, not required) · `ICD` (schematic is correct; the ICD must change)

**How to use:** This document lists only issues present in the current KiCad design. When an issue is resolved, delete it. Item IDs are stable, so gaps in the numbering are expected. Don't renumber.

---

## Summary

The pin-to-pad check is clean for every footprint. The main risks are:
- No critical items are open. Bench and datasheet confirmations are tracked under Open Questions (Q3, Q9).

The BOM is not production-ready (no MPNs or capacitor ratings). Several ICD items need to be brought in line with the schematic (see [ICD Update Actions](#icd-update-actions)).

**Design context:**
- UNP NS requirements apply. Launch inhibits live in EPS, so the PIB and CDH are fully unpowered until after deployment.
- HV boards (igniter 1 kV, discharge 400 V) scale all signals to 0 to <3.3 V with op amps on ±5 V.
- GSE OPEN/~CLOSE (J1.2) is a 3.3 V digital logic line.
- The NC valve (valve 2) is a Lee LHDB1223518H: non-latching, normally closed, rated for continuous 12 V DC (not spike-and-hold), about 600 mW, so about 50 mA (≈ 240 Ω). It is held open continuously during burns and GSE fills.
- The latching valve (valve 3) is a Lee LHLA1221211H: 12 V DC, face mount, FKM seal, 5.5 mJ per switch, 10–30 ms max response, operating range 4–49 °C, coil max 107 °C. It needs reversed polarity to close (U3 H-bridge). The coil resistance and the required pulse width are still unknown (Q3).
- **Valve 3 polarity convention:** U3 INB (`VALVE3_H`, OUTB → J4.3) opens the valve, and INA (`VALVE3_L`, OUTA → J4.4) closes it. This applies to both flight and GSE. Confirm at bench bring-up; if it's reversed, swap the coil leads in the J4 harness rather than changing firmware or the GSE wiring.
- Each heater is well under 10 W (TBD).
- Power architecture: the EPS supplies `VBAT_HOT` with a 4 A OCP. `VBAT_HOT` (from J3, D3 TVS) → U19 upstream eFuse (3.6 A, set to trip before the EPS) → `VBAT_PROT` → heaters, LT3065 `+12V`, and the discharge (U5) and igniter (U7) branch eFuses. The undervoltage cutoffs are staged and intentional: the branches trip at 12.75 V and the upstream at 12.0 V, both before the EPS cutoff, which protects the pack and sheds the thruster first.
- The CDH connector is the vertical (MS) part.
- Only 2 P/T sensors (tank, frit) are fitted; a third may be added later.
- Op amps with heavy-ion DSEE test heritage (OP27/OP37 family) are preferred over RRIO parts that lack it. Clamp the outputs to the ADC range instead. The heavy-ion-tested LMP2022 is **not** suitable for the Rogowski or other fast paths: it's auto-zero, slew-limited to ≈ 40 mV/µs, and TI recommends closed-loop bandwidth under 20 kHz (LMP202x datasheet §8.1.5). It's fine for slow sensors only.
- `/~{PD_RST}` (PA12) is the shared active-low reset for the Rogowski integrator and the igniter peak detectors. The igniter board inverts it (Q5 → `/IGNITER_nRST`) to drive its peak-detector reset FETs.
- **BOM policy:** every resistor whose value sets a threshold, gain, current limit or timing uses **0.1% tolerance**. That includes UV/OV, ISET, FB_PG and FLB dividers, regulator feedback (R17/R18, R75), IMAX (R2), the Rogowski network and V<sub>REF</sub> divider, NTC bias and pulse-timing resistors. Generic pull-ups, pull-downs and series/gate resistors (e.g. 10k/100k pull-downs, 100 Ω series) can stay at standard tolerance. Current-sense resistors are 1%.
- Discharge branch load (`VBAT_DCHG`, J12): 20 W average, flat, as 0.8 s on / 0.2 s idle per second. That's ≈ 25 W while drawing: 1.5 A at 16.8 V, up to 2.1 A at the 12 V cutoff.
- Rogowski sensor: the flight unit flies only the PEM CWTMini50HF/150 coil head, with the PIB integrating. The complete CWT transducer (0.2 mV/A, ±6 V at 30 kA) is ground calibration equipment.
- Discharge current is a damped, bidirectional ring. An early scope shot (over a year old) showed ≈ 16 kA peak, ≈ 24 kHz and ≈ 65 µs. The thruster is experimental, so treat these as indicative only, not design inputs.

| Category | Items |
|---|---|
| Critical | 0 |
| Significant | 1 |
| Footprint / Part | 3 |
| Project setup | 2 |
| ICD / Block diagram | 1 |
| Open questions | 3 |

---

## Critical (safety or function)

_None open._

---

## Significant

### S14 — Non-ARM 74HC08 inputs float at reset — `OPTIONAL`
- R88 holds `/ARM/~{SAFE}` low, so all eight gated outputs are low during reset. The other inputs (`IGNITER_EN`, `DISCHARGE_EN`, `IGNITER_PULSE`, `VALVE1_L/H`, `VALVE2_EN`, `VALVE3_L/H`) float while the STM32 pins are high-Z.
- That's safe, but floating HC inputs can draw extra supply current or oscillate. Add 100k pull-downs if layout space allows.

---

## Footprints & Part Numbers

Pin-to-pad check: every symbol pin maps to a footprint pad (custom DFN-8/10_DD, SOT95P280X145-5N, HARWIN M80, ABM3B, PowerPAK SO-8, SOD-123). MIC4424 pads 1/8 are NC, which is correct. SP0502BAJT on SC-70 is correct.

### F5 — Symbol, value and datasheet mismatches — `OPEN`
- **Symbol ≠ part:** Q9–Q11 use the `IRF7403` symbol for an IRF7832. U14/U15 use `74LS08` (DIP filter) for a 74HC08PW. Q1/Q2/Q6 use the `Si7489DP` symbol for an SQJ457EP, and Q3–Q5 use `BSS131` for a BSS123N. D17/D39–D44/D55–D59 use `BAT54J` for a BAT54WS. The pinouts all match.
- **Stale datasheet links:** Q1/Q2/Q6 → si7489dp.pdf, Q3–Q5 → BSS131, the BAT54WS parts → Nexperia BAT54J, D3/D10/D13/D14 → Vishay SMF (the parts are Littelfuse), D11/D36/D37 → MCC MBR0560 (the part is Slkor), D38 → Littelfuse SD-C (the part is Semtech SD12C), Q9–Q11 → IRF7403, U14/U15 → SN74LS08.
- **Fix:** update the `Datasheet` field to each MPN's datasheet, or swap in matching symbols. Optional cleanup: delete the `KLC_S3.3`/`KLC_S4.1` fields (318 parts) so they don't appear in BOM exports.

### F7 — Flight connector retention — `OPEN`
- PicoBlade (J2, J4, J5, J9, J10, J11, J13, J15) is friction-lock only, which is a vibration risk. The DPT board's J1 (53261-0671) is PicoBlade too, so changing J9/J10 means changing the DPT side as well. Consider Pico-Lock, CLIK-Mate, or Gecko.

### F8 — Flight population — `OPEN`
- SW1 (SKRKAHE020 tactile switch) and J8 (debug header): decide flight population. A DNP SW1 on flight units is typical.
- **JP1 (`WD_ENABLE`) must be bridged for flight.** It is a normally-open solder jumper between `/WDG_IN` (PB2, R76) and MAX823 WDI (U12.4). While open, WDI floats and the watchdog is disabled; MAX823 POR supervision and the SW1 reset still reach NRST. Add it to the assembly notes and the flight build checklist.

---

## Project Setup

### P3 — Empty title blocks — `OPEN`
- None of the 6 sheets (including the two load-switch sheets) has a title, revision, date or company.

### P4 — ERC cleanup — `OPEN`
- With stock libraries loaded (KiCad 10.0, 2026-10-21): 13 messages:
  - 11× `power_pin_not_driven`: needs PWR_FLAGs.
  - 1× `pin_not_connected`: U4 PC15. Add a no-connect flag or use it.
  - 1× `lib_symbol_mismatch`: C40 (`Device:C_Small`) differs from the installed library copy, most likely a library update. Run "Update symbol from library" on C40.

---

## ICD / Block Diagram Discrepancies

| ID | Item | ICD / Block Diagram | Schematic | Status |
|---|---|---|---|---|
| D13 | Block diagram (`PIB Block Diagram.drawio.png`) | Unchanged since before the review. It shows BAT_HOT feeding the 12 V regulator, both ADM1270s and the heater switches directly; a "Valve GSE Bypass" with its own H-bridge and 12 V NMOS; and only the 2 tank thermistors | An upstream ADM1270 (U19) sits between BAT_HOT and `VBAT_PROT`, which feeds the LT3065, U5, U7 and the heater switches Q9/Q11. The GSE path is EGSE-only logic (ADP7142 UVLO, Schmitt edge-pulse U18/U9) diode-OR'd into the shared U3 and Q10, with `GSE_12V` diode-OR'd into `12V_INT` (D36/D11). The PIB, igniter-board and discharge-board NTCs are also read, for 10 external ADC inputs in total | `ICD` — update the drawing: add U19/`VBAT_PROT`, redraw the GSE bypass as logic into the shared drivers, add the 3 extra thermistors |

### ICD Update Actions

Checklist of ICD edits the review has generated. Tick these off as the ICD is revised.

- [ ] **Block diagram (D13):** add the U19 upstream eFuse and `VBAT_PROT`, redraw the GSE bypass as EGSE logic diode-OR'd into the shared drivers, and add the PIB, igniter and discharge thermistors. The ICD text itself is complete as of Rev 2.1.

---

## Open Questions

| ID | Question | Known so far |
|---|---|---|
| Q3 | Latching valve datasheet (LHLA1221211H): coil resistance and minimum pulse width. These confirm the GSE pulse (63–205 ms) and the D36 margin. (Low-voltage operation is settled: both valves bench-tested working at 10 V, 2026-10-21.) | Part selected from Lee's catalog: 12 V, 5.5 mJ/switch, 10–30 ms max response. The energy bound gives ≲ 50–230 mA. The symbol library has LHLA1252211H/LHLA1252411H, not the selected part. |
| Q8 | Flight Rogowski coil (PEM CWTMini50HF/150 coil head): confirm the precise open-circuit M (offset-corrected integral at the trough ÷ CT peak). **After the BNC → MMCX re-termination, re-measure the coil's DC resistance and M through the final cable and connector.** The integrator gain depends on R79 + Z<sub>s</sub>. | **Measured 2026-10-19** (380 µF from 200 V via a shorting stick, CT reference): open-circuit **M ≈ 1.7 nH (±10%)** from ∫V<sub>coil</sub>dt vs CT and from the onset step. **Coil resistance 154 Ω DC.** A 50 Ω load attenuated the signal ≈ 3–4×, resistively. The steps in dI/dt at ≈ 41 and 84 µs come from the shorting-stick arc. The transducer trough read ≈ 10–20% above the CT (rough read); check the CT scaling. Full test record: `Rogowski_Coil_Characterization.md`. |
| Q9 | GSE latching-valve pulse bring-up check (not design-blocking). Scope `+12V` and U3 INA at GSE power-up, and confirm a single CLOSE pulse with `+12V` ≥ 10 V. Check that OPEN/CLOSE commands from J1.2 each give one pulse of the expected polarity (INB opens). Confirm whether the 10 V valve test was measured at the coil or at the MIC4424 supply. If it was at the coil, the bridge drop raises the floor needed at `12V_GSE` (coil current, Q3). | The design analysis is complete (Verified OK, *GSE latching-valve pulse*). The pulse is 63–205 ms. The UVLO turns on at 10.43–11.81 V on `12V_GSE` with 0.1% R43/R44. Both valves were bench-tested working at 10 V on 2026-10-21. |

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
- **Integrator V<sub>REF</sub>:** R96/R97 (10k/10k) divide `+3.3V`, not VDDA, a layout choice. C70 (1 µF) gives a ≈ 32 Hz corner, so digital noise is filtered. The DC difference from VDDA is the FB1 drop (≪ 1 mV), which the pre-shot baseline removes. It still tracks the ADC reference, because both come from the same 3.3 V source.
- **J7 protection:** D38 is a Semtech SD12C.TCT (DigiKey SD12C.TCTCT-ND), bidirectional with a 12 V standoff, SOD-323. Pad 1 is on GND, pad 2 on the coil node at J7.1 ahead of C69 stays off at the ≈ 5–6 V coil peak. R95 (100k) defines DC with the coil unplugged and costs ≈ 0.15% against the coil's 154 Ω.
- **PA0/PA1 front ends:** Each channel has 100 Ω (R106 / R102) → BAT54J pair to +3.3V/GND (D43/D44, D39/D40) → 1k (R105 / R104) → pin with 470 pF (C72 / C71). Orientation is correct, and the matched RCs (f<sub>c</sub> ≈ 339 kHz, ≈ 0.47 µs, −0.02 dB / −4° at 24 kHz) keep the V×I timing aligned. Injection in a fault is ≪ 0.1 mA.
- **Valve supply at low battery:** the LT3065 regulates 12.6 V (R18 20k / R17 1k), so `+12V` ≈ 12.3 V after D11 while BAT ≳ 12.9 V. Below that it's in dropout, and `+12V` ≈ BAT − 0.6 V, about 11.4 V just before U19's 12.0 V UV cutoff. In GSE mode, `+12V` = `12V_GSE` − V<sub>F</sub>(D36). **Both valves were bench-tested working at 10 V (2026-10-21)**, below that worst case. Cold is a separate thermal limit: the LHLA1221211H is rated to 4 °C minimum operating.
- **Igniter ADC front ends (J5):** the three op-amp-driven channels each have J5 ESD diode → 1k series R → BAT54J pair to +3.3V/GND → MCU pin. Diode orientation is correct (K to +3.3V / K to the node):

  | Channel | ESD | Series R | Clamp (+3.3V / GND) | Clamped net → pin |
  |---|---|---|---|---|
  | `ADC_I_IGN_PK` (J5.4) | D24 | R130 1k | D56 / D57 | `ADC_I_IGN_PK_C` → PC2 (ADC1_IN8, slow) |
  | `ADC_V_IGN_PK` (J5.5) | D25 | R131 1k | D58 / D59 | `ADC_V_IGN_PK_C` → PB14 (ADC1_IN5, fast) |
  | `ADC_V_IGN_SUPPLY` (J5.6) | D26 | R129 1k | D55 / D17 | `ADC_V_IGN_SUPPLY_C` → PA3 (ADC1_IN4, fast) |

  - **Fault case:** an igniter op amp stuck at −5 V pushes ≈ 4.5 mA through the GND-side BAT54J (V<sub>F</sub> ≈ 0.3–0.4 V), so the pin stays near the −0.3 V absolute max and well within the −5 mA injection limit. At +5 V, ≈ 1.3 mA flows into +3.3V, which is negligible.
  - `ADC_NTC_IGN` (J5.3 → PC3) is deliberately unclamped. It's a passive NTC against R78 (from VDDA) with C44, not an op-amp output.
  - **Firmware:** these channels need a sample time of ≥ 47.5 cycles (see S17).
- **GSE pulse circuit:** J1.2 is isolated by R46 (1k) with R45 pull-down. U18 buffers both phases, so no RC cap loads the GSE line. U18 (74LVC2G14) and U9 (74LVC2G17) are Schmitt parts with I<sub>off</sub>. D41/D42 clamp the RC nodes to GND (orientation correct). D47/D48 (BAT54C) diode-OR flight and GSE into U3 INA/INB with R41/R42 pull-downs, so there's no back-power in either direction on valve 3. U9 and U18 are decoupled (C31, C75).
- **IGNITER to CDH:** U8 (74AHC1G125, OE tied to GND, decoupled by C77) buffers `IGNITER_PULSE_GATED` onto J3.22 through R29.
- **PIB thermistor:** TH1 10k NTC / R109 10k from VDDA / C76 100 nF on PB1 gives ≈ VDDA/2 at 25 °C, ratiometric with the ADC reference.
- **J8 / reset:** J8.5 = +3.3V VTref through R108 (1k), which is short-protected. MAX823 ~RESET drives NRST directly (R77 pull-up), so SW1 is always a hardware reset; use a manual connect-under-reset for recovery. JP1 in the WDI line disables only the watchdog. J8 ESD: D51–D54 (PESD3V3U1UL) sit on the connector side of R20/R21 (SWD, 100 Ω) and R80/R81 (UART, 22 Ω), oriented K-to-signal.
- **ICD Rev 2.1 (working copy, 10/22/2026):** closes D1, D2, D4, D5, D6, D9, D10 and D12. The ICD now has: CDH connector G125-MS12605M2P; EGSE pinout 1 = 12V, 2 = OPEN/CLOSE, 3/4 = GND; 2 P/T sensors; igniter peak V, peak I and supply V; 5 thermistors plus MCU temperature; the Table 3 protection setpoints with auto-retry; the EPS launch-inhibit statement; and the GSE rules (12–13 V supply, UVLO 9.6 V / 11.8 V, 63–205 ms pulses, transitions > 205 ms apart, state-matching power-up pulse, no OPEN/CLOSE high before power, line low ≥ 205 ms before removing 12 V). 205 ms is the worst-case pulse over C ±20% and the 74LVC2G17 V<sub>T−</sub> range at −40 to 85 °C, so it's an adequate bound for room-temperature GSE use.
- **ICD power-interface update (`Payload ICD.pdf`, working copy 2026-10-21; closes D3 and D11):**
  - The 5 V use (gate drive plus op amps with the internal −5 V) and the heaters on BAT are now stated.
  - Every Table 3 setpoint was checked against the dividers and ADM1270 Rev. A (UV/OV threshold 0.985/1.0/1.015 V, UV hysteresis 55/60/65 mV, OV hysteresis 25/30/35 mV):

    | Bus | UV / OV divider | UV off / on (min/typ/max) | OV off / on | Current limit |
    |---|---|---|---|---|
    | Main (U19) | R111 33k / R112 1k / R113 2k: UV ÷12, OV ÷18 | 11.82/12.00/12.18 V; 12.48/12.72/12.96 V | 17.73/18.00/18.27 V; 17.10/17.46/17.82 V | 36 mV / R38 10 mΩ = 3.6 A |
    | Discharge, igniter (U5, U7) | R50 158k / R49 3.92k / R48 9.53k: UV ÷12.75, OV ÷17.99 | 12.56/12.75/12.94 V; 13.26/13.51/13.77 V | 17.72/17.99/18.26 V; 17.09/17.45/17.81 V | 3.2 A (R35 10 mΩ); 0.5 A (50 mV / R65 100 mΩ) |

  - The listed current-limit spreads (≈ ±10%) are reasonable for the ADM1270 sense accuracy at 36/32/50 mV plus 1% sense resistors. Main max 3.95 A stays under the EPS 4 A OCP. Divider tolerance isn't included; it's negligible with the 0.1% parts in F1.
- **ADC configuration (`firmware.ioc`, 2026-10-21):**
  - **Clock:** synchronous HCLK/2 = 32 MHz, under the 52 MHz limit with both ADCs running (DS12589 Table 60).
  - **Burst:** ADC1 (master, IN1/PA0) and ADC2 (slave, IN2/PA1) in dual regular simultaneous mode. Continuous, 2.5 cycles, 12-bit → **2.133 MSPS** per channel with V and I sampled together. DMA access mode 12/10-bit, so each V/I pair packs into one word (ADC1 in [15:0], ADC2 in [31:16]). DMA1_Channel1: Normal, Word/Word, Very High, IRQ on, DMAMUX sync off. `MX_DMA_Init` runs before `MX_ADC1_Init`. Capture length = N passed to `HAL_ADCEx_MultiModeStart_DMA`; set it from current thruster data. An exact 2.000 MSPS timer trigger doesn't fit at 32 MHz (needs t<sub>CONV</sub> + t<sub>LATR</sub> = 539 ns).
  - **Slow channels:** ADC1 injected group, 1 rank, software start. Firmware re-points rank 1 for each read, and only between bursts, since sample time can't change mid-conversion. At 32 MHz:

    | Pin | ADC1 channel | Net | Source at pin | Sample time | t<sub>S</sub> |
    |---|---|---|---|---|---|
    | PA3 | IN4 (fast) | `ADC_V_IGN_SUPPLY_C` | R129 1k series, BAT54J clamps, no pin cap | `ADC_SAMPLETIME_92CYCLES_5` | 2.9 µs |
    | PB14 | IN5 (fast) | `ADC_V_IGN_PK_C` | R131 1k series, BAT54J clamps, no pin cap | `ADC_SAMPLETIME_92CYCLES_5` | 2.9 µs |
    | PC2 | IN8 (slow) | `ADC_I_IGN_PK_C` | R130 1k series, BAT54J clamps, no pin cap | `ADC_SAMPLETIME_92CYCLES_5` | 2.9 µs |
    | PC3 | IN9 | `ADC_NTC_IGN` | R78 10k bias, C44 100 nF | `ADC_SAMPLETIME_92CYCLES_5` | 2.9 µs |
    | PB12 | IN11 | `ADC_NTC_DCHG` | R87 10k bias, C61 100 nF | `ADC_SAMPLETIME_92CYCLES_5` | 2.9 µs |
    | PB1 | IN12 | `ADC_NTC_PIB` | R109 10k / TH1, C76 100 nF | `ADC_SAMPLETIME_92CYCLES_5` | 2.9 µs |
    | PB11 | IN14 | `ADC_TANK_NTC_1` | R4 22k bias, C19 100 nF | `ADC_SAMPLETIME_92CYCLES_5` | 2.9 µs |
    | PB0 | IN15 | `ADC_TANK_NTC_2` | R3 22k bias, C20 100 nF | `ADC_SAMPLETIME_92CYCLES_5` | 2.9 µs |
    | — | TEMPSENSOR_ADC1 | internal | t<sub>S_temp</sub> ≥ 5 µs (DS12589 Table 75) | `ADC_SAMPLETIME_247CYCLES_5` | 7.7 µs |
    | — | VREFINT | internal | t<sub>S_vrefint</sub> ≥ 4 µs (DS12589 Table 20) | `ADC_SAMPLETIME_247CYCLES_5` | 7.7 µs |

    - **Basis:** the 1k igniter channels need ≥ 47.5 cycles (DS12589 Table 61: R<sub>AIN</sub> ≤ 1.8k on slow channels). 92.5 cycles (3.9k slow / 4.7k fast) also covers series resistance on the igniter board. The NTC channels' 100 nF supplies the sampling charge, so 92.5 is a uniform default rather than a requirement there. Read V<sub>REFINT</sub> before and after each burst.
    - A full 10-channel pass takes ≈ 8 × 3.3 µs + 2 × 8.1 µs ≈ 43 µs. Optional: injected oversampling ×16 / shift 4 for less noise.
  - **Firmware:** calibrate both ADCs at start-up. After generation, confirm `hadc1.Init.ClockPrescaler` and `hadc2.Init.ClockPrescaler` are both `ADC_CLOCK_SYNC_PCLK_DIV2`; the ioc stores it only under ADC2.
- **DPT MCLK (TIM3_CH2 on PC7):** `firmware.ioc` sets OC mode Toggle with ARR (`PeriodNoDither`) = 976 and the default PSC = 0 and CCR = 0, on a 64 MHz timer clock (APB1 ×1). The pin toggles once per 977 counts, so f = 64 MHz / (2 × 977) = **32.75 kHz**, inside the MS5540C's 30–35 kHz range (0.05% from 32.768 kHz). **Firmware:** CubeMX only configures the timer. Call `HAL_TIM_OC_Start(&htim3, TIM_CHANNEL_2)` after `MX_TIM3_Init()`, before the first DPT read. If APB1 is ever divided, the timer clock stays at 2 × PCLK1, so re-check the frequency after any clock change.
- **GSE latching-valve pulse and UVLO (Valve Driver sheet):**
  - **Path:** J1.2 → R46 1k / R45 100k → U18 (74LVC2G14) makes OPEN and ~OPEN. OPEN → C73 1 µF / R47 100k → U9 2A (74LVC2G17) → D47 → U3 INB (`VALVE3_H`, opens). ~OPEN → C74 / R107 → U9 1A → D48 → U3 INA (`VALVE3_L`, closes). D41/D42 clamp negative edges. Each output is diode-OR'd with flight, with R41/R42 pull-downs.
  - **Pulse width:** τ·ln(3.3/V<sub>T−</sub>) ≈ **63–205 ms**, about 100 ms typical (V<sub>T−</sub> 0.60–1.50 V at 3 V for NXP; TI ≥ 0.8 V; C ±20%). That covers the LHLA1221211H's 10–30 ms response; its minimum pulse width is still to be confirmed (Q3).
  - **UVLO:** U6 ADP7142AUJZ-3.3 (VIN 1, GND 2, EN 3, SENSE 4 = VOUT 5) with EN from `12V_GSE` via R44 12.1k / R43 1.5k (÷9.07). EN thresholds: rising 1.15 / 1.22 / 1.30 V, falling 1.06 / 1.12 / 1.18 V. So the logic turns on at **10.43 / 11.06 / 11.79 V** (≈ 11.81 V worst case with 0.1% resistors, ≈ 12.0 V with 1%) and off at 9.61 / 10.15 / 10.70 V. R43/R44 must be 0.1% (F1).
  - **Power-up pulse:** as `3.3V_EGSE` comes up, a pulse fires that matches the J1.2 state (low or floating → CLOSE). It starts with `+12V` ≥ ≈ 10.0 V. A collapsing rail only makes negative edges, so no pulse fires on a normal power-down.
  - **Power removal can't make an OPEN pulse:** a pulse needs a rising edge on OPEN or ~OPEN. When `12V_GSE` is removed with J1.2 steady, OPEN is either already low and stays low, or it's high and falls with `3.3V_EGSE`. A falling edge drives the RC node negative, and D41/D42 clamp it. Below ≈ 1.65 V the LVC outputs are undefined, but U9's output can't exceed the collapsing rail, and after D47 that's well under the MIC4424's 2.4 V V<sub>IH</sub>, so no glitch can reach the driver. In GSE-only mode, `+12V` collapses too. In flight-powered mode, R41/R42 hold the MIC4424 inputs low. The only power-down pulse is the CLOSE that fires when J1.2 falls while `3.3V_EGSE` is still up (procedure below).
  - **Fast power cycling:** after removal, C73/C74 recover through R47/R107 (τ = 100 ms), and C3 may hold `3.3V_EGSE` up for a while under the µA logic load. Re-applying power within about 0.5–1 s can give a shorter or missing power-up pulse, so the explicit CLOSE after power-up still applies. If `12V_GSE` dips below the UVLO turn-off (≈ 10.15 V) and recovers while J1.2 is high, the restart fires an OPEN pulse that matches the commanded state.
  - **GSE procedure:**
    - Set the GSE supply to ≥ 12.2 V at J1.
    - Send an explicit CLOSE once powered.
    - Space OPEN/CLOSE commands ≥ 250 ms. Overlapping pulses brake the coil, which is safe.
    - Before unplugging J1, command the final state and switch the GSE supply off. If J1.2 falls while `3.3V_EGSE` is still up (≈ 20 ms on C2), a CLOSE pulse fires. In GSE-only mode it's energy-limited (≈ 2 mJ on the `+12V` capacitors), but with the PIB flight-powered it may close the valve. The GSE controller must hold J1.2 until its 12 V has collapsed.
- **BOM / MPN check (2026-10-24):** every part has an MPN (`MP`) and a DigiKey PN, and each package matches its footprint and pinout. The DigiKey PNs pin down the generic MPNs: BAT54C/BAT54WS → Diotec (SOT-23 / SOD-323F), MBR0560 → Slkor (5399-), SMF14A/SMF18A → Littelfuse SMF (SOD-123FL). KiCad `D_SMF` (DO-219AB pads 1.3 × 1.4 mm on 2.9 mm centres) covers the SOD-123FL land pattern. All threshold, timing and divider resistors are 0.1% (Yageo RT, KOA RN73, Panasonic ERA, TE RP73); current-sense parts are 1%. Capacitor ratings:
  - 1 µF on `VBAT_*`: 35 V (GMK107AB7105KAHT).
  - C2 `12V_GSE`: 25 V.
  - C6 (12.6 V) and C4 (≈ 10 V) keep ≈ 3–5 µF under bias, adequate.
  - C73/C74 (1 µF 6.3 V 0402) lose some capacitance at 3.3 V, within the ±20% assumed for the 63–205 ms GSE pulse.
- **C1 voltage rating (designer decision, 2026-10-24):** C1 (10 µF `VBAT_PROT`) stays 25 V X7R 0805, since no 35 V X7R 10 µF 0805 was found. Steady state is 16.8 V (67% of rating, ≈ 3 µF effective under bias). The ≈ 29 V SMF18A clamp transients that reach `VBAT_PROT` before U19's OV turns Q6 off are an accepted risk.
- **Availability substitutions (2026-10-24):**
  - **Q1/Q2/Q6 = Vishay SQJ457EP-T1_GE3** (replaces Si7489DP): P-ch −60 V, V<sub>GS</sub> ±20 V, 25 mΩ at −10 V, V<sub>GS(th)</sub> −1.5 to −2.5 V, Q<sub>g</sub> 65 nC, AEC-Q101. PowerPAK SO-8L mounts on the PowerPAK SO-8 single land (Vishay AN913); same S 1–3 / G 4 / D tab. −60 V covers the 29 V VBAT_HOT clamp on Q6 at 2× margin. Single-pulse Z<sub>thJA</sub> ≈ 1.4 °C/W at 10 ms keeps the start-up and fault pulses (≈ 31–32 W for ≲ 1–2.2 ms at U5/U19, 6.6 W for 8.5 ms at U7) well inside T<sub>J</sub>max, comparable to the Si7489DP.
  - **Q3–Q5 = Infineon BSS123NH6327XTSA1** (replaces BSS131): I<sub>DSS</sub> ≤ 10 nA at 100 V, 25 °C (≤ 5 µA at 150 °C), the same guarantee as the BSS131. V<sub>GS(th)</sub> 0.8/1.4/1.8 V; C<sub>iss</sub> 15.7 pF and C<sub>rss</sub> 2.1 pF (less reset charge injection); R<sub>DS(on)</sub> 2.7/10 Ω at 4.5 V (faster reset). SOT-23 G1/S2/D3, AEC-Q101. Generic BSS123/BSS138/2N7002 (≤ 1 µA I<sub>DSS</sub>) are not equivalent.
- **Sense resistors (R35, R38, R65):** R35/R38 are Ohmite MCS1320R010FER (10 mΩ 1% 0805, 0.75 W; R38 ≈ 0.13 W at 3.6 A). R65 is a Bourns CRL0603 100 mΩ 1%. SENSE+/SENSE− are Kelvin-routed from the pads (designer-confirmed, 2026-10-24).
- **eFuse auto-retry and start-up capacitance (U5, U7, U19, ADM1270 Rev. A):** ~FAULT is tied to ENABLE through 0 Ω (R51, R64, R116), so all three auto-retry with a 200 ms off time (C29, C34, C80 = 100 nF). U19's 22 nF timer outlasts the branch timers, so a branch fault clears at U5/U7 first. The limits come from ∫dV/I through foldback to 16.8 V; worst case covers the timer current, threshold and ±10% capacitor tolerance. They're recorded as schematic text notes for the later supply design cycle:
  - **U5 → `VBAT_DCHG`:** 3.2 A, C30 = 10 nF, ≈ 100 µF worst case.
  - **U7 → `VBAT_IGN`:** 0.5 A (ISET = VCAP → 50 mV / R65 100 mΩ), C35 = 68 nF, ≈ 115 µF worst case (≈ 107 µF with the current-limit tolerance). Q2 dissipates ≤ 6.6 W for ≤ 8.5 ms, well inside its SOA.
  - **U19 → `VBAT_PROT`:** 3.6 A, C81 = 22 nF, ≈ 290 µF nominal (≈ 235 µF worst case). The start-up load is ≈ 14 µF plus the LT3065.
  - **Firmware:** PWRGD (`/DISCHARGE_PGOOD` PC13, `/IGNITER_PGOOD` PC14, `/BAT_PGOOD` PA11; ≈ 11 V threshold) is also low when the branch is commanded off, when ARM is safe, or when BAT is below the 12 V UV, and it pulses during retries. Flag a fault only when the branch is commanded on, BAT is in range, and PWRGD stays low or keeps toggling past a debounce window.
- **PIB J5 ↔ igniter J3:** through a standard mirrored PicoBlade cable (pin n ↔ 14 − n), the pulse, reset, NTC, supply-V, grounds and ±5 V match pin-for-pin (igniter board at `85303e3`). J5.4/PC2 = `/ADC_I_IGN_PK` ↔ J3.10 (U4B, Hall current path) and J5.5/PB14 = `/ADC_V_IGN_PK` ↔ J3.9 (U4A, U2 voltage path). Net names match on both boards. `VBAT_IGN` (J5.12/13) and J12 (M80) are unconnected on the test board.
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
- **Libraries:** every symbol resolves (one library-mismatch warning, C40, tracked in P4), all 37 PCB 3D-model paths resolve, and PCB ↔ schematic parity is clean.
