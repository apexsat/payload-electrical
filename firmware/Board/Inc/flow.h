#ifndef INC_FLOW_H_
#define INC_FLOW_H_

#include "stm32g4xx_hal.h"
#include <stdbool.h>

typedef struct {
    uint8_t addr;
    I2C_HandleTypeDef * i2c;
    bool hold_master; // 1: Sensor holds master for each measuremenet trigger until conversion complete. 0: Sensor accepts trigger commands and should be polled for results
} lg160431d_t;

void lg160431d_init(lg160431d_t * lg); // Writes to user registers to change calibration, measurement, and resolution settings
void lg160431d_reset(lg160431d_t * lg);
uint32_t lg160431d_read_sn(lg160431d_t * lg);
void lg160431d_trigger_flow(lg160431d_t * lg);
void lg160431d_trigger_vdd(lg160431d_t * lg);
void lg160431d_trigger_temp(lg160431d_t * lg);
bool lg160431d_poll_flow(lg160431d_t * lg, int16_t * flow);
bool lg160431d_poll_vdd(lg160431d_t * lg, uint16_t * vdd);
bool lg160431d_poll_temp(lg160431d_t * lg, uint16_t * temp);
int16_t lg160431d_read_flow_blocking(lg160431d_t * lg);
uint16_t lg160431d_read_vdd_blocking(lg160431d_t * lg);
uint16_t lg160431d_read_temp_blocking(lg160431d_t * lg);

#endif /* INC_FLOW_H_ */
