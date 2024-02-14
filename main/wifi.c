#include "config.h"
#include "module.h"
#include "util.h"
int wifi_state = -1;
int wifi_connected = 0;
#include "check.h"

/* based on WiFi station Example (Public Domain or CC0 licensed) */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"

/* The examples use WiFi configuration that you can set via project configuration menu
   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
//#define EXAMPLE_ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
//#define EXAMPLE_ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_MAXIMUM_RETRY  WIFI_MAXIMUM_RETRY

#if CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "wifi";

static int s_retry_num = 0;


static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (EXAMPLE_ESP_MAXIMUM_RETRY < 0 || s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            if (EXAMPLE_ESP_MAXIMUM_RETRY >= 0)
                s_retry_num++;
            ESP_LOGI(TAG, "event: retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGW(TAG,"event: AP disconnected");
        wifi_connected = 0;
        module_network(wifi_connected);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGW(TAG, "event: got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        wifi_connected = 1;
        module_network(NET_WIFI);
    }
}

static wifi_config_t wifi_config = {
    .sta = {
        // use initial values
        .ssid = CONFIG_ESP_WIFI_SSID,
        .password = CONFIG_ESP_WIFI_PASSWORD,
        /* Setting a password implies station will connect to all security modes including WEP/WPA.
         * However these modes are deprecated and not advisable to be used. Incase your Access point
         * doesn't support WPA2, these mode can be enabled by commenting below line */
        .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
        .sae_pwe_h2e = 2,
    },
};

void wifi_init_sta(void)
{
    ESP_LOGI(TAG, "wifi_init_sta");
    s_wifi_event_group = xEventGroupCreate();

    //ESP_ERROR_CHECK(esp_netif_init());

    //ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    strncpy((char *) &wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strncpy((char *) &wifi_config.sta.password, WIFI_PASSWORD, sizeof(wifi_config.sta.password));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    //ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    // start is moved to wifi_start_sta
    return;

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGW(TAG, "connected to ap SSID:%s",
                 wifi_config.sta.ssid);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGW(TAG, "Failed to connect to SSID:%s",
                 wifi_config.sta.ssid);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

void wifi_init()
{
    ESP_LOGW(TAG, "init: %s", WIFI_SSID);

    //Initialize NVS
    /*
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    */

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();

    esp_wifi_set_country_code(WIFI_COUNTRY_CODE, true);

    char country[3] = {0};
    esp_wifi_get_country_code(country);
    ESP_LOGW(TAG, "country code: %s", country);
}

// call this to ensure wifi connection
int wifi_run(int add, int nonblock, TaskHandle_t owner)
{
    // will call wifi_start_sta
    return ADC2_WAIT(ADC2_WIFI, add, 0, nonblock, owner);
}

// do not call directly
void _wifi_start_sta(int nonblock)
{
//    if (wifi_state > 0)
//        return;

    s_retry_num = 0;
    ESP_LOGI(TAG, "start_sta");
    //ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_start();

    // used to restore wifi without waiting
    if (nonblock)
        return;

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            (WIFI_RETRY_WAIT_MS == 0)? portMAX_DELAY : MS_TO_TICK(WIFI_RETRY_WAIT_MS));

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "start_sta: connected to ap SSID:%s",
                 wifi_config.sta.ssid);
        wifi_state = 1;
        //wifi_connected = 1;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "start_sta: Failed to connect to SSID:%s",
                 wifi_config.sta.ssid);
        //wifi_connected = 0;
    } else {
        // TODO why is this happening - multiple starts?
        ESP_LOGE(TAG, "start_sta: UNEXPECTED EVENT");
        // wait will keep blocking other tasks, possibly blocking them forever
        //if (WIFI_RETRY_WAIT_MS > 0)
        //    _vTaskDelay(MS_TO_TICK(WIFI_RETRY_WAIT_MS));
    }
}

// do not call directly
void _wifi_stop_sta(int force)
{
//    if (!force && wifi_state != 1)
//        return;

    ESP_LOGI(TAG, "stop_sta");
    ESP_LOGD(TAG, "stop_sta: disconnect");
    esp_err_t res;
    // set state before change because of logging
    wifi_state = 0;
    wifi_connected = 0;
    module_network(wifi_connected);
    res = esp_wifi_disconnect();
    if (res != ESP_ERR_WIFI_NOT_INIT && res != ESP_ERR_WIFI_NOT_STARTED)
        ESP_ERROR_CHECK(res);
    //esp_wifi_disconnect();
    ESP_LOGD(TAG, "stop_sta: stop");

    res = esp_wifi_stop();
    if (res != ESP_ERR_WIFI_NOT_INIT && res != ESP_ERR_WIFI_NOT_STARTED)
        ESP_ERROR_CHECK(res);

    //ESP_LOGI(TAG, "deinit sta");
    //ESP_ERROR_CHECK(esp_wifi_deinit());
    //esp_wifi_deinit();
}

// TODO seen this failing and working after hw reboot (happened after router reboot but it's not reproducible)
static void wifi_check_task(void *pvParameter)
{
    // basing it on wifi_connected, that's a little brute-forcish
    TickType_t last_connected = 0;
    while (1) {
        TickType_t now = xTaskGetTickCount();
        if (wifi_connected) {
            last_connected = now;
        } else {
            if (now > last_connected+MS_TO_TICK(WIFI_CHECK_MS)) {
                ESP_LOGE(TAG, "reconnecting wifi");
                // try nonblocking connect
                if (wifi_run(0, 0, NULL) == 0)
                    if (wifi_connected)
                        last_connected = now;
            }
        }

        _vTaskDelay(MS_TO_TICK(2*WIFI_RETRY_WAIT_MS));
    }
}

void wifi_check()
{
    xxTaskCreate(wifi_check_task, "wifi_check", 2*1024, NULL, 3, NULL);
}

void wifi_update(char *ssid, char *password, int apply)
{
    if (ssid != NULL) {
        ESP_LOGE(TAG, "setting SSID %s", ssid);
        strncpy(WIFI_SSID, ssid, sizeof(WIFI_SSID));
        strncpy((char *) &wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    }

    if (password != NULL) {
        ESP_LOGE(TAG, "setting password");
        strncpy(WIFI_PASSWORD, password, sizeof(WIFI_PASSWORD));
        strncpy((char *) &wifi_config.sta.password, WIFI_PASSWORD, sizeof(wifi_config.sta.password));
    }

    if (apply) {
        // we are booting up
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        ADC2_WAIT(ADC2_WIFI, 0, MS_TO_TICK(500), 1, NULL);
    }
}
