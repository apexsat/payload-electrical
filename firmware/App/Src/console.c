#include "console.h"

#include <stdio.h>

#define CONSOLE_TX_TIMEOUT_MS 10u

static UART_HandleTypeDef *console_uart;

void console_init(UART_HandleTypeDef *huart)
{
    console_uart = huart;
    setvbuf(stdout, NULL, _IONBF, 0);
}

int __io_putchar(int ch)
{
    if (console_uart != NULL) {
        uint8_t c = (uint8_t)ch;
        HAL_UART_Transmit(console_uart, &c, 1, CONSOLE_TX_TIMEOUT_MS);
    }
    return ch;
}
