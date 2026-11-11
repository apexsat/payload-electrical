#ifndef STM32G4XX_HAL_H
#define STM32G4XX_HAL_H

#include <stdint.h>

typedef enum {
    HAL_OK = 0x00,
    HAL_ERROR = 0x01,
    HAL_BUSY = 0x02,
    HAL_TIMEOUT = 0x03,
} HAL_StatusTypeDef;

typedef enum {
    GPIO_PIN_RESET = 0,
    GPIO_PIN_SET,
} GPIO_PinState;

typedef struct {
    int id;
} GPIO_TypeDef;

typedef struct {
    uint32_t BaudRatePrescaler;
} SPI_InitTypeDef;

typedef struct {
    SPI_InitTypeDef Init;
    volatile uint32_t ErrorCode;
} SPI_HandleTypeDef;

#define SPI_CR1_BR_Pos 3u
#define SPI_BAUDRATEPRESCALER_128 0x00000030u

extern GPIO_TypeDef sim_gpioa;
extern GPIO_TypeDef sim_gpiob;

uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t ms);
uint32_t HAL_RCC_GetPCLK2Freq(void);
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState s);
void HAL_GPIO_TogglePin(GPIO_TypeDef *port, uint16_t pin);
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *h, uint8_t *tx, uint8_t *rx, uint16_t n, uint32_t timeout);
HAL_StatusTypeDef HAL_SPI_Abort(SPI_HandleTypeDef *h);

#endif
