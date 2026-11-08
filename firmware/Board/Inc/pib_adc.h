#ifndef PIB_ADC_H
#define PIB_ADC_H

#include "stm32g4xx_hal.h"
#include <stdbool.h>

/*
 * PIB (200_40) analog inputs on ADC1 + ADC2.
 *
 *  - Burst: ADC_V_DCHG (PA0, ADC1_IN1) + ADC_I_DCHG (PA1, ADC2_IN2) on the
 *    regular group, dual regular-simultaneous, DMA one-shot, 2.133 MSPS per
 *    channel. Configured by CubeMX (firmware.ioc).
 *  - Housekeeping: every other channel, read through the ADC1 injected group.
 *    This driver replaces the CubeMX injected setup at init.
 *
 * Sample times and channel map: 200_40 PIB/PIB_Schematic_Review.md,
 * "ADC configuration" section.
 *
 * Only one pib_adc_t may be initialised (the HAL ADC callbacks are global).
 */

/* ---- Housekeeping channels ------------------------------------------------ */

/* Order sets the injected sequence: 4 channels per sequence, in this order. */
typedef enum {
    HK_VREFINT = 0,     /* internal reference, used to measure VDDA */
    HK_TEMP_MCU,        /* internal temperature sensor              */
    HK_NTC_PIB,
    HK_TANK_NTC_1,
    HK_TANK_NTC_2,
    HK_NTC_DCHG,
    HK_NTC_IGN,
    HK_V_IGN_SUPPLY,
    HK_V_IGN_PK,
    HK_I_IGN_PK,
    HK_NUM_CH
} hk_ch_t;

/* ADC1 channel for each signal (from the 200_40 PIB schematic).
   These must match the pin assignments in firmware.ioc. */
#define HK_ADC_CH_VREFINT       ADC_CHANNEL_VREFINT
#define HK_ADC_CH_TEMP_MCU      ADC_CHANNEL_TEMPSENSOR_ADC1
#define HK_ADC_CH_NTC_PIB       ADC_CHANNEL_12   /* PB1  ADC_NTC_PIB        */
#define HK_ADC_CH_TANK_NTC_1    ADC_CHANNEL_14   /* PB11 ADC_TANK_NTC_1     */
#define HK_ADC_CH_TANK_NTC_2    ADC_CHANNEL_15   /* PB0  ADC_TANK_NTC_2     */
#define HK_ADC_CH_NTC_DCHG      ADC_CHANNEL_11   /* PB12 ADC_NTC_DCHG       */
#define HK_ADC_CH_NTC_IGN       ADC_CHANNEL_9    /* PC3  ADC_NTC_IGN        */
#define HK_ADC_CH_V_IGN_SUPPLY  ADC_CHANNEL_4    /* PA3  ADC_V_IGN_SUPPLY_C */
#define HK_ADC_CH_V_IGN_PK      ADC_CHANNEL_5    /* PB14 ADC_V_IGN_PK_C     */
#define HK_ADC_CH_I_IGN_PK      ADC_CHANNEL_8    /* PC2  ADC_I_IGN_PK_C     */

/* Sample time per channel. ADC clk = HCLK/2 = 32 MHz.
   Igniter channels (1k series + BAT54J clamps) need >= 47.5 cycles
   (DS12589 Table 61, R_AIN <= 1.8k on slow channels); 92.5 also covers
   series resistance on the igniter board. NTC channels have 100 nF at the
   pin, so 92.5 is a uniform default there, not a requirement. */
#define HK_SMP_VREFINT          ADC_SAMPLETIME_247CYCLES_5  /* 7.7 us, needs >= 4 us (DS12589 Table 20) */
#define HK_SMP_TEMP_MCU         ADC_SAMPLETIME_247CYCLES_5  /* 7.7 us, needs >= 5 us (DS12589 Table 75) */
#define HK_SMP_NTC_PIB          ADC_SAMPLETIME_92CYCLES_5   /* 2.9 us, R109 10k / TH1, C76 100 nF       */
#define HK_SMP_TANK_NTC_1       ADC_SAMPLETIME_92CYCLES_5   /* 2.9 us, R4 22k bias, C19 100 nF          */
#define HK_SMP_TANK_NTC_2       ADC_SAMPLETIME_92CYCLES_5   /* 2.9 us, R3 22k bias, C20 100 nF          */
#define HK_SMP_NTC_DCHG         ADC_SAMPLETIME_92CYCLES_5   /* 2.9 us, R87 10k bias, C61 100 nF         */
#define HK_SMP_NTC_IGN          ADC_SAMPLETIME_92CYCLES_5   /* 2.9 us, R78 10k bias, C44 100 nF         */
#define HK_SMP_V_IGN_SUPPLY     ADC_SAMPLETIME_92CYCLES_5   /* 2.9 us, R129 1k series, no pin cap       */
#define HK_SMP_V_IGN_PK         ADC_SAMPLETIME_92CYCLES_5   /* 2.9 us, R131 1k series, no pin cap       */
#define HK_SMP_I_IGN_PK         ADC_SAMPLETIME_92CYCLES_5   /* 2.9 us, R130 1k series, no pin cap       */

#define HK_INJ_MAX_RANKS   4U   /* hardware limit of the injected sequencer */
#define HK_NUM_SEQ         ((HK_NUM_CH + HK_INJ_MAX_RANKS - 1U) / HK_INJ_MAX_RANKS)

typedef struct {
    uint16_t raw[HK_NUM_CH];   /* 12-bit counts, indexed by hk_ch_t          */
    uint32_t mv[HK_NUM_CH];    /* pin voltage in mV, scaled by measured VDDA */
    uint32_t vdda_mv;          /* from VREFINT + factory VREFINT_CAL         */
    int32_t  temp_mcu_degC;    /* from TS_CAL1/TS_CAL2                       */
} hk_adc_t;

/* ---- Discharge burst ------------------------------------------------------ */

/* Unpack one burst word (DMA access mode 12/10-bit):
   ADC1 master = V_DCHG in [15:0], ADC2 slave = I_DCHG in [31:16]. */
#define ADC_BURST_V_RAW(word)   ((uint16_t)((word) & 0xFFFFU))
#define ADC_BURST_I_RAW(word)   ((uint16_t)((word) >> 16))

typedef enum {
    ADC_BURST_IDLE = 0,
    ADC_BURST_RUNNING,   /* DMA filling the buffer                       */
    ADC_BURST_CAPTURED,  /* buffer full, post-burst VREFINT not read yet */
    ADC_BURST_DONE,      /* buffer full, vdda_after_mv valid             */
    ADC_BURST_ERROR
} adc_burst_state_t;

typedef struct {
    volatile adc_burst_state_t state;
    uint32_t *buf;             /* caller-owned, one V/I pair per word */
    uint32_t  n_samples;
    uint32_t  vdda_before_mv;  /* VREFINT read just before the burst  */
    uint32_t  vdda_after_mv;   /* VREFINT read just after the burst   */
} adc_burst_t;

/* ---- Driver instance ------------------------------------------------------ */

typedef struct {
    ADC_HandleTypeDef *master;   /* ADC1: V_DCHG + injected housekeeping */
    ADC_HandleTypeDef *slave;    /* ADC2: I_DCHG                         */

    volatile hk_adc_t hk;        /* latest housekeeping results          */
    adc_burst_t       burst;     /* current / last burst                 */

    /* JSQR value for each injected sequence, captured at init */
    uint32_t jsqr_hk[HK_NUM_SEQ];
    uint32_t jsqr_vrefint;
} pib_adc_t;

/* Call once after MX_ADC1_Init()/MX_ADC2_Init(), before any ADC use.
   Calibrates both ADCs and builds the injected sequences. */
HAL_StatusTypeDef pib_adc_init(pib_adc_t *adc, ADC_HandleTypeDef *master, ADC_HandleTypeDef *slave);

/* Read every housekeeping channel once into adc->hk (~45 us, blocking).
   Returns HAL_BUSY while a burst is running. */
HAL_StatusTypeDef pib_adc_read_housekeeping(pib_adc_t *adc);

/* Start a burst into buf (n_samples words, max 65535). Returns at once. */
HAL_StatusTypeDef pib_adc_burst_start(pib_adc_t *adc, uint32_t *buf, uint32_t n_samples);

/* Non-blocking; call from thread context until DONE or ERROR. */
adc_burst_state_t pib_adc_burst_poll(pib_adc_t *adc);

/* Blocking: start a burst and wait for it (timeout_ms >= 2). */
HAL_StatusTypeDef pib_adc_burst_read(pib_adc_t *adc, uint32_t *buf, uint32_t n_samples,
                                     uint32_t timeout_ms);

/* Stop a running burst. Leaves the ADCs enabled. */
void pib_adc_burst_abort(pib_adc_t *adc);

#endif /* PIB_ADC_H */
