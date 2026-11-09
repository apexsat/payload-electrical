#include "thermistor.h"

float ratiometric_to_thermistor_temp(thermistor_t * therm, float ratio)
{
  // Returns temperature in C given a ratiometric measurement (raw adc / adc resolution). Assumes ADC Vref is also the Vref for the thermistor voltage divider
  if (ratio >= 1 || ratio < 0 || therm->RUPPER < 0 || therm->RREF <= 0 || therm->BETA <= 0 || therm->TREF <= - CELSIUS_TO_KELVIN)
  {
    return 0 - CELSIUS_TO_KELVIN;
  }
  float temp_k = 1 / (log(ratio * therm->RUPPER / ((1 - ratio) * therm->RREF)) / therm->BETA + 1 / (therm->TREF + CELSIUS_TO_KELVIN));
  return temp_k - CELSIUS_TO_KELVIN;
}