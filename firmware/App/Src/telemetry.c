#include "telemetry.h"

#include <string.h>

#define HK_IMAGE_SIZE   TLM_REG_WAVEFORM_FIRST
#define WAVE_SLOT_SIZE  16u
#define WAVE_SLOTS      (TLM_REG_WAVEFORM_LAST - TLM_REG_WAVEFORM_FIRST + 1u)
#define IMAGE_SIZE      (HK_IMAGE_SIZE + WAVE_SLOTS * WAVE_SLOT_SIZE)

typedef struct {
    uint8_t addr;
    uint8_t len;
} cmd_def_t;

static const cmd_def_t cmd_defs[] = {
    { TLM_REG_SAFE_PAYLOAD,    0u },
    { TLM_REG_ARM_PAYLOAD,     1u },
    { TLM_REG_FIRE_PULSE,      4u },
    { TLM_REG_HEATER_OVERRIDE, 2u },
    { TLM_REG_SET_TANK_TEMP,   4u },
};

static uint8_t hk_image[2][HK_IMAGE_SIZE];
static volatile uint8_t front;
static uint8_t wave_image[WAVE_SLOTS * WAVE_SLOT_SIZE];
static volatile bool wave_read;
static uint8_t pulse_header[16];

static telemetry_cmd_t queue[TLM_CMD_QUEUE_LEN];
static volatile uint8_t q_head;
static volatile uint8_t q_tail;

static volatile telemetry_stats_t stats;
static volatile uint8_t i2c_read_addr;

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void put_f32(uint8_t *p, float f)
{
    uint32_t v;
    memcpy(&v, &f, sizeof v);
    put_u32(p, v);
}

float telemetry_get_f32(const uint8_t *p)
{
    uint32_t v = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    float f;
    memcpy(&f, &v, sizeof f);
    return f;
}

static int32_t image_offset(uint8_t addr)
{
    if (addr < TLM_REG_WAVEFORM_FIRST) {
        return addr;
    }
    if (addr <= TLM_REG_WAVEFORM_LAST) {
        return (int32_t)(HK_IMAGE_SIZE + (uint32_t)(addr - TLM_REG_WAVEFORM_FIRST) * WAVE_SLOT_SIZE);
    }
    return -1;
}

void telemetry_init(void)
{
    memset(hk_image, 0, sizeof hk_image);
    memset(wave_image, 0, sizeof wave_image);
    memset(pulse_header, 0, sizeof pulse_header);
    front = 0;
    q_head = 0;
    q_tail = 0;
    i2c_read_addr = 0;
}

void telemetry_update_snapshot(const telemetry_hk_t *hk)
{
    uint8_t back = front ^ 1u;
    uint8_t *img = hk_image[back];

    memset(img, 0, HK_IMAGE_SIZE);
    img[TLM_REG_STATUS] = hk->status;
    put_f32(&img[TLM_REG_UPTIME], hk->uptime_s);
    put_f32(&img[TLM_REG_FRIT_PT], hk->frit_mbar);
    put_f32(&img[TLM_REG_FRIT_PT + 4u], hk->frit_degC);
    put_f32(&img[TLM_REG_TANK_PT], hk->tank_mbar);
    put_f32(&img[TLM_REG_TANK_PT + 4u], hk->tank_degC);
    put_f32(&img[TLM_REG_TANK_TEMP], hk->tank_ntc_degC[0]);
    put_f32(&img[TLM_REG_TANK_TEMP + 4u], hk->tank_ntc_degC[1]);
    put_f32(&img[TLM_REG_PIB_TEMP], hk->pib_degC[0]);
    put_f32(&img[TLM_REG_PIB_TEMP + 4u], hk->pib_degC[1]);
    put_f32(&img[TLM_REG_FLOW], hk->flow_ul_min);
    put_f32(&img[TLM_REG_FLOW + 4u], hk->flow_degC);
    put_f32(&img[TLM_REG_IGN_SUPPLY_V], hk->ign_supply_v);
    put_f32(&img[TLM_REG_IGN_TEMP], hk->ign_degC);
    put_f32(&img[TLM_REG_DCHG_TEMP], hk->dchg_degC);
    memcpy(&img[TLM_REG_PULSE_HEADER], pulse_header, sizeof pulse_header);

    front = back;
}

void telemetry_set_pulse_header(uint32_t pulse_index, uint16_t ign_peak_i, uint16_t ign_peak_v,
                                float sample_us, uint32_t fire_sample)
{
    put_u32(&pulse_header[0], pulse_index);
    pulse_header[4] = (uint8_t)ign_peak_i;
    pulse_header[5] = (uint8_t)(ign_peak_i >> 8);
    pulse_header[6] = (uint8_t)ign_peak_v;
    pulse_header[7] = (uint8_t)(ign_peak_v >> 8);
    put_f32(&pulse_header[8], sample_us);
    put_u32(&pulse_header[12], fire_sample);
    memcpy(&hk_image[0][TLM_REG_PULSE_HEADER], pulse_header, sizeof pulse_header);
    memcpy(&hk_image[1][TLM_REG_PULSE_HEADER], pulse_header, sizeof pulse_header);
}

void telemetry_set_waveform(const uint32_t *samples)
{
    for (uint32_t i = 0; i < TLM_WAVE_SAMPLES; i++) {
        put_u32(&wave_image[i * 4u], samples[i]);
    }
    wave_read = false;
}

bool telemetry_take_waveform_read(void)
{
    bool r = wave_read;
    wave_read = false;
    return r;
}

uint16_t telemetry_read(uint8_t addr, uint8_t *dst, uint16_t n)
{
    int32_t off = image_offset(addr);
    if (off < 0) {
        return 0;
    }

    const uint8_t *hk = hk_image[front];
    uint16_t done = 0;
    while (done < n && off < (int32_t)IMAGE_SIZE) {
        dst[done++] = (off < (int32_t)HK_IMAGE_SIZE) ? hk[off] : wave_image[off - (int32_t)HK_IMAGE_SIZE];
        off++;
    }
    if (off == (int32_t)IMAGE_SIZE) {
        wave_read = true;
    }
    stats.reads++;
    return done;
}

void telemetry_on_write(uint8_t addr, const uint8_t *data, uint16_t n)
{
    for (uint32_t i = 0; i < sizeof cmd_defs / sizeof cmd_defs[0]; i++) {
        if (cmd_defs[i].addr != addr) {
            continue;
        }
        if (n != cmd_defs[i].len) {
            stats.rejected++;
            return;
        }
        uint8_t next = (uint8_t)((q_head + 1u) % TLM_CMD_QUEUE_LEN);
        if (next == q_tail) {
            stats.dropped++;
            return;
        }
        queue[q_head].addr = addr;
        queue[q_head].len = (uint8_t)n;
        memcpy(queue[q_head].data, data, n);
        q_head = next;
        stats.commands++;
        return;
    }
    stats.rejected++;
}

bool telemetry_take_command(telemetry_cmd_t *cmd)
{
    if (q_tail == q_head) {
        return false;
    }
    *cmd = queue[q_tail];
    q_tail = (uint8_t)((q_tail + 1u) % TLM_CMD_QUEUE_LEN);
    return true;
}

void telemetry_get_stats(telemetry_stats_t *s)
{
    s->commands = stats.commands;
    s->rejected = stats.rejected;
    s->dropped = stats.dropped;
    s->reads = stats.reads;
}

uint16_t telemetry_on_spi_frame(const uint8_t *rx, uint16_t n, uint8_t *tx_next, uint16_t tx_max)
{
    uint8_t first = rx[0];

    if (first == 0x00u || first == 0xFFu) {
        return 0;
    }
    if ((first & TLM_READ_FLAG) != 0u) {
        if (n < 2u) {
            stats.rejected++;
            return 0;
        }
        uint16_t len = rx[1];
        if (len > tx_max) {
            len = tx_max;
        }
        return telemetry_read(first & (uint8_t)~TLM_READ_FLAG, tx_next, len);
    }
    telemetry_on_write(first, &rx[1], (uint16_t)(n - 1u));
    return 0;
}

void telemetry_on_i2c_write(const uint8_t *rx, uint16_t n)
{
    if ((rx[0] & TLM_READ_FLAG) != 0u) {
        i2c_read_addr = rx[0] & (uint8_t)~TLM_READ_FLAG;
        return;
    }
    telemetry_on_write(rx[0], &rx[1], (uint16_t)(n - 1u));
}

uint16_t telemetry_on_i2c_read(uint8_t *tx, uint16_t max)
{
    return telemetry_read(i2c_read_addr, tx, max);
}
