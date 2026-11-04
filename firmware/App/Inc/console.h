#ifndef CONSOLE_H
#define CONSOLE_H

#include "stm32g4xx_hal.h"

// Routes printf() to the given UART (LPUART1 on J8: pin 4 TX, pin 2 RX, 115200 8N1).
void console_init(UART_HandleTypeDef *huart);

#endif /* CONSOLE_H */
