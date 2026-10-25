# 200_80 Igniter Supply Board: Schematic Review (Living Document)

| | |
|---|---|
| **Board** | 200_80 Igniter + Discharge sense board (test/boilerplate). External −1 kV DC → C8 → SiC low-side switch → igniter; external −400 V discharge bank sensed only. 200_70 is merged into this board's `Discharge` sheet. |
| **Reviewed files** | `Igniter Supply Board.kicad_sch`, `Discharge.kicad_sch`, `Igniter Supply Board.kicad_pcb` (pad nets and footprints only) |
| **Context** | `200_40 PIB/Payload ICD.pdf` (Rev 2.1), `PIB Block Diagram.drawio.png`, PIB netlist (J5, J6, J12; working copy 2026-10-25) |
| **Design state** | branch `pib-split`, `85303e3` (2026-10-24) |
| **Tools** | KiCad 10.0.1 (`kicad-cli` ERC, netlist, DRC with schematic parity). Datasheets: ACS37041/2, Si826x Rev 1.2, Würth 750319697, MAX961–MAX999 Rev 8, LMP2021/22 (SNOSAY9G) |
| **Last updated** | 2026-10-25 |

**Status key:** `OPEN` (needs a design change) · `VERIFY` (needs a datasheet, bench or part check) · `OPTIONAL` (recommended hardening)

**How to use:** This document lists only issues present in the current design. Delete an item when it is resolved. IDs are stable; don't renumber.

---

## Summary

**How the circuit works (from the netlist):**
- **Igniter:** J1 (−1 kV in) charges C8 (1 µF, 0.5 J) between `GND` and Q1's source. Q1 (G2R1000MT33J, Kelvin source) switches the igniter on J2. Igniter current returns through the J2 shield and U6 (ACS37042) to `GND`. The floating gate supply is LT8301 + T1 + D1, and U7 (Si8261) drives the gate. Q3 (pulled down by R14) sinks U7's LED current on `IGNITER_PULSE`.
- **Igniter sensing:** U1 (`ADC_V_IGN_SUPPLY`) and U2 are inverting 4.53 MΩ/10 kΩ dividers (−1 kV → +2.21 V). U3 level-shifts the Hall output. U9/U8 (MAX997) + D10/D9 + R19/R18 charge hold caps C34/C35. U4 (LMP2022) buffers them to J3. Q5 inverts the PIB's active-low `/~PD_RST` into `IGNITER_nRST`, which drives Q2/Q4 to clear the hold caps.
- **Discharge sensing (`Discharge` sheet):** J4 (BNC, −400 V bank) → R20 4 MΩ → U10 (OP37, gain −0.00565, noise gain 6 from R21) → R24/C51 → J6 (MMCX) → PIB J6 → `ADC_V_DCHG` (PA0).
- **Not used:** PIB J12 (Harwin M80: `VBAT_DCHG`, ±5 V, `ADC_NTC_DCHG`) and J3.1/J3.2 (`VBAT_IGN`) are unconnected, because the HV comes from external supplies.

**Fixed since the last pass:** the peak outputs are now labelled correctly on this board, the hold nodes are buffered (U4), Q3 has a gate pull-down, the reset is inverted on-board, the J3 ↔ PIB J5 harness matches pin-for-pin by name, U7's PCB pads are correct, the test points have footprints, the ±5 V ESD parts are now 5.6 V zeners, and U10 is stable (R21 noise-gain resistor).

**Biggest risks:**
- **Both peak detectors latch to the rail** (I8). The MAX997 inputs are swapped: the signal is on IN− and the hold cap on IN+.
- **U1–U4 sit on 5.3 mm-wide SOIC footprints**, but the parts are 3.9 mm narrow SOIC. U4's `MM` suffix is the VSSOP part (I10). Gerbers have already been exported.
- **J1/J2 are BNC jacks rated 500 Vrms at 1 kV DC** (I9).
- **The discharge-voltage output is filtered to 16 kHz**, but the PIB captures it at 2 MSPS over a 20 µs window (S13).
- Carried over: U6 pinout (I1), Hall polarity (I2), T1 is a push-pull transformer (I3), OP37 instability on U1–U3 (I5).

---

## Critical (safety or function)

### I1 — U6 (ACS37042) symbol pinout and footprint don't match the part — `OPEN`
Datasheet (LH package, SOT23-W): **pins 1, 2 = IP+ / IP− (wide leads on one side), 3 = GND, 4 = VOUT, 5 = VDD.**
Schematic and PCB (unchanged at `85303e3`): 1 = GND, 2 = VOUT, 3 = VDD, 4 = IP+, 5 = IP−, on a generic `TSOT-23-5_HandSoldering` footprint.
As built: pad 3 (IC GND) gets 5 V, pad 5 (VDD) gets GND, and pad 4 (VOUT) carries the igniter return current.
- Make a custom symbol and an LH footprint from the Allegro package drawing. Keep the datasheet's 1.9 mm clearance and 2 mm creepage between the IP leads and the signal leads.
- The value for U6 is still `~`. Fill in the MPN, and choose the range on purpose (see I2).
- R_IC is 1.6 mΩ, so the drop is ≈ 50 mV at 30 A. Referencing U3 to `GND` is fine.

### I2 — Hall polarity vs. the peak detector, and U3 scaling — `OPEN`
Igniter current flows GND → IP− → IP+ → J2 shield. Under Allegro's convention (positive = IP+ → IP−), that is *negative* current, so VOUT falls below 2.5 V during the pulse. The U8 detector holds the **maximum** (once I8 is fixed), so it will hold the zero-current baseline (≈ 1.66 V at U3) and miss the pulse.
- Swap the IP connections (GND → IP+, J2 shield → IP−) so the pulse drives the output up.
- Re-scale U3 for a unipolar range. Currently U3 out = 2.55·VOUT − 4.73 V, which maps VOUT 1.85–3.14 V to 0–3.3 V: a ±0.65 V window, or ±9.7 A on the `-030B5` part and ±3.2 A on the `-010B5`. Once only positive current matters, put 0 A near 0.1 V and I_max near 3.2 V to roughly double the resolution.
- The offset comes from −5 V through R11, so any error on the −5 V rail shows up directly as offset. That's acceptable if it's calibrated out.

### I3 — T1 (Würth 750319697) is a push-pull transformer and won't work as an LT8301 flyback — `OPEN`
750319697 is a **WE-PPTI push-pull** part (SN6505-style): 1:1.07, L ≥ 500 µH, ungapped, **36 V·µs per half winding**. A flyback stores its energy in a gapped core. At 5 V the full primary saturates after ≈ 14 µs, at about 0.14 A, well below LT8301's ≈ 1.2 A switch limit. Expect the LT8301 to run in hard current limit with poor or no regulation.
- **Option A:** keep LT8301 and use one of the flyback transformers listed in its datasheet, with Np:Ns ≈ 1:3–1:4 and insulation suited to 1 kV DC working.
- **Option B:** keep the WE-PPTI family and drive it with an SN6505. Pick a ratio for ~17–20 V, then regulate.
- Insulation: this part is hipot-tested at 2500 VAC but has no stated working-voltage rating. Confirm it suits a continuous 1 kV DC (the secondary sits at −1 kV).
- The BOM description for U5 is still LT8300's ("100VIN… 150V/450mA"). LT8300 needs V_IN ≥ 6 V and won't start from 5 V. Make sure **LT8301** is what gets ordered.

### I4 — HV divider resistors R4/R6 need an HV-rated part — `OPEN`
R4 and R6 are now 2010. A standard 2010 is rated for about 200 V working. Each sees up to 1 kV and R4 dissipates 0.22 W, which is fine for 2010 power, but not for voltage.
- Specify an HV thick-film MPN rated ≥ 1.2 kV working (e.g. Vishay CRHV or Ohmite HVC series), or use a series string. The `Discharge` sheet's R20 note already does this. Do the same here.
- C6/C12 (1 pF, 2225): specify ≥ 2 kV C0G. PCB stray capacitance of ~0.2–0.5 pF is a large fraction of 1 pF, so expect to tune the divider compensation (R4·C6 = 4.53 µs vs R5·C9 = 4.7 µs).

### I5 — OP37 is used below its minimum stable gain on U1–U3 — `OPEN`
OP37 is decompensated and stable only at noise gain ≥ 5.
- U1/U2: noise gain ≈ 1.002 at DC, and about 1 + (C10 + C6)/C9 ≈ 2 at high frequency.
- U3: noise gain 3.5 (R9/R10, and C36/C37 at high frequency).

U10 on the `Discharge` sheet solves this with R21, a noise-gain resistor from the summing node to GND. Do the same on U1/U2: about 2.2 kΩ from the summing node to GND gives a noise gain of ≈ 5.5 without changing the signal gain. For U3, either fit OP27 or raise its noise gain. **OP27 is still the simplest fix**: same pinout and same heavy-ion heritage. With OP27, C10/C11 do nothing useful and lower the phase margin, so remove them or make them small.

### I6 — Firing chain and HV have no safe default state — `OPEN`
- **`IGNITER_RST` (Q5 gate) has no pull-down.** If the PIB's PA12 is high-Z at reset, Q5 floats, and so does the hold/reset state. Add 100 kΩ gate-to-GND. Then "unplugged or MCU in reset" means the hold caps are cleared. (Q3 now has R14, so the pulse path is covered.)
- **R2 (Q1 gate–source) is DNP.** If the external HV is on while the 5 V/gate supply is off, Q1's gate floats and drain dV/dt can turn it on through Cgd. Fit 10 kΩ.
- **C8 discharges only through R4** (τ ≈ 4.5 s). Add a dedicated HV bleeder sized for a known time to < 50 V. An HV-present indicator is worth adding for a bench board.
- **Add a series charging resistor between J1 and C8** (e.g. 100 kΩ–1 MΩ HV). It limits inrush and keeps the discharge transient off the bench supply's output and cable.

### I7 — Q1 discharge current and SOA have no defined bound — `VERIFY`
The only resistance in the discharge path is Q1's R_DS(on) (≈ 1 Ω, rising with temperature) plus cable/igniter impedance. If the igniter breaks down into a low-impedance arc, Q1 hits its saturation current with most of the 1 kV across it. That's a short-circuit event, and C8 holds 0.5 J.
- Find the igniter's breakdown voltage and arc impedance. Check Q1's pulsed I_D and short-circuit withstand time at V_GS ≈ 17 V. Add a series resistor in the discharge path if needed.
- Check the peak current against U6's range (±10 or ±30 A) and its overcurrent/surge capability.
- Consider an RC snubber or TVS across Q1 for turn-off with igniter-cable inductance.

### I8 — Comparator peak detectors are wired with inverted inputs and latch to the rail — `OPEN`
MAX997 SO-8 (datasheet pin table): 2 = IN−, 3 = IN+, 6 = Q, 8 = SHDN (high = shutdown, so SHDN → GND is correct).
- U9: IN− (pin 2) = U2 output (signal), IN+ (pin 3) = C34 (hold).
- U8: IN− (pin 2) = U3 output (signal), IN+ (pin 3) = C35 (hold).

The output goes high when **hold > signal**, and charges the hold cap through D10/R19 (D9/R18), which raises it further. This is positive feedback. As soon as noise or offset puts the hold cap 3.5 mV above the signal, the cap runs up to ≈ V_OH − V_F ≈ 4.6 V and stays there. U4 then drives ≈ 4.6 V into D11/D12 (3.6 V zeners) with no series resistance. Pulsing Q2/Q4 can't clear the latch either, because the comparator keeps charging through 100 Ω.
- Swap the comparator inputs: signal → IN+ (pin 3), hold cap → IN− (pin 2). This is a pin swap on U8 and U9.

### I9 — J1/J2 are BNC jacks rated 500 Vrms, at 1 kV DC — `OPEN`
J1, J2 and J4 all use Amphenol 112659. The `Discharge` sheet's own J4 note gives **500 Vrms working**, which is fine for −400 V on J4. J1 (−1 kV source) and J2 (Q1 drain, swings to −1 kV) exceed it. BNC also invites someone to plug a scope or a BNC tee into a live HV line.
- Use SHV (e.g. Amphenol 31-xxxx SHV PCB jacks) for J1 and J2, along with matching SHV cable and a supply output. Avoid MHV, because it mates with BNC.

### I10 — SOIC footprints don't match the parts on U1–U4; U4 suffix is VSSOP — `OPEN`
- U1–U3 (OP37) are on `SOIC-8_5.3x6.2mm` (208-mil EIAJ). OP37 SOIC is 3.9 mm narrow (`R-8`). U10 on the `Discharge` sheet already uses `SOIC-8_3.9x4.9mm`, which is correct.
- U4 is `LMP2022MM` on `SOIC-8_5.3x6.2mm`. Per TI, the `MM` suffix is VSSOP-8 (3 × 3 mm), and SOIC-8 is 4.90 × 3.91 mm (`MA`). Change the part to LMP2022MA and the footprint to `SOIC-8_3.9x4.9mm`.
- U7 (Si8261BAC-C-IS) is also on `SOIC-8_5.3x6.2mm`. Check this against the Si826x package drawing. I believe the `-IS` SOIC-8 is the narrow body.
- Gerbers in `IgniterGerbers/` were exported with these footprints. Re-export the gerbers after the fix.

---

## Significant

### S2 — U7 symbol has a duplicate pin 2 — `OPEN`
The visible Si8261 pins match the SOIC-8 datasheet, and the PCB pads are now correct (2 = anode, 3 = cathode, 5 = VEE, 6/7 = VO, 8 = VCC). The symbol still has a hidden NC pin numbered **2** (ERC `duplicate_pins`), which should be 1 and 4. Fix it in the library symbol so the next PCB update doesn't move the anode net.

### S3 — Si8261BAC has a 5 V UVLO driving a SiC FET — `VERIFY`
The Si8261 4 A SOIC-8 part comes only with a 5 V UVLO. While the gate supply ramps up or browns out (VDD 5–12 V), a pulse command drives Q1 (V_th up to 3.5 V) partly on, in its linear region, with 1 kV across it.
- Either use a driver with a ~12 V UVLO, or make sure IGNITER_PULSE can't be asserted until the gate supply has had time to settle after 5 V comes up.

### S4 — Si8261 input current is marginal — `OPEN`
I_F(ON) is 6 mA minimum. With 5 V at −5 % and V_F at 2.8 V, R12 = 370 Ω gives about 5.3 mA (less Q3's R_DS(on) drop). Use R12 ≈ 240–270 Ω, which gives ~8 mA nominal and ≤ 14 mA at V_F = 1.6 V, within the 30 mA max.

### S5 — Gate drive: R1 = 0 Ω, and the D2 clamp overlaps VDD — `OPEN`
- With 4 A drive and no gate resistor, there's no dV/dt control on a 1 kV edge into a cable, and you can expect ringing. Start with R1 ≈ 10 Ω.
- With T1 as drawn, LT8301 regulates to about 100 µA × 165 k × 1.07 − V_F ≈ **17.3 V**. D2 (BZX84C18) can start conducting at **16.8 V**, so with R1 = 0 the zener loads the driver output directly. Set VDD comfortably below the zener's minimum (e.g. ~15–16 V), or clamp the VDD rail instead of the gate. Revisit this once T1 is changed (I3).

### S6 — Gate-supply rectifier and minimum load — `OPEN`
- D1 (BAT54) is rated 30 V and 200 mA (300 mA repetitive). The flyback secondary peak is roughly I_SW × Np/Ns, and V_R ≈ V_OUT + V_IN·Ns/Np ≈ 23 V plus leakage ringing. Use a ≥ 60 V, ≥ 1 A Schottky.
- The only load is the Si8261 (1.5–2.4 mA). Check LT8301's minimum-load requirement, and add a preload resistor or a rail zener so V_OUT can't creep up at light load.

### S7 — Op-amp outputs drive the clamps and the cable without series resistance — `OPEN`
- U1 drives D3/D4 and J3.8 (harness) directly. R15/C40 is a shunt RC to GND, not a series isolation resistor.
- U4 (LMP2022) drives D11/D12 and J3.9/J3.10 directly. LMP2022 phase margin is specified only at C_L = 20 pF (plots go to 100 pF). The harness plus MMSZ4685 junction capacitance (on the order of 100 pF) is past that.
- U2/U3 drive D5–D8 and the MAX997 inputs directly. That's acceptable on-board, but a clamp in conduction sinks the op amp's full short-circuit current.
- Add 100 Ω–1 kΩ between each op-amp output and its clamp/connector node, as U10 does with R24. The PIB already clamps these inputs to 3.3 V (D55–D59) behind 1 kΩ, so the on-board zeners only need to protect the harness.

### S8 — Peak-hold droop, overshoot and reset — `VERIFY`
- **Droop:** each hold cap (1 nF) is loaded by a MAX997 input. Per the datasheet, I_B is µA-class (±15 µA max limit; the typical curve shows a few µA). At 5 µA, the droop is ≈ **5 mV/µs (5 V/ms)**. The PIB reads `ADC_V_IGN_PK` on PC2 (slow channel), so unless the sample happens within a few µs of the peak, the reading is meaningless. Options: raise C34/C35 to 10–100 nF C0G (and lower R18/R19 to keep the charge time), or buffer the hold node before the comparator.
- **Overshoot:** with 4.5 ns of delay, a 100 Ω/1 nF charge path at ~4.6 V drive slews ≈ 40 V/µs, so the cap overshoots by ≈ 0.2 V per decision. The 3.5 mV hysteresis doesn't help with that. Larger C (above) also reduces overshoot. Otherwise, characterise it on the bench.
- **Leakage:** D9/D10 are BAT54 parts, which leak µA-level current hot in reverse (with the comparator output low, the full hold voltage sits across the diode). Use a low-leakage switching diode (e.g. BAS416/BAV199 class) once the droop budget is set.

### S9 — Measurement bandwidth vs. pulse width — `VERIFY`
The U1/U2 front ends roll off at ≈ 34 kHz (R5·C9). The Hall sensor's bandwidth is ≈ 125–150 kHz. If the igniter event lasts µs (the ICD puts the IGNITER pulse in the single-digit µs range), the U2 and Hall peaks will read low. Define the expected pulse shape and set these values to suit.

### S10 — C8 symbol and rating — `OPEN`
C8 still uses a polarized (CP) symbol ("Polarized capacitor, US symbol"). The footprint is a film MKS4, so the part is non-polar, but the symbol invites someone to fit an electrolytic backwards. Use a non-polar symbol.
Confirm the voltage rating has margin over 1 kV. Use a pulse-rated film part (MKP rather than MKS polyester), since it's discharged hard.

### S11 — TH1 under Q1 crosses the HV barrier — `VERIFY`
TH1 (`GND`/`ADC_NTC_IGN`) is now on B.Cu directly under Q1 (F.Cu, tab = drain, 0 to −1 kV). Confirm that In1/In2/B.Cu carry no LV copper under Q1's tab within the HV spacing, and that TH1's pads clear the tab's footprint by that distance through the board.

### S13 — Discharge voltage is filtered to 16 kHz, but the PIB captures it at 2 MSPS — `OPEN`
PIB J6 → R106 → R105/C72 (≈ 340 kHz) → PA0 is the fast, simultaneous discharge V/I capture (ICD: 20 µs waveform per pulse). This board's R24 (100 Ω) and C51 (100 nF) form a **τ = 10 µs, 16 kHz** pole. That smears the bank's voltage collapse across the whole capture window.
- If you want a waveform, drop C51 to ~1 nF (≈ 1.6 MHz) or remove it. Then also **compensate the divider**: R20 has no parallel C and R23 has none either, so the ~0.2 pF stray across R20 gives a zero near 200 kHz and HF peaking. Add C across R23 so that R23·C_f = R20·C_par (e.g. ≈ 35 pF for 0.2 pF stray, tuned on the bench), or add a small known C across R20 and match it.
- If only the DC bank voltage is wanted, C51 is fine, but then this channel doesn't belong on the 2 MSPS capture.

---

## Footprints, BOM, Project

### F1 — BOM not production-ready — `OPEN`
- No MPNs or Digi-Key PNs on this board (the PIB has them). U6 value is blank.
- No voltage ratings on C6/C12/C8. R4/R6 need HV MPNs (I4), and R20 needs one too (its note says "verify rating").
- TP4/TP5 (Keystone 5015) sit on the Q1 gate/Kelvin node, which floats at −1 kV. They're exposed metal at HV on a bench board. Use insulated HV test points or label them.
- Q1's description says 1700 V. The G2R1000MT33J is a 3.3 kV part.
- U5's description is LT8300's (I3). U4 should be LMP2022MA (I10).

### P1 — ERC cleanup — `OPEN`
4 errors and 9 warnings:
- U7 duplicate pin 2 (S2), U7 VO/VO output-to-output (expected), and power pins not driven on U7 VCC/VEE (add PWR_FLAGs on the floating gate rail).
- 7 library symbol mismatches (T1, D_Schottky × 5, D_Zener), plus GND/GNDPWR (P2).

### P2 — GND/GNDPWR joined without a net-tie footprint — `OPTIONAL`
The two grounds are merged in the netlist (ERC `multiple_net_names`), so the PCB has a single `GND` net and nothing controls where the HV return joins. If you want a single-point join, use a NetTie footprint. The block diagram's note applies here: chassis GND must not join on the far side of the Hall sensor (or Rogowski). H1–H4 are AC-coupled through C46–C49, which respects that.

### P3 — Empty title block — `OPEN`
Neither sheet has a title block.

### P5 — PIB channels left unconnected by the merged board — `OPEN`
- PIB J12 (M80) is unconnected, so `ADC_NTC_DCHG` (PB12) floats to the R87 pull-up and reads as an open thermistor. The ICD lists a discharge-supply thermistor. Either have firmware ignore or mask it for this board, or route the TH1 position or a second NTC to it. Otherwise, any health check run at ARM time will fail.
- `VBAT_IGN` (J5.12/13) and `VBAT_DCHG` (J12.1/2) see no load. The PIB eFuses and UVLO are unaffected, but firmware power-good checks on those branches should expect "enabled, ~0 A".
