#include "spi_slave.h"

#include <string.h>

static SPI_HandleTypeDef *spi;
static spi_slave_frame_cb_t frame_cb;
static uint8_t rx_buf[SPI_SLAVE_BUF_SIZE];
static uint8_t tx_buf[SPI_SLAVE_BUF_SIZE];
static volatile spi_slave_stats_t stats;

static void arm(void)
{
    if (HAL_SPI_TransmitReceive_DMA(spi, tx_buf, rx_buf, SPI_SLAVE_BUF_SIZE) != HAL_OK) {
        stats.errors++;
    }
}

static void stop_and_reset(void)
{
    HAL_DMA_Abort(spi->hdmarx);
    HAL_DMA_Abort(spi->hdmatx);
    __HAL_RCC_SPI1_FORCE_RESET();
    __HAL_RCC_SPI1_RELEASE_RESET();
    spi->State = HAL_SPI_STATE_READY;
    spi->ErrorCode = HAL_SPI_ERROR_NONE;
    if (HAL_SPI_Init(spi) != HAL_OK) {
        stats.errors++;
    }
}

HAL_StatusTypeDef spi_slave_init(SPI_HandleTypeDef *h, spi_slave_frame_cb_t on_frame)
{
    if (h->Instance != SPI1 || h->Init.Mode != SPI_MODE_SLAVE ||
        h->Init.DataSize != SPI_DATASIZE_8BIT || h->hdmarx == NULL || h->hdmatx == NULL) {
        return HAL_ERROR;
    }

    spi = h;
    frame_cb = on_frame;
    memset(tx_buf, 0, sizeof tx_buf);

    SYSCFG->EXTICR[1] = (SYSCFG->EXTICR[1] & ~SYSCFG_EXTICR2_EXTI4) | SYSCFG_EXTICR2_EXTI4_PA;
    EXTI->FTSR1 &= ~EXTI_FTSR1_FT4;
    EXTI->RTSR1 |= EXTI_RTSR1_RT4;
    EXTI->PR1 = EXTI_PR1_PIF4;
    EXTI->IMR1 |= EXTI_IMR1_IM4;
    HAL_NVIC_SetPriority(EXTI4_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(EXTI4_IRQn);

    arm();
    return HAL_OK;
}

void spi_slave_on_cs_release(void)
{
    uint16_t n = (uint16_t)(SPI_SLAVE_BUF_SIZE - __HAL_DMA_GET_COUNTER(spi->hdmarx));

    stop_and_reset();

    if (n == 0u) {
        arm();
        return;
    }

    stats.frames++;
    uint16_t reply = 0;
    if (frame_cb != NULL) {
        reply = frame_cb(rx_buf, n, tx_buf, SPI_SLAVE_BUF_SIZE);
    }
    if (reply > SPI_SLAVE_BUF_SIZE) {
        reply = SPI_SLAVE_BUF_SIZE;
    }
    memset(tx_buf + reply, 0, SPI_SLAVE_BUF_SIZE - reply);
    arm();
}

void spi_slave_get_stats(spi_slave_stats_t *s)
{
    s->frames = stats.frames;
    s->errors = stats.errors;
}

void EXTI4_IRQHandler(void)
{
    if ((EXTI->PR1 & EXTI_PR1_PIF4) != 0u) {
        EXTI->PR1 = EXTI_PR1_PIF4;
        if (spi != NULL) {
            spi_slave_on_cs_release();
        }
    }
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *h)
{
    if (h == spi) {
        stats.errors++;
    }
}
