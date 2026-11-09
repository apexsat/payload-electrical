# PIB Firmware Architecture

STM32G431RBT6 w/ no RTOS.

## Layers

- `Core/`, `Drivers/`: CubeMX-generated init code and ST HAL
- `Board/`: one driver per PIB chip or circuit
- `App/`: mission logic, calls Board drivers

Each folder has `Inc/` and `Src/`. `main.c` calls `app_init()` once and `app_run_once()` in its `while (1)` loop.

## Board drivers

Each driver is the only code that touches its pins and peripherals. HAL handles and pins (port + pin) are passed in at init, so drivers never include `main.h`.

CubeMX's `MX_*_Init()` functions configure every peripheral before `app_init()` runs, so drivers receive initialized handles. A driver's init does what CubeMX does not:
- start peripherals: TIM3 MCLK output, ADC calibration, SPI1 receive interrupt, DWT counter
- reconfigure at runtime when needed: ADC1 scan vs. dual mode, I2C2 slave address
- check the handle settings it depends on (e.g. SPI1 8-bit slave with DMA) and return an error if they don't match

| Driver    | Hardware                                                                                              |
|-----------|-------------------------------------------------------------------------------------------------------|
| `DPT`     | Bit-banged GPIO: PC10 SCK, PC12 DIN, PC11 DOUT. PA15 mux select (low tank, high frit), TIM3 CH2 MCLK  |
| `lg16`    | I2C3 flow sensor                                                                                      |
| `adc`     | ADC1, ADC2, DMA1 Ch1, sample trigger timer. Housekeeping scan mode and dual-simultaneous capture mode |
| `analog`  | Conversions only: counts to volts, amps, °C                                                           |
| `power`   | BAT_EN, DISCHARGE_EN, IGNITER_EN, ARM/SAFE, the four PGOOD inputs                                     |
| `valves`  | VALVE1 L/H, VALVE2_EN, VALVE3 L/H                                                                     |
| `igniter` | IGNITER_PULSE, ~PD_RST, DWT cycle counter for microsecond timing                                      |

HTR1_EN, HTR2_EN, WDG_IN (MAX823) and DEBUG_LED have no driver. App code drives them directly with `HAL_GPIO_WritePin` / `HAL_GPIO_TogglePin`, using the pin names from `main.h`.

## App modules

| Module         | Job                                                                   |
|----------------|-----------------------------------------------------------------------|
| `app`          | Init and main loop                                                    |
| `state`        | ARM/SAFE state machine. Only module that changes power, valves or ARM |
| `pulse`        | Pulse sequence                                                        |
| `housekeeping` | Non-blocking 10 Hz sensor polling                                     |
| `thermal`      | Tank heater control                                                   |
| `health`       | PGOOD fault detection, comms-loss timeout                             |
| `telemetry`    | Telemetry record and waveform buffer                                  |
| `commands`     | Single command dispatcher                                             |
| `cdh_link`     | SPI1 slave (I2C2 slave later), DRDY                                   |
| `regmap`       | CDH register map                                                      |
| `console`      | Debug commands over LPUART1                                           |
| `config.h`     | Tunable constants                                                     |

`cdh_link` and `console` both feed `commands`, so every command can be tested over UART before SPI exists.

## Main loop

```c
void app_run_once(void)
{
    HAL_GPIO_TogglePin(WDG_IN_GPIO_Port, WDG_IN_Pin);
    cdh_link_service();
    console_service();
    state_step();
    housekeeping_step();
    thermal_step();
    health_step();
}
```

Every step returns immediately. Slow operations are split into states that are checked on later passes. Timing uses `HAL_GetTick()`. The only blocking sections are the pulse sequence and latching-valve pulses.

## Interrupts

Interrupts copy data, set flags and return.

| Interrupt         | Priority | Hands off via                               |
|-------------------|----------|---------------------------------------------|
| SPI1 (CDH)        | 0        | Command ring buffer, `pulse_requested` flag |
| DMA1 Ch1 (ADC)    | 1        | `adc_done` flag                             |
| I2C2 (CDH backup) | 2        | Command ring buffer                         |
| LPUART1 RX        | 3        | Console line buffer                         |
| SysTick           | 15       | `HAL_GetTick()`                             |

HAL completion callbacks are shared by every instance of a peripheral type (one `HAL_I2C_ErrorCallback` serves I2C2 and I2C3), so each callback dispatches on `hi2c->Instance`.

Shared variables are `volatile`. Telemetry is double-buffered: the main loop fills the back copy and swaps a pointer, and interrupts read only the front copy.

## State machine

```
SAFE ── ARM ──▶ ARMING ── PGOOD ok──▶ ARMED ──PULSE──▶ PULSING
 ▲               │                   │  ▲               │
 │               │ fault/timeout     │  └─────done──────┘
 │               ▼                   │
 └──────────── SAFING ◀──────────────┘
                 SAFE cmd, comms timeout, fault
```

| State   | Action                                                       |
|---------|--------------------------------------------------------------|
| SAFE    | HV off, valves closed, ARM low. Heaters run                  |
| ARMING  | ARM high, open valves, enable discharge and igniter supplies |
| ARMED   | Wait for PULSE                                               |
| PULSING | Run the pulse sequence once                                  |
| SAFING  | HV off, close valves, ARM low                                |

PULSE outside ARMED is ignored. Faults go to SAFING; the PIB does not re-arm itself.

## Pulse sequence

1. DRDY low
2. Set up dual ADC capture into the waveform buffer
3. Release ~PD_RST
4. Start the 2 MHz sample trigger
5. Drive IGNITER_PULSE high for the pulse width
6. Wait for capture complete
7. Read igniter peak V and peak I
8. Assert ~PD_RST
9. Write the pulse header to telemetry
10. DRDY high

Waveform buffer: `uint32_t wave[N]`. Each word holds ADC1 (discharge voltage) in the low half and ADC2 (discharge current) in the high half.
