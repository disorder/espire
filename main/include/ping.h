#ifndef __PING_H__
#define __PING_H__

#include "module.h"
#include "ping/ping_sock.h"

typedef struct
{
    module_t module;
    int connected;
    int timeout;
    time_t last;

    esp_ping_config_t ping_config;
    esp_ping_callbacks_t cbs;
    esp_ping_handle_t ping;
} ping_t;

extern ping_t ping_online;

void ping_add(ping_t *ping, char *addr);
void ping_run(ping_t *self, int run);

void ping_online_init(char *addr);


#endif /* __PING_H__ */
