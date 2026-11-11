#include "spi_telemetry_test.h"
#include "main.h"
#include "telemetry.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define PIB_BUF 384u
#define RUN_MS 20000u

typedef enum {
    F_NONE,
    F_MISO_OPEN,
    F_MISO_HIGH,
    F_HAL_TIMEOUT,
    F_STUCK_SNAPSHOT,
    F_STATUS_SET,
    F_RATE_2X,
    F_PIB_RESET,
    F_COUNT,
} fault_t;

static const char *fault_names[F_COUNT] = {
    "normal PIB", "MISO open (pull-down)", "MISO stuck high", "HAL timeout",
    "PIB snapshot stuck", "status != 0", "uptime runs 2x", "PIB resets at 4 s",
};

GPIO_TypeDef sim_gpioa;
GPIO_TypeDef sim_gpiob;

static uint32_t now_ms;
static fault_t fault;
static bool cs_low;
static uint32_t cs_falls;
static uint32_t bad_cs_events;
static uint8_t pib_rx[PIB_BUF];
static uint8_t pib_tx[PIB_BUF];
static uint16_t pib_n;
static uint32_t pib_boot_ms;
static uint32_t pib_last_hk_ms;
static uint32_t request_frames_ok;
static uint32_t request_frames_bad;

uint32_t HAL_GetTick(void) { return now_ms; }
void HAL_Delay(uint32_t ms) { now_ms += ms + 1u; }
uint32_t HAL_RCC_GetPCLK2Freq(void) { return 16000000u; }
void HAL_GPIO_TogglePin(GPIO_TypeDef *port, uint16_t pin) { (void)port; (void)pin; }
HAL_StatusTypeDef HAL_SPI_Abort(SPI_HandleTypeDef *h) { (void)h; return HAL_OK; }

static void pib_on_cs_release(void)
{
    if (pib_n == 0u) {
        return;
    }
    if (pib_rx[0] == 0x80u) {
        if (pib_n == 2u && pib_rx[1] == SPI_TEST_RESP_LEN) {
            request_frames_ok++;
        } else {
            request_frames_bad++;
        }
    }
    uint16_t reply = telemetry_on_spi_frame(pib_rx, pib_n, pib_tx, PIB_BUF);
    if (reply > PIB_BUF) {
        reply = PIB_BUF;
    }
    memset(pib_tx + reply, 0, PIB_BUF - reply);
    pib_n = 0;
}

void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState s)
{
    if (port != PIB_CS_GPIO_Port || pin != PIB_CS_Pin) {
        return;
    }
    bool low = (s == GPIO_PIN_RESET);
    if (low && cs_low) {
        bad_cs_events++;
    }
    if (low && !cs_low) {
        cs_falls++;
        pib_n = 0;
    }
    if (!low && cs_low) {
        cs_low = false;
        pib_on_cs_release();
        return;
    }
    cs_low = low;
}

HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *h, uint8_t *tx, uint8_t *rx, uint16_t n, uint32_t timeout)
{
    (void)h;
    (void)timeout;
    if (!cs_low) {
        bad_cs_events++;
    }
    if (fault == F_HAL_TIMEOUT) {
        return HAL_TIMEOUT;
    }
    for (uint16_t i = 0; i < n; i++) {
        uint8_t miso = (pib_n < PIB_BUF) ? pib_tx[pib_n] : 0u;
        if (pib_n < PIB_BUF) {
            pib_rx[pib_n++] = tx[i];
        }
        if (fault == F_MISO_OPEN) {
            miso = 0x00u;
        } else if (fault == F_MISO_HIGH) {
            miso = 0xFFu;
        }
        rx[i] = miso;
    }
    return HAL_OK;
}

static void pib_snapshot(void)
{
    uint32_t pib_ms = now_ms - pib_boot_ms;
    telemetry_hk_t hk;
    memset(&hk, 0, sizeof hk);
    hk.status = (fault == F_STATUS_SET) ? TLM_STATUS_ARMED : 0u;
    hk.uptime_s = (float)pib_ms / 1000.0f;
    if (fault == F_RATE_2X) {
        hk.uptime_s *= 2.0f;
    }
    hk.frit_mbar = NAN;
    hk.tank_mbar = NAN;
    telemetry_update_snapshot(&hk);
}

static void pib_tick(void)
{
    if (fault == F_PIB_RESET && now_ms >= 4000u && pib_boot_ms == 0u) {
        pib_boot_ms = now_ms;
        telemetry_init();
        pib_snapshot();
        pib_last_hk_ms = now_ms;
    }
    if (fault == F_STUCK_SNAPSHOT && now_ms > 2000u) {
        return;
    }
    if ((now_ms - pib_last_hk_ms) >= 100u) {
        pib_last_hk_ms = now_ms;
        pib_snapshot();
    }
}

static int check(bool ok, const char *what)
{
    printf("    [%s] %s\n", ok ? "ok" : "FAIL", what);
    return ok ? 0 : 1;
}

static int run(fault_t f)
{
    SPI_HandleTypeDef h;
    memset(&h, 0, sizeof h);
    h.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_128;

    fault = f;
    now_ms = 1234u;
    cs_low = false;
    cs_falls = 0;
    bad_cs_events = 0;
    pib_n = 0;
    memset(pib_tx, 0, sizeof pib_tx);
    pib_boot_ms = 0;
    request_frames_ok = 0;
    request_frames_bad = 0;
    memset((void *)&spi_test, 0, sizeof spi_test);

    telemetry_stats_t before;
    telemetry_get_stats(&before);
    telemetry_init();
    pib_snapshot();
    pib_last_hk_ms = now_ms;

    spi_test_init(&h);
    uint32_t end = now_ms + RUN_MS;
    uint32_t pass_ms = 0;
    bool cs_left_low = false;
    while (now_ms < end) {
        pib_tick();
        spi_test_run_once();
        if (cs_low) {
            cs_left_low = true;
        }
        if (pass_ms == 0u && spi_test.result == SPI_TEST_SPI_TELEMETRY_DEMO_PASSED) {
            pass_ms = now_ms - (end - RUN_MS);
        }
        now_ms++;
    }

    telemetry_stats_t after;
    telemetry_get_stats(&after);
    telemetry_cmd_t cmd;
    bool queued = telemetry_take_command(&cmd);

    printf("%s: result=%d polls=%u valid=%u hal_err=%u invalid=%u zero=%u ff=%u bad_up=%u status=%u stalls=%u resets=%u uptime=%.2f progress=%.2f elapsed=%.2f pass_at=%ums\n",
           fault_names[f], (int)spi_test.result, (unsigned)spi_test.polls, (unsigned)spi_test.valid_samples,
           (unsigned)spi_test.hal_errors, (unsigned)spi_test.invalid_responses, (unsigned)spi_test.zero_responses,
           (unsigned)spi_test.ff_responses, (unsigned)spi_test.bad_uptime, (unsigned)spi_test.unexpected_status,
           (unsigned)spi_test.uptime_stalls, (unsigned)spi_test.uptime_resets, (double)spi_test.uptime_s,
           (double)spi_test.uptime_progress_s, (double)spi_test.nucleo_elapsed_s, (unsigned)pass_ms);

    int fails = 0;
    fails += check(spi_test.sck_hz == 125000u, "derived SCK = 125000 Hz");
    fails += check(!cs_left_low && bad_cs_events == 0u, "CS high between transfers, never double-asserted");
    fails += check(after.commands == before.commands && after.rejected == before.rejected && !queued,
                   "PIB received no write/actuator commands");
    fails += check(spi_test.polls >= 39u && spi_test.polls <= 41u, "polled about every 500 ms");

    switch (f) {
    case F_NONE:
        fails += check(request_frames_ok == spi_test.polls && request_frames_bad == 0u, "PIB saw [0x80, 0x05] request each poll");
        fails += check(pass_ms > 0u && pass_ms <= 6000u, "passed within ~5-6 s");
        fails += check(spi_test.result == SPI_TEST_SPI_TELEMETRY_DEMO_PASSED, "still passing at end");
        fails += check(spi_test.invalid_responses == 0u && spi_test.hal_errors == 0u, "no invalid responses or HAL errors");
        fails += check(fabsf(spi_test.uptime_s - (float)(spi_test.last_valid_ms - pib_boot_ms) / 1000.0f) <= 0.15f,
                       "decoded uptime matches PIB clock at last sample");
        break;
    case F_PIB_RESET:
        fails += check(spi_test.uptime_resets == 1u, "PIB reset detected once");
        fails += check(spi_test.result == SPI_TEST_SPI_TELEMETRY_DEMO_PASSED, "re-passes after reset");
        break;
    default:
        fails += check(!spi_test.passed_ever, "never reported pass");
        fails += check(spi_test.result == SPI_TEST_FAILING, "result = FAILING at end");
        break;
    }
    switch (f) {
    case F_MISO_OPEN: fails += check(spi_test.zero_responses > 0u, "zero responses counted"); break;
    case F_MISO_HIGH: fails += check(spi_test.ff_responses > 0u, "0xFF responses counted"); break;
    case F_HAL_TIMEOUT: fails += check(spi_test.hal_errors > 0u && spi_test.valid_samples == 0u, "HAL errors counted, no samples accepted"); break;
    case F_STUCK_SNAPSHOT: fails += check(spi_test.uptime_stalls > 0u, "stalled uptime counted"); break;
    case F_STATUS_SET: fails += check(spi_test.unexpected_status > 0u, "unexpected status counted"); break;
    default: break;
    }
    return fails;
}

int main(void)
{
    int fails = 0;
    for (int f = 0; f < F_COUNT; f++) {
        fails += run((fault_t)f);
    }
    printf("\n%s (%d failed checks)\n", fails == 0 ? "ALL HOST CHECKS PASSED" : "HOST CHECKS FAILED", fails);
    return fails == 0 ? 0 : 1;
}
