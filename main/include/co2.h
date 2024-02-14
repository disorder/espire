#ifndef __CO2_H__
#define __CO2_H__

extern int co2_ppm;
void co2_init();
int senseair_s8_co2_ppm();
int senseair_s8_abc();
int senseair_s8_abc_enable();
int senseair_s8_abc_disable();
int senseair_s8_fwver();
uint32_t senseair_s8_id();

#endif /* __CO2_H__ */
