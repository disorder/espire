#include "dummy.h"
#include "module.h"
#include "esp_log.h"
static const char *TAG = "dummy";

dummy_t *dummy_new()
{
    dummy_t *self = calloc(1, sizeof(dummy_t));
    assert(self != NULL);
    // initialize

    self->module.name = TAG;
    self->module.type = M_DUMMY;
    self->module.run = (void (*)(void*,int)) dummy_run;
    // trigger start at online/ntp and stop at offilne
    // self->module.network = 1;
    // trigger start at internet/ntp and stop at offilne
    // self->module.online = 1;
    // self->module.ntp = 1;

    module_add(&self->module);
    return self;
}

static void dummy_task(dummy_t *self)
{
    while (!self->module.stop) {
        ESP_LOGI(TAG, "dummy task");
        _vTaskDelay(S_TO_TICK(1));
    }

    task_t *task = self->task;
    self->module.state = 0;
    self->task = NULL;
    self->module.stop = 0;
    xvTaskDelete(task);
}

void dummy_run(dummy_t *self, int run)
{
    assert(self != NULL);
    if (run) {
        if (self->module.state == 0) {
            // start
            self->module.state = 1;
            xxTaskCreate((void (*)(void*))dummy_task, "dummy_task", 2*1024, self, 4, &self->task);
        }
    } else {
        if (self->module.state != 0) {
            // stop
            //self->module.state = 0;
            // or graceful exit when memory leaks are possible
            // tasks need to end and xvTaskDelete()
            self->module.stop = 1;
        }
    }
}
