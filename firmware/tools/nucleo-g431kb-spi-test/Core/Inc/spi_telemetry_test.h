#ifndef SPI_TELEMETRY_TEST_H
#define SPI_TELEMETRY_TEST_H

#include "stm32g4xx_hal.h"

#define SPI_TEST_RESP_LEN 5u

typedef enum {
    SPI_TEST_RUNNING = 0,
    SPI_TEST_SPI_TELEMETRY_DEMO_PASSED = 1,
    SPI_TEST_FAILING = 2,
} spi_test_result_t;

typedef struct {
    spi_test_result_t result;
    uint32_t sck_hz;

    uint8_t raw[SPI_TEST_RESP_LEN];
    uint8_t status;
    float uptime_s;

    uint32_t polls;
    uint32_t valid_samples;
    uint32_t hal_errors;
    uint32_t last_hal_status;
    uint32_t last_spi_error_code;
    uint32_t invalid_responses;
    uint32_t zero_responses;
    uint32_t ff_responses;
    uint32_t bad_uptime;
    uint32_t unexpected_status;
    uint32_t uptime_stalls;
    uint32_t uptime_resets;

    uint32_t consecutive_valid;
    float window_start_uptime_s;
    float uptime_progress_s;
    float nucleo_elapsed_s;
    uint32_t progression_ok;

    uint32_t passed_ever;
    uint32_t first_pass_poll;
    uint32_t last_valid_ms;
} spi_test_t;

extern volatile spi_test_t spi_test;

void spi_test_init(SPI_HandleTypeDef *h);
void spi_test_run_once(void);

#endif
