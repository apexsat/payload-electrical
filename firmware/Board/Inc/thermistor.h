#ifndef INC_THERMISTOR_H_
#define INC_THERMISTOR_H_

#include <math.h>

#define CELSIUS_TO_KELVIN 273.15

typedef struct
{
    float BETA, TREF, RREF, RUPPER;
    /*
      BETA: K
      TREF: DegC
      RREF: KOhm
      RUPPER: KOhm - voltage divider upper resistor
    */
} thermistor_t;

float ratiometric_to_thermistor_temp(thermistor_t * therm, float ratio);

#endif /* INC_THERMISTOR_H_ */
