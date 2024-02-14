#ifndef __ADC2_H__
#define __ADC2_H__

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "util.h"

typedef enum {
    ADC2_NONE = 0,
    ADC2_ADC,
    ADC2_WIFI,
} adc2_mode_t;

extern adc2_mode_t adc2_use;
extern int wifi_count;
void ADC2_FREE();
void WIFI_ADD(TaskHandle_t owner);
void WIFI_DEL(TaskHandle_t owner);
int ADC2_WAIT(adc2_mode_t value, int add, TickType_t force_tick, int nonblock, TaskHandle_t owner);

typedef struct {
    time_t time;
    TaskHandle_t task;
} wifi_owner_t;

extern list_t wifi_owner;

#endif /* __ADC2_H__ */
