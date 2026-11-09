#ifndef SPI_SLAVE_H
#define SPI_SLAVE_H

#include "stm32g4xx_hal.h"

#define SPI_SLAVE_BUF_SIZE 384u

typedef uint16_t (*spi_slave_frame_cb_t)(const uint8_t *rx, uint16_t n_rx, uint8_t *tx_next, uint16_t tx_max);

typedef struct {
    uint32_t frames;
    uint32_t errors;
} spi_slave_stats_t;

HAL_StatusTypeDef spi_slave_init(SPI_HandleTypeDef *h, spi_slave_frame_cb_t on_frame);
void spi_slave_on_cs_release(void);
void spi_slave_get_stats(spi_slave_stats_t *s);

#endif
