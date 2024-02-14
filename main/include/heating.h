#ifndef __HEATING_H__
#define __HEATING_H__

#include "util.h"

typedef struct {
    char name[10];
    float prev;
    int i;
    int c;
    float vals[5];
    float val;
    float set;

    time_t triggered;
    int state;
    time_t change;
} heating_t;

void thermostat_init();
heating_t *heating_find(char *name, int create);
heating_t *heating_temp_val(char *name, float val);
heating_t *heating_temp_set(char *name, float set);
iter_t heating_iter();
iter_t heating_next(iter_t iter, heating_t **zone);
void th_aes_init();

#endif /* __HEATING_H__ */
