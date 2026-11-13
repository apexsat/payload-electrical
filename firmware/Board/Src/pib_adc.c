#include "pib_adc.h"

#include <stddef.h>

#define HK_INJ_TIMEOUT_MS  5U

typedef struct {
    uint32_t channel;       /* ADC_CHANNEL_x    */
    uint32_t sample_time;   /* ADC_SAMPLETIME_x */
} hk_ch_cfg_t;

/* Channel + sample time for each hk_ch_t entry (defines in pib_adc.h) */
static const hk_ch_cfg_t hk_ch_cfg[HK_NUM_CH] = {
    [HK_VREFINT]      = { HK_ADC_CH_VREFINT,      HK_SMP_VREFINT      },
    [HK_TEMP_MCU]     = { HK_ADC_CH_TEMP_MCU,     HK_SMP_TEMP_MCU     },
    [HK_NTC_PIB]      = { HK_ADC_CH_NTC_PIB,      HK_SMP_NTC_PIB      },
    [HK_TANK_NTC_1]   = { HK_ADC_CH_TANK_NTC_1,   HK_SMP_TANK_NTC_1   },
    [HK_TANK_NTC_2]   = { HK_ADC_CH_TANK_NTC_2,   HK_SMP_TANK_NTC_2   },
    [HK_NTC_DCHG]     = { HK_ADC_CH_NTC_DCHG,     HK_SMP_NTC_DCHG     },
    [HK_NTC_IGN]      = { HK_ADC_CH_NTC_IGN,      HK_SMP_NTC_IGN      },
    [HK_V_IGN_SUPPLY] = { HK_ADC_CH_V_IGN_SUPPLY, HK_SMP_V_IGN_SUPPLY },
    [HK_V_IGN_PK]     = { HK_ADC_CH_V_IGN_PK,     HK_SMP_V_IGN_PK     },
    [HK_I_IGN_PK]     = { HK_ADC_CH_I_IGN_PK,     HK_SMP_I_IGN_PK     },
};

/* HAL rank constants aren't 1..4, so map index -> constant */
static const uint32_t hk_inj_rank[HK_INJ_MAX_RANKS] = {
    ADC_INJECTED_RANK_1, ADC_INJECTED_RANK_2, ADC_INJECTED_RANK_3, ADC_INJECTED_RANK_4
};

/* The HAL ADC callbacks are global, so they need a way back to the instance */
static pib_adc_t *active_adc;

static HAL_StatusTypeDef config_injected(pib_adc_t *adc);
static HAL_StatusTypeDef run_injected(pib_adc_t *adc, uint32_t jsqr);
static HAL_StatusTypeDef read_vdda(pib_adc_t *adc, uint32_t *vdda_mv);

/* Number of channels in injected sequence seq (4, except maybe the last) */
static uint32_t seq_len(uint32_t seq)
{
    uint32_t n = HK_NUM_CH - (seq * HK_INJ_MAX_RANKS);
    return (n > HK_INJ_MAX_RANKS) ? HK_INJ_MAX_RANKS : n;
}

/* ---- Init ----------------------------------------------------------------- */

/*
 * Calibrate both ADCs (they must still be disabled) and replace the CubeMX
 * injected group with this driver's sequences. The regular group (burst) is
 * left as CubeMX configured it.
 */
HAL_StatusTypeDef pib_adc_init(pib_adc_t *adc, ADC_HandleTypeDef *master, ADC_HandleTypeDef *slave)
{
    adc->master = master;
    adc->slave  = slave;
    adc->burst.state = ADC_BURST_IDLE;
    active_adc = adc;

    if (HAL_ADCEx_Calibration_Start(master, ADC_SINGLE_ENDED) != HAL_OK) {
        return HAL_ERROR;
    }
    if (HAL_ADCEx_Calibration_Start(slave, ADC_SINGLE_ENDED) != HAL_OK) {
        return HAL_ERROR;
    }

    return config_injected(adc);
}

/*
 * Build every injected sequence with HAL and save each resulting JSQR value.
 * Each HAL call also sets that channel's sample time and, for VREFINT / temp
 * sensor, turns on the internal path; both need the ADCs disabled.
 *
 * A software injected trigger pre-empts the current regular conversion, and
 * regular conversions resume once the injected sequence ends.
 */
static HAL_StatusTypeDef config_injected(pib_adc_t *adc)
{
    ADC_InjectionConfTypeDef inj = {0};
    uint32_t seq, r, first, n;

    /* On G4 ScanConvMode is software-only. HAL ignores ranks > 1 of the
       injected group unless it is enabled. This does not change the regular
       sequence, which was already written to hardware by HAL_ADC_Init(). */
    adc->master->Init.ScanConvMode = ADC_SCAN_ENABLE;

    inj.InjectedSingleDiff            = ADC_SINGLE_ENDED;
    inj.InjectedOffsetNumber          = ADC_OFFSET_NONE;
    inj.InjectedOffset                = 0;
    inj.InjectedDiscontinuousConvMode = DISABLE;
    inj.AutoInjectedConv              = DISABLE;
    inj.QueueInjectedContext          = DISABLE;   /* JQDIS = 1: JSQR writes apply at once */
    inj.ExternalTrigInjecConv         = ADC_INJECTED_SOFTWARE_START;
    inj.ExternalTrigInjecConvEdge     = ADC_EXTERNALTRIGINJECCONV_EDGE_NONE;
    inj.InjecOversamplingMode         = DISABLE;

    /* HAL writes JSQR once the last rank of a sequence is configured */
    for (seq = 0; seq < HK_NUM_SEQ; seq++) {
        first = seq * HK_INJ_MAX_RANKS;
        n = seq_len(seq);

        inj.InjectedNbrOfConversion = n;
        for (r = 0; r < n; r++) {
            inj.InjectedChannel      = hk_ch_cfg[first + r].channel;
            inj.InjectedSamplingTime = hk_ch_cfg[first + r].sample_time;
            inj.InjectedRank         = hk_inj_rank[r];
            if (HAL_ADCEx_InjectedConfigChannel(adc->master, &inj) != HAL_OK) {
                return HAL_ERROR;
            }
        }
        adc->jsqr_hk[seq] = adc->master->Instance->JSQR;
    }

    /* Extra 1-rank sequence: VREFINT alone, for the before/after-burst reads */
    inj.InjectedNbrOfConversion = 1;
    inj.InjectedChannel         = hk_ch_cfg[HK_VREFINT].channel;
    inj.InjectedSamplingTime    = hk_ch_cfg[HK_VREFINT].sample_time;
    inj.InjectedRank            = ADC_INJECTED_RANK_1;
    if (HAL_ADCEx_InjectedConfigChannel(adc->master, &inj) != HAL_OK) {
        return HAL_ERROR;
    }
    adc->jsqr_vrefint = adc->master->Instance->JSQR;
    return HAL_OK;
}

/* ---- Injected helpers ----------------------------------------------------- */

/*
 * Load an injected sequence into JSQR, run it once, and wait for the end of
 * the sequence (JEOS). JSQR may only be written while JADSTART = 0.
 * HAL_ADCEx_InjectedPollForConversion() isn't used because EOCSelection is
 * SINGLE_CONV, so it would return after rank 1 (JEOC).
 */
static HAL_StatusTypeDef run_injected(pib_adc_t *adc, uint32_t jsqr)
{
    uint32_t t0;

    if (LL_ADC_INJ_IsConversionOngoing(adc->master->Instance) != 0UL) {
        return HAL_BUSY;
    }
    adc->master->Instance->JSQR = jsqr;

    if (HAL_ADCEx_InjectedStart(adc->master) != HAL_OK) {
        return HAL_ERROR;
    }

    t0 = HAL_GetTick();
    while (__HAL_ADC_GET_FLAG(adc->master, ADC_FLAG_JEOS) == 0U) {
        if ((HAL_GetTick() - t0) > HK_INJ_TIMEOUT_MS) {
            return HAL_TIMEOUT;
        }
    }
    __HAL_ADC_CLEAR_FLAG(adc->master, ADC_FLAG_JEOC | ADC_FLAG_JEOS);
    return HAL_OK;
}

/* Run the VREFINT-only sequence and convert it to VDDA in mV (~8 us) */
static HAL_StatusTypeDef read_vdda(pib_adc_t *adc, uint32_t *vdda_mv)
{
    HAL_StatusTypeDef st;
    uint32_t raw;

    st = run_injected(adc, adc->jsqr_vrefint);
    if (st != HAL_OK) {
        return st;
    }
    raw = HAL_ADCEx_InjectedGetValue(adc->master, ADC_INJECTED_RANK_1);
    if (raw == 0U) {
        return HAL_ERROR;   /* avoid divide-by-zero in the VDDA calc */
    }
    *vdda_mv = __HAL_ADC_CALC_VREFANALOG_VOLTAGE(raw, ADC_RESOLUTION_12B);
    return HAL_OK;
}

/* ---- Housekeeping --------------------------------------------------------- */

/*
 * Run every housekeeping sequence once, then convert: VDDA from VREFINT,
 * mV for each channel using that VDDA, and MCU temperature.
 */
HAL_StatusTypeDef pib_adc_read_housekeeping(pib_adc_t *adc)
{
    HAL_StatusTypeDef st;
    uint32_t seq, r, first, ch, vdda;

    /* An injected read would pre-empt the burst and leave a gap in it */
    if (adc->burst.state == ADC_BURST_RUNNING) {
        return HAL_BUSY;
    }

    for (seq = 0; seq < HK_NUM_SEQ; seq++) {
        st = run_injected(adc, adc->jsqr_hk[seq]);
        if (st != HAL_OK) {
            return st;
        }
        first = seq * HK_INJ_MAX_RANKS;
        for (r = 0; r < seq_len(seq); r++) {
            adc->hk.raw[first + r] =
                (uint16_t)HAL_ADCEx_InjectedGetValue(adc->master, hk_inj_rank[r]);
        }
    }

    /* VDDA from VREFINT, using the factory VREFINT_CAL (taken at 3.0 V) */
    if (adc->hk.raw[HK_VREFINT] == 0U) {
        return HAL_ERROR;
    }
    vdda = __HAL_ADC_CALC_VREFANALOG_VOLTAGE(adc->hk.raw[HK_VREFINT], ADC_RESOLUTION_12B);
    adc->hk.vdda_mv = vdda;

    for (ch = 0; ch < HK_NUM_CH; ch++) {
        adc->hk.mv[ch] = __HAL_ADC_CALC_DATA_TO_VOLTAGE(vdda, adc->hk.raw[ch], ADC_RESOLUTION_12B);
    }

    /* Factory TS_CAL1/TS_CAL2 (30 / 130 degC, taken at 3.0 V); the macro
       rescales the reading from the measured VDDA */
    adc->hk.temp_mcu_degC =
        __HAL_ADC_CALC_TEMPERATURE(vdda, adc->hk.raw[HK_TEMP_MCU], ADC_RESOLUTION_12B);

    return HAL_OK;
}

/* ---- Burst ---------------------------------------------------------------- */

/*
 * Read VREFINT, then start ADC1+ADC2 dual regular-simultaneous conversions
 * with DMA into buf (one V/I pair per word). Returns at once; the DMA
 * complete IRQ stops the ADCs. buf must stay valid until the burst ends.
 * Capture time = n_samples / 2.133 MSPS (about 0.47 us per sample).
 */
HAL_StatusTypeDef pib_adc_burst_start(pib_adc_t *adc, uint32_t *buf, uint32_t n_samples)
{
    HAL_StatusTypeDef st;

    if ((buf == NULL) || (n_samples == 0U) || (n_samples > 0xFFFFU)) {
        return HAL_ERROR;   /* DMA CNDTR is 16-bit */
    }
    if ((adc->burst.state == ADC_BURST_RUNNING) || (adc->burst.state == ADC_BURST_CAPTURED)) {
        return HAL_BUSY;
    }

    st = read_vdda(adc, &adc->burst.vdda_before_mv);
    if (st != HAL_OK) {
        return st;
    }

    adc->burst.buf           = buf;
    adc->burst.n_samples     = n_samples;
    adc->burst.vdda_after_mv = 0U;
    adc->burst.state         = ADC_BURST_RUNNING;

    st = HAL_ADCEx_MultiModeStart_DMA(adc->master, buf, n_samples);
    if (st != HAL_OK) {
        adc->burst.state = ADC_BURST_ERROR;
    }
    return st;
}

/* Once the DMA has filled the buffer, read VREFINT again and report DONE */
adc_burst_state_t pib_adc_burst_poll(pib_adc_t *adc)
{
    if (adc->burst.state == ADC_BURST_CAPTURED) {
        /* ADSTP from the IRQ finishes within one conversion (~0.5 us) */
        while (LL_ADC_REG_IsConversionOngoing(adc->master->Instance) != 0UL) {
        }
        if (read_vdda(adc, &adc->burst.vdda_after_mv) == HAL_OK) {
            adc->burst.state = ADC_BURST_DONE;
        } else {
            adc->burst.state = ADC_BURST_ERROR;
        }
    }
    return adc->burst.state;
}

HAL_StatusTypeDef pib_adc_burst_read(pib_adc_t *adc, uint32_t *buf, uint32_t n_samples,
                                     uint32_t timeout_ms)
{
    HAL_StatusTypeDef st;
    adc_burst_state_t s;
    uint32_t t0;

    st = pib_adc_burst_start(adc, buf, n_samples);
    if (st != HAL_OK) {
        return st;
    }

    t0 = HAL_GetTick();
    do {
        s = pib_adc_burst_poll(adc);
        if (s == ADC_BURST_DONE) {
            return HAL_OK;
        }
        if (s == ADC_BURST_ERROR) {
            return HAL_ERROR;
        }
    } while ((HAL_GetTick() - t0) <= timeout_ms);

    pib_adc_burst_abort(adc);
    return HAL_TIMEOUT;
}

void pib_adc_burst_abort(pib_adc_t *adc)
{
    ADC_HandleTypeDef *h = adc->master;

    if (LL_ADC_REG_IsConversionOngoing(h->Instance) != 0UL) {
        LL_ADC_REG_StopConversion(h->Instance);   /* master ADSTP stops both */
        while (LL_ADC_REG_IsConversionOngoing(h->Instance) != 0UL) {
        }
    }
    (void)HAL_DMA_Abort(h->DMA_Handle);
    CLEAR_BIT(h->State, HAL_ADC_STATE_REG_BUSY);
    SET_BIT(h->State, HAL_ADC_STATE_READY);
    adc->burst.state = ADC_BURST_IDLE;
}

uint32_t pib_adc_burst_count(pib_adc_t *adc)
{
    return adc->burst.n_samples - __HAL_DMA_GET_COUNTER(adc->master->DMA_Handle);
}

/* ---- HAL callbacks (global: must not be defined anywhere else) ------------ */

/*
 * DMA transfer complete (DMA1_Channel1 IRQ). The regular group is in
 * continuous mode, so the ADCs keep converting after the DMA stops; stop
 * them here. Only regular conversions are stopped, so an injected read is
 * not disturbed. Post-burst VREFINT is read later in pib_adc_burst_poll().
 */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    pib_adc_t *adc = active_adc;

    if ((adc != NULL) && (hadc == adc->master) && (adc->burst.state == ADC_BURST_RUNNING)) {
        LL_ADC_REG_StopConversion(hadc->Instance);
        CLEAR_BIT(hadc->State, HAL_ADC_STATE_REG_BUSY);
        if ((hadc->State & HAL_ADC_STATE_INJ_BUSY) == 0UL) {
            SET_BIT(hadc->State, HAL_ADC_STATE_READY);
        }
        adc->burst.state = ADC_BURST_CAPTURED;
    }
}

/* DMA error during a burst */
void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *hadc)
{
    pib_adc_t *adc = active_adc;

    if ((adc != NULL) && (hadc == adc->master) && (adc->burst.state == ADC_BURST_RUNNING)) {
        LL_ADC_REG_StopConversion(hadc->Instance);
        adc->burst.state = ADC_BURST_ERROR;
    }
}
