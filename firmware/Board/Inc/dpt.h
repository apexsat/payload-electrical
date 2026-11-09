#ifndef DPT_H
#define DPT_H

#include "stm32g4xx_hal.h"
#include <stdbool.h>
typedef struct{
	int32_t C1, C2, C3, C4, C5, C6;
} ms5540c_cal_t;

typedef struct {
    ms5540c_cal_t cal[2];
    GPIO_TypeDef * clk_gpio_port;
    uint16_t clk_gpio_pin;
    GPIO_TypeDef * dout_gpio_port;
    uint16_t dout_gpio_pin;
    GPIO_TypeDef * din_gpio_port;
    uint16_t din_gpio_pin;
    GPIO_TypeDef * mux_gpio_port;
    uint16_t mux_gpio_pin;
    TIM_HandleTypeDef * mclk_tim;
    uint32_t mclk_tim_channel;
} ms5540c_t;

void ms5540c_init(ms5540c_t * dpt);
bool ms5540c_measure(ms5540c_t * dpt, bool dpt_sel, float * temp, float * p_mbar);

#endif
