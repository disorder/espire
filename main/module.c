#include <assert.h>
#include <string.h>
#include "ntp.h"
#include "module.h"
#include "util.h"

#include "esp_log.h"
static const char *TAG = "module";

static list_t modules = {};

void module_add(module_t *module)
{
    ESP_LOGE(TAG, "adding %s", module->name);
    list_prepend(&modules, module);
}

int module_match_name(module_t *module, char *name)
{
    assert(module != NULL);
    if (name == NULL || module->name == NULL) {
        // match if both are NULL
        if (name == module->name)
            return 1;
        return 0;
    }

    if (strcmp(module->name, name) == 0)
        return 1;
    return 0;
}

// it's list_t* but keep iter type opaque
inline iter_t module_iter()
{
    return (iter_t) &modules;
}

inline iter_t module_next(iter_t iter, module_t **module)
{
    return iter_next(iter, (void**) module);
}

void module_offline(int timeout)
{
    ESP_LOGI(TAG, "trigger offline timeout=%d", timeout);
    iter_t iter = module_iter();
    module_t *m;
    while ((iter = module_next(iter, &m)) != NULL) {
        if (m->offline) {
            if (!m->ntp || ntp_synced)
                m->run(m, m->offline >= timeout);
        }
    }
}

void module_network(int connected)
{
    ESP_LOGI(TAG, "trigger network connected=0x%x", connected);
    iter_t iter = module_iter();
    module_t *m;
    while ((iter = module_next(iter, &m)) != NULL) {
        if (m->network) {
            if (!m->ntp || ntp_synced)
                m->run(m, connected & m->network);
        }
    }
}

void module_ntp()
{
    ESP_LOGI(TAG, "trigger ntp");
    iter_t iter = module_iter();
    module_t *m;
    while ((iter = module_next(iter, &m)) != NULL) {
        if (m->ntp) {
            m->run(m, 1);
        }
    }
}

