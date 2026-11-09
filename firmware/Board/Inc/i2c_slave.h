#ifndef I2C_SLAVE_H
#define I2C_SLAVE_H

#include "stm32g4xx_hal.h"

#define I2C_SLAVE_RX_SIZE 16u
#define I2C_SLAVE_TX_SIZE 384u

typedef void (*i2c_slave_write_cb_t)(const uint8_t *rx, uint16_t n);
typedef uint16_t (*i2c_slave_read_cb_t)(uint8_t *tx, uint16_t max);

typedef struct {
    uint32_t writes;
    uint32_t reads;
    uint32_t errors;
} i2c_slave_stats_t;

HAL_StatusTypeDef i2c_slave_init(I2C_HandleTypeDef *h, uint8_t addr7,
                                 i2c_slave_write_cb_t on_write, i2c_slave_read_cb_t on_read);
void i2c_slave_get_stats(i2c_slave_stats_t *s);

#endif
