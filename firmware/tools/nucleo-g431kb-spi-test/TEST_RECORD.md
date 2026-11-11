# PIB Test 2 — SPI portion demonstrated using NUCLEO-G431KB instead of Raspberry Pi; I²C not tested.

**Date:**  
**Operator:**  
**Location:**  

## Firmware

| Item | Value |
|---|---|
| PIB firmware commit (`git rev-parse --short HEAD`, `firmware/`) | |
| PIB firmware uncommitted changes? (`git status --short`) | |
| Nucleo test commit (`tools/nucleo-g431kb-spi-test`) | |
| Nucleo test uncommitted changes? | |
| Nucleo ELF SHA-256 (`shasum -a 256 Debug/nucleo-g431kb-spi-test.elf`) | |

## Hardware

| Item | Value |
|---|---|
| PIB board revision / serial | |
| Nucleo board (NUCLEO-G431KB, MB1430 revision on sticker) | |
| Nucleo ST-LINK serial | |
| PIB ST-LINK model and serial | |
| PIB power source (model, pin, verified voltage, current rating) | |
| Actuator loads disconnected? (valves, heaters, igniter, discharge) | ☐ yes |
| Cable / adapter used on J3 | |
| Wire length (approx.) | |
| Continuity checks from README §4.3 done? | ☐ GND ☐ SCK ☐ MISO ☐ MOSI ☐ CS ☐ no shorts |

## SPI settings

| Item | Value |
|---|---|
| Role | Nucleo = controller, PIB = peripheral |
| Mode | 0 (CPOL low, CPHA 1st edge), 8-bit, MSB first |
| Pins (Nucleo → PIB J3) | A4/PA5 SCK → 8, D12/PB4 MISO ← 6, D11/PB5 MOSI → 7, D10/PA11 CS → 9, GND → 14 |
| `spi_test.sck_hz` observed | |
| Request / response | `[0x80, 0x05]`, ~5–6 ms gap, 5 dummy `0x00` bytes |
| Poll period | 500 ms |

## Observed telemetry (normal run, ≥ 60 s)

| Item | Value |
|---|---|
| Run duration | |
| `result` at end | |
| `raw[0..4]` (one snapshot) | |
| `status` | |
| `uptime_s` (and PIB on-time by stopwatch, if known) | |
| `uptime_progress_s` / `nucleo_elapsed_s` | |
| `progression_ok` | |
| `polls` | |
| `valid_samples` | |
| `hal_errors` | |
| `invalid_responses` | |
| `zero_responses` / `ff_responses` / `bad_uptime` / `unexpected_status` / `uptime_stalls` | |
| `uptime_resets` | |
| `first_pass_poll` | |
| LD2 state | |

## Negative controls

| Check | Expected | Observed |
|---|---|---|
| MISO unplugged | `zero_responses` rises, `result` → 2 within ~10 s, LD2 fast blink | |
| MISO reconnected | `result` → 1 within ~5–6 s | |
| PIB reset pressed | `uptime_resets` +1, `uptime_s` restarts near 0, `result` → 1 within ~5–6 s | |

## Result

☐ **SPI telemetry demonstration passed**  ☐ **Failed**

This result covers only the SPI telemetry path using a NUCLEO-G431KB as controller. It does not demonstrate I²C and does not by itself complete PIB Test 2.

**Notes / deviations:**  
