# Rogowski Coil Characterization: PEM CWTMini50HF/150 (flight coil head)

| | |
|---|---|
| **Purpose** | Record of the measurements that size the PIB Rogowski integrator (Rogowski sheet, U11). The open calibration check lives in `PIB_Schematic_Review.md`, item Q8. |
| **Flight configuration** | Only the bare coil head flies. The PIB does the integration. The complete CWT transducer, with PEM's battery-powered integrator, is ground calibration equipment only. |
| **Scope** | Teledyne LeCroy WavePro 735Zi (3.5 GHz, 40 GS/s) |
| **Recorded** | 2026-10-20, from measurements taken 2025-09-03 and 2026-10-19 |

All numbers read from scope photos are pixel-level estimates (about ±10–15%) unless they're quoted from the scope's own measurement parameters.

---

## 1. PEM CWTMini50HF/150 published specs (complete transducer)

Source: pemuk.com CWTMini50HF product page.

| Parameter | Value |
|---|---|
| Sensitivity (coil + PEM integrator) | 0.2 mV/A (5000 A/V) |
| Peak current rating | 30 kA (±6 V output at full scale) |
| Peak di/dt | 100 kA/µs (1.2 kA/µs rms) |
| Bandwidth | 1.0 Hz (−3 dB LF) to 50 MHz (HF, 100 mm coil) |
| Droop | 1.0 %/ms |
| Noise | 8.0 mV p-p |
| Output load | ≥ 100 kΩ ("not recommended for driving a 50 Ω load") |
| Output | BNC on a fixed 0.5 m lead |
| Integrator power / temperature | 4 × AA (alkaline or NiMH), 0 to +40 °C. Bench equipment, not flight. |

PEM doesn't publish the bare coil's mutual inductance or resistance, so those were measured (sections 4–6).

---

## 2. Thruster discharge current (2025-09-03)

Captured with the complete CWT transducer, scope channel C1, at 20 µs/div.

| Quantity | Value | Source |
|---|---|---|
| Peak current | **16.26 kA** | scope P3: max(C1) |
| Charge | 139.8 mC | scope P1: area(C1) |
| Pulse energy | 10.69 mJ | scope P2: area(F2), F2 = C2 × F1 |
| Igniter/second-channel peak | 220 A | scope P4: max(C2), C2 at 100 A/div |
| Waveform | Damped sinusoidal ring, bidirectional | screen |
| Period / frequency | ≈ 41 µs / ≈ 24 kHz | screen (≈ 20 µs per half-cycle) |
| Duration | ≈ 3 half-cycles, ≈ 65 µs | screen |
| Damping | ≈ 0.6× amplitude per half-cycle | screen |
| Derived peak onset dI/dt | ≈ 3.1 kA/µs | damped-sine fit to the above |

A narrow spike is visible at the trigger, before the main current. It's probably the igniter pulse.

---

## 3. Stick-discharge test setup (2026-10-19)

- **Source:** 380 µF charged to 200 V, discharged through a shorting stick.
- **Reference:** current transformer (CT) on scope C2, 2 kA/div, DC 1 MΩ.
- **Coil:** bare CWTMini50HF/150 coil head on scope C1, 1.00 V/div, DC 1 MΩ (and DC 50 Ω for the loading test).
- **Math:** F4 = integral of C1, in Wb = V·s.
- **Timebase:** 10–20 µs/div, 1 GS/s.
- **CWT transducer input check:** the PEM integrator's input measured **3 MΩ** on a multimeter, so PEM's integrator does not terminate the coil.

---

## 4. Transducer vs CT (capture at 5:27 PM, 20 µs/div)

- The transducer (C1, rescaled 5000 A/V, 2 kA/div) and the CT (C2, 2 kA/div) **track closely in shape**.
- On a rough pixel read, the transducer's negative trough reads **≈ 10–20% higher** than the CT's (≈ 6.3 kA vs ≈ 5.1 kA). **Check the CT's scaling** before using it as the absolute calibration reference.
- Stick-discharge ring: ≈ 49–55 µs period (≈ 18–20 kHz), heavily damped (≈ 0.22× per half-cycle).

---

## 5. Mutual inductance M (captures at 5:32 and 5:34 PM, 10 µs/div, coil into 1 MΩ)

| Method | Reading | Result |
|---|---|---|
| Integral ÷ current | F4 trough ≈ 1.14 div × 10 µWb ≈ **11.4 µV·s**; CT trough ≈ 3.2 div × 2 kA ≈ **6.4 kA** | **M ≈ 1.8 nH** |
| Onset step ÷ dI/dt | Coil step at t = 0 ≈ **−2.5 V**; onset dI/dt ≈ **1.6 kA/µs** (damped-sine fit: ≈ 49 µs period, ≈ 0.22× per half-cycle) | **M ≈ 1.6 nH** |
| **Working value** | | **M ≈ 1.7 nH (±10%)** |

- **Polarity:** a negative dI/dt at onset gives a negative coil voltage, with this coil orientation and cable.
- **Shape check:** the integral of the coil voltage matches the CT current shape, so the coil is behaving correctly.
- **dI/dt steps:** the coil voltage has two sharp steps, at ≈ 41 µs and ≈ 84 µs. Those are abrupt dI/dt changes in the discharge circuit (shorting-stick arcing or contact bounce), not coil artifacts: the integral stays smooth through them.
- **Integral drift:** without offset correction, the scope integral drifts (the uncorrected 1 MΩ positive lobe doesn't scale cleanly against the CT). For precise M, apply the offset correction ("then add" ≈ −30 mV before integration) and read at the **negative trough**.

---

## 6. Coil source impedance (captures at 5:51 PM at 50 Ω and 5:52 PM at 1 MΩ, 10 µs/div)

| Load | Onset step | Integral trough | CT trough | M<sub>eff</sub> |
|---|---|---|---|---|
| 1 MΩ | ≈ 2.0 V | off-screen at 2 µWb/div (consistent with ≈ 9.8 µV·s) | ≈ 5.5 kA | ≈ 1.8 nH |
| 50 Ω (offset-corrected) | ≈ 0.68 V | ≈ 1.64 div × 2 µWb ≈ 3.3 µV·s | ≈ 5.5 kA | ≈ 0.6 nH |

- **Attenuation at 50 Ω:** ≈ 0.33–0.34× by both the step and the integral, with the waveform shape unchanged. So the coil's source impedance is **resistive**, not inductive.
- **DC resistance, measured with a multimeter (center pin to shield):** **154 Ω**. That's the authoritative value. It predicts 50/(50 + 154) ≈ 0.25× at 50 Ω, within the error of reading a scope photo. (The photo ratio alone would imply ≈ 100 Ω.)
- **Conclusion:** the coil must only ever see a high-impedance load (PEM uses 3 MΩ). **Never terminate it at 50 Ω.**

---

## 7. Derived flight values and PIB integrator design

| Quantity | Value |
|---|---|
| Flight peak current | 16.26 kA |
| Peak onset dI/dt | ≈ 3.1 kA/µs |
| Peak open-circuit coil voltage | ≈ 1.7 nH × 3.1 kA/µs ≈ **5.3 V** (≤ ~6 V with M uncertainty) |
| Gain target | R79·C45 ≈ M·I<sub>pk</sub>/1.5 V ≈ 18 µs |

As built on the Rogowski sheet:
- J7 (MMCX), protected by D38 (SD12C-01F, bidirectional 12 V TVS), with R95 = 100k as a bleeder (not a termination).
- C69 4.7 µF X7R (0603, ≥ 10 V) → R79 2.00k → OP27 inverting integrator, with C45 10 nF C0G ∥ R98 1 M, and + input at V<sub>REF</sub> = +3.3V/2 (R96/R97 10k/10k, C70 1 µF).
- Reset: Q3/Q4 (BSS131, chosen for lower leakage) back-to-back across C45, gate-driven to +5 V by Q5 (BSS131) from `~{PD_RST}`.
- Output to PA1: R102 100 Ω → BAT54J clamp pair → R104 1k + C71 470 pF, matched to PA0.
- Peak output = M·I<sub>pk</sub>/((R79 + 154 Ω)·C45) ≈ **1.28 V** around 1.65 V (0.37–2.93 V); ≈ 1.41 V if M is 10% high.
- Coil-resistance effects:
  - **Loss:** ≈ 7% (154/2154), a fixed gain factor that calibration absorbs.
  - **Temperature:** if the 154 Ω is copper, ±40 °C moves it ±24 Ω, so the gain moves ≈ ±1.1%. Correct with the PIB thermistor if needed. The lower-tempco alternative (R79 4.99k, C45 4.7 nF) gives ≈ ±0.5%.
  - **R95:** 100k costs only ≈ 0.15%.
- Droop ≈ 1.3–1.6% over 65 µs (τ<sub>AC</sub> ≈ 7–10 ms after C69 DC-bias derating; τ<sub>leak</sub> = 10 ms). The OP27 bias offset (≈ 40–60 mV) is removed by the pre-shot baseline.

---

## 8. Measurements still to take

1. **Bare coil on a real thruster shot** (≈ 16 kA), into 1 MΩ with the CT or CWT as reference:
   - peak coil voltage (expected ≈ 5.3 V), which confirms the J7 TVS standoff and the integrator gain;
   - fast transients at ignition (use a fast timebase around the trigger);
   - **exact M** from the offset-corrected integral at the trough ÷ reference peak.
2. **Coil in its flight mounting position:** Rogowski sensitivity varies a few percent with conductor position inside the coil, so calibrate M as the coil will be mounted.
3. **Pickup floor:** one shot with the coil near the discharge but not around the conductor, to measure electrostatic and magnetic pickup from the 400 V electrodes.
4. **CT scaling check** against the CWT transducer (section 4 discrepancy).
5. **After the BNC → MMCX re-termination:** re-measure the coil's DC resistance (154 Ω before) and M through the final cable and connector. The integrator gain depends on R79 + Z<sub>s</sub>. (The coil detaches from the CWT integrator by BNC, confirmed 2026-10-20.)
