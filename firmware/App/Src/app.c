#include "app.h"
#include "main.h"
#include "config.h"
#include "valves.h"
#include "dpt.h"
#include "pib_adc.h"

// CubeMX handles, defined in Core/Src/main.c
extern ADC_HandleTypeDef hadc1;
extern ADC_HandleTypeDef hadc2;

static valve_t valve1;  // latching
static valve_t valve2;  // normally-closed solenoid
static valve_t valve3;  // latching

// ADC1 + ADC2. Latest housekeeping values: pib_adc.hk (raw[], mv[],
// vdda_mv, temp_mcu_degC, indexed by hk_ch_t). Bursts: pib_adc_burst_*().
static pib_adc_t pib_adc;

static uint32_t last_hk_ms;
static uint32_t last_led_ms;

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
}

void app_run_once(void)
{
    uint32_t now = HAL_GetTick();

    // End any latching-valve pulse whose time is up
    valve_update(&valve1);
    valve_update(&valve2);
    valve_update(&valve3);

    // Housekeeping read. Returns HAL_BUSY (skipped) while a burst is running.
    if ((now - last_hk_ms) >= HK_READ_PERIOD_MS) {
        last_hk_ms = now;
        (void)pib_adc_read_housekeeping(&pib_adc);
    }

    // Finishes any burst started with pib_adc_burst_start() (post-burst VREFINT)
    (void)pib_adc_burst_poll(&pib_adc);

    if ((now - last_led_ms) >= LED_TOGGLE_PERIOD_MS) {
        last_led_ms = now;
        HAL_GPIO_TogglePin(DEBUG_LED_GPIO_Port, DEBUG_LED_Pin);
    }
}
