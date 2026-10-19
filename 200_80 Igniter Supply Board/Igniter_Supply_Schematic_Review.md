# 200_80 Igniter Supply Board: Schematic Review (Living Document)

| | |
|---|---|
| **Board** | 200_80 Igniter Supply Board (test/boilerplate: external −1 kV DC → C8 → SiC low-side switch → igniter) |
| **Reviewed files** | `Igniter Supply Board.kicad_sch`, `Igniter Supply Board.kicad_pcb` (pad nets only; layout is a floorplan) |
| **Design state** | branch `pib-split`, `2d73650` (2026-10-19) |
| **Tools** | KiCad 10.0.4 (`kicad-cli` ERC, netlist, DRC with schematic parity), datasheets: ACS37041/2, Si826x Rev 1.2, Würth 750319697 |
| **Last updated** | 2026-10-19 |

**Status key:** `OPEN` (needs a design change) · `VERIFY` (needs a datasheet, bench or part check) · `OPTIONAL` (recommended hardening)

**How to use:** This document lists only issues present in the current design. Delete an item when it is resolved. IDs are stable; don't renumber.

---

## Summary

**How the circuit works (from the netlist):** J1 (−1 kV in) charges C8 (1 µF, 0.5 J) between `GND` and Q1's source. Q1 (G2R1000MT33J, Kelvin source) switches the igniter on J2: igniter current returns through the J2 shield and U6 (ACS37042) to `GND`. The floating gate supply is LT8301 + T1 + D1, and U7 (Si8261) drives the gate. U1 (`ADC_V_IGN_SUPPLY`) and U2 are inverting 4.53 MΩ/10 kΩ dividers (−1 kV → +2.21 V; 3.3 V full scale ≈ 1.49 kV). U3 level-shifts the hall output. U4 holds the peaks of U2 and U3 on C34/C35, which IGNITER_RST resets through Q2/Q4.

**Biggest risks:**
- **U6 hall sensor symbol pinout and footprint are wrong** (I1). As drawn, 5 V lands on the IC's GND pin and the igniter current goes into VOUT.
- **T1 is a push-pull transformer, not a flyback transformer** (I3). It will saturate long before LT8301's current limit, so the gate supply is unlikely to work.
- **OP37 is not stable at the gains used** (I5). Swap to OP27: same pinout, same rad heritage.
- **R4/R6 are 0603 resistors with 1 kV across them** (I4): roughly 15× over voltage and 2× over power.
- **The current peak detector catches the wrong polarity** (I2), and **the two peak outputs are labelled the wrong way round** (S1).
- **Nothing holds the firing chain off by default** (I6). There's no pull-down on Q3's gate, and R2 (Q1 gate–source) is DNP.

---

## Critical (safety or function)

### I1 — U6 (ACS37042) symbol pinout and footprint don't match the part — `OPEN`
Datasheet (LH package, SOT23-W): **pins 1, 2 = IP+ / IP− (wide leads on one side), 3 = GND, 4 = VOUT, 5 = VDD.**
Schematic: 1 = GND, 2 = VOUT, 3 = VDD, 4 = IP+, 5 = IP−, on a generic `TSOT-23-5` footprint (3 pads on one side, 2 on the other).
As built: pad 3 (IC GND) gets 5 V, pad 5 (VDD) gets GND, and pad 4 (VOUT) carries the igniter return current.
- Make a custom symbol and an LH footprint from the Allegro package drawing. Keep the datasheet's 1.9 mm clearance and 2 mm creepage between the IP leads and the signal leads.
- The BOM value for U6 is blank. Fill in the MPN, and choose the range on purpose (see I2).
- The schematic note "1R hall sensor… I- can vary by ~3 V" is wrong: R_IC is 1.6 mΩ, so the drop is ≈ 50 mV at 30 A. Referencing U2 to `GND` is fine. The ±3 V concern doesn't apply.

### I2 — Hall polarity vs. the peak detector, and U3 scaling — `OPEN`
Igniter current flows GND → IP− → IP+ → J2 shield. Under Allegro's convention (positive = IP+ → IP−), that is *negative* current, so VOUT falls below 2.5 V during the pulse. U4B holds the **maximum**, so it will hold the zero-current baseline (≈ 1.66 V at U3) and miss the pulse.
- Swap the IP connections (GND → IP+, J2 shield → IP−) so the pulse drives the output up.
- Re-scale U3 for a unipolar range. Currently U3 maps VOUT 1.85–3.14 V to 0–3.3 V, which is a ±0.65 V window: ±9.7 A on the `-030B5` part, ±3.2 A on the `-010B5`. Once only positive current matters, put 0 A near 0.1 V and I_max near 3.2 V to roughly double the resolution.
- The offset comes from −5 V through R11, so any error on the −5 V rail shows up directly as offset. That's acceptable if it's calibrated out.

### I3 — T1 (Würth 750319697) is a push-pull transformer and won't work as an LT8301 flyback — `OPEN`
750319697 is a **WE-PPTI push-pull** part (SN6505-style): 1:1.07, L ≥ 500 µH, ungapped, **36 V·µs per half winding**. A flyback stores its energy in a gapped core. At 5 V the full primary saturates after ≈ 14 µs, at about 0.14 A, well below LT8301's ≈ 1.2 A switch limit. Expect the LT8301 to run in hard current limit with poor or no regulation.
- **Option A:** keep LT8301 and use one of the flyback transformers listed in its datasheet, with Np:Ns ≈ 1:3–1:4 and insulation suited to 1 kV DC working.
- **Option B:** keep the WE-PPTI family and drive it with an SN6505. Pick a ratio for ~17–20 V, then regulate.
- Insulation: this part is hipot-tested at 2500 VAC but has no stated working-voltage rating. Confirm it suits a continuous 1 kV DC (the secondary sits at −1 kV).
- The polarity as drawn (dots at 1 and 6, D1 on pin 4) is correct for a flyback.
- The BOM description for U5 is LT8300's ("100VIN… 150V/450mA"). LT8300 needs V_IN ≥ 6 V and won't start from 5 V. Make sure **LT8301** is what gets ordered.

### I4 — The HV divider resistors are 0603 — `OPEN`
R4 and R6 (4.53 MΩ) each see up to 1 kV. A typical 0603 is rated for 50–75 V working. **R4 dissipates 0.22 W** (1 kV²/4.53 MΩ) against a typical 0.1 W rating, and the pad gap is under 1 mm. Expect drift, flashover or failure.
- Use a series string of HV resistors, e.g. 4–5 × 1206/2512 HV-rated parts, or a single HV thick-film part. Keep IPC-2221B external spacing, about 5 mm at 1 kV uncoated, along the string and from it to low-voltage copper.
- C6/C12 (1 pF, 2225): specify ≥ 2 kV C0G. Note that PCB stray capacitance of ~0.2–0.5 pF is a large fraction of 1 pF, so expect to tune the divider compensation (R4·C6 = 4.53 µs vs R5·C9 = 4.7 µs).
- R4 is also C8's only bleeder (τ ≈ 4.5 s). See I6.

### I5 — OP37 is used below its minimum stable gain — `OPEN`
OP37 is decompensated and stable only at noise gain ≥ 5.
- U1/U2: noise gain ≈ 1.002 at DC, and about 1 + (C10 + C6)/C9 ≈ 2 at high frequency.
- U3: gain 3.5.

All three are likely to oscillate. **Fit OP27**: same pinout and same heavy-ion heritage, so it's a drop-in swap. With OP27, C10/C11 (470 pF from the summing node to GND) do nothing useful and lower the phase margin. Remove them or make them small.

### I6 — Firing chain and HV have no safe default state — `OPEN`
- **IGNITER_PULSE (Q3 gate) has no pull-down.** With J3 unplugged, or the host's GPIO floating at reset, Q3 can turn on and fire 1 kV. Add 100 kΩ gate-to-GND on Q3, and also on IGNITER_RST.
- **R2 (Q1 gate–source) is DNP.** If the external HV is on while the 5 V/gate supply is off, Q1's gate floats and drain dV/dt can turn it on through Cgd. Fit 10 kΩ.
- **C8 discharges only through R4** (an 0603, τ ≈ 4.5 s). Add a dedicated HV bleeder sized for a known time to < 50 V. An HV-present LED or neon is worth adding for a bench board.
- **Add a series charging resistor between J1 and C8** (e.g. 100 kΩ–1 MΩ HV). It limits inrush and keeps the discharge transient off the bench supply's output and cable.

### I7 — Q1 discharge current and SOA have no defined bound — `VERIFY`
The only resistance in the discharge path is Q1's R_DS(on) (≈ 1 Ω, rising with temperature) plus cable/igniter impedance. If the igniter breaks down into a low-impedance arc, Q1 hits its saturation current with most of the 1 kV across it. That's a short-circuit event, and C8 holds 0.5 J.
- Find the igniter's breakdown voltage and arc impedance. Check Q1's pulsed I_D and short-circuit withstand time at V_GS ≈ 17 V. Add a series resistor in the discharge path if needed.
- Check the peak current against U6's range (±10 or ±30 A) and its overcurrent/surge capability.
- Consider an RC snubber or TVS across Q1 for turn-off with igniter-cable inductance.

---

## Significant

### S1 — Peak-detector net labels are swapped — `OPEN`
U2 measures Q1's drain, which is the **igniter voltage**, but its peak goes out as `ADC_I_IGN_PK`. U3 measures the **hall current**, but its peak goes out as `ADC_V_IGN_PK`. Swap the labels and check the PIB/firmware mapping.

### S2 — U7 symbol has a duplicate pin 2, and the PCB pad nets are stale — `OPEN`
The visible Si8261 pins match the SOIC-8 datasheet (2 = anode, 3 = cathode, 5 = GND, 6/7 = VO, 8 = VDD). The symbol also has a hidden NC pin numbered **2** (ERC `duplicate_pins`), which should be 1 and 4.
The PCB currently puts the anode on **pad 1** and VEE on **pad 4**, both of which are NC on SOIC-8. Fix the symbol, then update the PCB from the schematic.

### S3 — Si8261BAC has a 5 V UVLO driving a SiC FET — `VERIFY`
The Si8261 4 A SOIC-8 part comes only with a 5 V UVLO. While the gate supply ramps up or browns out (VDD 5–12 V), a pulse command drives Q1 (V_th up to 3.5 V) partly on, in its linear region, with 1 kV across it.
- Either use a driver with a ~12 V UVLO, or make sure IGNITER_PULSE can't be asserted until the gate supply has had time to settle after 5 V comes up.

### S4 — Si8261 input current is marginal — `OPEN`
I_F(ON) is 6 mA minimum. With 5 V at −5 % and V_F at 2.8 V, R12 = 370 Ω gives about 5.3 mA. Use R12 ≈ 240–270 Ω, which gives ~8 mA nominal and ≤ 14 mA at V_F = 1.6 V, within the 30 mA max.

### S5 — Gate drive: R1 = 0 Ω, and the D2 clamp overlaps VDD — `OPEN`
- With 4 A drive and no gate resistor, there's no dV/dt control on a 1 kV edge into a cable, and you can expect ringing. Start with R1 ≈ 10 Ω.
- With T1 as drawn, LT8301 regulates to about 100 µA × 165 k × 1.07 − V_F ≈ **17.3 V**. D2 (BZX84C18) can start conducting at **16.8 V**, so with R1 = 0 the zener would be loading the driver output directly. Set VDD comfortably below the zener's minimum (e.g. ~15–16 V), or clamp the VDD rail instead of the gate. Revisit this once T1 is changed (I3).

### S6 — Gate-supply rectifier and minimum load — `OPEN`
- D1 (BAT54) is rated 30 V and 200 mA (300 mA repetitive). The flyback secondary peak is roughly I_SW × Np/Ns, and V_R ≈ V_OUT + V_IN·Ns/Np ≈ 23 V plus leakage ringing. Use a ≥ 60 V, ≥ 1 A Schottky.
- The only load is the Si8261 (1.5–2.4 mA). Check LT8301's minimum-load requirement, and add a preload resistor or a rail zener so V_OUT can't creep up at light load.

### S7 — Op-amp outputs drive the clamps and the cable without series resistance — `OPEN`
U1 drives D3/D4 and J3 pin 8 (harness capacitance) directly. U2 drives D5/D6 directly, and U3 drives D7/D8 directly. In a fault, the clamps sink the op amp's full short-circuit current, and U1 sees the cable as a capacitive load.
- Add 100 Ω–1 kΩ between each op-amp output and its clamp node. For U1, also add a series R before the connector.
- MMSZ4685 (3.6 V) leaks µA-level current well below its knee, and 3.6 V is above the usual ADC limit of VDD + 0.3 V at 3.3 V. Consider a clamp to the 3.3 V rail on the PIB side (cross-ref PIB C6).

### S8 — Peak-detector hold accuracy and timing — `VERIFY`
C34/C35 are 1 nF. Leakage from D11/D12 (zener, µA-level), D9/D10 (BAT54 reverse, up to µA), Q2/Q4 off-state and the harness gives a droop of mV/µs to V/ms. Each ADC sample also pulls about 1 % of charge out of 1 nF, and the hold node leaves the board unbuffered.
- Remove D11/D12, since the U4 inputs are already clamped. Use a low-leakage diode for D9/D10. Use 10–100 nF C0G with a small series R for loop stability, or buffer the hold node.
- U4's output saturates low between pulses and has to slew back up (LMP2012 slew ≈ 4 V/µs). A second diode from the output to −IN (the two-diode peak detector) keeps the loop closed.
- IGNITER_RST is active-high here. The PIB's `/~PD_RST` is active-low, and the inverter for this board is still pending (PIB review context).

### S9 — Measurement bandwidth vs. pulse width — `VERIFY`
The U1/U2 front ends roll off at ≈ 34 kHz (R5·C9). The hall sensor's bandwidth is ≈ 125–150 kHz. If the igniter event lasts µs, the peaks will read low. Define the expected pulse shape and set these values to suit.

### S10 — C8 symbol and rating — `OPEN`
C8 uses a polarized (CP) symbol with "+" on the −1 kV node. The footprint is a film MKS4, so the part is non-polar, but the symbol invites someone to fit an electrolytic backwards. Use a non-polar symbol.
Also confirm that a 1 µF capacitor rated ≥ 1.2 kV DC exists at a 27.5 mm pitch. I believe 1 µF at 1000 V needs a larger body. Use a pulse-rated film part (e.g. MKP), since it's discharged hard.

### S11 — TH1 "under main transistor" crosses the HV barrier — `OPEN`
Q1's tab is the drain (0 to −1 kV), and TH1 sits at `GND`. Either keep about 5 mm of external spacing, or mount TH1 on the opposite side under solid FR4 with no LV copper on the layers near Q1's pad. Check the J3/PIB clearance budget too.

---

## Footprints, BOM, Project

### F1 — BOM not production-ready — `OPEN`
- No MPNs. U6 value is blank.
- No voltage ratings on C6/C12/C8.
- TP1–TP6 have no footprints (DRC `missing_footprint`). TP2–TP5 sit at HV, so use HV-safe test points.
- Q1's description says 1700 V. The G2R1000MT33J is a 3.3 kV part.

### F2 — PESD5V0S1UL on ±5 V — `VERIFY`
The standoff is 5.0 V. If the PIB's 5 V rail runs high, D14/D15 will leak.

### P1 — ERC cleanup — `OPEN`
- 7 errors: power pins not driven (add PWR_FLAGs), U7 duplicate pin 2 (S2), and U7 VO/VO output-to-output (expected).
- 11 warnings: library symbol mismatches, and J1/J2 footprint library link.

### P2 — GND/GNDPWR joined without a net-tie footprint — `OPTIONAL`
The two grounds are merged in the netlist (ERC `multiple_net_names`), so the PCB has a single `GND` net and nothing controls where the HV return joins. If you want a single-point join, use a NetTie footprint.

### P3 — Empty title block — `OPEN`

### P4 — Cross-board — `OPEN`
The PIB J5 ↔ igniter J3 pinout incompatibility is tracked in PIB review C13.
