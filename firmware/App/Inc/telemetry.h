#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <stdbool.h>
#include <stdint.h>

#define TLM_REG_STATUS            0x00u
#define TLM_REG_UPTIME            0x01u
#define TLM_REG_SAFE_PAYLOAD      0x06u
#define TLM_REG_ARM_PAYLOAD       0x07u
#define TLM_REG_FIRE_PULSE        0x08u
#define TLM_REG_SET_VALVE         0x09u
#define TLM_REG_HEATER_OVERRIDE   0x0Au
#define TLM_REG_SET_OUTPUT        0x0Bu
#define TLM_REG_SET_TANK_TEMP     0x0Cu
#define TLM_REG_FRIT_PT           0x10u
#define TLM_REG_TANK_PT           0x18u
#define TLM_REG_TANK_TEMP         0x20u
#define TLM_REG_PIB_TEMP          0x28u
#define TLM_REG_FLOW              0x30u
#define TLM_REG_IGN_SUPPLY_V      0x38u
#define TLM_REG_IGN_TEMP          0x3Cu
#define TLM_REG_DCHG_TEMP         0x40u
#define TLM_REG_PIB_STATE         0x44u
#define TLM_REG_OUTPUTS           0x45u
#define TLM_REG_INPUTS            0x46u
#define TLM_REG_VALVES            0x47u
#define TLM_REG_NTC_RAW           0x48u
#define TLM_REG_PULSE_HEADER      0x50u
#define TLM_REG_WAVEFORM_FIRST    0x60u
#define TLM_REG_WAVEFORM_LAST     0x73u

#define TLM_READ_FLAG             0x80u
#define TLM_ARM_CODE              0x53u

#define TLM_STATUS_ARMED          (1u << 0)
#define TLM_STATUS_FLOW_FAULT     (1u << 1)
#define TLM_STATUS_IGNITER_FAULT  (1u << 2)
#define TLM_STATUS_DISCHARGE_FAULT (1u << 3)
#define TLM_STATUS_DRDY           (1u << 4)
#define TLM_STATUS_BATT_UVLO      (1u << 5)
#define TLM_STATUS_TEMP_FAULT     (1u << 6)

typedef enum {
    TLM_OUT_VBAT_EN = 0,
    TLM_OUT_ARM_SAFE,
    TLM_OUT_IGNITER_EN,
    TLM_OUT_DISCHARGE_EN,
    TLM_OUT_HTR1_EN,
    TLM_OUT_HTR2_EN,
    TLM_OUT_PD_RST,
    TLM_OUT_WDG_IN,
    TLM_OUT_COUNT
} tlm_output_t;

typedef enum {
    TLM_IN_PGOOD_12V = 0,
    TLM_IN_VBAT_PGOOD,
    TLM_IN_IGNITER_PGOOD,
    TLM_IN_DISCHARGE_PGOOD,
    TLM_IN_COUNT
} tlm_input_t;

#define TLM_NTC_RAW_COUNT         3u

#define TLM_WAVE_SAMPLES          ((TLM_REG_WAVEFORM_LAST - TLM_REG_WAVEFORM_FIRST + 1u) * 4u)

#define TLM_CMD_MAX_DATA          4u
#define TLM_CMD_QUEUE_LEN         4u

typedef struct {
    uint8_t status;
    float uptime_s;
    float frit_mbar;
    float frit_degC;
    float tank_mbar;
    float tank_degC;
    float tank_ntc_degC[2];
    float pib_degC[2];
    float flow_ul_min;
    float flow_degC;
    float ign_supply_v;
    float ign_degC;
    float dchg_degC;
    uint8_t pib_state;
    uint8_t outputs;
    uint8_t inputs;
    uint8_t valves;
    uint16_t ntc_raw[TLM_NTC_RAW_COUNT];
} telemetry_hk_t;

typedef struct {
    uint8_t addr;
    uint8_t len;
    uint8_t data[TLM_CMD_MAX_DATA];
} telemetry_cmd_t;

typedef struct {
    uint32_t commands;
    uint32_t rejected;
    uint32_t dropped;
    uint32_t reads;
} telemetry_stats_t;

void telemetry_init(void);
void telemetry_update_snapshot(const telemetry_hk_t *hk);
void telemetry_set_waveform(const uint32_t *samples);
void telemetry_set_pulse_header(uint32_t pulse_index, uint16_t ign_peak_i, uint16_t ign_peak_v,
                                float sample_us, uint32_t fire_sample);
bool telemetry_take_waveform_read(void);
uint16_t telemetry_read(uint8_t addr, uint8_t *dst, uint16_t n);
void telemetry_on_write(uint8_t addr, const uint8_t *data, uint16_t n);
bool telemetry_take_command(telemetry_cmd_t *cmd);
void telemetry_get_stats(telemetry_stats_t *s);

uint16_t telemetry_on_spi_frame(const uint8_t *rx, uint16_t n, uint8_t *tx_next, uint16_t tx_max);
void telemetry_on_i2c_write(const uint8_t *rx, uint16_t n);
uint16_t telemetry_on_i2c_read(uint8_t *tx, uint16_t max);

float telemetry_get_f32(const uint8_t *p);

#endif
