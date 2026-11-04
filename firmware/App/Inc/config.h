#ifndef CONFIG_H
#define CONFIG_H

// Latching-valve (V1, V3) coil pulse length. The valve needs 10-30 ms to switch;
// the GSE hardware pulse is 63-205 ms (about 100 ms typical). Revisit once the
// LHLA1221211H minimum pulse width is known (PIB review Q3).
#define VALVE_LATCH_PULSE_MS 100u

#endif /* CONFIG_H */
