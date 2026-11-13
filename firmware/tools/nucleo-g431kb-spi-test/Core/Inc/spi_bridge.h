#ifndef SPI_BRIDGE_H
#define SPI_BRIDGE_H

#include "stm32g4xx_hal.h"

#define BRIDGE_SYNC_REQ   0xA5u
#define BRIDGE_SYNC_RESP  0x5Au
#define BRIDGE_MAX_LEN    384u
#define BRIDGE_BAUD       115200u

void spi_bridge_init(SPI_HandleTypeDef *h);
void spi_bridge_run_once(void);

#endif
