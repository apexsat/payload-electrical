# 200_80 Igniter Supply Board: Schematic Review (Living Document)

| | |
|---|---|
| **Board** | 200_80 Igniter + Discharge sense board (test/boilerplate). External −1 kV DC → C8 → SiC low-side switch → igniter; external −400 V discharge bank sensed only. 200_70 is merged into this board's `Discharge` sheet. |
| **Reviewed files** | `Igniter Supply Board.kicad_sch`, `Discharge.kicad_sch`, `Igniter Supply Board.kicad_pcb` (pad nets, footprints, HV spacing, gerber freshness) |
| **Context** | `200_40 PIB/Payload ICD.pdf` (Rev 2.1), `PIB Block Diagram.drawio.png`, PIB netlist (J5, J6, J12; working copy 2026-10-25) |
| **Design state** | branch `pib-split`, `6e0f588` (2026-10-26) |
| **Tools** | KiCad 10.0.1 (`kicad-cli` ERC, netlist, DRC with schematic parity, gerber re-export; pcbnew Python for HV copper spacing after an in-memory zone refill). Datasheets: ACS37041/2, UCC23513-Q1 (DWY), OPA994 (product page), Würth 750319697, LMP2021/22 (SNOSAY9G) |
| **Last updated** | 2026-10-27 |

**Status key:** `OPEN` (needs a design change) · `VERIFY` (needs a datasheet, bench or part check) · `OPTIONAL` (recommended hardening)

**How to use:** This document lists only issues present in the current design. Delete an item when it is resolved. IDs are stable; don't renumber.

---

## Summary

**How the circuit works (from the netlist):**
- **Igniter:** J1 (−1 kV in) charges C8 (1 µF, 0.5 J) between `GND` and Q1's source. Q1 (G2R1000MT33J, Kelvin source) switches the igniter on J2. Igniter current returns through the J2 shield and U6 (ACS37042) to `GND`. The floating gate supply is LT8301 + T1 + D1. U7 (UCC23513-Q1, opto-emulator gate driver, stretched SO-6) drives the gate. Q3 (pulled down by R14) sinks U7's input current through R12 on `IGNITER_PULSE`.
- **Igniter sensing:** U1 (`ADC_V_IGN_SUPPLY`) and U2 are inverting 4.53 MΩ/10 kΩ dividers (−1 kV → +2.21 V). U3 level-shifts the Hall output. U12/U11 (OPA994, signal on IN+, hold cap fed back to IN−) charge hold caps C34/C35 through D10/D9. U4 (LMP2022) buffers them to J3. Q5 inverts `IGNITER_RST` into `IGNITER_nRST`, which drives Q2/Q4 to clear the hold caps.
- **Discharge sensing (`Discharge` sheet):** J4 (BNC, −400 V bank) → R30 820 kΩ → U10 (OP37 inverting, R31 3.3 kΩ, gain −0.00402). The +IN is biased to 1.60 V (R26/R27 from 5 V, C7). That maps ±400 V to ≈ 0–3.22 V, matching the sheet note. R28 + C51 (1 kΩ + 1 nF to GND) set the high-frequency noise gain. The output goes to J6 (MMCX) directly, with an R25/C50 snubber and a D20 negative clamp → PIB J6 → `ADC_V_DCHG`.
- **Not used:** PIB J12 (Harwin M80: `VBAT_DCHG`, ±5 V, `ADC_NTC_DCHG`) and J3.1/J3.2 (`VBAT_IGN`) are unconnected, because the HV comes from external supplies.

**Fixed since the last pass (`706f496` → `6e0f588`):**
- The PCB was re-routed. DRC with schematic parity shows **no shorts** (old I10).
- U11/U12 are now OPA994: unity-gain stable, rated for unlimited capacitive load, 2.7–32 V (old I11).
- U7 is now **UCC23513-Q1**: V_IOWM 2121 V DC, creepage/clearance > 8.5 mm, 12.5 V UVLO (old I12, S2, S3). The KiCad `SSO-6_6.8x4.6mm…Clearance8mm` land pattern matches TI's DWY example (10.75 mm pad span, 1.27 mm pitch), and the pinout matches the ACPL-P343 symbol.

**Biggest risks:**
- **U7 will likely never turn on** (I14). R12 = 1.4 kΩ gives ≈ 2 mA. UCC23513 needs I_F(ON) of 7–16 mA, and its turn-on threshold is up to 4 mA.
- **HV spacing on the new layout** (I13). The drain node (R6 pad) is **0.22 mm from a 5 V via**. A Kelvin-source via is 0.87 mm from the In1 GND plane.
- **The committed gerbers don't match the PCB** (I13). A fresh export differs on copper, mask and paste. Don't order from `IgniterGerbers/`.
- Carried over: U6 pinout (I1), Hall polarity (I2), T1 is a push-pull transformer (I3), R4/R6 voltage rating (I4), OP37 below its minimum gain on U1–U3 and now also U10 (I5), no safe default state (I6), BNC at 1 kV (I9).

---

## Critical (safety or function)

### I1 — U6 (ACS37042) symbol pinout and footprint don't match the part — `OPEN`
Datasheet (LH package, SOT23-W): **pins 1, 2 = IP+ / IP− (wide leads on one side), 3 = GND, 4 = VOUT, 5 = VDD.**
Schematic and PCB (unchanged at `6e0f588`): 1 = GND, 2 = VOUT, 3 = VDD, 4 = IP+, 5 = IP−, on a generic `TSOT-23-5_HandSoldering` footprint.
As built: pad 3 (IC GND) gets 5 V, pad 5 (VDD) gets GND, and pad 4 (VOUT) carries the igniter return current.
- Make a custom symbol and an LH footprint from the Allegro package drawing. Keep the datasheet's 1.9 mm clearance and 2 mm creepage between the IP leads and the signal leads.
- U6's value is still `~` and its library symbol is named `-030B3` while the PN is `-010B5`. Fill in the MPN, and choose the range on purpose (see I2).
- R_IC is 1.6 mΩ, so the drop is ≈ 50 mV at 30 A. Referencing U3 to `GND` is fine.

### I2 — Hall polarity vs. the peak detector, and U3 scaling — `OPEN`
Igniter current flows GND → IP− → IP+ → J2 shield. Under Allegro's convention (positive = IP+ → IP−), that is *negative* current, so VOUT falls below 2.5 V during the pulse. The U11 detector holds the **maximum**, so it will hold the zero-current baseline (≈ 1.66 V at U3) and miss the pulse.
- Swap the IP connections (GND → IP+, J2 shield → IP−) so the pulse drives the output up.
- Re-scale U3 for a unipolar range. Currently U3 out = 2.55·VOUT − 4.73 V, which maps VOUT 1.85–3.14 V to 0–3.3 V: a ±0.65 V window, or ±9.7 A on the `-030B5` part and ±3.2 A on the `-010B5`. Once only positive current matters, put 0 A near 0.1 V and I_max near 3.2 V to roughly double the resolution.
- The offset comes from −5 V through R11, so any error on the −5 V rail shows up directly as offset. That's acceptable if it's calibrated out.

### I3 — T1 (Würth 750319697) is a push-pull transformer and won't work as an LT8301 flyback — `OPEN`
750319697 is a **WE-PPTI push-pull** part (SN6505-style): 1:1.07, L ≥ 500 µH, ungapped, **36 V·µs per half winding**. A flyback stores its energy in a gapped core. At 5 V the full primary saturates after ≈ 14 µs, at about 0.14 A, well below LT8301's ≈ 1.2 A switch limit. Expect the LT8301 to run in hard current limit with poor or no regulation.
- **Option A:** keep LT8301 and use one of the flyback transformers listed in its datasheet, with Np:Ns ≈ 1:3–1:4 and insulation suited to 1 kV DC working.
- **Option B:** keep the WE-PPTI family and drive it with an SN6505. Pick a ratio for ~17–20 V, then regulate.
- UCC23513-Q1 (12 V UVLO) needs **VCC 14–33 V** (recommended). Whatever replaces T1 has to hold VCC ≥ 14 V under load.
- Insulation: this part is hipot-tested at 2500 VAC but has no stated working-voltage rating. Confirm it suits a continuous 1 kV DC (the secondary sits at −1 kV).
- The BOM description for U5 is still LT8300's ("100VIN… 150V/450mA"). LT8300 needs V_IN ≥ 6 V and won't start from 5 V. Make sure **LT8301** is what gets ordered (the PN field `LT8301ES5#TRMPBF` is correct).

### I4 — HV divider resistors R4/R6 need an HV-rated part — `OPEN`
R4 and R6 are now specified as **RC2010FK-074M53L** (Yageo RC, 2010), which is rated for **200 V working**. Each sees up to 1 kV, and R4 dissipates 0.22 W.
- Specify an HV thick-film MPN rated ≥ 1.2 kV working (e.g. Vishay CRHV or Ohmite HVC series), or use a series string.
- C6/C12 (1 pF, 2225, `1608-1546-1-ND`): confirm the PN is ≥ 2 kV C0G. PCB stray capacitance of ~0.2–0.5 pF is a large fraction of 1 pF, so expect to tune the divider compensation (R4·C6 = 4.53 µs vs R5·C9 = 4.7 µs).

### I5 — OP37 is used below its minimum stable gain on U1–U3 and U10 — `OPEN`
OP37 is decompensated and stable only at noise gain ≥ 5. What matters is the noise gain near crossover (set by the capacitors and the high-frequency network):
- **U1/U2:** 1 + (C10 + C6)/C9 = 1 + 471 p/470 p ≈ **2.0** (same for U2 with C11/C13).
- **U3:** 1 + C37/C36 = 1 + 820 p/330 p ≈ **3.5**.
- **U10 (new):** 1 + R31/(R28 ‖ R30) = 1 + 3.3 k/1.0 k ≈ **4.3**. The U10 description says "stable for noise gain ≥ 5 (set by R3)". That refers to an old reference, and the value doesn't reach 5.

Fixes:
- U1/U2: raise C10/C11 to ≈ 2.2 nF C0G (NG ≈ 5.7). Signal gain is unchanged.
- U3: raise C37 to ≈ 1.5–1.8 nF (NG ≈ 5.5–6.5), or fit OP27.
- U10: drop R28 to 680 Ω (NG ≈ 5.9; the C51 corner moves to 234 kHz).
- Or fit **OP27** throughout: same pinout and the same heavy-ion heritage. That also frees you to add feedback capacitors (see S13).

### I6 — Firing chain and HV have no safe default state — `OPEN`
- **`IGNITER_RST` (Q5 gate) has no pull-down.** Only D13 sits on it. If the PIB's pin is high-Z at reset, Q5 floats, and so does the hold/reset state. Add 100 kΩ gate-to-GND. (Q3 has R14, so the pulse path is covered.)
- **R2 (Q1 gate–source) is still not fitted** (value "DNP", excluded from the BOM). If the external HV is on while the 5 V/gate supply is off, Q1's gate floats and drain dV/dt can turn it on through Cgd. Fit 10 kΩ.
- **C8 discharges only through R4** (τ ≈ 4.5 s). Add a dedicated HV bleeder sized for a known time to < 50 V. An HV-present indicator is worth adding for a bench board.
- **Add a series charging resistor between J1 and C8** (e.g. 100 kΩ–1 MΩ HV). It limits inrush and keeps the discharge transient off the bench supply's output and cable.

### I7 — Q1 discharge current and SOA have no defined bound — `VERIFY`
The only resistance in the discharge path is Q1's R_DS(on) (≈ 1 Ω, rising with temperature) plus cable/igniter impedance. If the igniter breaks down into a low-impedance arc, Q1 hits its saturation current with most of the 1 kV across it. That's a short-circuit event, and C8 holds 0.5 J.
- Find the igniter's breakdown voltage and arc impedance. Check Q1's pulsed I_D and short-circuit withstand time at V_GS ≈ 17 V. Add a series resistor in the discharge path if needed.
- Check the peak current against U6's range (±10 A on `-010B5`) and its overcurrent/surge capability.
- Consider an RC snubber or TVS across Q1 for turn-off with igniter-cable inductance.

### I9 — J1/J2 are BNC jacks rated 500 Vrms, at 1 kV DC — `OPEN`
J1, J2 and J4 all use Amphenol 112659 (500 Vrms working). That's fine for −400 V on J4. J1 (−1 kV source) and J2 (Q1 drain, swings to −1 kV) exceed it. The layout confirms it: center-pin to shell copper is **2.42 mm** on F.Cu. BNC also invites someone to plug a scope or a BNC tee into a live HV line.
- Use SHV (e.g. Amphenol 31-xxxx SHV PCB jacks) for J1 and J2, along with matching SHV cable and a supply output. Avoid MHV, because it mates with BNC.

### I13 — HV copper spacing on the new layout; gerbers stale — `OPEN`
Measured on the current PCB after an in-memory zone refill, HV nets vs everything else on the same layer. For 1 kV, IPC-2221B gives ≈ 5 mm external uncoated (B2) and ≈ 2.5 mm internal (B1).
- **B.Cu, R6 pad 2 (`Net-(J2-In)`, Q1 drain, 0 to −1 kV) at (158.5, 119.4): 0.22 mm to a 5 V via**, 0.70 mm to a GND via, 1.55 mm to H3. This will arc on the first pulse. Move the vias and pull the pours back ≥ 5 mm around the HV end of R6.
- **In1.Cu, Kelvin-source via (`Net-(D2-A)`, −1 kV) at (123.5, 102.2): 0.87 mm to the GND plane.** The In1/In2 keepout stops at y = 101. Extend it to clear this via by ≥ 2.5 mm.
- B.Cu: a D2-A via at (122.8, 108.5) is 2.35 mm from the `ADC_V_IGN_SUPPLY` track. R6/C12's HV end is 2.52–2.60 mm from the J2 shield and a −5 V via. J1's center pin is 2.93 mm from a GND track.
- Q1's own leads (TO-263-7 source/gate pins to drain tab) are 2.15 mm. That's set by the package, so conformal-coat this area.
- **The D2-A (Kelvin source) pour on F.Cu never fills.** It lies entirely inside an F.Cu "no pour" keepout (113.4–135.2 × 93.4–110.2). Either delete the pour or cut it out of the keepout.
- **Gerbers:** `IgniterGerbers/` was created at 23:10, and the PCB was committed at 23:35. A fresh `kicad-cli pcb export gerbers` differs from the committed set on F/B/In1/In2 copper, F.Mask, F.Paste and both silkscreens. Refill the zones and re-export after fixing the items above.
- Remaining DRC: 4 courtyard overlaps (T1 with TP5, TP6 and D1; D1 with C2). Also dangling tracks and vias (5 V at (122.6, 93.1) and (146.3, 106.0); −5 V at (155.6, 116.5); 4 GND vias around (135–146, 110–113)).

### I14 — U7 (UCC23513) input current far below I_F(ON) — `OPEN`
UCC23513 recommends **I_F(ON) = 7–16 mA**. Its turn-on threshold I_FLH is 1.5 / 2.8 / **4 mA** (min/typ/max), and V_F is 1.8–2.4 V. R12 = 1.4 kΩ gives (5 − 2.1)/1.4 k ≈ **2.1 mA** nominal and ≈ 1.7 mA worst case. That's below the typical threshold, so Q1 may never turn on, or only on some units or temperatures.
- Use **R12 = 270 Ω**: ≈ 8.5 mA worst case (4.75 V, V_F 2.4 V) and ≈ 12.8 mA best case (5.25 V, V_F 1.8 V). That's within 7–16 mA, and Q3 (BSS123) handles it easily.
- Update the stale note "Designed for 6 mA at 2.8 V Vf (worst case)". It was written for the Si8261.

---

## Significant

### S5 — Gate drive: R1 = 0 Ω, and the D2 clamp overlaps VDD — `OPEN`
- UCC23513 sources and sinks 4.5–5.3 A. With no gate resistor, there's no dV/dt control on a 1 kV edge into a cable, and you can expect ringing. Start with R1 ≈ 10 Ω.
- With T1 as drawn, LT8301 regulates to about 100 µA × 165 k × 1.07 − V_F ≈ **17.3 V**. D2 (BZX84C18) can start conducting at **16.8 V**, so with R1 = 0 the zener loads the driver output directly. Set VDD at 15–16 V (still above the UCC23513-Q1's 14 V minimum), or clamp the VDD rail instead of the gate. Revisit this once T1 is changed (I3).

### S6 — Gate-supply rectifier and minimum load — `OPEN`
- D1 (BAT54) is rated 30 V and 200 mA (300 mA repetitive). The flyback secondary peak is roughly I_SW × Np/Ns, and V_R ≈ V_OUT + V_IN·Ns/Np ≈ 23 V plus leakage ringing. Use a ≥ 60 V, ≥ 1 A Schottky.
- The only load is U7's quiescent current (~2–3 mA). Check LT8301's minimum-load requirement, and add a preload resistor or a rail zener so V_OUT can't creep up at light load.

### S7 — Op-amp outputs drive the cable without series resistance — `OPEN`
- U1 drives D3/D4 and J3.8 (harness) directly. R15/C40 is a shunt RC to GND, not a series isolation resistor.
- U4 (LMP2022) drives J3.9/J3.10 directly (the D11/D12 clamps are gone). LMP2022 phase margin is specified only at C_L = 20 pF (plots go to 100 pF). A harness exceeds that.
- U10 (`Discharge`) drives D20 and the J6 coax directly. R25/C50 is a shunt snubber, not a back-termination. Put ~50 Ω in series to J6, ahead of the clamp.
- Add 100 Ω–1 kΩ between each op-amp output and its connector node. The PIB already clamps these inputs to 3.3 V (D55–D59) behind 1 kΩ.
- U11/U12 now drive the diode and 1 nF directly. That's fine with OPA994.

### S8 — Peak-hold droop and reset — `VERIFY`
OPA994 and LMP2022 inputs are pA-class, so bias-current droop isn't an issue. What's left:
- **Leakage:** D9/D10 (BAT54) sit reverse-biased at the full hold voltage once the op-amp output drops to 0 V. BAT54 leakage is µA-class at temperature, which gives ≈ 1 mV/µs on 1 nF. Q2/Q4 (BSS123) add their I_DSS. Use a low-leakage diode (e.g. BAS416/BAV199 class) and/or raise C34/C35 to 10–100 nF C0G. That should hold the reading over the PIB's slow-channel sampling delay. OPA994's ±125 mA output charges 10–100 nF quickly, but add ~100 Ω between diode and cap (feedback taken from the cap side) to limit the peak current.
- **Reset:** with Q2/Q4 on, the op amp is still driving the cap through the diode. Reset only discharges the cap while the input sits below the hold value, which is true between pulses. Check the reset timing in firmware.
- The sheet note "Need to update the peak detector circuits" is still there. Remove it, or say what's left to do.

### S9 — Measurement bandwidth vs. pulse width — `VERIFY`
The U1/U2 front ends roll off at ≈ 34 kHz (R5·C9). The Hall sensor's bandwidth is ≈ 125–150 kHz. If the igniter event lasts µs (the ICD puts the IGNITER pulse in the single-digit µs range), the U2 and Hall peaks will read low. Define the expected pulse shape and set these values to suit. The I5 fix (bigger C10/C11) doesn't change this corner.

### S10 — C8 symbol and rating — `OPEN`
C8 still uses a polarized (CP) symbol ("Polarized capacitor, US symbol"). The part (KEMET C4AQ, `399-C4AQSLU4100A11J-ND`) is polypropylene film and non-polar, but the symbol invites someone to fit an electrolytic backwards. Use a non-polar symbol. Confirm that the voltage code in the PN gives ≥ 1.1 kV DC, and that the C4AQ body/pitch matches the `C_Rect_L33.0mm_W20.0mm_P27.50mm_MKS4` footprint (DRC flags a library mismatch on it).

### S11 — TH1 under Q1 crosses the HV barrier — `VERIFY`
TH1 (`GND`/`ADC_NTC_IGN`) is on B.Cu at (120.8–124.2, 121.0–122.5), directly under Q1 (F.Cu, tab = drain, 0 to −1 kV). Through-board FR-4 is fine dielectrically. Confirm that no In1/In2/B.Cu LV copper reaches out from TH1 to the edge of the tab's footprint within the 2.5 mm internal / 5 mm external spacing.

### S13 — Discharge divider is uncompensated — `OPEN`
R30 (820 kΩ, 2512) has no parallel C, and neither does R31. About 0.2 pF of stray across R30 gives a zero near 1 MHz, which shows up as HF peaking in the 2 MSPS capture. The matching fix, C_f ≈ 0.2 p × 820 k/3.3 k ≈ 50 pF across R31, **lowers the high-frequency noise gain toward 1**. That would make the OP37 unstable (I5).
- Either switch U10 to OP27 and add C_f (tuned on the bench), or leave it uncompensated and characterize the peaking.
- R30's voltage rating: confirm `A130028CT-ND` is rated ≥ 500 V working (it sees 400 V, 0.2 W).
- Bias: the 1.60 V mid-point comes from 5 V through R26/R27, so 5 V error appears as offset (×0.32). Calibrate it out, or take it from a reference.

### S14 — D17 (MMSZ4685) leakage on the NTC line — `OPTIONAL`
A 3.6 V zener on `ADC_NTC_IGN` draws µA-class leakage at 1–2 V, rising with temperature. That's a few % error on a 10 kΩ NTC divider, and it tracks the temperature you're trying to measure. The PIB already clamps the input. Drop D17, or use a low-leakage TVS. D13/D16 on the digital lines are fine.

---

## Footprints, BOM, Project

### F1 — BOM not production-ready — `OPEN`
- **The "Digikey PN" field mixes PNs and MPNs.** R3/R4/R5/R7 (`RT0603…`, `RC2010…`, `RT1206…`), R8/R10 (`RNCF0603BTE1K00`), U1–U4/U10 (`OP37GSZ`, `LMP2022MA/NOPB`), U5, U6, T1 and TH1 hold manufacturer PNs. Pick one convention, or add a separate MPN field.
- **H1–H4 carry `BSS123NCT-ND`** (copy-paste). They're excluded from the BOM, so nothing gets ordered, but clear the field.
- **J3:** the value is `53261-1371` (PicoBlade right-angle) but the footprint is `53398-1371` (vertical). They have different land patterns. Decide which one and make the value, footprint and `WM14352CT-ND` agree, then check against the PIB harness.
- **U7:** the footprint field lacks the library nickname (`SSO-6_…` instead of `Package_SO:SSO-6_…`). That causes ERC `footprint_link_issues` and DRC `footprint_symbol_mismatch`. The value is `UCC23513`, but the PN is the `-Q1` part. Fine, just be consistent.
- Q1's description says 1700 V. The G2R1000MT33J is a 3.3 kV part. U5's description is LT8300's (I3). U11/U12 have empty descriptions. U10's description references "R3". C1/C3/C7/C51 say "place right at U1" (they're on the `Discharge` sheet, near U10).
- TP4/TP5/TP6 (Keystone 5015) sit on the gate, Kelvin-source and gate-VCC nodes, which float at −1 kV. They're exposed metal at HV on a bench board. Use insulated HV test points or label them.
- R9 = 2.5k and C40–C42/C50 = 500 pF are orderable as listed (Stackpole RNCF, Yageo CC…501), so no change needed.

### P1 — ERC cleanup — `OPEN`
3 errors and 14 warnings:
- U7 VCC/VEE "power pin not driven": add PWR_FLAGs on the floating gate rail. Separately, two PWR_FLAGs (#FLG014 at (115.6, 142.2) and #FLG015 at (114.3, 177.8)) sit on the same net (power output ↔ power output). Remove one.
- 10 library symbol mismatches (D_Schottky × 7, D_Zener, Transformer_SP_1S, ACPL-P343), plus GND/GNDPWR (P2), plus the U7 footprint link (F1).
- The 1.27 mm wire stub at U12's GND pin (331.5, 168.9) is still there. On `Discharge`, `AMP_OUT` and `DISCHARGE_V` still label the same net. Remove one of the labels.

### P2 — GND/GNDPWR joined without a net-tie footprint — `OPTIONAL`
The two grounds are merged in the netlist (ERC `multiple_net_names`), so the PCB has a single `GND` net and nothing controls where the HV return joins. If you want a single-point join, use a NetTie footprint. The block diagram's note applies here: chassis GND must not join on the far side of the Hall sensor (or Rogowski). H1–H4 are AC-coupled through C46–C49, which respects that.

### P3 — Empty title block — `OPEN`
Neither sheet has a title block.

### P5 — PIB channels left unconnected by the merged board — `OPEN`
- PIB J12 (M80) is unconnected, so `ADC_NTC_DCHG` (PB12) floats to the R87 pull-up and reads as an open thermistor. The ICD lists a discharge-supply thermistor. Either have firmware ignore or mask it for this board, or route the TH1 position or a second NTC to it. Otherwise, any health check run at ARM time will fail.
- `VBAT_IGN` (J5.12/13) and `VBAT_DCHG` (J12.1/2) see no load. The PIB eFuses and UVLO are unaffected, but firmware power-good checks on those branches should expect "enabled, ~0 A".
