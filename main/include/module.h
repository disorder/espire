#ifndef __MODULE_H__
#define __MODULE_H__

#include "util.h"

typedef enum {
    M_DUMMY = 0,
    M_PING,
    M_AUTO,
    M_HTTPD,
    M_METAR,
    M_TEMP,
    M_SLEEP,
    M_OTHER,
} module_type_t;

typedef struct {
    module_type_t type;
    const char *name;
    // current state and request to stop
    int state;
    int stop;
    // start/stop when connected
    int network;
    // start and stop when online and possibly after NTP sync
    // number of ping timeouts to be considered offline
    int offline;
    int ntp;
    //void *(*init)(void *self);
    void (*run)(void *self, int run);
} module_t;

#define NET_WIFI    (1 << 0)
#define NET_ETH     (1 << 1)
#define NET_ETH_ANY (NET_WIFI|NET_ETH)
#define NET_ESPNOW  (1 << 2)
#define NET_ANY     (NET_ETH_ANY|NET_ESPNOW)

void module_add(module_t *module);
iter_t module_iter();
iter_t module_next(void *iter, module_t **module);
int module_match_name(module_t *module, char *name);

void module_offline(int timeout);
void module_network(int connected);
void module_ntp();

#endif /* __API_H__ */
