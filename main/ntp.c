#include "config.h"
#include "device.h"
#include "module.h"
#include "util.h"
#include <time.h>
#include <sntp.h>

#include "esp_log.h"
static const char *TAG = "ntp";

int ntp_synced = 0;
time_t ntp_start = 0;

static void ntp_task(void *pvParameter)
{
    while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED)
        _vTaskDelay(MS_TO_TICK(1000));

    char strftime_buf[64];
    struct tm timeinfo;
    time(&ntp_start);

    localtime_r(&ntp_start, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "The current date/time is: %s", strftime_buf);

    esp.start = ntp_start - (TICK_TO_MS(xTaskGetTickCount()) / 1000);
    ntp_synced = (TICK_TO_MS(xTaskGetTickCount()) + esp.slept_ms) / 1000;
    module_ntp();
    xvTaskDelete(NULL);
}

void ntp_init(void)
{
    // TODO CONFIG_LWIP_SNTP_UPDATE_DELAY
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, NTP_SERVER1);
    sntp_setservername(1, NTP_SERVER2);
    //sntp_servermode_dhcp(1);
    sntp_init();

    // according to man page, tzset should be set before localtime_r
    setenv("TZ", NTP_TZ, 1);
    tzset();

    // wait for first sync
    xxTaskCreate(ntp_task, "ntp_task", 3*2048, NULL, 5, NULL);
    // for any sync use void callback(struct timeval *tv) and read tv->tv_sec
    //void sntp_set_time_sync_notification_cb(sntp_sync_time_cb_t callback)
}
