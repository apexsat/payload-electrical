#ifndef IGNITER_H
#define IGNITER_H

#include "stm32g4xx_hal.h"

typedef struct {
    GPIO_TypeDef *pulse_port;
    uint16_t      pulse_pin;
    GPIO_TypeDef *pd_rst_port;
    uint16_t      pd_rst_pin;
} igniter_t;

void igniter_init(igniter_t *ign);
void igniter_pd_release(igniter_t *ign);
void igniter_pd_reset(igniter_t *ign);
void igniter_pulse(igniter_t *ign, uint16_t width_100ns);

#endif
