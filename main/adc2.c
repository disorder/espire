#include "config.h"
#include "adc2.h"
#include "wifi.h"
#include "util.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#ifndef ADC2_MUTEX_BYPASS
#include "freertos/semphr.h"
static const char *TAG = "adc2";
#else
static const char *TAG = "adc2_bypass";
#endif

adc2_mode_t adc2_use = ADC2_NONE;
int wifi_count = 0;

#ifndef ADC2_MUTEX_BYPASS
SemaphoreHandle_t mutex = NULL;
#endif

static inline void _MUTEX_INIT()
{
#ifndef ADC2_MUTEX_BYPASS
    if (mutex == NULL) {
        mutex = xSemaphoreCreateMutex();
        assert(mutex != NULL);
    }
#endif
}

static void _MUTEX_ENTER_CRITICAL()
{
    _MUTEX_INIT();
    ESP_LOGD(TAG, "requesting mutex");
#ifndef ADC2_MUTEX_BYPASS
    xSemaphoreTake(mutex, portMAX_DELAY);
#endif
    ESP_LOGD(TAG, "mutex locked");
}

static void _MUTEX_EXIT_CRITICAL()
{
#ifndef ADC2_MUTEX_BYPASS
    xSemaphoreGive(mutex);
#endif
    ESP_LOGD(TAG, "mutex unlocked");
}

list_t wifi_owner = {0};
inline static void WIFI_OWNER_REMOVE(TaskHandle_t task)
{
    assert(task != NULL);

    //list_t *item = list_find(&wifi_owner, task);
    list_t *item = &wifi_owner;
    while ((item = list_iter(item)) != NULL) {
        if (LIST(wifi_owner_t, item, task) == task) {
            break;
        }
    }

    if (item != NULL) {
        wifi_owner_t *o = item->data;
        list_remove(&wifi_owner, item);
        free(o);

        // double allocation per task is tentatively invalid (ftp/http has own task)
        item = &wifi_owner;
        while ((item = list_iter(item)) != NULL) {
            if (LIST(wifi_owner_t, item, task) == task) {
                break;
            }
        }
        assert(item == NULL);

        return;
    }

    ESP_LOGE(TAG, "wifi owner not found: %x", task);
    assert(item != NULL);
}

// time tracking relies on fact that only one allocation is made per task
// otherwise we would need to issue id in add and require it in remove
inline static void WIFI_OWNER_ADD(TaskHandle_t task)
{
    assert(task != NULL);

    // double allocation per task is tentatively invalid (ftp/http has own task)
    //list_t *item = list_find(&wifi_owner, (void *) task);
    list_t *item = &wifi_owner;
    while ((item = list_iter(item)) != NULL) {
        if (LIST(wifi_owner_t, item, task) == task) {
            break;
        }
    }
    assert(item == NULL);

    wifi_owner_t *o = calloc(1, sizeof(wifi_owner_t));
    assert(o != NULL);
    time(&o->time);
    o->task = task;

    //list_prepend(&wifi_owner, task);
    list_prepend(&wifi_owner, o);
}

inline void WIFI_ADD(TaskHandle_t owner)
{
    _MUTEX_ENTER_CRITICAL();
    wifi_count += 1;
    WIFI_OWNER_ADD(owner);
    _MUTEX_EXIT_CRITICAL();
}

inline void WIFI_DEL(TaskHandle_t owner)
{
    _MUTEX_ENTER_CRITICAL();
    if (adc2_use != ADC2_WIFI)
        wifi_count = 0;
    else {
        if (wifi_count > 0) {
            wifi_count -= 1;
        }
        WIFI_OWNER_REMOVE(owner);
    }
    _MUTEX_EXIT_CRITICAL();
}

inline void ADC2_FREE()
{
    _MUTEX_ENTER_CRITICAL();
    adc2_use = 0;
    ESP_LOGW(TAG, "freed", adc2_use);
    _MUTEX_EXIT_CRITICAL();
}

inline int ADC2_WAIT(adc2_mode_t value, int add, TickType_t force_tick, int nonblock, TaskHandle_t owner)
{
    // do not use add=1, it seems to increment in some cases when it shouldn't
    // use WIFI_ADD instead after this succeeds
    assert(add == 0);
#if JTAG_DEBUG
    if (value == ADC2_WIFI)
        return 1;
#endif
    TickType_t start = xTaskGetTickCount();
    while (1) {
        _MUTEX_ENTER_CRITICAL();
        // already taken
        if (value == adc2_use) {
            switch (adc2_use) {
            case ADC2_WIFI:
                if (add) {
                    //_WIFI_ADD(1);
                    wifi_count += 1;
                    WIFI_OWNER_ADD(owner);
                }
                _MUTEX_EXIT_CRITICAL();
// TODO causes issue in reconnect?
//                 while (!wifi_connected) {
//                     ESP_LOGW(TAG, "waiting for connect", value);
//                     _vTaskDelay(MS_TO_TICK(500));
//                 }
                break;
            default:
                _MUTEX_EXIT_CRITICAL();
                while (wifi_connected) {
                    ESP_LOGI(TAG, "waiting for disconnect", value);
                    _vTaskDelay(MS_TO_TICK(10));
                }
            }
            return 0;
        }

        if (force_tick > 0 && xTaskGetTickCount() >= start+force_tick) {
            switch (adc2_use) {
            case ADC2_WIFI:
                ESP_LOGE(TAG, "forcing disconnect for %d", value);
                // maybe only do this when not connected - that can block us
                _wifi_stop_sta(1);
                wifi_count = 0;
                adc2_use = 0;
                break;
            case ADC2_NONE:
            case ADC2_ADC:
            }
        }

        switch (adc2_use) {
        case 0:
            adc2_use = value;
            break;
        case ADC2_ADC:
            // wait for ADC2_FREE to be called
            break;
        case ADC2_WIFI:
            if (wifi_count == 0) {
                adc2_use = value;
            }
        }
        _MUTEX_EXIT_CRITICAL();

        // successfully taken
        if (adc2_use == value) {
            ESP_LOGW(TAG, "taken for %d", value);
            if (adc2_use == ADC2_WIFI) {
                // this will already block for configured time
                _wifi_start_sta(nonblock);
                if (nonblock)
                    break;
                TickType_t start = xTaskGetTickCount();
                while (!wifi_connected && WIFI_RETRY_CANCEL_MS > 0) {
                    // we can also let it go until somebody requests ADC2
                    if (xTaskGetTickCount() > start + MS_TO_TICK(WIFI_RETRY_CANCEL_MS)) {
                        // need to remove all tasks
                        ESP_LOGW(TAG, "wifi connection cancelled");
                        _MUTEX_ENTER_CRITICAL();
                        // there's still a race for ADC2 that can postpone others
                        // so any critical task need to use force_tick
                        adc2_use = ADC2_NONE;
                        wifi_count = 0;
                        _MUTEX_EXIT_CRITICAL();
                        // we know we tried to connect and fail so force
                        // if we don't do this pins won't work properly
                        _wifi_stop_sta(1);
                        // failure
                        return 1;
                    }
                    _vTaskDelay(MS_TO_TICK(500));
                }
                //while (add && !wifi_connected) {
                //    ESP_LOGW(TAG, "waiting for connect", value);
                //    _vTaskDelay(MS_TO_TICK(500));
                //}
            } else {
                _wifi_stop_sta(0);
                while (wifi_connected) {
                    ESP_LOGI(TAG, "waiting for disconnect", value);
                    _vTaskDelay(MS_TO_TICK(10));
                }
            }
            break;
        }
        ESP_LOGW(TAG, "wanted by %d, taken by %d, waiting %d", value, adc2_use, wifi_count);
        _vTaskDelay(MS_TO_TICK(1000));
    }
    // success
    // doing this in loop can probably double increment when we exit critical
    // TODO this did not seem to help but doing WIFI_ADD outside seem to do
    _MUTEX_ENTER_CRITICAL();
    if (adc2_use == ADC2_WIFI && add) {
        wifi_count += 1;
        WIFI_OWNER_ADD(owner);
    }
    _MUTEX_EXIT_CRITICAL();
    return 0;
}
