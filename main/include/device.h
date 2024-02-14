#ifndef __DEVICE_H__
#define __DEVICE_H__

#include "config.h"
#include "module.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_pm.h"

typedef struct {
    module_t module;
    int force;
    void (*wake_cb)(int);
} sleep_t;

typedef struct {
    // 6 bytes
    char *mac;
    char *hostname;
    int controller;
    int relays;
    int th_def_cnt;
    th_def_t *th_defs;
    int metar;
} device_t;

typedef struct {
    // TODO
    //display_t *oled;
    //oled_update_t *oled_update;
    device_t *dev;
    SemaphoreHandle_t sockets;
    // buttons and possibly other recent activity flag to prevent sleep
    time_t activity;
    time_t start;
    int sleeping;
    uint64_t slept_ms;
    sleep_t *sleep;

    int pm;
    //esp_pm_lock_handle_t oled;
} system_t;

extern system_t esp;

extern device_t devices[];

void system_init();
void device_init();

void sleep_loop(sleep_t *self);
void sleep_run(sleep_t *dummy, int run);
sleep_t *sleep_init(void (*wake_cb)(int), int force);

#endif /* __DEVICE_H__ */
