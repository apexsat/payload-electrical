#include "flow.h"

static uint8_t checksum(uint16_t data16)
{
    uint8_t data[2]; uint8_t byteCtr; uint8_t calc_crc = 0;
    data[1] = data16 & 0xff;
    data[0] = data16 >> 8;
    for (byteCtr = 0; byteCtr < 2; ++byteCtr)
    {
        calc_crc ^= (data[byteCtr]);
        for (uint8_t i = 8; i > 0; --i)
        {
            if (calc_crc & 0x80) { calc_crc = (calc_crc << 1) ^ LG160431D_CRC_POLYNOMIAL;}
            else { calc_crc = (calc_crc << 1); }
        }
    }
    return calc_crc;
}

bool lg160431d_init(lg160431d_t * lg, uint8_t cf, bool heater, uint8_t res, bool hold_master)
{
    /*
      cf: 3 bits, Calibration Field (sensor default 0) CF0 H20, CF1 IPA, CF2-4 unused
      heater: 1 bit, 0 = shut down heater between measurements, 1 = keep heater power as-is
      res: 3 bits, Resolution setting
            res   | flow res | conv time | temp/vdd res
            000:  | 9 bit    | 0.8ms     | 6 bit
            001:  | 10 bit   | 1.3ms     | 7 bit
            010:  | 11 bit   | 2.4ms     | 8 bit
            011:  | 12 bit   | 4.6ms     | 9 bit
            100:  | 13 bit   | 8.9ms     | 10 bit
            101:  | 14 bit   | 17.5ms    | 11 bit
            110:  | 15 bit   | 34.8ms    | 12 bit
            111:  | 16 bit   | 69.3ms    | 13 bit
      hold_master: 1 bit, whether to hold master or trigger conversion then wait for results polling. Saved as state variable in this driver
    */
    lg160431d_reset(lg);

    cf &= 0b111;
    res &= 0b111;
    uint8_t data[3];

    // Set user register
    data[0] = LG160431D_USER_REG_R;
    if (HAL_I2C_Master_Transmit (lg->i2c, lg->addr << 1, data, 1, 10)) return 1;
    if (HAL_I2C_Master_Receive (lg->i2c, lg->addr << 1, data, 3, 10)) return 1;
    uint16_t userreg = (data[0] << 8) | data[1];
    if (checksum(userreg) != data[2]) return 1;
    uint16_t new_userreg = (userreg & 0xff8f) | (cf << 4);
    data[0] = LG160431D_USER_REG_W;
    data[1] = new_userreg >> 8;
    data[2] = new_userreg & 0xff;
    if (HAL_I2C_Master_Transmit (lg->i2c, lg->addr << 1, data, 3, 10)) return 1;

    // Set advanced user register
    data[0] = LG160431D_ADV_USER_REG_R;
    if (HAL_I2C_Master_Transmit (lg->i2c, lg->addr << 1, data, 1, 10)) return 1;
    if (HAL_I2C_Master_Receive (lg->i2c, lg->addr << 1, data, 3, 10)) return 1;
    uint16_t adv_reg = (data[0] << 8) | data[1];
    if (checksum(adv_reg) != data[2]) return 1;
    uint16_t new_adv_reg = (adv_reg & 0xe1fd) | (heater << 12) | (res << 9) | (hold_master << 1);
    data[0] = LG160431D_ADV_USER_REG_W;
    data[1] = new_adv_reg >> 8;
    data[2] = new_adv_reg & 0xff;
    if (HAL_I2C_Master_Transmit (lg->i2c, lg->addr << 1, data, 3, 10)) return 1;
    lg->hold_master = hold_master;

    // Read scale factor
    data[0] = LG160431D_EEPROM_R;
    data[1] = scale_factor_addr[cf] >> 4;
    data[2] = (scale_factor_addr[cf] << 4) & 0xff;
    if (HAL_I2C_Master_Transmit (lg->i2c, lg->addr << 1, data, 3, 10)) return 1;
    if (HAL_I2C_Master_Receive (lg->i2c, lg->addr << 1, data, 3, 10)) return 1;
    uint16_t scale_factor = (data[0] << 8) | data[1];
    if (checksum(scale_factor) != data[2]) return 1;
    lg->scale_factor = scale_factor;

    return 0;
}


void lg160431d_reset(lg160431d_t * lg)
{
    uint8_t data[1] = {LG160431D_SOFT_RESET};
    HAL_I2C_Master_Transmit (lg->i2c, lg->addr << 1, data, 1, 10);
    HAL_Delay(5); // The maximum time for a soft reset is 2.6 ms if the SCL line is HIGH and up to 31 ms if it is pulled LOW. 
    lg->conv_in_progress = 0;
    lg->hold_master = 1; // Sensor default
}

uint32_t lg160431d_read_sn(lg160431d_t * lg)
{
    uint8_t data[3];
    uint32_t sn;
    data[0] = LG160431D_EEPROM_R;
    data[1] = LG160431D_SN_ADDR >> 4;
    data[2] = (LG160431D_SN_ADDR << 4) & 0xff;
    HAL_I2C_Master_Transmit(lg->i2c, lg->addr << 1, data, 3, 10);
    HAL_I2C_Master_Receive(lg->i2c, lg->addr << 1, data, 3, 10);
    uint16_t raw = (data[0] << 8) | data[1];
    if (checksum(raw) != data[2]) return 0;
    sn = (uint32_t)raw << 16;
    HAL_I2C_Master_Receive(lg->i2c, lg->addr << 1, data, 3, 10);
    raw = (data[0] << 8) | data[1];
    if (checksum(raw) != data[2]) return 0;
    sn |= raw;

    return sn;
}

bool lg160431d_trigger_flow(lg160431d_t * lg)
{
    /*
        Trigger a flow measurement, return 1 if error during transaction or if device is in hold master mode
        If device is in hold master mode use lg160431d_read_flow_blocking instead
    */
    if (lg->hold_master || lg->conv_in_progress) return 1;
    uint8_t data[3];
    data[0] = LG160431D_TRIGGER_FLOW_MEASUREMENT;
    if (HAL_I2C_Master_Transmit (lg->i2c, lg->addr << 1, data, 1, 10)) return 1;
    if (HAL_I2C_Master_Receive(lg->i2c, lg->addr << 1, data, 3, 10)) return 1; // Sensor begins measurement upon receiving a read header
    lg->conv_in_progress = 1;
    return 0;
}

bool lg160431d_trigger_vdd(lg160431d_t * lg)
{
    /*
        Trigger a vdd measurement, return 1 if error during transaction or if device is in hold master mode
        If device is in hold master mode use lg160431d_read_vdd_blocking instead
    */
    if (lg->hold_master || lg->conv_in_progress) return 1;
    uint8_t data[3];
    data[0] = LG160431D_TRIGGER_VDD_MEASUREMENT;
    if (HAL_I2C_Master_Transmit (lg->i2c, lg->addr << 1, data, 1, 10)) return 1;
    if (HAL_I2C_Master_Receive(lg->i2c, lg->addr << 1, data, 3, 10)) return 1; // Sensor begins measurement upon receiving a read header
    lg->conv_in_progress = 1;
    return 0;
}

bool lg160431d_trigger_temp(lg160431d_t * lg)
{
    /*
        Trigger a temp measurement, return 1 if error during transaction or if device is in hold master mode
        If device is in hold master mode use lg160431d_read_temp_blocking instead
    */
    if (lg->hold_master || lg->conv_in_progress) return 1;
    uint8_t data[3];
    data[0] = LG160431D_TRIGGER_TEMP_MEASUREMENT;
    if (HAL_I2C_Master_Transmit (lg->i2c, lg->addr << 1, data, 1, 10)) return 1;
    if (HAL_I2C_Master_Receive(lg->i2c, lg->addr << 1, data, 3, 10)) return 1; // Sensor begins measurement upon receiving a read header
    lg->conv_in_progress = 1;
    return 0;
}

bool lg160431d_poll_flow(lg160431d_t * lg, float * flow)
{
    /*
        Polls the sensor after a conversion has been triggered, returns 0 if a valid result is found. Ignores calls if hold master mode is on
        This driver assumes that measurement units are ul/min as that is the only unit the LG16-0431D uses in its standard calibrations
        flow: Flow in ul/min
    */
    if (lg->hold_master || !lg->conv_in_progress || !lg->scale_factor) return 1;
    uint8_t data[3];
    if (HAL_I2C_Master_Receive (lg->i2c, lg->addr << 1, data, 3, 10)) return 1;
    
    lg->conv_in_progress = 0;
    int16_t flow_raw = (data[0] << 8) | data[1];
    if (checksum(flow_raw) != data[2]) return 1;
    *flow = (float)flow_raw / lg->scale_factor;

    return 0;
}

bool lg160431d_poll_vdd(lg160431d_t * lg, float * vdd)
{
    /*
        Polls the sensor after a conversion has been triggered, returns 0 if a valid result is found. Ignores calls if hold master mode is on
        vdd: VDD in volts
    */
    if (lg->hold_master || !lg->conv_in_progress) return 1;
    uint8_t data[3];
    if (HAL_I2C_Master_Receive (lg->i2c, lg->addr << 1, data, 3, 10)) return 1;

    lg->conv_in_progress = 0;
    uint16_t vdd_raw = (data[0] << 8) | data[1];
    if (checksum(vdd_raw) != data[2]) return 1;
    *vdd = vdd_raw / 1000.0; // Scale factor is 1mV

    return 0;
}

bool lg160431d_poll_temp(lg160431d_t * lg, float * temp)
{
    /*
        Polls the sensor after a conversion has been triggered, returns 0 if a valid result is found. Ignores calls if hold master mode is on
        temp: Temperature in degC
    */
    if (lg->hold_master || !lg->conv_in_progress) return 1;
    uint8_t data[3];
    if (HAL_I2C_Master_Receive (lg->i2c, lg->addr << 1, data, 3, 10)) return 1;

    lg->conv_in_progress = 0;
    int16_t temp_raw = (data[0] << 8) | data[1];
    if (checksum(temp_raw) != data[2]) return 1;
    *temp = temp_raw / 10.0; // Scale factor is 0.1C

    return 0;
}

float lg160431d_read_flow_blocking(lg160431d_t * lg)
{
    /*
        Trigger a measurement and wait for it to complete in hold master mode. Returns zero if error or if sensor is not in hold master mode or CRC fail
        returns float: flow in u/min
    */
    if (!lg->hold_master || !lg->scale_factor) return 0;
    uint8_t data[3];
    data[0] = LG160431D_TRIGGER_FLOW_MEASUREMENT;
    if (HAL_I2C_Master_Transmit (lg->i2c, lg->addr << 1, data, 1, 10)) return 0;
    if (HAL_I2C_Master_Receive (lg->i2c, lg->addr << 1, data, 3, 150)) // This will block for up to 80ms depending on conversion settings
    { 
        HAL_I2C_DeInit(lg->i2c); // Reset i2c bus
        HAL_I2C_Init(lg->i2c);
        return 0;
    }

    int16_t flow_raw = (data[0] << 8) | data[1];
    if (checksum(flow_raw) != data[2]) return 0;
    return (float)flow_raw / lg->scale_factor;
}

float lg160431d_read_vdd_blocking(lg160431d_t * lg)
{
    /*
        Trigger a vdd measurement and wait for it to complete in hold master mode. Returns zero if error or if sensor is not in hold master mode or CRC fail
        returns float: vdd in volts
    */
    if (!lg->hold_master || !lg->scale_factor) return 0;
    uint8_t data[3];
    data[0] = LG160431D_TRIGGER_VDD_MEASUREMENT;
    if (HAL_I2C_Master_Transmit (lg->i2c, lg->addr << 1, data, 1, 10)) return 0;
    if (HAL_I2C_Master_Receive (lg->i2c, lg->addr << 1, data, 3, 150)) // This will block for up to 80ms depending on conversion settings
    {
        HAL_I2C_DeInit(lg->i2c); // Reset i2c bus
        HAL_I2C_Init(lg->i2c);
        return 0;
    }

    uint16_t vdd_raw = (data[0] << 8) | data[1];
    if (checksum(vdd_raw) != data[2]) return 0;
    return vdd_raw / 1000.0; // Scale factor is 1mV
}

float lg160431d_read_temp_blocking(lg160431d_t * lg)
{
    /*
        Trigger a temp measurement and wait for it to complete in hold master mode. Returns zero if error or if sensor is not in hold master mode or CRC fail
        returns float: temp in degC
    */
    if (!lg->hold_master || !lg->scale_factor) return 0;
    uint8_t data[3];
    data[0] = LG160431D_TRIGGER_TEMP_MEASUREMENT;
    if (HAL_I2C_Master_Transmit (lg->i2c, lg->addr << 1, data, 1, 10)) return 0;
    if (HAL_I2C_Master_Receive (lg->i2c, lg->addr << 1, data, 3, 150)) // This will block for up to 80ms depending on conversion settings
    {
        HAL_I2C_DeInit(lg->i2c); // Reset i2c bus
        HAL_I2C_Init(lg->i2c);
        return 0;
    }

    int16_t temp_raw = (data[0] << 8) | data[1];
    if (checksum(temp_raw) != data[2]) return 0;
    return temp_raw / 10.0; // Scale factor is 0.1C
}