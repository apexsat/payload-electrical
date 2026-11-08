#include "valves.h"

#include <stddef.h>

static void pin_low(GPIO_TypeDef *port, uint16_t pin)
{
    HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET);
}
//resets pin

static void pin_high(GPIO_TypeDef *port, uint16_t pin)
{
    HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET);
}
//drives pin high

// Starts a pulse on one H-bridge input and returns; valve_update() ends it.
// The other input is driven low first, which also ends a pulse that was
// running in the opposite direction.
static void latching_pulse_start(valve_t *v, GPIO_TypeDef *on_port, uint16_t on_pin,
                                 GPIO_TypeDef *off_port, uint16_t off_pin)
{
    pin_low(off_port, off_pin);
    pin_high(on_port, on_pin);

    v->pulse_port     = on_port;
    v->pulse_pin      = on_pin;
    v->pulse_start_ms = HAL_GetTick();
    v->pulsing        = true;
}
//initialize latching valve
void valve_init_latching(valve_t *v,
                         GPIO_TypeDef *open_port, uint16_t open_pin,
                         GPIO_TypeDef *close_port, uint16_t close_pin,
                         uint32_t pulse_ms)
{
    v->type       = VALVE_LATCHING;
    v->open_port  = open_port;
    v->open_pin   = open_pin;
    v->close_port = close_port;
    v->close_pin  = close_pin;
    v->pulse_ms   = pulse_ms;
    v->state      = VALVE_UNKNOWN;  // it kept whatever position it had before boot
    v->pulsing    = false;
    v->pulse_port = NULL;
    v->pulse_pin  = 0;

    pin_low(open_port, open_pin);
    pin_low(close_port, close_pin);
}

//initialize solenoid regular valve (NC)
void valve_init_solenoid(valve_t *v, GPIO_TypeDef *en_port, uint16_t en_pin)
{
    v->type       = VALVE_SOLENOID;
    v->open_port  = en_port;
    v->open_pin   = en_pin;
    v->close_port = NULL;
    v->close_pin  = 0;
    v->pulse_ms   = 0;
    v->state      = VALVE_CLOSED;   // de-energized = closed
    v->pulsing    = false;
    v->pulse_port = NULL;
    v->pulse_pin  = 0;

    pin_low(en_port, en_pin);
}

//opens valve depending on type then declares the new state
void valve_open(valve_t *v)
{
    if (v->type == VALVE_LATCHING) {
        latching_pulse_start(v, v->open_port, v->open_pin, v->close_port, v->close_pin);
    } else {
        pin_high(v->open_port, v->open_pin);
    }

    v->state = VALVE_OPEN;
}

//same concept, other direction
void valve_close(valve_t *v)
{
    if (v->type == VALVE_LATCHING) {
        latching_pulse_start(v, v->close_port, v->close_pin, v->open_port, v->open_pin);
    } else {
        pin_low(v->open_port, v->open_pin);
    }

    v->state = VALVE_CLOSED;
}

//ends a latching pulse once pulse_ms has passed (unsigned math handles tick wrap)
void valve_update(valve_t *v)
{
    if (v->pulsing && ((HAL_GetTick() - v->pulse_start_ms) >= v->pulse_ms)) {
        pin_low(v->pulse_port, v->pulse_pin);
        v->pulsing = false;
    }
}

bool valve_is_pulsing(const valve_t *v)
{
    return v->pulsing;
}

//simple getstate command
valve_state_t valve_get_state(const valve_t *v)
{
    return v->state;
}
