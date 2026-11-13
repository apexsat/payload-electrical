#ifndef CONFIG_H
#define CONFIG_H

// Latching-valve (V1, V3) coil pulse length. The valve needs 10-30 ms to switch;
// the GSE hardware pulse is 63-205 ms (about 100 ms typical). Revisit once the
// LHLA1221211H minimum pulse width is known (PIB review Q3).
#define VALVE_LATCH_PULSE_MS 100u

// How often app_run_once() reads the housekeeping ADC channels (NTCs, igniter
// V/I, MCU temp, VDDA). Each read blocks for ~45 us.
#define HK_READ_PERIOD_MS    100u

// Heartbeat LED toggle period.
#define LED_TOGGLE_PERIOD_MS 500u

#define DPT_READ_PERIOD_MS        500u

#define CDH_I2C_ADDR              0x42u
#define CDH_HEARTBEAT_TIMEOUT_MS  10000u
#define ARM_PGOOD_TIMEOUT_MS      200u
#define HEATER_OVERRIDE_MAX_MS    60000u

#define PD_RST_SETTLE_US          5u
#define PD_RST_HOLD_MS            50u
#define WAVE_PRE_SAMPLES          16u
#define PULSE_CAPTURE_TIMEOUT_MS  2u

#define NTC_10K_BETA_K            3380.0f
#define NTC_10K_R25_KOHM          10.0f
#define NTC_PIB_BIAS_KOHM         10.0f
#define NTC_IGN_BIAS_KOHM         10.0f
#define NTC_MIN_COUNTS            20u
#define NTC_MAX_COUNTS            4075u

#define IGN_SUPPLY_V_PER_PIN_V    (-455.0f)

#endif /* CONFIG_H */
