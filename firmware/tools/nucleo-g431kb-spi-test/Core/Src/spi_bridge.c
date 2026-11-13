#include "spi_bridge.h"
#include "main.h"

#define FRAME_GAP_MS  100u

typedef enum {
    WAIT_SYNC,
    WAIT_LEN_LO,
    WAIT_LEN_HI,
    WAIT_DATA,
} rx_state_t;

static SPI_HandleTypeDef *spi;
static uint8_t tx_buf[BRIDGE_MAX_LEN];
static uint8_t rx_buf[BRIDGE_MAX_LEN];
static rx_state_t rx_state;
static uint16_t rx_len;
static uint16_t rx_got;
static uint32_t last_byte_ms;

static void uart_init(void)
{
    GPIO_InitTypeDef g = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();

    g.Pin = GPIO_PIN_2 | GPIO_PIN_3;
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    g.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &g);

    USART2->CR1 = 0;
    USART2->BRR = (HAL_RCC_GetPCLK1Freq() + BRIDGE_BAUD / 2u) / BRIDGE_BAUD;
    USART2->CR3 = USART_CR3_OVRDIS;
    USART2->CR1 = USART_CR1_FIFOEN | USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

static void uart_put(uint8_t c)
{
    while ((USART2->ISR & USART_ISR_TXE_TXFNF) == 0u) {
    }
    USART2->TDR = c;
}

static int uart_get(void)
{
    if ((USART2->ISR & USART_ISR_RXNE_RXFNE) == 0u) {
        return -1;
    }
    return (int)(USART2->RDR & 0xFFu);
}

static void do_transfer(uint16_t n)
{
    uint32_t timeout_ms = 10u + n / 10u;

    HAL_GPIO_WritePin(PIB_CS_GPIO_Port, PIB_CS_Pin, GPIO_PIN_RESET);
    HAL_StatusTypeDef st = HAL_SPI_TransmitReceive(spi, tx_buf, rx_buf, n, timeout_ms);
    HAL_GPIO_WritePin(PIB_CS_GPIO_Port, PIB_CS_Pin, GPIO_PIN_SET);
    if (st != HAL_OK) {
        (void)HAL_SPI_Abort(spi);
    }

    uart_put(BRIDGE_SYNC_RESP);
    uart_put((uint8_t)st);
    uart_put((uint8_t)n);
    uart_put((uint8_t)(n >> 8));
    for (uint16_t i = 0; i < n; i++) {
        uart_put(rx_buf[i]);
    }
    HAL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin);
}

void spi_bridge_init(SPI_HandleTypeDef *h)
{
    spi = h;
    rx_state = WAIT_SYNC;
    HAL_GPIO_WritePin(PIB_CS_GPIO_Port, PIB_CS_Pin, GPIO_PIN_SET);
    uart_init();
}

void spi_bridge_run_once(void)
{
    uint32_t now = HAL_GetTick();
    int c = uart_get();

    if (c < 0) {
        if (rx_state != WAIT_SYNC && (now - last_byte_ms) > FRAME_GAP_MS) {
            rx_state = WAIT_SYNC;
        }
        return;
    }
    last_byte_ms = now;

    switch (rx_state) {
    case WAIT_SYNC:
        if (c == BRIDGE_SYNC_REQ) {
            rx_state = WAIT_LEN_LO;
        }
        break;

    case WAIT_LEN_LO:
        rx_len = (uint16_t)c;
        rx_state = WAIT_LEN_HI;
        break;

    case WAIT_LEN_HI:
        rx_len |= (uint16_t)((uint16_t)c << 8);
        rx_got = 0;
        rx_state = (rx_len == 0u || rx_len > BRIDGE_MAX_LEN) ? WAIT_SYNC : WAIT_DATA;
        break;

    case WAIT_DATA:
        tx_buf[rx_got++] = (uint8_t)c;
        if (rx_got == rx_len) {
            rx_state = WAIT_SYNC;
            do_transfer(rx_len);
        }
        break;
    }
}
