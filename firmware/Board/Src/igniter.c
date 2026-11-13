#include "igniter.h"

void igniter_init(igniter_t *ign)
{
    ign->pulse_port->BRR = ign->pulse_pin;
    igniter_pd_reset(ign);

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0u;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

void igniter_pd_release(igniter_t *ign)
{
    ign->pd_rst_port->BSRR = ign->pd_rst_pin;
}

void igniter_pd_reset(igniter_t *ign)
{
    ign->pd_rst_port->BRR = ign->pd_rst_pin;
}

void igniter_delay_us(uint16_t us)
{
    uint32_t cycles = (uint32_t)(((uint64_t)us * SystemCoreClock) / 1000000u);
    uint32_t start = DWT->CYCCNT;
    while ((DWT->CYCCNT - start) < cycles) {
    }
}

void igniter_pulse(igniter_t *ign, uint32_t width_100ns)
{
    uint32_t cycles = (uint32_t)(((uint64_t)width_100ns * SystemCoreClock) / 10000000u);
    if (cycles == 0u) {
        return;
    }

    uint32_t start = DWT->CYCCNT;
    ign->pulse_port->BSRR = ign->pulse_pin;
    while ((DWT->CYCCNT - start) < cycles) {
    }
    ign->pulse_port->BRR = ign->pulse_pin;
}
