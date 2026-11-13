#include "app.h"
#include "main.h"
#include "config.h"
#include "valves.h"
#include "dpt.h"
#include "pib_adc.h"
#include "spi_slave.h"
#include "i2c_slave.h"
#include "telemetry.h"
#include "igniter.h"
#include "thermistor.h"
#include "flow.h"

#include <math.h>

// CubeMX handles, defined in Core/Src/main.c
extern ADC_HandleTypeDef hadc1;
extern ADC_HandleTypeDef hadc2;
extern SPI_HandleTypeDef hspi1;
extern I2C_HandleTypeDef hi2c2;
extern I2C_HandleTypeDef hi2c3;
extern TIM_HandleTypeDef htim3;

enum {
    DPT_TANK = 0,
    DPT_FRIT = 1,
};

typedef enum {
    PIB_SAFE,
    PIB_ARMING,
    PIB_ARMED,
    PIB_SAFING,
} pib_state_t;

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
} pin_t;

static const pin_t outputs[TLM_OUT_COUNT] = {
    [TLM_OUT_VBAT_EN]      = { VBAT_EN_GPIO_Port,      VBAT_EN_Pin },
    [TLM_OUT_ARM_SAFE]     = { ARM_SAFE_GPIO_Port,     ARM_SAFE_Pin },
    [TLM_OUT_IGNITER_EN]   = { IGNITER_EN_GPIO_Port,   IGNITER_EN_Pin },
    [TLM_OUT_DISCHARGE_EN] = { DISCHARGE_EN_GPIO_Port, DISCHARGE_EN_Pin },
    [TLM_OUT_HTR1_EN]      = { HTR1_EN_GPIO_Port,      HTR1_EN_Pin },
    [TLM_OUT_HTR2_EN]      = { HTR2_EN_GPIO_Port,      HTR2_EN_Pin },
    [TLM_OUT_PD_RST]       = { PD_RST_GPIO_Port,       PD_RST_Pin },
    [TLM_OUT_WDG_IN]       = { WDG_IN_GPIO_Port,       WDG_IN_Pin },
};

static const pin_t inputs[TLM_IN_COUNT] = {
    [TLM_IN_PGOOD_12V]       = { PGOOD_12V_GPIO_Port,       PGOOD_12V_Pin },
    [TLM_IN_VBAT_PGOOD]      = { VBAT_PGOOD_GPIO_Port,      VBAT_PGOOD_Pin },
    [TLM_IN_IGNITER_PGOOD]   = { IGNITER_PGOOD_GPIO_Port,   IGNITER_PGOOD_Pin },
    [TLM_IN_DISCHARGE_PGOOD] = { DISCHARGE_PGOOD_GPIO_Port, DISCHARGE_PGOOD_Pin },
};

static valve_t valve1;  // latching
static valve_t valve2;  // normally-closed solenoid
static valve_t valve3;  // latching

// ADC1 + ADC2. Latest housekeeping values: pib_adc.hk (raw[], mv[],
// vdda_mv, temp_mcu_degC, indexed by hk_ch_t). Bursts: pib_adc_burst_*().
static pib_adc_t pib_adc;

static igniter_t igniter;
static uint32_t burst_buf[WAVE_PRE_SAMPLES + TLM_WAVE_SAMPLES];
static bool drdy;
static uint32_t pulse_index;
static bool pd_rst_held;
static uint32_t pd_rst_ms;

static thermistor_t ntc_pib = { NTC_10K_BETA_K, 25.0f, NTC_10K_R25_KOHM, NTC_PIB_BIAS_KOHM };
static thermistor_t ntc_ign = { NTC_10K_BETA_K, 25.0f, NTC_10K_R25_KOHM, NTC_IGN_BIAS_KOHM };

static ms5540c_t dpt;
static float dpt_t_C[2];
static float dpt_p_mbar[2];
static bool dpt_ok[2];
static uint8_t dpt_next;

typedef enum {
    FLOW_MEAS_FLOW,
    FLOW_MEAS_TEMP,
} flow_meas_t;

static lg160431d_t flow = { .addr = FLOW_I2C_ADDR, .i2c = &hi2c3 };
static bool flow_ok;              // init succeeded
static flow_meas_t flow_meas;     // measurement currently triggered / next to trigger
static float flow_ul_min = NAN;   // last good readings; NAN until the first one
static float flow_temp_degC = NAN;

static uint32_t last_hk_ms;
static uint32_t last_led_ms;
static uint32_t last_dpt_ms;

static pib_state_t state;
static uint32_t state_ms;
static uint32_t heartbeat_ms;
static bool heater_on;
static uint32_t heater_on_ms;
static float tank_setpoint_degC;
static uint32_t bad_arm_codes;

static void set_heaters(bool on)
{
    GPIO_PinState level = on ? GPIO_PIN_SET : GPIO_PIN_RESET;
    if (on) {
        HAL_GPIO_WritePin(VBAT_EN_GPIO_Port, VBAT_EN_Pin, GPIO_PIN_SET);
    }
    HAL_GPIO_WritePin(HTR1_EN_GPIO_Port, HTR1_EN_Pin, level);
    HAL_GPIO_WritePin(HTR2_EN_GPIO_Port, HTR2_EN_Pin, level);
}

static bool pin_high(const pin_t *p)
{
    return HAL_GPIO_ReadPin(p->port, p->pin) == GPIO_PIN_SET;
}

static bool arm_output(uint8_t id)
{
    return (id == TLM_OUT_ARM_SAFE) || (id == TLM_OUT_IGNITER_EN) || (id == TLM_OUT_DISCHARGE_EN);
}

static bool any_arm_output_high(void)
{
    for (uint8_t i = 0; i < TLM_OUT_COUNT; i++) {
        if (arm_output(i) && pin_high(&outputs[i])) {
            return true;
        }
    }
    return false;
}

static void set_output(uint8_t id, bool on, uint32_t now)
{
    bool heater = (id == TLM_OUT_HTR1_EN) || (id == TLM_OUT_HTR2_EN);
    if (on && heater) {
        HAL_GPIO_WritePin(VBAT_EN_GPIO_Port, VBAT_EN_Pin, GPIO_PIN_SET);
    }
    HAL_GPIO_WritePin(outputs[id].port, outputs[id].pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
    if (on && arm_output(id)) {
        heartbeat_ms = now;
    }
    if (heater) {
        heater_on = pin_high(&outputs[TLM_OUT_HTR1_EN]) || pin_high(&outputs[TLM_OUT_HTR2_EN]);
        if (on) {
            heater_on_ms = now;
        }
    }
}

static valve_t *valve_by_id(uint8_t id)
{
    switch (id) {
    case 1u: return &valve1;
    case 2u: return &valve2;
    case 3u: return &valve3;
    default: return NULL;
    }
}

static void start_arming(uint32_t now)
{
    HAL_GPIO_WritePin(VBAT_EN_GPIO_Port, VBAT_EN_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(ARM_SAFE_GPIO_Port, ARM_SAFE_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(IGNITER_EN_GPIO_Port, IGNITER_EN_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(DISCHARGE_EN_GPIO_Port, DISCHARGE_EN_Pin, GPIO_PIN_SET);
    state = PIB_ARMING;
    state_ms = now;
}

static void start_safing(uint32_t now)
{
    HAL_GPIO_WritePin(IGNITER_EN_GPIO_Port, IGNITER_EN_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(DISCHARGE_EN_GPIO_Port, DISCHARGE_EN_Pin, GPIO_PIN_RESET);
    valve_close(&valve1);
    valve_close(&valve2);
    valve_close(&valve3);
    state = PIB_SAFING;
    state_ms = now;
}

static void state_step(uint32_t now)
{
    switch (state) {
    case PIB_SAFE:
        if (any_arm_output_high() && (now - heartbeat_ms) >= CDH_HEARTBEAT_TIMEOUT_MS) {
            start_safing(now);
        }
        break;

    case PIB_ARMING:
        if (HAL_GPIO_ReadPin(PGOOD_12V_GPIO_Port, PGOOD_12V_Pin) == GPIO_PIN_SET) {
            valve_open(&valve1);
            valve_open(&valve2);
            valve_open(&valve3);
            state = PIB_ARMED;
        } else if ((now - state_ms) >= ARM_PGOOD_TIMEOUT_MS) {
            start_safing(now);
        }
        break;

    case PIB_ARMED:
        if ((now - heartbeat_ms) >= CDH_HEARTBEAT_TIMEOUT_MS) {
            start_safing(now);
        }
        break;

    case PIB_SAFING:
        if (!valve_is_pulsing(&valve1) && !valve_is_pulsing(&valve3)) {
            HAL_GPIO_WritePin(ARM_SAFE_GPIO_Port, ARM_SAFE_Pin, GPIO_PIN_RESET);
            state = PIB_SAFE;
        }
        break;

    default:
        break;
    }
}

static void set_drdy(bool on)
{
    drdy = on;
    HAL_GPIO_WritePin(DRDY_GPIO_Port, DRDY_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void fire_pulse(uint32_t width_100ns)
{
    set_drdy(false);
    igniter_pd_release(&igniter);
    igniter_delay_us(PD_RST_SETTLE_US);

    if (pib_adc_burst_start(&pib_adc, burst_buf, WAVE_PRE_SAMPLES + TLM_WAVE_SAMPLES) != HAL_OK) {
        igniter_pd_reset(&igniter);
        pd_rst_held = false;
        return;
    }

    uint32_t c0 = 0u;
    uint32_t c1 = 0u;
    __disable_irq();
    uint32_t sync0 = pib_adc_burst_sync(&pib_adc, 0u, &c0);
    uint32_t first = pib_adc_burst_sync(&pib_adc, WAVE_PRE_SAMPLES, &c1);
    bool synced = (sync0 != 0u) && (first > sync0);
    if (first == 0u) {
        first = pib_adc_burst_count(&pib_adc);
    }
    igniter_pulse(&igniter, width_100ns);
    __enable_irq();
    pd_rst_ms = HAL_GetTick();
    pd_rst_held = true;

    float sample_us = NAN;
    if (synced) {
        sample_us = (float)(c1 - c0) * 1e6f / ((float)SystemCoreClock * (float)(first - sync0));
    }

    adc_burst_state_t s;
    uint32_t t0 = HAL_GetTick();
    do {
        s = pib_adc_burst_poll(&pib_adc);
    } while (s != ADC_BURST_DONE && s != ADC_BURST_ERROR &&
             (HAL_GetTick() - t0) <= PULSE_CAPTURE_TIMEOUT_MS);

    pulse_index++;
    uint16_t peak_i = 0u;
    uint16_t peak_v = 0u;
    if (s == ADC_BURST_DONE && pib_adc_read_housekeeping(&pib_adc) == HAL_OK) {
        peak_i = pib_adc.hk.raw[HK_I_IGN_PK];
        peak_v = pib_adc.hk.raw[HK_V_IGN_PK];
    }

    uint32_t start = (first > WAVE_PRE_SAMPLES) ? WAVE_PRE_SAMPLES : first;
    telemetry_set_pulse_header(pulse_index, peak_i, peak_v, sample_us, first - start);

    if (s != ADC_BURST_DONE) {
        pib_adc_burst_abort(&pib_adc);
        return;
    }

    telemetry_set_waveform(&burst_buf[start]);
    set_drdy(true);
}

static void app_execute(const telemetry_cmd_t *cmd, uint32_t now)
{
    switch (cmd->addr) {
    case TLM_REG_SAFE_PAYLOAD:
        start_safing(now);
        break;

    case TLM_REG_ARM_PAYLOAD:
        if (cmd->data[0] != TLM_ARM_CODE) {
            bad_arm_codes++;
            break;
        }
        heartbeat_ms = now;
        if (state == PIB_SAFE) {
            start_arming(now);
        }
        break;

    case TLM_REG_FIRE_PULSE:
        if (state == PIB_ARMED) {
            heartbeat_ms = now;
            fire_pulse((uint32_t)cmd->data[0] | ((uint32_t)cmd->data[1] << 8) |
                       ((uint32_t)cmd->data[2] << 16) | ((uint32_t)cmd->data[3] << 24));
        }
        break;

    case TLM_REG_HEATER_OVERRIDE:
        heater_on = (cmd->data[0] != 0u) && (cmd->data[1] != 0u);
        heater_on_ms = now;
        set_heaters(heater_on);
        break;

    case TLM_REG_SET_VALVE: {
        valve_t *v = valve_by_id(cmd->data[0]);
        if (v == NULL) {
            break;
        }
        if (cmd->data[1] != 0u) {
            valve_open(v);
        } else {
            valve_close(v);
        }
        break;
    }

    case TLM_REG_SET_OUTPUT:
        if (cmd->data[0] < TLM_OUT_COUNT) {
            set_output(cmd->data[0], cmd->data[1] != 0u, now);
        }
        break;

    case TLM_REG_SET_TANK_TEMP:
        tank_setpoint_degC = telemetry_get_f32(cmd->data);
        break;

    default:
        break;
    }
}

static void read_dpt(void)
{
    uint8_t s = dpt_next;
    dpt_ok[s] = !ms5540c_measure(&dpt, s == DPT_FRIT, &dpt_t_C[s], &dpt_p_mbar[s]);
    dpt_next = (s == DPT_TANK) ? DPT_FRIT : DPT_TANK;
}

// Alternate flow -> temp. Each call either triggers the next
// measurement or polls the one in progress; the sensor NACKs until it is done.
static void flow_step(void)
{
    if (!flow_ok) {
        return;
    }

    if (!flow.conv_in_progress) {
        switch (flow_meas) {
        case FLOW_MEAS_FLOW: (void)lg160431d_trigger_flow(&flow); break;
        case FLOW_MEAS_TEMP: (void)lg160431d_trigger_temp(&flow); break;
        }
        return;  // on failure conv_in_progress stays false and we retry next call
    }

    float v;
    bool err;
    switch (flow_meas) {
    case FLOW_MEAS_FLOW: err = lg160431d_poll_flow(&flow, &v); if (!err) flow_ul_min = v;    break;
    default:             err = lg160431d_poll_temp(&flow, &v); if (!err) flow_temp_degC = v; break;
    }

    // Still converting (NACK): keep polling. Done (good result or CRC fail): move on.
    if (!err || !flow.conv_in_progress) {
        flow_meas = (flow_meas == FLOW_MEAS_FLOW) ? FLOW_MEAS_TEMP : FLOW_MEAS_FLOW;
    }
}

static float ntc_degC(thermistor_t *t, hk_ch_t ch)
{
    uint16_t raw = pib_adc.hk.raw[ch];
    if (raw < NTC_MIN_COUNTS || raw > NTC_MAX_COUNTS) {
        return NAN;
    }
    return ratiometric_to_thermistor_temp(t, (float)raw / 4095.0f);
}

static void build_housekeeping(telemetry_hk_t *hk, uint32_t now)
{
    hk->status = (state == PIB_ARMED) ? TLM_STATUS_ARMED : 0u;
    if (drdy) {
        hk->status |= TLM_STATUS_DRDY;
    }
    hk->uptime_s = (float)now / 1000.0f;
    hk->frit_mbar = dpt_ok[DPT_FRIT] ? dpt_p_mbar[DPT_FRIT] : NAN;
    hk->frit_degC = dpt_ok[DPT_FRIT] ? dpt_t_C[DPT_FRIT] : NAN;
    hk->tank_mbar = dpt_ok[DPT_TANK] ? dpt_p_mbar[DPT_TANK] : NAN;
    hk->tank_degC = dpt_ok[DPT_TANK] ? dpt_t_C[DPT_TANK] : NAN;
    hk->tank_ntc_degC[0] = NAN;
    hk->tank_ntc_degC[1] = NAN;
    hk->pib_degC[0] = ntc_degC(&ntc_pib, HK_NTC_PIB);
    hk->pib_degC[1] = (float)pib_adc.hk.temp_mcu_degC;
    hk->flow_ul_min = flow_ul_min;
    hk->flow_degC = flow_temp_degC;
    if (!flow_ok) {
        hk->status |= TLM_STATUS_FLOW_FAULT;
    }
    hk->ign_supply_v = (float)pib_adc.hk.mv[HK_V_IGN_SUPPLY] / 1000.0f * IGN_SUPPLY_V_PER_PIN_V;
    hk->ign_degC = ntc_degC(&ntc_ign, HK_NTC_IGN);
    hk->dchg_degC = NAN;
    hk->pib_state = (uint8_t)state;
    hk->outputs = 0u;
    for (uint8_t i = 0; i < TLM_OUT_COUNT; i++) {
        if (pin_high(&outputs[i])) {
            hk->outputs |= (uint8_t)(1u << i);
        }
    }
    hk->inputs = 0u;
    for (uint8_t i = 0; i < TLM_IN_COUNT; i++) {
        if (pin_high(&inputs[i])) {
            hk->inputs |= (uint8_t)(1u << i);
        }
    }
    hk->valves = (uint8_t)(valve_get_state(&valve1) | (valve_get_state(&valve2) << 2) |
                           (valve_get_state(&valve3) << 4) |
                           (valve_is_pulsing(&valve1) ? 0x40u : 0u) |
                           (valve_is_pulsing(&valve3) ? 0x80u : 0u));
    hk->ntc_raw[0] = pib_adc.hk.raw[HK_TANK_NTC_1];
    hk->ntc_raw[1] = pib_adc.hk.raw[HK_TANK_NTC_2];
    hk->ntc_raw[2] = pib_adc.hk.raw[HK_NTC_DCHG];
}

void app_init(void)
{
    valve_init_latching(&valve1, VALVE1_H_GPIO_Port, VALVE1_H_Pin,
                                 VALVE1_L_GPIO_Port, VALVE1_L_Pin,
                                 VALVE_LATCH_PULSE_MS);
    valve_init_solenoid(&valve2, VALVE2_EN_GPIO_Port, VALVE2_EN_Pin);
    valve_init_latching(&valve3, VALVE3_H_GPIO_Port, VALVE3_H_Pin,
                                 VALVE3_L_GPIO_Port, VALVE3_L_Pin,
                                 VALVE_LATCH_PULSE_MS);

    // ADC1 = master (V_DCHG + housekeeping), ADC2 = slave (I_DCHG)
    if (pib_adc_init(&pib_adc, &hadc1, &hadc2) != HAL_OK) {
        Error_Handler();
    }

    dpt.clk_gpio_port = DPT_SCK_GPIO_Port;
    dpt.clk_gpio_pin = DPT_SCK_Pin;
    dpt.dout_gpio_port = DPT_MISO_GPIO_Port;
    dpt.dout_gpio_pin = DPT_MISO_Pin;
    dpt.din_gpio_port = DPT_MOSI_GPIO_Port;
    dpt.din_gpio_pin = DPT_MOSI_Pin;
    dpt.mux_gpio_port = DPT_TANK_FRIT_SEL_GPIO_Port;
    dpt.mux_gpio_pin = DPT_TANK_FRIT_SEL_Pin;
    dpt.mclk_tim = &htim3;
    dpt.mclk_tim_channel = TIM_CHANNEL_2;
    ms5540c_init(&dpt);

    // No-hold-master mode so app_run_once() can poll instead of blocking
    flow_ok = !lg160431d_init(&flow, FLOW_CAL_FIELD, false, FLOW_RESOLUTION, false);

    igniter.pulse_port = IGNITER_PULSE_GPIO_Port;
    igniter.pulse_pin = IGNITER_PULSE_Pin;
    igniter.pd_rst_port = PD_RST_GPIO_Port;
    igniter.pd_rst_pin = PD_RST_Pin;
    igniter_init(&igniter);

    state = PIB_SAFE;
    telemetry_init();
    (void)pib_adc_read_housekeeping(&pib_adc);
    telemetry_hk_t hk;
    build_housekeeping(&hk, HAL_GetTick());
    telemetry_update_snapshot(&hk);

    if (spi_slave_init(&hspi1, telemetry_on_spi_frame) != HAL_OK) {
        Error_Handler();
    }
    if (i2c_slave_init(&hi2c2, CDH_I2C_ADDR, telemetry_on_i2c_write, telemetry_on_i2c_read) != HAL_OK) {
        Error_Handler();
    }
}

void app_run_once(void)
{
    uint32_t now = HAL_GetTick();

    // End any latching-valve pulse whose time is up
    valve_update(&valve1);
    valve_update(&valve2);
    valve_update(&valve3);

    telemetry_cmd_t cmd;
    while (telemetry_take_command(&cmd)) {
        app_execute(&cmd, now);
    }

    state_step(now);

    if (pd_rst_held && (now - pd_rst_ms) >= PD_RST_HOLD_MS) {
        pd_rst_held = false;
        igniter_pd_reset(&igniter);
    }

    if (drdy && telemetry_take_waveform_read()) {
        set_drdy(false);
    }

    if (heater_on && (now - heater_on_ms) >= HEATER_OVERRIDE_MAX_MS) {
        heater_on = false;
        set_heaters(false);
    }

    if ((now - last_dpt_ms) >= DPT_READ_PERIOD_MS &&
        !valve_is_pulsing(&valve1) && !valve_is_pulsing(&valve3)) {
        last_dpt_ms = now;
        read_dpt();
        now = HAL_GetTick();
    }

    flow_step();

    // Housekeeping read. Returns HAL_BUSY (skipped) while a burst is running.
    if ((now - last_hk_ms) >= HK_READ_PERIOD_MS) {
        last_hk_ms = now;
        (void)pib_adc_read_housekeeping(&pib_adc);
        telemetry_hk_t hk;
        build_housekeeping(&hk, now);
        telemetry_update_snapshot(&hk);
    }

    // Finishes any burst started with pib_adc_burst_start() (post-burst VREFINT)
    (void)pib_adc_burst_poll(&pib_adc);

    if ((now - last_led_ms) >= LED_TOGGLE_PERIOD_MS) {
        last_led_ms = now;
        HAL_GPIO_TogglePin(DEBUG_LED_GPIO_Port, DEBUG_LED_Pin);
    }
}
