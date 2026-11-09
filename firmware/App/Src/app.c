#include "app.h"
#include "main.h"
#include "config.h"
#include "valves.h"
#include "dpt.h"
#include "pib_adc.h"
#include "spi_slave.h"
#include "i2c_slave.h"
#include "telemetry.h"

#include <math.h>

// CubeMX handles, defined in Core/Src/main.c
extern ADC_HandleTypeDef hadc1;
extern ADC_HandleTypeDef hadc2;
extern SPI_HandleTypeDef hspi1;
extern I2C_HandleTypeDef hi2c2;
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

static valve_t valve1;  // latching
static valve_t valve2;  // normally-closed solenoid
static valve_t valve3;  // latching

// ADC1 + ADC2. Latest housekeeping values: pib_adc.hk (raw[], mv[],
// vdda_mv, temp_mcu_degC, indexed by hk_ch_t). Bursts: pib_adc_burst_*().
static pib_adc_t pib_adc;

static ms5540c_t dpt;
static float dpt_t_C[2];
static float dpt_p_mbar[2];
static bool dpt_ok[2];
static uint8_t dpt_next;

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

static void start_arming(uint32_t now)
{
    HAL_GPIO_WritePin(VBAT_EN_GPIO_Port, VBAT_EN_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(ARM_SAFE_GPIO_Port, ARM_SAFE_Pin, GPIO_PIN_SET);
    state = PIB_ARMING;
    state_ms = now;
}

static void start_safing(uint32_t now)
{
    valve_close(&valve1);
    valve_close(&valve2);
    valve_close(&valve3);
    state = PIB_SAFING;
    state_ms = now;
}

static void state_step(uint32_t now)
{
    switch (state) {
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

static void app_execute(const telemetry_cmd_t *cmd, uint32_t now)
{
    switch (cmd->addr) {
    case TLM_REG_SAFE_PAYLOAD:
        if (state == PIB_ARMING || state == PIB_ARMED) {
            start_safing(now);
        }
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
        }
        break;

    case TLM_REG_HEATER_OVERRIDE:
        heater_on = (cmd->data[0] != 0u) && (cmd->data[1] != 0u);
        heater_on_ms = now;
        set_heaters(heater_on);
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

static void build_housekeeping(telemetry_hk_t *hk, uint32_t now)
{
    hk->status = (state == PIB_ARMED) ? TLM_STATUS_ARMED : 0u;
    hk->uptime_s = (float)now / 1000.0f;
    hk->frit_mbar = dpt_ok[DPT_FRIT] ? dpt_p_mbar[DPT_FRIT] / 10.0f : NAN;
    hk->frit_degC = dpt_ok[DPT_FRIT] ? dpt_t_C[DPT_FRIT] / 10.0f : NAN;
    hk->tank_mbar = dpt_ok[DPT_TANK] ? dpt_p_mbar[DPT_TANK] / 10.0f : NAN;
    hk->tank_degC = dpt_ok[DPT_TANK] ? dpt_t_C[DPT_TANK] / 10.0f : NAN;
    hk->tank_ntc_degC[0] = NAN;
    hk->tank_ntc_degC[1] = NAN;
    hk->pib_degC[0] = NAN;
    hk->pib_degC[1] = (float)pib_adc.hk.temp_mcu_degC;
    hk->flow_ul_min = NAN;
    hk->flow_degC = NAN;
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
