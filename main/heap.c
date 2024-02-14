#include "config.h"
#include "heap.h"
#include "util.h"
#include "ntp.h"
#include <time.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
static const char *TAG = "heap";

double heap_estimate_s = 0;
uint32_t heap_first_free;
uint32_t heap_first_t_free;
static uint32_t last_free = 0;
//static TickType_t first;
time_t heap_first_t = 0;
//static TickType_t last = 0;
static time_t last = 0;

static int (*reboot_cb)(uint32_t, uint32_t) = NULL;

void heap_task(void *pvParameter)
{
    while (1) {
        //TickType_t now = xTaskGetTickCount();
        time_t now;
        time(&now);
        uint32_t free = esp_get_free_heap_size();

        if (ntp_synced && heap_first_t == 0) {
            time(&heap_first_t);
            heap_first_t_free = free;
        }

        double estimate = (double) (((double)free)/(last_free-free)) * (60.0/HEAP_PERIOD_S);
        //if (now >= last + S_TO_TICK(HEAP_PERIOD_INFO_S)) {
        if (now >= last + HEAP_PERIOD_INFO_S) {
            last = now;
            ESP_LOGI(TAG, "%d/%d free, diff %d to %.1f%% (%.2f days remaining)", free, heap_first_free, free-last_free, 100.0 - ((double)free/(double)heap_first_free), estimate/(24*60));
        }

        if (last_free > free && last_free - free > HEAP_WARN_DECREASE_B)
            ESP_LOGW(TAG, "%d/%d: %d decrease in %ds (%.0fm)", free, heap_first_free, last_free-free, HEAP_PERIOD_S, estimate);

        if (HEAP_REBOOT_B >= 0 && free < HEAP_REBOOT_B) {
            if (reboot_cb == NULL || reboot_cb(free, heap_first_free)) {
                ESP_LOGE(TAG, "low heap reboot (%.1fm)", estimate);
                //esp_restart();
                esp_system_abort("low heap");
            } else if (reboot_cb != NULL) {
                ESP_LOGE(TAG, "low heap reboot cancelled (%.1fm)", estimate);
            }
        }

        int32_t diff = heap_first_t_free - free;
        //time_t now_t;
        //time(&now_t);
        double tdiff_s = difftime(now, heap_first_t);
        //ESP_LOGE(TAG, "free %zu diff %d diff_s %f", free, diff, tdiff_s);
        if (tdiff_s > 0) {
            estimate = ((double)free) / (((double) diff) / tdiff_s);
            heap_estimate_s = estimate;
            ESP_LOGE(TAG, "%.1f days", estimate/(24.0*60*60));
        }

        last_free = free;
        //_vTaskDelay(S_TO_TICK(HEAP_PERIOD_S));
        wall_clock_wait(HEAP_PERIOD_S, S_TO_TICK(5));
    }
}

// use esp_register_shutdown_handler()
// handler() can be used to postpone reboot
void heap_init(int (*handler)(uint32_t free, uint32_t first_free))
{
    reboot_cb = handler;
    heap_first_free = esp_get_free_heap_size();
    //first = xTaskGetTickCount();
    ESP_LOGI(TAG, "heap_init: %d free", heap_first_free);
    xxTaskCreate(heap_task, "heap_task", 3*1024, NULL, 5, NULL);
}
