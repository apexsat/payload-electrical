#ifndef INC_FLOW_H_
#define INC_FLOW_H_

#include "stm32g4xx_hal.h"
#include <stdbool.h>

#define LG160431D_CRC_POLYNOMIAL 0x131
#define LG160431D_USER_REG_W 0xE2
#define LG160431D_USER_REG_R 0xE3
#define LG160431D_ADV_USER_REG_W 0xE4
#define LG160431D_ADV_USER_REG_R 0xE5
#define LG160431D_TRIGGER_FLOW_MEASUREMENT 0xF1
#define LG160431D_TRIGGER_TEMP_MEASUREMENT 0xF3
#define LG160431D_TRIGGER_VDD_MEASUREMENT 0xF5
#define LG160431D_EEPROM_W 0xFA
#define LG160431D_EEPROM_R 0xFA
#define LG160431D_SOFT_RESET 0xFE

#define LG160431D_SN_ADDR 0x2F8
#define LG160431D_PN_ADDR 0x2E8

static uint16_t scale_factor_addr[8] = { // Maps calibration field to scale factor
    0x2B6,
    0x5B6,
    0x8B6,
    0xBB6,
    0xEB6,
    0xEB6,
    0xEB6,
    0xEB6,
};

typedef struct {
    uint8_t addr;
    I2C_HandleTypeDef * i2c;
    bool hold_master; // 1: Sensor holds master for each measuremenet trigger until conversion complete. 0: Sensor accepts trigger commands and should be polled for results
    bool conv_in_progress;
    uint16_t scale_factor;
} lg160431d_t;

bool lg160431d_init(lg160431d_t * lg, uint8_t cf, bool heater, uint8_t res, bool hold_master); // Writes to user registers to change calibration, measurement, and resolution settings
void lg160431d_reset(lg160431d_t * lg);
uint32_t lg160431d_read_sn(lg160431d_t * lg);
bool lg160431d_trigger_flow(lg160431d_t * lg);
bool lg160431d_trigger_vdd(lg160431d_t * lg);
bool lg160431d_trigger_temp(lg160431d_t * lg);
bool lg160431d_poll_flow(lg160431d_t * lg, float * flow);
bool lg160431d_poll_vdd(lg160431d_t * lg, float * vdd);
bool lg160431d_poll_temp(lg160431d_t * lg, float * temp);
float lg160431d_read_flow_blocking(lg160431d_t * lg);
float lg160431d_read_vdd_blocking(lg160431d_t * lg);
float lg160431d_read_temp_blocking(lg160431d_t * lg);

#endif /* INC_FLOW_H_ */
