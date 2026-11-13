#include "spi_telemetry_test.h"
#include "main.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

#define PIB_READ_FLAG        0x80u
#define PIB_REG_STATUS       0x00u
#define PIB_EXPECTED_STATUS  0x00u

#define SPI_TIMEOUT_MS       10u
#define PIB_PREPARE_MS       5u
#define POLL_PERIOD_MS       500u

#define UPTIME_MAX_S         1.0e6f
#define PASS_MIN_SAMPLES     10u
#define PASS_MIN_ELAPSED_S   5.0f
#define RATE_TOLERANCE       0.10f
#define UPTIME_SLACK_S       0.3f
#define FAIL_AFTER_MS        10000u

#define LED_RUNNING_MS       500u
#define LED_FAILING_MS       100u

volatile spi_test_t spi_test;

static SPI_HandleTypeDef *spi;
static uint8_t request[2] = { PIB_READ_FLAG | PIB_REG_STATUS, SPI_TEST_RESP_LEN };
static uint8_t request_rx[2];
static uint8_t dummy_tx[SPI_TEST_RESP_LEN];
static float last_uptime_s;
static bool have_last_uptime;
static uint32_t window_start_ms;
static uint32_t last_poll_ms;
static uint32_t last_led_ms;

static HAL_StatusTypeDef transfer(uint8_t *tx, uint8_t *rx, uint16_t n)
{
    HAL_GPIO_WritePin(PIB_CS_GPIO_Port, PIB_CS_Pin, GPIO_PIN_RESET);
    HAL_StatusTypeDef st = HAL_SPI_TransmitReceive(spi, tx, rx, n, SPI_TIMEOUT_MS);
    HAL_GPIO_WritePin(PIB_CS_GPIO_Port, PIB_CS_Pin, GPIO_PIN_SET);
    if (st != HAL_OK) {
        spi_test.hal_errors++;
        spi_test.last_hal_status = (uint32_t)st;
        spi_test.last_spi_error_code = spi->ErrorCode;
        (void)HAL_SPI_Abort(spi);
    }
    return st;
}

static float get_f32_le(const uint8_t *p)
{
    uint32_t v = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    float f;
    memcpy(&f, &v, sizeof f);
    return f;
}

static bool all_bytes_equal(const uint8_t *p, uint8_t v)
{
    for (uint32_t i = 0; i < SPI_TEST_RESP_LEN; i++) {
        if (p[i] != v) {
            return false;
        }
    }
    return true;
}

static void restart_window(void)
{
    spi_test.consecutive_valid = 0;
    spi_test.progression_ok = 0;
    spi_test.uptime_progress_s = 0.0f;
    spi_test.nucleo_elapsed_s = 0.0f;
}

static void reject(void)
{
    spi_test.invalid_responses++;
    restart_window();
}

static void evaluate(const uint8_t *rx, uint32_t now)
{
    for (uint32_t i = 0; i < SPI_TEST_RESP_LEN; i++) {
        spi_test.raw[i] = rx[i];
    }
    float up = get_f32_le(&rx[1]);
    spi_test.status = rx[0];
    spi_test.uptime_s = up;

    if (all_bytes_equal(rx, 0x00u)) {
        spi_test.zero_responses++;
        reject();
        return;
    }
    if (all_bytes_equal(rx, 0xFFu)) {
        spi_test.ff_responses++;
        reject();
        return;
    }
    if (!isfinite(up) || up <= 0.0f || up > UPTIME_MAX_S) {
        spi_test.bad_uptime++;
        reject();
        return;
    }
    if (rx[0] != PIB_EXPECTED_STATUS) {
        spi_test.unexpected_status++;
        reject();
        return;
    }
    if (have_last_uptime) {
        if (up < last_uptime_s) {
            spi_test.uptime_resets++;
            restart_window();
        } else if (up <= last_uptime_s) {
            spi_test.uptime_stalls++;
            reject();
            return;
        }
    }

    spi_test.valid_samples++;
    spi_test.last_valid_ms = now;
    last_uptime_s = up;
    have_last_uptime = true;
    if (spi_test.consecutive_valid == 0u) {
        spi_test.window_start_uptime_s = up;
        window_start_ms = now;
    }
    spi_test.consecutive_valid++;

    float progress = up - spi_test.window_start_uptime_s;
    float elapsed = (float)(now - window_start_ms) / 1000.0f;
    spi_test.uptime_progress_s = progress;
    spi_test.nucleo_elapsed_s = elapsed;
    spi_test.progression_ok = (spi_test.consecutive_valid >= PASS_MIN_SAMPLES) &&
                              (elapsed >= PASS_MIN_ELAPSED_S) &&
                              (fabsf(progress - elapsed) <= RATE_TOLERANCE * elapsed + UPTIME_SLACK_S);
}

static void update_result(uint32_t now)
{
    bool window_full = (spi_test.consecutive_valid >= PASS_MIN_SAMPLES) &&
                       (spi_test.nucleo_elapsed_s >= PASS_MIN_ELAPSED_S);

    if (spi_test.progression_ok) {
        spi_test.result = SPI_TEST_SPI_TELEMETRY_DEMO_PASSED;
        if (!spi_test.passed_ever) {
            spi_test.passed_ever = 1u;
            spi_test.first_pass_poll = spi_test.polls;
        }
    } else if (window_full || (now - spi_test.last_valid_ms) >= FAIL_AFTER_MS) {
        spi_test.result = SPI_TEST_FAILING;
    } else {
        spi_test.result = SPI_TEST_RUNNING;
    }
}

static void update_led(uint32_t now)
{
    if (spi_test.result == SPI_TEST_SPI_TELEMETRY_DEMO_PASSED) {
        HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);
        return;
    }
    uint32_t period = (spi_test.result == SPI_TEST_FAILING) ? LED_FAILING_MS : LED_RUNNING_MS;
    if ((now - last_led_ms) >= period) {
        last_led_ms = now;
        HAL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin);
    }
}

void spi_test_init(SPI_HandleTypeDef *h)
{
    spi = h;
    have_last_uptime = false;
    HAL_GPIO_WritePin(PIB_CS_GPIO_Port, PIB_CS_Pin, GPIO_PIN_SET);
    uint32_t br = (h->Init.BaudRatePrescaler >> SPI_CR1_BR_Pos) & 0x7u;
    spi_test.sck_hz = HAL_RCC_GetPCLK2Freq() >> (br + 1u);
    uint32_t now = HAL_GetTick();
    spi_test.last_valid_ms = now;
    last_poll_ms = now - POLL_PERIOD_MS;
    last_led_ms = now;
}

void spi_test_run_once(void)
{
    uint32_t now = HAL_GetTick();
    update_led(now);
    if ((now - last_poll_ms) < POLL_PERIOD_MS) {
        return;
    }
    last_poll_ms = now;
    spi_test.polls++;

    uint8_t rx[SPI_TEST_RESP_LEN] = { 0 };
    if (transfer(request, request_rx, sizeof request) == HAL_OK) {
        HAL_Delay(PIB_PREPARE_MS);
        if (transfer(dummy_tx, rx, SPI_TEST_RESP_LEN) == HAL_OK) {
            evaluate(rx, HAL_GetTick());
        } else {
            restart_window();
        }
    } else {
        restart_window();
    }
    update_result(HAL_GetTick());
}
