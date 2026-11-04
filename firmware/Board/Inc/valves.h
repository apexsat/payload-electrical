#ifndef VALVES_H
#define VALVES_H

#include "stm32g4xx_hal.h"

//   Valve 1, Valve 3:
//latching valves controlled by quad-FET setup
//A short pulse on the H input opens the valve, a pulse on the L input closes it.
//Stays where it is when power is removed
//
//   Valve 2
//normally-closed solenoid on a low-side FET
//Open while VALVE2_EN is high


typedef enum {
    VALVE_LATCHING,
    VALVE_SOLENOID,
} valve_type_t;

typedef enum {
    VALVE_UNKNOWN,  // latching valve not yet commanded since boot
    VALVE_CLOSED,
    VALVE_OPEN,
} valve_state_t;

//one per valve
typedef struct {
    valve_type_t  type;
    GPIO_TypeDef *open_port;   // latching: MIC4424 input
    uint16_t      open_pin;
    GPIO_TypeDef *close_port;  // latching: L input
    uint16_t      close_pin;
    uint32_t      pulse_ms;    // latching only
    valve_state_t state;       // last commanded state
} valve_t;

/*
 * +12		+12
 * 	|		 |
 * 	|-[coil]-|
 * 	|		 |
 * 	gnd		gnd
 */
//MIC4424 h-brdige
/*

  VALVE3_L ──> INA ─┐           |─ OUTA ─|
                    │-[MIC4424]-│      [coil]  --> this is valve 3
  VALVE3_H ──> INB ─┘           |─ OUTB ─|
 */

void valve_init_latching(valve_t *v,
                         GPIO_TypeDef *open_port, uint16_t open_pin,
                         GPIO_TypeDef *close_port, uint16_t close_pin,
                         uint32_t pulse_ms);

void valve_init_solenoid(valve_t *v, GPIO_TypeDef *en_port, uint16_t en_pin);

void valve_open(valve_t *v);
void valve_close(valve_t *v);

//last commanded state
valve_state_t valve_get_state(const valve_t *v);

#endif /* VALVES_H */
