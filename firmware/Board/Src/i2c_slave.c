#include "i2c_slave.h"

#include <string.h>

static I2C_HandleTypeDef *i2c;
static i2c_slave_write_cb_t write_cb;
static i2c_slave_read_cb_t read_cb;
static uint8_t rx_buf[I2C_SLAVE_RX_SIZE];
static uint16_t rx_n;
static uint8_t rx_byte;
static uint8_t tx_buf[I2C_SLAVE_TX_SIZE];
static volatile i2c_slave_stats_t stats;

static void finish_write(void)
{
    if (rx_n > 0u) {
        stats.writes++;
        if (write_cb != NULL) {
            write_cb(rx_buf, rx_n);
        }
        rx_n = 0;
    }
}

HAL_StatusTypeDef i2c_slave_init(I2C_HandleTypeDef *h, uint8_t addr7,
                                 i2c_slave_write_cb_t on_write, i2c_slave_read_cb_t on_read)
{
    i2c = h;
    write_cb = on_write;
    read_cb = on_read;
    rx_n = 0;

    h->Init.OwnAddress1 = (uint32_t)addr7 << 1;
    h->Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    if (HAL_I2C_Init(h) != HAL_OK) {
        return HAL_ERROR;
    }
    return HAL_I2C_EnableListen_IT(h);
}

void i2c_slave_get_stats(i2c_slave_stats_t *s)
{
    s->writes = stats.writes;
    s->reads = stats.reads;
    s->errors = stats.errors;
}

void HAL_I2C_AddrCallback(I2C_HandleTypeDef *h, uint8_t TransferDirection, uint16_t AddrMatchCode)
{
    (void)AddrMatchCode;
    if (h != i2c) {
        return;
    }

    if (TransferDirection == I2C_DIRECTION_TRANSMIT) {
        rx_n = 0;
        HAL_I2C_Slave_Seq_Receive_IT(h, &rx_byte, 1, I2C_NEXT_FRAME);
    } else {
        finish_write();
        uint16_t n = 0;
        if (read_cb != NULL) {
            n = read_cb(tx_buf, I2C_SLAVE_TX_SIZE);
        }
        if (n > I2C_SLAVE_TX_SIZE) {
            n = I2C_SLAVE_TX_SIZE;
        }
        memset(tx_buf + n, 0, I2C_SLAVE_TX_SIZE - n);
        stats.reads++;
        HAL_I2C_Slave_Seq_Transmit_IT(h, tx_buf, I2C_SLAVE_TX_SIZE, I2C_LAST_FRAME);
    }
}

void HAL_I2C_SlaveRxCpltCallback(I2C_HandleTypeDef *h)
{
    if (h != i2c) {
        return;
    }
    if (rx_n < I2C_SLAVE_RX_SIZE) {
        rx_buf[rx_n++] = rx_byte;
    }
    HAL_I2C_Slave_Seq_Receive_IT(h, &rx_byte, 1, I2C_NEXT_FRAME);
}

void HAL_I2C_ListenCpltCallback(I2C_HandleTypeDef *h)
{
    if (h != i2c) {
        return;
    }
    finish_write();
    HAL_I2C_EnableListen_IT(h);
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *h)
{
    if (h != i2c) {
        return;
    }
    if ((HAL_I2C_GetError(h) & ~HAL_I2C_ERROR_AF) != 0u) {
        stats.errors++;
    }
    if (HAL_I2C_GetState(h) == HAL_I2C_STATE_READY) {
        rx_n = 0;
        HAL_I2C_EnableListen_IT(h);
    }
}
