#ifndef __TEMP_H__
#define __TEMP_H__

#include "config.h"
#include "module.h"
#include "check.h"
#include "heating.h"

typedef struct {
    module_t module;
    task_t *task;
    int stop;
} temp_t;

typedef struct {
    int adc;
    char name[member_size(heating_t, name)];
} th_zone_t;

extern th_zone_t temp_zones[ADC1_CNT+ADC2_CNT];

void temp_init_(int count, thermistor_t *th, char *label);
#define temp_init(count, th) temp_init_(count, th, #th)
void temp_run(temp_t *dummy, int run);
th_zone_t *temp_zone_find(char *name);
void temp_zone_init(char *name);
th_zone_t *temp_zone_adc(char *name, int adc);
void temp_zone_load();

#endif /* __TEMP_H__ */
