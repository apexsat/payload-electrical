#include "app.h"
#include "main.h"
#include "config.h"
#include "valves.h"

static valve_t valve1;  // latching
static valve_t valve2;  // normally-closed solenoid
static valve_t valve3;  // latching

void app_init(void)
{
    valve_init_latching(&valve1, VALVE1_H_GPIO_Port, VALVE1_H_Pin,
                                 VALVE1_L_GPIO_Port, VALVE1_L_Pin,
                                 VALVE_LATCH_PULSE_MS);
    valve_init_solenoid(&valve2, VALVE2_EN_GPIO_Port, VALVE2_EN_Pin);
    valve_init_latching(&valve3, VALVE3_H_GPIO_Port, VALVE3_H_Pin,
                                 VALVE3_L_GPIO_Port, VALVE3_L_Pin,
                                 VALVE_LATCH_PULSE_MS);
}

void app_run_once(void)
{
    HAL_GPIO_TogglePin(DEBUG_LED_GPIO_Port, DEBUG_LED_Pin);
    HAL_Delay(500);
}
