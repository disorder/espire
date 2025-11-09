#include "config.h"
#include "device.h"
#include "driver/gpio.h"
#include "esp_pm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

#include "nvs.h"
#include "temp.h"
#include "thermistor.h"
#include "heating.h"
#include "check.h"
#include "wifi.h"
#include "oled.h"
#include "nv.h"
#include "util.h"

#include "esp_log.h"
static const char *TAG = "temp";

static temp_t singleton = {
    .task = NULL,
    .module = {
        .type = M_TEMP,
        .run = (void (*)(void*,int)) temp_run,
    },
};

static int th_count = 0;
// is allocation interfering with wifi
static int wifi = 0;
thermistor_handle_t ths[ADC1_CNT+ADC2_CNT] = {
    {0}, {0}, {0}, {0}, {0}, {0},
    {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0},
};
th_zone_t temp_zones[COUNT_OF(ths)] = {0};

int adc1_ch[ADC1_CNT] = {
    // ADC1
    0, 3, 6, 7, 4, 5,
};
int adc2_ch[ADC2_CNT] = {
    // ADC2
    8, 9, 7, 6, 5, 4,
    // not sure how safe are these
    2, 3,
    // bogus data, maybe usable with some sample filtering?
    0, 1,
};

static inline int adc1_channel_to_gpio(int channel)
{
    for (int i=0; i<ADC1_CNT; i++) {
        if (adc1_ch[i] == channel)
            return adc1_gpio[i];
    }
    return -1;
}

static inline int adc2_channel_to_gpio(int channel)
{
    for (int i=0; i<ADC2_CNT; i++) {
        if (adc2_ch[i] == channel)
            return adc2_gpio[i];
    }
    return -1;
}

static inline int adc_channel_to_gpio(int adc, int channel)
{
    if (adc == ADC_UNIT_1)
        return adc1_channel_to_gpio(channel);
    else
        return adc2_channel_to_gpio(channel);
}

static void thermistor_read(thermistor_handle_t *th)
{
    // TODO two separate samplings give better result, choose lower temperature
    // (in addition to filtering)
    float a = thermistor_get_celsius(th);
    float b = thermistor_get_celsius(th);
    //th->celsius = thermistor_get_celsius(th);
    th->celsius = fminf(a, b);
    time(&th->time);

    // seeing -19 and 49 on disconnected pins
    if (th->celsius < 45.0 && th->celsius > 0.0) {
        ESP_LOGI(TAG, "Voltage: %" PRIu32 " mV\tTemperature: %2.1f C\tResistance: %.0f ohm\tADC%d CH %d GPIO %d",
                 th->vout, th->celsius, th->t_resistance, th->adc_unit == ADC_UNIT_1? 1 : 2, th->channel, adc_channel_to_gpio(th->adc_unit, th->channel));
    } else {
        ESP_LOGI(TAG, "GPIO %d seems to be faulty value: %.1f", adc_channel_to_gpio(th->adc_unit, th->channel), th->celsius);
    }

    for (int i=0; i<COUNT_OF(ths); i++) {
        if (temp_zones[i].name[0] != '\0' && temp_zones[i].adc == adc_channel_to_gpio(th->adc_unit, th->channel)) {
            // this is fine for now but exterior temperature may be over/under
            // negating temperature to disable heating may work well but
            // TODO need some flag to increase range for exterior thermistor
            //      or use value from ebus
            if (th->celsius < 45.0 && th->celsius > 0.0)
                heating_temp_val(temp_zones[i].name, th->celsius, 1);
            else
                heating_temp_val(temp_zones[i].name, NAN, 1);
            // there normally can't be 2 names for gpio
            break;
        }
    }
}

static void temp_task(temp_t *self)
{
    while (!self->module.stop) {
        ESP_LOGI(TAG, "collection triggered");
        int had_wifi = adc2_use == ADC2_WIFI;
        if (wifi) {
            // we're trying to be nice but can't ensure collection period
            ADC2_WAIT(ADC2_ADC, 0, MS_TO_TICK(TEMP_FORCE_WIFI_MS), 0, NULL);
        }

        for (int i=0; i<th_count; i++) {
            thermistor_read(&ths[i]);
        }
        if (wifi) {
            ADC2_FREE();
            if (had_wifi) { // connect ASAP
                // this will never fail in nonblock=1
                if (wifi_run(0, 1, NULL))
                    ESP_LOGE(TAG, "wifi connection not restored");
            }
        }
        ESP_LOGI(TAG, "collection finished");
        //_vTaskDelay(S_TO_TICK(TEMP_PERIOD_S));
        wall_clock_wait(TEMP_PERIOD_S, S_TO_TICK(5));
    }

    task_t *task = self->task;
    self->module.state = 0;
    self->task = NULL;
    self->module.stop = 0;
    xvTaskDelete(task);
}

void temp_init_(int count, thermistor_t *th, char *label)
{
    if (singleton.module.name == NULL) {
        singleton.module.name = TAG;
        module_add(&singleton.module);
    }

    ESP_LOGI(TAG, "init: %d thermistors (%s)", count, label);
    int before = th_count;
    for (int i=0; i<COUNT_OF(ths) && th_count<before+count; i++) {
        int adc=0, adc_unit=0, idx=0, ch=0, gpio=0;
        if (i < ADC1_CNT) {
            idx = i;
            adc = 1;
            adc_unit = ADC_UNIT_1;
            ch = adc1_ch[idx];
            gpio = adc1_gpio[idx];
        } else {
            wifi = 1;
            idx = i-ADC1_CNT;
            adc = 2;
            adc_unit = ADC_UNIT_2;
            ch = adc2_ch[idx];
            gpio = adc2_gpio[idx];
        }
        if (gpio_owner(gpio) != NONE) {
            ESP_LOGI(TAG,"skipping GPIO %d used by %s", gpio, gpio_owner_str(gpio_owner(gpio)));
            continue;
        }

        ESP_LOGI(TAG,"ADC%d #%d on CH %d GPIO %d", adc, idx, ch, gpio);
        check_gpio(gpio, TEMP);
        //gpio_reset_pin(gpio);
        gpio_set_direction(gpio, GPIO_MODE_INPUT);
        gpio_set_pull_mode(gpio, GPIO_FLOATING);

        char key[] = "th.serial.XX";
        u32_t r;
        if (snprintf(&key[10], 2+1, "%d", gpio) > 0 &&
            nv_read_u32(key, &r) == ESP_OK) {
            th->serial_resistance = (float) r;
        }

        ESP_ERROR_CHECK(thermistor_init(&ths[th_count], gpio, adc_unit, ch,
                                        th->serial_resistance, //CONFIG_SERIE_RESISTANCE,
                                        th->nominal_resistance, //CONFIG_NOMINAL_RESISTANCE,
                                        th->nominal_temperature, //CONFIG_NOMINAL_TEMPERATURE,
                                        th->beta_val, //CONFIG_BETA_VALUE,
                                        th->vsource)); //CONFIG_VOLTAGE_SOURCE));
        ths[th_count].celsius = nanf("");
        th_count += 1;
    }
    assert(th_count == before+count);
}

th_zone_t *temp_zone_find(char *name)
{
    int len = strlen(name);
    if (len >= member_size(th_zone_t, name)) {
        ESP_LOGE(TAG, "zone name too long: '%s'", name);
        return NULL;
    }
    if (name[0] == '\0') {
        ESP_LOGE(TAG, "zone name too short");
        return NULL;
    }

    for (int i=0; i<COUNT_OF(ths); i++) {
        if (temp_zones[i].name[0] == '\0')
            continue;

        if (strncmp(temp_zones[i].name, name, sizeof(temp_zones[i].name)) == 0)
            return &temp_zones[i];
    }

    return NULL;
}

void temp_zone_load()
{
    nvs_iterator_t iter = NULL;
    esp_err_t res = nvs_entry_find(NVS_LABEL, NVS_NAMESPACE, NVS_TYPE_I8, &iter);

    while (res == ESP_OK) {
        nvs_entry_info_t info;
        nvs_entry_info(iter, &info);

        if (strlen(info.key) > sizeof("tpin.")-1 && memcmp(info.key, "tpin.", sizeof("tpin.")-1) == 0) {
            char *zone = info.key + sizeof("tpin.")-1;
            ESP_LOGI(TAG, "loading zone '%s'", zone);
            temp_zone_init(zone);
        }
        res = nvs_entry_next(&iter);
    }
    nvs_release_iterator(iter);
}

// used for preloading when adding new heating_t
void temp_zone_init(char *name)
{
    int len = strlen(name);
    if (len >= member_size(th_zone_t, name)) {
        ESP_LOGE(TAG, "zone name too long: '%s'", name);
        return;
    }
    if (name[0] == '\0') {
        ESP_LOGE(TAG, "zone name too short");
        return;
    }

    char tkey[5+member_size(th_zone_t, name)] = "tpin.";
    strncpy(tkey+5, name, len);
    int8_t val;
    if (nv_read_i8(tkey, &val) == ESP_OK)
        temp_zone_adc(name, val);

    // this is useless in temp zone, moving to heating init
    /*
    tkey[0] = 'r';
    if (nv_read_i8(tkey, &val) == ESP_OK)
        heating_relay(name, val);
    */
}

// adc=-1 to clear
th_zone_t *temp_zone_adc(char *name, int adc)
{
    int ok = 0;
    int len = strlen(name);
    if (len >= member_size(th_zone_t, name)) {
        ESP_LOGE(TAG, "zone name too long: '%s'", name);
        return NULL;
    }
    if (name[0] == '\0') {
        ESP_LOGE(TAG, "zone name too short");
        return NULL;
    }

    for (int i=0; i<COUNT_OF(ths); i++) {
        if (adc == -1) {
            if (temp_zones[i].name[0] != '\0' &&
                strncmp(temp_zones[i].name, name, sizeof(temp_zones[i].name))) {
                ESP_LOGI(TAG, "clearing zone %s", name);
                temp_zones[i].name[0] = '\0';
                temp_zones[i].adc = 0;

                char tkey[5+member_size(th_zone_t, name)] = "tpin.";
                strncpy(tkey+5, name, len);
                nv_remove(tkey);

                // rpin.*
                //tkey[0] = 'r';
                //nv_remove(tkey);
                ok = 1;
                return NULL;
            }
        }
        else if (temp_zones[i].name[0] == '\0' || temp_zones[i].adc == adc) {
            if (temp_zones[i].adc == adc && strncmp(temp_zones[i].name, name, sizeof(temp_zones[i].name)) == 0) {
                // already set
                return &temp_zones[i];
            }

            ESP_LOGI(TAG, "setting zone %s ADC %d (was '%s')", name, adc, temp_zones[i].name);
            strncpy(temp_zones[i].name, name, sizeof(temp_zones[i].name));
            temp_zones[i].adc = adc;

            char tkey[5+member_size(th_zone_t, name)] = "tpin.";
            strncpy(tkey+5, name, len);
            nv_write_i8(tkey, adc);

            // load from flash
            /*
            int8_t rpin = -1;
            tkey[0] = 'r';
            nv_read_i8(tkey, &rpin);
            temp_zones[i].relay = (int) rpin;
            */

            return &temp_zones[i];
        }
    }

    if (!ok && adc >= 0) {
        ESP_LOGE(TAG, "could not find slot for temp_zone_adc %s=%d", name, adc);
    }

    return NULL;
}

void temp_run(temp_t *self, int run)
{
    self = &singleton;
    assert(self != NULL);
    if (run) {
        if (self->module.state == 0 && th_count > 0) {
            // start
            self->module.state = 1;
            xxTaskCreate((void (*)(void*))temp_task, "temp_task", 2*1024, self, 4, &self->task);
        }
    } else {
        if (self->module.state != 0) {
            // stop
            self->module.stop = 1;
        }
    }
}
