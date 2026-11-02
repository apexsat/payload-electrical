#include "dpt.h"

typedef struct {
	int32_t C1, C2, C3, C4, C5, C6;
} ms5540c_cal_t;

void ms5540c_init(ms5540c_t * dpt)
{
    dwt_init();
    ms5540c_reset(dpt, 0);
    ms5540c_reset(dpt, 1);
    ms5540c_read_calibration(dpt, 0);
    ms5540c_read_calibration(dpt, 1);
    return;
}

static void dwt_init(void) {
	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
	DWT->CYCCNT = 0;
	DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static void delay_us(uint32_t us) {
	uint32_t start = DWT->CYCCNT;
	uint32_t ticks = us * (SystemCoreClock / 1000000U);
	while ((DWT->CYCCNT - start) < ticks) {
	}
}

/* Write: sensor samples DIN on the rising edge of SCLK, MSB first */
static void send_bits(ms5540c_t * dpt, bool dpt_sel, uint32_t value, int nbits) {
    HAL_GPIO_WritePin(dpt->mux_gpio_port, dpt->mux_gpio_pin, dpt_sel);
	for (int i = nbits - 1; i >= 0; i--) {
		HAL_GPIO_WritePin(dpt->din_gpio_port, dpt->din_gpio_pin,
				((value >> i) & 1U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
		delay_us(2);
		HAL_GPIO_WritePin(dpt->clk_gpio_port, dpt->clk_gpio_pin, GPIO_PIN_SET);
		delay_us(2);
		HAL_GPIO_WritePin(dpt->clk_gpio_port, dpt->clk_gpio_pin, GPIO_PIN_RESET);
		delay_us(2);
	}
}

/* Read: DOUT changes on the rising edge, we sample on the falling edge, MSB first */
static uint16_t read_word(ms5540c_t * dpt, bool dpt_sel) {
    HAL_GPIO_WritePin(dpt->mux_gpio_port, dpt->mux_gpio_pin, dpt_sel);
	uint16_t v = 0;
	for (int i = 0; i < 16; i++) {
		HAL_GPIO_WritePin(dpt->clk_gpio_port, dpt->clk_gpio_pin, GPIO_PIN_SET);
		delay_us(2);
		HAL_GPIO_WritePin(dpt->clk_gpio_port, dpt->clk_gpio_pin, GPIO_PIN_RESET);
		delay_us(2);
		v = (uint16_t) ((v << 1)
				| (HAL_GPIO_ReadPin(dpt->dout_gpio_port, dpt->dout_gpio_pin) == GPIO_PIN_SET ?
						1U : 0U));
	}
	return v;
}

static void ms5540c_reset(ms5540c_t * dpt, bool dpt_sel) {
	send_bits(dpt, dpt_sel, 0x155540, 24); /* bytes 0x15 0x55 0x40 */
}

/* Send a 16-bit command, optionally wait for a conversion, read a 16-bit result */
static uint16_t ms5540c_transaction(ms5540c_t * dpt, bool dpt_sel, uint16_t cmd, uint32_t wait_ms) {
    HAL_GPIO_WritePin(dpt->mux_gpio_port, dpt->mux_gpio_pin, dpt_sel);
	ms5540c_reset(dpt, dpt_sel);
	send_bits(dpt, dpt_sel, cmd, 16);
	if (wait_ms)
		HAL_Delay(wait_ms);
	uint16_t r = read_word(dpt, dpt_sel);
	ms5540c_reset(dpt, dpt_sel);
	return r;
}

/* ---------- calibration ---------- */
static void ms5540c_read_calibration(ms5540c_t * dpt, bool dpt_sel) {
	uint16_t W1 = ms5540c_transaction(0x1D50, 0);
	uint16_t W2 = ms5540c_transaction(0x1D60, 0);
	uint16_t W3 = ms5540c_transaction(0x1D90, 0);
	uint16_t W4 = ms5540c_transaction(0x1DA0, 0);

	/* MS5540C coefficient extraction - double-check against your datasheet */
	dpt->cal[dpt_sel].C1 = (W1 >> 1) & 0x7FFF;
	dpt->cal[dpt_sel].C2 = ((W3 & 0x3F) << 6) | (W4 & 0x3F);
	dpt->cal[dpt_sel].C3 = (W4 >> 6) & 0x3FF;
	dpt->cal[dpt_sel].C4 = (W3 >> 6) & 0x3FF;
	dpt->cal[dpt_sel].C5 = ((W1 & 0x1) << 10) | ((W2 >> 6) & 0x3FF);
	dpt->cal[dpt_sel].C6 = W2 & 0x3F;
}

/* Returns temperature in 0.1 degC and pressure in 0.1 mbar (integers). */
void ms5540c_measure(ms5540c_t * dpt, bool dpt_sel, uint16_t *d1, uint16_t *d2, int32_t *temp_dC,
		int32_t *p_dmbar) {
	*d1 = ms5540c_transaction(dpt, dpt_sel, 0x0F40, 40); /* D1 = pressure,    ~35 ms conversion */
	*d2 = ms5540c_transaction(dpt, dpt_sel, 0x0F20, 40); /* D2 = temperature, ~35 ms conversion */

	int32_t UT1 = (dpt->cal[dpt_sel].C5 << 3) + 20224;
	int32_t dT = (int32_t) (*d2) - UT1;
	int32_t TEMP = 200 + ((dT * (dpt->cal[dpt_sel].C6 + 50)) >> 10);
	int32_t OFF = (dpt->cal[dpt_sel].C2 * 4) + (((dpt->cal[dpt_sel].C4 - 512) * dT) >> 12);
	int32_t SENS = dpt->cal[dpt_sel].C1 + ((dpt->cal[dpt_sel].C3 * dT) >> 10) + 24576;
	int32_t X = (int32_t) (((int64_t) SENS * ((int32_t) (*d1) - 7168)) >> 14)
			- OFF;
	int32_t P = ((X * 10) >> 5) + 2500;

	*temp_dC = TEMP;
	*p_dmbar = P;
}