#include "stm32g4xx_hal.h"

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
    
} ms5540c_t;

void ms5540c_init(ms5540c_t * dpt);
void ms5540c_measure(ms5540c_t * dpt, bool dpt_sel, uint16_t *d1, uint16_t *d2, int32_t *temp_dC, int32_t *p_dmbar);