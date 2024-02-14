#ifndef __AUTO_H__
#define __AUTO_H__

#define WIFI_SSID_KEY     "wifi.ssid"
#define WIFI_PASSWORD_KEY "wifi.pass"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "util.h"
#include "module.h"

typedef struct {
    module_t module;
    time_t last_task;
    list_t handlers;
    task_t *task;
} auto_t;

typedef struct auto_handler auto_handler_t;
struct auto_handler {
    char *name;
    char is_blob;
    void (*handler)(auto_handler_t *self, char *value);
    void (*apply)(char *value);
    // 15+1 is max nvs key and namespace length
    //char namespace[NVS_KEY_NAME_MAX_SIZE];
    //char key[NVS_KEY_NAME_MAX_SIZE];
};

auto_t *auto_init();
void auto_run(auto_t *dummy, int run);
void auto_handler_add(auto_t *self, auto_handler_t *handler);
void generic_key_handler(auto_handler_t *self, char *value);

void controller_ip_handler(auto_handler_t *self, char *value);
void hostname_handler(auto_handler_t *self, char *value);

#endif /* __AUTO_H__ */
