#include "adc2.h"
#include "esp_system.h"

#include <time.h>
#include <math.h>
#include "config.h"
#include "device.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "driver/gpio.h"
#include "thermistor.h"
#include "button.h"
#include "heating.h"
#include "temp.h"
#include "relay.h"
#include "check.h"
#include "wifi.h"
#include "co2.h"
#include "metar.h"
#include "owm.h"
#include "httpd.h"
#include "ntp.h"
#include "util.h"
#include "heap.h"
#include "auto.h"
#include "api.h"
#include "nv.h"
#include "ping.h"
#include "oled.h"
#include "ota.h"
#include "dummy.h"
#include "log.h"

#include "nvs_flash.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_err.h"
#include "esp_log.h"
static const char *TAG = "dev";

oled_update_t oled_update = {
    .temp_set = NAN,
    .temp_mod = NAN,
    .external = NAN,
};

#include "esp_task_wdt.h"
static void oled_update_task(void *pvParameter)
{
    // Subscribe this task to TWDT, then check if it is subscribed
    //ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    //ESP_ERROR_CHECK(esp_task_wdt_status(NULL));
    // Subscribe as user of the the TWDT
    //ESP_LOGI(TAG, "subscribing task to TWDT");

    int power_change = 0;
    oled_update.power_state = -1;
    while (1) {
        //esp_task_wdt_reset();

        oled_update.mode %= MODE_MAX;

        if (oled_update.power_toggle) {
            oled_power(&oled, !oled.power);
            oled_update.power_toggle = 0;
            power_change = 1;
        }
        if (oled_update.power_state != -1) {
            // this is meant as a replacement for oled_power, always execute
            oled_power(&oled, oled_update.power_state);
            oled_update.power_state = -1;
            power_change = 1;
        }

        if (oled_update.temp || oled_update.invalidate) {
            oled_update.temp = 0;
            if (esp.dev) {
                // display local temperature - zone name = hostname
                heating_t *data = heating_find(esp.dev->hostname, 0);
                if (data != NULL)
                    oled_temp(0, data->val, data->set, oled_update.temp_mod);
            }
        }

        // fallback to locally sourced external temperature if metar is old
        time_t now_t;
        time(&now_t);
        // this will never replace stale value with NAN
        if (!isnanf(oled_update.external) &&
            (oled_update.metar == NULL && oled_update.metar_last + (30*60) <= now_t)) {
            // initialize from external zone if available, then metar
            //heating_t *zone = heating_find("external", 0);
            //if (zone)
            //    oled_external(0, zone->vals[HEATING_LAST_VAL_I(zone)]);
            oled_external(0, oled_update.external);
            oled_update.external = NAN;
        } else if (oled_update.invalidate) {
        }

        if (oled_update.metar != NULL) {
            metar_t *self = oled_update.metar;
            char *nl = strchrnul(self->buf, '\n');
            if (nl[0] == '\n') // remove time and date if present
                oled_bottom_scroll0(0, nl+1);
            else
                oled_bottom_scroll0(0, self->buf);
            oled_bottom_scroll1(0, self->decoded);
            oled_metar(0, self, 2);
            oled_update.metar = NULL;
            time(&oled_update.metar_last);
        } else if (oled_update.invalidate) {
            oled_bottom_scroll0(0, NULL);
            oled_bottom_scroll1(0, NULL);
            oled_metar(0, NULL, 2);
        }

        if (oled_update.owm != NULL) {
            // this can cause residual SHMU images, not a big issue and
            // not necessary to invalidate whole screen
            oled_owm(0, oled_update.owm);
            oled_update.owm = NULL;
        }

        if (oled_update.message != NULL) {
            oled_message(0, oled_update.message);
            oled_update.message = NULL;
        }

        if (oled_update.invalidate) {
            oled_clock(0, NULL, NULL);
            oled_network(0, wifi_connected, ping_online.connected);
            oled_top_right(0, NULL);
            oled_top_left(0, NULL);
            oled_owm(0, NULL);
            oled_message(0, NULL);

            oled_update.invalidate = 0;
            //oled_invalidate(&oled);
            if (power_change)
                power_change = 0;
            //else
            // this condition makes stored state authoritative,
            // i'm cautious about it but worst case scenario user can
            // cycle through OFF state or 2s press toggle button
            else if (oled.power != (oled_update.mode != OFF))
                oled_power(&oled, oled_update.mode != OFF);
        }

        if (oled_update.mode != HEATING) {
            if (oled.lvgl_tick_timer_ms != LVGL_TICK_PERIOD_MS_SLOW) {
                ESP_ERROR_CHECK(esp_timer_stop(oled.lvgl_tick_timer));
                ESP_ERROR_CHECK(esp_timer_start_periodic(oled.lvgl_tick_timer, LVGL_TICK_PERIOD_MS_SLOW * 1000));
                oled.lvgl_tick_timer_ms = LVGL_TICK_PERIOD_MS_SLOW;
            }
        } else {
            if (oled.lvgl_tick_timer_ms != LVGL_TICK_PERIOD_MS) {
                ESP_ERROR_CHECK(esp_timer_stop(oled.lvgl_tick_timer));
                ESP_ERROR_CHECK(esp_timer_start_periodic(oled.lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000));
                oled.lvgl_tick_timer_ms = LVGL_TICK_PERIOD_MS;
            }
        }

        oled_co2(0, co2_ppm);

        _vTaskDelay(MS_TO_TICK(100));
    }

    //ESP_ERROR_CHECK(esp_task_wdt_delete(NULL));
    //vTaskDelete(NULL);
}

static int lowmem_reboot(uint32_t free, uint32_t first_free)
{
    ESP_LOGE(TAG, "heap_reboot(%" PRIu32 ", %" PRIu32 ")", free, first_free);
    nvs_flash_deinit();
    // proceed
    return 1;
}

// keep this separate so in case of issues at least time is updating
static void oled_time_task(TimerHandle_t pxTimer)
{
    time_t now;
    struct timeval tv_now;
    char datestr[4+1+2+1+2 +1] = "";
    char timestr[2+1+2+1+2 +1] = "";
    //TickType_t last = 0;
    struct timeval last = {0};
    int reset = 0;
    int locked = 0;
    do {
        extern SemaphoreHandle_t lvgl_mutex;
        assert(lvgl_mutex != NULL);
        locked = (xSemaphoreTake(lvgl_mutex, MS_TO_TICK(100)) == pdTRUE);
        // last can possibly be exactly 0 again after overflow
        // TODO this is plain wrong
        //if (last > 0 && !locked) {
        if (!locked) {
            gettimeofday(&tv_now, NULL);
            double diff_ms = 1000*(tv_now.tv_sec - last.tv_sec) + (tv_now.tv_usec - last.tv_usec)/1000;
            //if (!reset && TICK_TO_MS(xTaskGetTickCount() - last) > 1000) {
            //if (last > 0 && !reset && TICK_TO_MS(xTaskGetTickCount() - last) > 1000) {
            //if (last > 0 && !reset && TICK_TO_MS(xTaskGetTickCount() - last) > 1000) {
            if ((last.tv_sec > 0 || last.tv_usec > 0) && !reset && diff_ms > 1) {
                ESP_LOGE(TAG, "lvgl is blocked, attempting i2c reinit");
                oled_reinit(&oled);
                reset = 1;
            }
            _vTaskDelay(MS_TO_TICK(100));
            continue;
        }
        reset = 0;

        time(&now);
        format_time(now, datestr, sizeof(datestr), "%Y-%m-%d");
        oled_top_left(1, datestr);

        // update both so switching mode is immediate
        format_time(now, timestr, sizeof(timestr), "%H:%M");
        oled_clock(1, datestr, timestr);
        format_time(now, timestr, sizeof(timestr), "%H:%M:%S");
        oled_top_right(1, timestr);

        oled_network(1, wifi_connected, ping_online.connected);

        xSemaphoreGive(lvgl_mutex);
        locked = 0;
        if (pxTimer == NULL) {
            //vTaskDelayUntil(&last, S_TO_TICK(1));
            time_t now;
            time(&now);
            last.tv_sec = now;
            last.tv_usec = 0;
            // still wait 1s, this is to make sure it's aligned to 1s
            wall_clock_wait_until((struct timeval) {.tv_sec = now+1, .tv_usec=0}, S_TO_TICK(1));
        }
    } while (pxTimer == NULL);
}

#include "esp_task_wdt.h"
#define TWDT_TIMEOUT_MS 1000
void twdt_init()
{
    esp_task_wdt_config_t twdt_config = {
        .timeout_ms = TWDT_TIMEOUT_MS,
        .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,    // Bitmask of all cores
        // seeing watchdogs in this task so panic instead
        .trigger_panic = true,
    };
    ESP_ERROR_CHECK(esp_task_wdt_init(&twdt_config));
    ESP_LOGI(TAG , "TWDT initialized");
}

#if 0
#include "http.h"
static void http_stream_test(http_request_t *req)
{
    log_local("%d ", req->bufsize);
    /*
    char *copy = malloc(req->bufsize+1);
    assert(copy != NULL);
    memcpy(copy, req->buf, req->bufsize);
    copy[req->bufsize] = '\0';
    log_local("%s", copy);
    free(copy);
    */
}
#endif

void system_init()
{
    esp.sockets = xSemaphoreCreateCounting(CONFIG_LWIP_MAX_SOCKETS,
                                           CONFIG_LWIP_MAX_SOCKETS);
    ESP_ERROR_CHECK(esp_netif_init());
}

void device_init(device_t *dev)
{
    assert(esp.sockets != NULL);
    esp.dev = dev;
    ESP_LOGW(TAG, "is controller: %d", esp.dev->controller);
    log_init();

    // already initialized, only way is to set CONFIG_ESP_TASK_WDT_PANIC
    //twdt_init();

    dummy_new();

    // controller needs pins for relay
    if (!esp.dev->controller) {
#ifdef OLED_SSD1306
        oled_ssd1306_init(&oled);
#endif
        if (oled.scr == NULL) {
#ifdef LCD_ST7735S
            tft_st7735s_init(&oled);
#elif defined(LCD_ST7789_1) || defined(LCD_ST7789_2)
            tft_st7789_init(&oled);
#endif
        } else if (esp.dev->co2) {
            // SPI currently conflicts with CO2 UART GPIO
            // this feature is currently just for testing CO2 sensor anyway
            co2_init();
        }
    }

    if (oled.scr != NULL) {
        theme_init(&oled);
        TimerHandle_t oled_time_timer = NULL;
        // don't trust calling lvgl from timer
        //oled_time_timer = xTimerCreate("oled_time", S_TO_TICK(1), pdTRUE,
        //                               (void*) 1, oled_time_task);
        if (oled_time_timer == NULL ||
            xTimerStart(oled_time_timer, S_TO_TICK(1)) != pdPASS) {
            xxTaskCreate((void (*)(void*)) oled_time_task, "oled_time", 2*1024, NULL, 3, NULL);
        }
        // there were some issues with priority 2, seems ok now
        xxTaskCreate((void (*)(void*)) oled_update_task, "oled_update", 3*1024, NULL, 2, NULL);
        oled_update.temp = 1;
    }
    // late power on to prevent random buffer content
    oled_update.power_state = 1;

    // these can be set from autoconfig so can keep verbose at boot?
    esp_log_level_set("*", ESP_LOG_INFO);
//    esp_log_level_set("*", ESP_LOG_WARN);
    esp_log_level_set("adc2", ESP_LOG_INFO);
    esp_log_level_set("adc2_bypass", ESP_LOG_INFO);
    //esp_log_level_set("auto", ESP_LOG_INFO);
    //esp_log_level_set("nv", ESP_LOG_INFO);
    esp_log_level_set("check", ESP_LOG_INFO);
    esp_log_level_set("heap", ESP_LOG_INFO);
    esp_log_level_set("relay", ESP_LOG_INFO);
    esp_log_level_set("temp", ESP_LOG_INFO);
    esp_log_level_set("button", ESP_LOG_WARN);
    esp_log_level_set("metar", ESP_LOG_ERROR);
    esp_log_level_set("owm", ESP_LOG_INFO);
    esp_log_level_set("wifi", ESP_LOG_WARN);
    esp_log_level_set("httpd", ESP_LOG_INFO);
    esp_log_level_set("http", ESP_LOG_WARN);
    esp_log_level_set("ftplib", ESP_LOG_WARN);
    esp_log_level_set("ftp", ESP_LOG_WARN);
    esp_log_level_set("ping", ESP_LOG_WARN);
    esp_log_level_set("co2", ESP_LOG_INFO);
    esp_log_level_set("module", ESP_LOG_WARN);
    esp_log_level_set("esp-x509-crt-bundle", ESP_LOG_WARN);

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    nv_init();

    // start early so HTTPS has enough memory to start
    ESP_LOGE(TAG, "free heap %" PRIu32, esp_get_free_heap_size());
    httpd_t *httpd = httpd_new(0);
    httpd_run(httpd, 1);

    config_init();
    ping_online_init(PING_ONLINE_IP);

    nv_dump(NVS_LABEL, NULL, 0);
    ESP_LOGW(TAG, "dumping namespace");
    nv_dump(NVS_LABEL, NVS_NAMESPACE, 1);

    // 2 to allow ADC2 and planning to use wifi
    check_init(2);

    // possible unique identifier
    char smac[13+5];
    read_mac((char *)&smac, 1);
    ESP_LOGI(TAG, "MAC: %s", (char *) &smac);
    //read_mac(NULL);

    // this is useless now - relay has 5V pins + initialized by heating
    // before temp because it can use 2 ADC1 pins
// TODO tft ... what whas this, no relay if TFT?
#ifdef RELAY_3V3
    relay_init(dev->relays, 0);
#else
    relay_init(dev->relays, 1);
#endif
    // initialized by heating
    /*
    for (int i=0; i<dev->relays; i++) {
#ifdef RELAY_3V3
        relay_set_pin(i, !HEATING_ON);
#else
        relay_set_pin_5v(i, !HEATING_ON);
#endif
    */

    // this needs to go last because it can allocate remaining ADC2
    // and after relay so that ADC1 outputs won't get used
    //temp_init(6+4+2, th_4k7);
    for (int i=0; i<dev->th_def_cnt; i++)
        temp_init_(dev->th_defs[i].cnt, dev->th_defs[i].th, dev->th_defs[i].label);
    //temp_init_(6+4+2, &th_4k7);
    oled_update.temp = 1;
    temp_zone_load();
    // TODO configurable period in runtime/autoconfig
    //      run when wifi_count is 0?
    temp_run(NULL, 1);

    // print GPIO usage
    check_report(NULL, NULL);

    api_init(httpd);
    // seen hang on oled_power so make extra httpd
    //httpd = httpd_new(81);
    //httpd_run(httpd, 1);
    //api_init(httpd);
    wifi_init();
    hostname_handler(NULL, dev->hostname);
    hostname_set(dev->hostname);
    wifi_run(0, 0, NULL);
    wifi_check();
    ntp_init();
    auto_init();
    auto_run(NULL, 1);

    int8_t run = 1;
    if (sizeof(METAR_LOCATION) != 0) {
        if (!esp.dev->controller) {
            metar_t *metar = metar_new(METAR_LOCATION, SHMU_STATION);
            run = 1;
            nv_read_i8("module.metar", &run);
            metar_run(metar, run);
        }
    }

    if (!esp.dev->controller) {
        run = 1;
        nv_read_i8("module.owm", &run);
        // currently not module so only on boot
        if (run == 1)
            owm_init();
    }

    // monitor memory leaks
    heap_init(lowmem_reboot);

    ota_main();
    thermostat_init();
}

void sleep_task(sleep_t *self)
{
    // won't do anything without automatic light sleep
    esp_sleep_enable_wifi_wakeup();

    // set configurable values as state to restore
    int external_wake = 1;
    int power = oled_update.power_default;
    display_mode_t mode = oled_update.mode_default;
    while (!self->module.stop) {
        // auto-sleep seems feasible only for client devices which could sleep
        // all day until appropriate button wakes it up or at least night
        // ticks stop at sleep so task periods will be affected in long sleep,
        // sleeping < 60s should be ok but we should let tasks do their job
        // (chip seems to run much cooler now)
        struct timeval now;
        gettimeofday(&now, NULL);
        // prevent sleep when activity is registered in last 60s
        // or downloads are in progress
        // if this is controller and not NTP synced yet, prevent sleep to make
        // sure we do not miss update window of client devices
        // TODO
        ESP_LOGI(TAG, "ntp %lld activity %lld wifi_count %d", ntp_synced, now.tv_sec - esp.activity, wifi_count);
        if ((!esp.dev->controller || ntp_synced) && now.tv_sec - esp.activity >= 60 && wifi_count > 0) {
            list_t *item = &wifi_owner;
            time_t now;
            time(&now);
            // both iterations are risky, can be freed during iteration
            while ((item = list_iter(item)) != NULL) {
                extern list_t tasks;
                list_t *iter = &tasks;
                int found = 0;
                while ((iter = list_iter(iter)) != NULL) {
                    if (LIST(task_t, iter, task) == LIST(wifi_owner_t, item, task)) {
                        found = 1;
                        break;
                    }
                }
                if (!found)
                    ESP_LOGW(TAG, "wifi owner %x (%llds) does not exist", (size_t) LIST(wifi_owner_t, item, task), now - LIST(wifi_owner_t, item, time));
                else
                    ESP_LOGW(TAG, "wifi owner %x (%llds)", (size_t) LIST(wifi_owner_t, item, task), now - LIST(wifi_owner_t, item, time));
            }
        }
        //if ((!esp.dev->controller || ntp_synced) && now.tv_sec - esp.activity >= 60 && wifi_count == 0) {
        // TODO hard bypass for active CO2 data collection - to calibrate
        // FIXME TODO bypassed sleep seems more stable - either this branch or sleep/wake itself causes it
        // TODO testing again
        //esp.activity = now.tv_sec;
        // co2 doesn't sleep, controller only after NTP sync
        if (co2_ppm == -1 &&
            (!esp.dev->controller || ntp_synced) && now.tv_sec - esp.activity >= 60 && wifi_count == 0) {
            // inhibits button handler - doesn't seem to work as expected
            esp.sleeping = 1;

            if (external_wake) {
                mode = oled_update.mode;
                power = oled.power;
            }

            struct tm tm = {0};
            localtime_r(&now.tv_sec, &tm);
            int sleep_ms = (60 - (now.tv_sec % 60)) * 1000 - (now.tv_usec / 1000);
            TickType_t tick_now = xTaskGetTickCount();
            if (oled_update.mode != CLOCK) {
                oled_update.mode_restore = mode;
                oled_update.mode = CLOCK;
                oled_update.invalidate = 1;
                if (dt_synced && tm.tm_hour < 6 && oled.power)
                    oled_update.power_state = 0;
                // time not cleared for some reason and date garbled
                oled_top_right(0, NULL);
                oled_top_left(0, NULL);
                // seeing some uncleared parts with 300
                _vTaskDelay(MS_TO_TICK(400));
            } else if (dt_synced && tm.tm_hour < 6 && oled.power)
                oled_update.power_state = 0;

            external_wake = 0;
            // bypass
            if (esp.pm) {
                ESP_LOGI(TAG, "sleeping ~%ds", sleep_ms / 1000);
                // wait for network log and it seems to help a bit with uart log
                //_vTaskDelay(MS_TO_TICK(100));
                // seen some weirdness, not sure if this is needed after changes
                //xSemaphoreTake(oled.mutex_i2c, portMAX_DELAY);

                // avoid interrupting downloads... this can take too long
                //ADC2_WAIT(ADC2_NONE, 0, 0, 0);
                // subtract time that passed since calculation
                sleep_ms -= TICK_TO_MS(xTaskGetTickCount() - tick_now);
                esp_sleep_enable_timer_wakeup(sleep_ms * 1000);
                //xSemaphoreGive(oled.mutex_i2c);

                _wifi_stop_sta(0);
                struct timeval sleep_tv;
                gettimeofday(&sleep_tv, NULL);
                esp_light_sleep_start();
                _wifi_start_sta(1);

                esp_sleep_wakeup_cause_t r;
                r = esp_sleep_get_wakeup_cause();

                switch(r) {
                case ESP_SLEEP_WAKEUP_EXT0 : ESP_LOGI(TAG, "wake RTC_IO"); external_wake = 1; break;
                case ESP_SLEEP_WAKEUP_EXT1 : ESP_LOGI(TAG, "wake RTC_CNTL"); break;
                case ESP_SLEEP_WAKEUP_TIMER : ESP_LOGI(TAG, "wake timer"); break;
                case ESP_SLEEP_WAKEUP_TOUCHPAD : ESP_LOGI(TAG, "wake touchpad"); break;
                case ESP_SLEEP_WAKEUP_ULP : ESP_LOGI(TAG, "wake ULP program"); break;
                default : ESP_LOGI(TAG, "wake %d\n", r); break;
                }

                gettimeofday(&now, NULL);
                esp.slept_ms += (now.tv_sec - sleep_tv.tv_sec)*1000 + (now.tv_usec - sleep_tv.tv_usec)/1000;

                // TODO
                ESP_LOGE(TAG, "oled %d saved %d mode %d", oled.power, power, mode);
            }

            // restore last mode
            if (external_wake) {
                //_vTaskDelay(MS_TO_TICK(100));
                oled_update.power_state = power;
                // let button handler do this otherwise it will advance mode
                // (it's still processed even after inhibition ends)
                //oled_update.mode_restore = -1;
                //oled_update.mode = mode;
                oled_update.invalidate = 1;
            } else if (dt_synced && tm.tm_hour >= 6 && power && oled.power != 1 && oled_update.power_state != 1) {
                if (oled_update.power_default)
                    oled_update.power_state = 1;
            }

            esp.sleeping = 0;

            // TODO
            time_t now_t;
            time(&now_t);
            char buf[4+1+2+1+2 +1+ 2+1+2+1+2 +1];
            ESP_LOGI(TAG, "%s", (format_time(now_t, buf, sizeof(buf), "%Y-%m-%d %H:%M:%S"), buf));
        }

        if (self->wake_cb != NULL)
            self->wake_cb(external_wake);

        if (external_wake) {
            // button wakeup, keep working for a bit before clock mode
            _vTaskDelay(S_TO_TICK(60));
        } else {
            // work ~10s and sleep ~50s
            _vTaskDelay(S_TO_TICK(10));
        }

        //if (esp.pm) esp_pm_dump_locks(stdout);
    }

    self->module.state = 0;
    self->module.stop = 0;
    xvTaskDelete(NULL);
}

static sleep_t singleton = {
    .module = {
        .name = "sleep",
        .type = M_SLEEP,
        //.init = NULL, //(void *(*)(void *)) auto_init,
        .run = (void (*)(void*,int)) sleep_run,
    },
};

void sleep_run(sleep_t *self, int run)
{
    self = &singleton;
    if (run) {
        if (self->module.state == 0) {
            // start
            if (!esp.dev->controller || self->force) {
                ESP_LOGI(self->module.name, "starting");
                self->module.state = 1;
                xxTaskCreate((void (*)(void*)) sleep_task, "sleep_task", 2*1024, self, 2, NULL);
            }
        }
    } else {
        if (self->module.state != 0) {
            // stop
            ESP_LOGI(self->module.name, "stopping");
            self->module.stop = 1;
        }
    }
}

// requires CONFIG_PM_ENABLE (and tickless)
// 5.1+
//static esp_pm_config_t pm = {
static esp_pm_config_esp32_t pm = {
    // TODO will this ever be used without manual locking?
    .max_freq_mhz = 160,
    .min_freq_mhz = 80,
    // automatic light sleep would be useful for controller without OLED,
    // fast responses and sleep as much as possible...
    // currently wall_clock_wait wakes often and ping too, these are not
    // too useful on controller
    // TODO can't get it to work with all tasks running, still heating up
    .light_sleep_enable = 0//1,
};

sleep_t *sleep_init(void (*wake_cb)(int), int force)
{
    uint8_t nv;
    if (nv_read_u8("sleep.force", &nv) == ESP_OK)
        singleton.force = nv;
    else
        singleton.force = force;

#if !defined(CONFIG_RTC_CLK_SRC_INT_8MD256) && CONFIG_LWIP_TCP_MSL > 60000
#warning "insufficient clock precision for sleep"
    ESP_LOGW("sleep", "insufficient clock precision");
#endif

    esp_err_t ret = esp_pm_configure(&pm);
    if (ret == ESP_ERR_NOT_SUPPORTED)
        ESP_LOGW(TAG, "PM not supported");
    else if (ret != ESP_OK)
        ESP_LOGE(TAG, "PM failed");
    else {
        //ESP_ERROR_CHECK(esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "cpu_max", &esp.cpu_max));
        // this could be needed for modes using scrolling text
        //ESP_ERROR_CHECK(esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "oled", &esp.oled));
    }
    esp.pm = (ret == ESP_OK && pm.light_sleep_enable);

    singleton.wake_cb = wake_cb;
    module_add(&singleton.module);
    esp.sleep = &singleton;
    return &singleton;
}
