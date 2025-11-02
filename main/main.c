#include "cc.h"
#define INCLUDE_THERMISTORS
#include "config.h"
#include "device.h"
#include "button.h"
#include "oled.h"
#include "heating.h"
#include "api.h"
#include "wifi.h"
#include "shmu.h"
#include "log.h"

#include <math.h>

#include "esp_sleep.h"
#include "esp_netif.h"
#include "esp_log.h"
static const char *TAG = "main";

system_t esp = {0};

// handling pertaining to buttons and UI are very much hardcoded,

// on client device we can use some of the supported wakeup pins 32-39
// but 34-36 and 39 don't have internal pullup/pulldown
button_t buttons[] = {
    {
        .data = (void *)(button_enum_t)MINUS,
        .gpio = 32,//5,
        .handler = button_handler,
    },
    {
        .data = (void *)(button_enum_t)PLUS,
        .gpio = 33,//17,
        .handler = button_handler,
    },
    {
        .data = (void *)(button_enum_t)TOGGLE,
        .gpio = 27,//16,
        .handler = button_handler,
    },
};

void button_handler(button_t *b)
{
    time(&esp.activity);

    //ESP_LOGI(TAG, "Button: GPIO %d", b->gpio);
    heating_t *data = NULL;
    // TODO && !esp.dev->controller
    if (esp.dev)
        data = heating_find(esp.dev->hostname, 0);

    // TODO now when remote changes set value this will not work correctly?
    switch ((button_enum_t) b->data) {
    case MINUS:
        if (b->state == BUTTON_STATE_ON || b->reps) {
            if (oled_update.mode == HEATING) {
                if (isnanf(oled_update.temp_mod))
                    oled_update.temp_mod = HEATING_TEMP_DEFAULT;
                else
                    oled_update.temp_mod -= 0.1;
                ESP_LOGI(TAG, "Button: minus %f", oled_update.temp_mod);
                oled_update.temp = 1;
            } else if (oled_update.mode == OWM) {
                // this eliminates repeats but needs to be long enough
                if (b->state == BUTTON_STATE_OFF && b->changed && !b->longs)
                    shmu(-1);
            }
        }
        break;
    case PLUS:
        if (b->state == BUTTON_STATE_ON || b->reps) {
            if (oled_update.mode == HEATING) {
                if (isnanf(oled_update.temp_mod))
                    oled_update.temp_mod = HEATING_TEMP_DEFAULT;
                else
                    oled_update.temp_mod += 0.1;
                ESP_LOGI(TAG, "Button: plus %f", oled_update.temp_mod);
                oled_update.temp = 1;
            } else if (oled_update.mode == OWM) {
                // this eliminates repeats but needs to be long enough
                if (b->state == BUTTON_STATE_OFF && b->changed && !b->longs)
                    shmu(+1);
            }
        }
        break;
    case TOGGLE:
        // one button can have multiple functions:
        // 1: short press with temp change
        // 2: short press without temp change
        // 3: long press with temp change
        // 3: long press without temp change
        // 4: other long presses
        // some restrictions apply based on destructiveness of the function,
        // also can save last change and revert based on state=OFF change
        // and rep count
        // alternatively there can be function selection:
        // short press change, long press activate and it depends
        // on being able to see selected action on display
        // (more long presses can be added for special functions/reboot)

        // one possible variant:
        // single short press without change - change display mode
        // single short press with change - reset
        // long press with change - save change
        // long press without change - other function
        // 2s display toggle 5s reboot
        //if (b->state == BUTTON_STATE_ON && b->changed && !b->reps) {
        // was seeing spurious reps=1 on button release
        // need to test for low reps to prevent toggle after temp_set?
        if (b->state == BUTTON_STATE_OFF && b->changed && !b->longs && b->reps <= 1) {
            // first short press without repeat
            ESP_LOGI(TAG, "Button: toggle");
            if (oled_update.mode == HEATING && data != NULL) {
                //if (!isnanf(oled_update.temp_set) && fabs(oled_update.temp_set - oled_update.temp_mod) > 0.05) {
                if (!isnanf(oled_update.temp_mod) &&
                    !isnanf(data->set) && fabs(data->set - oled_update.temp_mod) > 0.05) {
                    // restore
                    //oled_update.temp_mod = oled_update.temp_set;
                    oled_update.temp_mod = data->set;
                    oled_update.temp = 1;
                } else
                    goto CYCLE;
            } else {
            CYCLE:
                if (oled_update.mode_restore == -1) {
                    // clear screan after possible image
                    if (oled_update.mode == OWM)
                        oled_invalidate(&oled);
                    // cycle modes
                    oled_update.mode += 1;
                    ESP_LOGW(TAG, "cycled %d", oled_update.mode);
                } else {
                    // this is too involved with internal logic
                    // but we need special case when we are not sleeping
                    // and we need to ignore toggle and restore saved mode
                    // (faking mode-1 can cause refresh of incorrect mode)
                    oled_update.mode = oled_update.mode_restore;
                    oled_update.mode_restore = -1;
                }
                oled_update.invalidate = 1;
            }
        }
        if (b->longpress) {
            switch (b->longs) {
            case 1:
                if (oled_update.mode == HEATING && data != NULL) {
                    //if (isnanf(oled_update.temp_set) || fabs(oled_update.temp_set - oled_update.temp_mod) > 0.05) {
                    if (isnanf(data->set) || fabs(data->set - oled_update.temp_mod) > 0.05) {
                        oled_update.temp_set = oled_update.temp_mod;
                        // set on controller - can't do it here though
                        // thermostat_update will keep updating and refresh ui
                        oled_update.temp_pending = 1;
                        ESP_LOGI(TAG, "value set: %f", oled_update.temp_set);
                        // oled_update.temp = 1;
                        //heating_temp_set(name, temp_set);
                        // temp=1 or invalidate=1 to force refresh ASAP
                        oled_update.invalidate = 1;
                    } else {
                        // long press without change
                        // TODO only works if data->set is not NAN?
                        // TODO not propagating?
                        // invert - effectively toggles heating
                        oled_update.temp_mod *= -1;
                        oled_update.temp_set = oled_update.temp_mod;
                        oled_update.temp_pending = 1;
                        ESP_LOGI(TAG, "value set: %f", oled_update.temp_set);
                        oled_update.invalidate = 1;
                    }
                }
                break;
            case 2:
                // probably not safe when overused
                //oled_power(&oled, !oled.power);
                // delegate to another task
                oled_update.power_toggle = 1;
                break;
            }

            if (b->longs >= 5) {
                ESP_LOGE(TAG, "button requested reboot");
                api_reboot(NULL);
            }
        }
        break;
    default:
        ESP_LOGE(TAG, "unknown button %d", b->data);
    }
}

th_def_t controller_defs[] = {
    {
        .cnt = 10,
        .th = &th_4k7,
        .label = "th_4k7",
    },
    // TODO 2 more - test
    {
        .cnt = 0,//2,
        .th = &th_1k,
        .label = "th_1k",
    },
};

th_def_t client_defs[] = {
    // TODO only testing
    {
        .cnt = 1,
        .th = &th_4k7,
        .label = "th_4k7",
    },
};

th_def_t co2_defs[] = {
    {
        .cnt = 1,
        .th = &th_1k,
        .label = "th_1k",
    },
};

device_t devices[] = {
    {
        .controller = 0,
        .mac = "\x9C\x9C\x1F\xCA\x05\x00",
        // with openwrt you can configure IP by MAC or just dig -r hostname @gw
        // static ip configuration is not needed
        .hostname = "esp32dev",
        .metar = 0,
        .relays = 0,
        .th_def_cnt = COUNT_OF(client_defs),
        .th_defs = (th_def_t *)&client_defs,
    },

    {
        .controller = 1,
        // only way to initialize controller
        .mac = "\x24\xD7\xEB\x0E\xCD\xFC",
        .hostname = "ctrl",
        .metar = 0,
        .relays = 10,//RELAY_CNT,
        .th_def_cnt = COUNT_OF(controller_defs),
        .th_defs = (th_def_t *)&controller_defs,
    },

    {
        .controller = 0,
        .mac = "\x0C\xB8\x15\xEC\x95\x90",
        .hostname = "co2",
        .metar = 1,
        .th_def_cnt = COUNT_OF(co2_defs),
        .th_defs = (th_def_t *) &co2_defs,
    },

    // default configuration goes last - this is for generic client devices
    // which are configurable over HTTP API/auto and values stored in flash
    {
        .controller = 0,
        .mac = (char *) &mac,
        .hostname = HOSTNAME_DEFAULT,
        .metar = 1,
        .th_def_cnt = COUNT_OF(client_defs),
        .th_defs = (th_def_t *) &client_defs,
    },
};

void wakeup_cb(int external_wake)
{
    // TODO
    ESP_LOGW(TAG, "heap %zu / %zu", esp_get_free_heap_size(), heap_first_t_free);
}

// TODO
#include "nv.h"
#include "ping.h"
#include "esp_event.h"
void app_main(void)
{
    system_init();
    // start logging now
    log_init();

    // TODO this seems to sleep
    /*
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    nv_init();
    httpd_t *httpd = httpd_new(0);
    httpd_run(httpd, 1);
    api_init(httpd);
    wifi_init();
    wifi_run(0, 0, NULL);

    ping_online_init(PING_ONLINE_IP);
    sleep_init(wakeup_cb, 0);
    while(1) {
        esp.sleep->wake_cb(0);
        //esp_pm_dump_locks(stdout);
        _vTaskDelay(S_TO_TICK(1));
    }
    */

    // netif available after some time, original hostname is "espressif"
    // initialize mac for "dummy" device match
    get_mac();
    device_t *dev = NULL;
    for (int i=0; i<COUNT_OF(devices); i++) {
        if (devices[i].mac == NULL || memcmp(devices[i].mac, mac, 6) != 0) {
            continue;
        }

        dev = &devices[i];

        // buttons are for client devices
        if (!dev->controller) {
            esp_sleep_enable_gpio_wakeup();
            for (int i=0; i<COUNT_OF(buttons); i++) {
                button_add(&buttons[i]);
            }
            button_run();
        }

        device_init(dev);
        break;
    }

    // this is optional but saves power and heats less
    sleep_init(&wakeup_cb, CONTROLLER_SLEEP);
    sleep_run(NULL, 1);
}
