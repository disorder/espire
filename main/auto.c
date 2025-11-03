#include "config.h"
#include "device.h"
#include "esp_http_client.h"
#include "heating.h"
#include "module.h"
#include "auto.h"
#include "http.h"
#include "oled.h"
#include "util.h"
#include "nv.h"
#include "temp.h"
#include "wifi.h"
#include "ping.h"

#include "esp_wifi.h"
#include "esp_log.h"
static const char *TAG = "auto";

static auto_t singleton = {
    .handlers = {0},
    .task = NULL,
    .module = {
        .type = M_AUTO,
        //.init = NULL, //(void *(*)(void *)) auto_init,
        .network = NET_ETH_ANY,
        .run = (void (*)(void*,int)) auto_run,
    },
};

auto_t *auto_singleton = &singleton;

static void wifi_ssid_apply(char *value)
{
    if (strncmp(WIFI_SSID, value, sizeof(WIFI_SSID)) != 0) {
        wifi_update(value, NULL, 1);
    }
}

static void wifi_pass_apply(char *value)
{
    if (strncmp(WIFI_PASSWORD, value, sizeof(WIFI_PASSWORD)) != 0) {
        wifi_update(NULL, value, 1);
    }
}

static void hostname_apply(char *value)
{
    if (strlen(value) && strncmp(HOSTNAME, value, sizeof(HOSTNAME)) != 0) {
        int size = strlen(value)+1;
        size = (size < sizeof(HOSTNAME))? size : sizeof(HOSTNAME);
        strncpy(HOSTNAME, value, size);
        HOSTNAME[size - 1] = '\0';
        esp.dev->hostname = HOSTNAME;
        hostname_set(HOSTNAME);
        // needed to apply to openwrt DHCP/DNS
        //_wifi_stop_sta(1);
        //_wifi_start_sta(1);
    }
}

static void loglevel_handler(auto_handler_t *self, char *value)
{
    if (value == NULL)
        return;

    char *name = value;
    value = strchrnul(value, '=');
    if (value[0] != '\0') {
        value[0] = '\0';
        value += 1;
    }

    esp_log_level_set(name, atoi(value));
}

static void temp_zone_adc_handler(auto_handler_t *self, char *value)
{
    if (value == NULL)
        return;

    char *name = value;
    value = strchrnul(value, '=');
    if (value[0] != '\0') {
        value[0] = '\0';
        value += 1;
    }

    int gpio = atoi(value);
    temp_zone_adc(name, gpio);
}

static void heating_relay_handler(auto_handler_t *self, char *value)
{
    if (value == NULL)
        return;

    char *name = value;
    value = strchrnul(value, '=');
    if (value[0] != '\0') {
        value[0] = '\0';
        value += 1;
    }

    int gpio = atoi(value);
    heating_relay(name, gpio);
}

static void heating_hc_url_handler(auto_handler_t *self, char *value)
{
    if (value == NULL)
        return;

    heating_hc_url_set(value);
}

void generic_str_key_handler(auto_handler_t *self, char *value)
{
    if (value == NULL)
        return;

    /*
    char *nv_value = NULL;
    size_t size = 0;
    if (self->is_blob)
        nv_read_blob(self->name, (void*) &nv_value, &size);
    else
        nv_read_str(self->name, &nv_value, &size);

    int ssid_cmp = 1;
    if (nv_value != NULL) {
        ssid_cmp = strcmp(nv_value, value);
        free(nv_value);
    }
    */

    if (self->apply != NULL)
        self->apply(value);

    /*
    if (ssid_cmp != 0) {
        if (self->is_blob)
            nv_write_blob(self->name, value, strlen(value));
        else
            nv_write_str(self->name, value);
    }
    */
    if (self->is_blob)
        // is_blob but only strings without \0 supported in config
        nv_write_blob(self->name, value, -1, 1);
    else
        nv_write_str(self->name, value);
}

inline void controller_ip_handler(auto_handler_t *self, char *value)
{
    int len = strlen(value);
    if (len >= sizeof(CONTROLLER_IP))
        return;

    if (init_sa(value, 0, &CONTROLLER_SA) == 0)
        return;

    strncpy(CONTROLLER_IP, value, len);
    nv_write_str("controller_ip", value);
}

inline void hostname_handler(auto_handler_t *self, char *value)
{
    hostname_apply(value);
    nv_write_str("hostname", value);
}

static void th_udp_secret_apply(char *value)
{
    if (strcmp(HEATING_UDP_SECRET, value) == 0)
        return;
    char *old = HEATING_UDP_SECRET;
    char *new = strdup(value);
    assert(new != NULL);
    HEATING_UDP_SECRET = new;
    th_aes_init();
    if(old != NULL)
        free(old);
    nv_write_str("th.udp.secret", value);
}

static void th_udp_port_handler(auto_handler_t *self, char *value)
{
    int port = atoi(value);
    if (port < 0 || port > 65535) {
        ESP_LOGE(TAG, "th.udp.port invalid: %d", port);
        return;
    }

    // then reboot
    nv_write_u16("th.udp.port", (uint16_t) port);
}

static void th_udp_key_handler(auto_handler_t *self, char *value)
{
    int len = strlen(value);
    if (len != sizeof(HEATING_UDP_KEY_B64)) {
        ESP_LOGE(TAG, "th.udp.key length %d instead of %d", len, sizeof(HEATING_UDP_KEY_B64));
        return;
    }

    if (memcmp(HEATING_UDP_KEY_B64, value, sizeof(HEATING_UDP_KEY_B64)) != 0) {
        memcpy(HEATING_UDP_KEY_B64, value, sizeof(HEATING_UDP_KEY_B64));
        nv_write_blob("th.udp.key", (void*)value, sizeof(HEATING_UDP_KEY_B64), 0);
        th_aes_init();
    }
}

static void th_udp_iv_handler(auto_handler_t *self, char *value)
{
    int len = strlen(value);
    if (len != sizeof(HEATING_UDP_IV_B64)) {
        ESP_LOGE(TAG, "th.iv.key length %d instead of %d", len, sizeof(HEATING_UDP_IV_B64));
        return;
    }

    if (memcmp(HEATING_UDP_IV_B64, value, sizeof(HEATING_UDP_IV_B64)) != 0) {
        memcpy(HEATING_UDP_IV_B64, value, sizeof(HEATING_UDP_IV_B64));
        nv_write_blob("th.udp.iv", (void*) value, sizeof(HEATING_UDP_KEY_B64), 0);
        th_aes_init();
    }
}

static void sleep_force_handler(auto_handler_t *self, char *value)
{
    int val = atoi(value);
    if (esp.sleep->force != val)
        nv_write_u8("sleep.force", (uint8_t) val);

    esp.sleep->force = val;
}

static void mode_default_handler(auto_handler_t *self, char *value)
{
    int val;
    if (strcmp(value, "heating") == 0)
        val = HEATING;
    else if (strcmp(value, "clock") == 0)
        val = CLOCK;
    else if (strcmp(value, "forecast") == 0)
        val = OWM;
    else if (strcmp(value, "off") == 0)
        val = OFF;
    else
        val = atoi(value) % MODE_MAX;

    if (oled_update.mode_default != val)
        nv_write_u8("mode_default", (uint8_t) val);

    oled_update.mode_default = val;
}

static void oled_power_handler(auto_handler_t *self, char *value)
{
    int val = atoi(value);
    if (oled_update.power_default != val)
        nv_write_u8("oled_power", (uint8_t) val);

    oled_update.power_default = val;
}

static void rm_handler(auto_handler_t *self, char *value)
{
    if (value == NULL)
        return;

    nv_remove(value);
}

static void pm_handler(auto_handler_t *self, char *value)
{
    if (value == NULL)
        return;

    esp.pm = atoi(value);
}

static void write_str_handler(auto_handler_t *self, char *value)
{
    if (value == NULL)
        return;

    char *key = value;
    value = strchrnul(value, '=');
    if (value[0] != '\0') {
        value[0] = '\0';
        value += 1;
    }
    ESP_LOGE(TAG, "writing %s", key);

    nv_write_str(key, value);
}

auto_handler_t default_handlers[] = {
    {
        .name = WIFI_SSID_KEY,
        .apply = wifi_ssid_apply,
        .handler = generic_str_key_handler,
    },
    {
        .name = WIFI_PASSWORD_KEY,
        .apply = wifi_pass_apply,
        // prevents logging values
        .is_blob = 1,
        .handler = generic_str_key_handler,
    },
    {
        .name = "controller_ip",
        .handler = controller_ip_handler,
    },
    {
        .name = "hostname",
        .handler = hostname_handler,
    },
    {
        .name = "temp_zone_adc",
        .handler = temp_zone_adc_handler,
    },
    {
        .name = "heating_relay",
        .handler = heating_relay_handler,
    },
    {
        .name = "heating_hc_url",
        .handler = heating_hc_url_handler,
    },
    {
        .name = "th.udp.key",
        .handler = th_udp_key_handler,
    },
    {
        .name = "th.udp.iv",
        .handler = th_udp_iv_handler,
    },
    {
        .name = "th.udp.secret",
        .apply = th_udp_secret_apply,
        .handler = generic_str_key_handler,
    },
    {
        .name = "th.udp.port",
        .handler = th_udp_port_handler,
    },
    {
        .name = "sleep.force",
        .handler = sleep_force_handler,
    },
    {
        .name = "oled_power",
        .handler = oled_power_handler,
    },
    {
        .name = "mode_default",
        .handler = mode_default_handler,
    },
    {
        .name = "rm",
        .handler = rm_handler,
    },
    // for single use only or it will keep overwriting flash
    {
        .name = "write_str",
        .handler = write_str_handler,
    },
    {
        .name = "loglevel",
        .handler = loglevel_handler,
    },
    {
        .name = "pm",
        .handler = pm_handler,
    },
};

// strict and simple format:
// # comment\n
// NAME=VALUE\n
// expecting \0 at the end of buffer
char *config_pair(char *start, char *stop, char **name, char **value)
{
    *name = NULL;
    *value = NULL;
    // \r is not tested, better not use it
    while (start < stop) {
        //ESP_LOGI(TAG, "%c", start[0]);
        switch (start[0]) {
        case '\0':
            // eat and end processing
            start += 1;
            return start;
        case '#':
            // finalize name, find next line and end processing
            start[0] = '\0';
            // eat comment
            if (start < stop)
                start = strchrnul(start+1, '\n');
            // this can return value NULL or both NULL
            return start+1;
        case '=':
            // finalize name and value
            start[0] = '\0';
            //ESP_LOGI(TAG, "name '%s'", *name);
            *value = start+1;
            start = strchrnul(*value, '\n');
            if (start[-1] == '\r')
                start[-1] = '\0';
            start[0] = '\0';
            //ESP_LOGI(TAG, "value '%s'", *value);
            return start+1;
        case '\n':
            if (*name != NULL) {
                // finalize name
                start[0] = '\0';
                return start+1;
            }
            // else eat starting whitespace
            [[fallthrough]];
        case '\r':
        case ' ':
        case '\t':
            // eat starting whitespace (newline from last line)
            if (*name == NULL) {
                start += 1;
                break;
            }
            // else continue as part of name:
            [[fallthrough]];
        default:
            if (*name == NULL)
                *name = start;
            start += 1;
        }
    }
    // did not reach '='
    return NULL;
}

void config_apply(auto_t *self, char *buf, int bufsize, int commit, int auth)
{
    if (self == NULL)
        self = &singleton;
    char *name;
    char *value;
    char *stop = buf + bufsize;
    char *start = buf;
    int authorized = auth || strlen(API_KEY) == 0;

    while ((start = config_pair(start, stop, &name, &value)) != NULL) {
        //ESP_LOGD(TAG, "pair '%s'='%s'", (name == NULL)? "(null)" : name, (value == NULL)? "(null)" : value);
        if (value != NULL) {
            ESP_LOGD(TAG, "pair '%s'='%s'", name, value);
        } else if (name != NULL) {
            ESP_LOGD(TAG, "pair '%s'", name);
        }
        if (name != NULL) {
            if (!authorized && value != NULL && strcmp(name, "apikey") == 0) {
                authorized = (strcmp(value, API_KEY) == 0);
                continue;
            }
            if (!authorized) {
                ESP_LOGE(TAG, "%s ignored, authorization failed", name);
                continue;
            }

            for (list_t *item = self->handlers.next; item != NULL; item = item->next) {
                if (strcmp(LIST(auto_handler_t, item, name), name) == 0) {
                    ESP_LOGI(TAG, "running handler for '%s'", name);
                    //ESP_LOGI(TAG, "running handler for '%s'='%s'", name, value);
                    assert(LIST(auto_handler_t, item, handler) != NULL);
                    LIST(auto_handler_t, item, handler)(item->data, value);
                }
            }
        }
    }

    if (commit)
        nv_commit();
}

static void config_task_cb(http_request_t *req, int success)
{
    if (!success)
        goto CLEANUP;
    if (!req->client || esp_http_client_get_status_code(req->client) != 200) {
        goto CLEANUP;
    }

    // this will be NULL if auth is set up but server doesn't do auth
    ESP_LOGD(TAG, "config data: %s", (req->buf == NULL)? "(null)" : req->buf);
    ESP_LOGI(TAG, "config data: 0x%x", (ssize_t) req->buf);

    auto_t *self = req->data;
    assert(self != NULL);
    config_apply(self, req->buf, req->bufsize, 1, 0);

    nv_commit();

CLEANUP:
    ESP_LOGI(TAG, "cleanup");
    if (req->buf != NULL)
        free(req->buf);
    free(req);
}

static void config_task(auto_t *self)
{
    assert(self != NULL);
    char url[] = AUTO_CONFIG_URL "/xxXXxxXXxxXX";
    char *mac = &url[sizeof(url)-1 - 12];
    read_mac(mac, 0);

    while (!self->module.stop) {
        /*
        TickType_t now = xTaskGetTickCount();
        if (now - self->last > S_TO_TICK(AUTO_CONFIG_PERIOD_S))
            // seeing some really long misses, maybe this helps
            _vTaskDelay(S_TO_TICK(10));
        else
        */
        if (self->last_task != 0) {
            //vTaskDelayUntil(&self->last_task, S_TO_TICK(AUTO_CONFIG_PERIOD_S));
            wall_clock_wait(AUTO_CONFIG_PERIOD_S, S_TO_TICK(1));
        }

        http_request_t *req = calloc(1, sizeof(http_request_t));
        assert(req != NULL);
        req->data = self;
        req->url = (char *) &url;
        req->callback = config_task_cb;
        req->pad = 1;
#if AUTO_HTTPS_INTERNAL
        extern char httpd_pem_start[] asm("_binary_httpd_pem_start");
        req->cert_pem = httpd_pem_start;
#endif
        req->skip_cert_common_name_check = AUTO_HTTPS_INSECURE;
        if (sizeof(AUTO_CONFIG_URL_USER) > 1) {
            req->username = AUTO_CONFIG_URL_USER;
            req->password = AUTO_CONFIG_URL_PASSWORD;
            req->auth_type = AUTO_AUTH_BASIC? HTTP_AUTH_TYPE_BASIC : HTTP_AUTH_TYPE_DIGEST;
        }
        https_get(req);

        //self->last_task = xTaskGetTickCount();
        time(&self->last_task);
        //_vTaskDelay(S_TO_TICK(AUTO_CONFIG_PERIOD_S));
        //wall_clock_wait(AUTO_CONFIG_PERIOD_S, S_TO_TICK(1));
    }

    task_t *task = self->task;
    self->module.state = 0;
    self->task = NULL;
    self->module.stop = 0;
    xvTaskDelete(task);
}

void auto_handler_add(auto_t *self, auto_handler_t *handler)
{
    assert(self != NULL);
    list_prepend(&self->handlers, handler);
}

auto_t *auto_init()
{
    singleton.module.name = TAG;
    module_add(&singleton.module);

    for (int i=0; i<COUNT_OF(default_handlers); i++)
        auto_handler_add(&singleton, &default_handlers[i]);
    //auto_run(&singleton, 1);
    return &singleton;
}

void auto_run(auto_t *self, int run)
{
    self = &singleton;
    if (self->module.offline != 0 && run && ping_online.connected == 0)
        return;
    assert(self != NULL);
    if (self->task == NULL) {
        assert(self->module.state == 0);
        if (run) {
            ESP_LOGI(TAG, "starting");
            xxTaskCreate((void (*)(void*))config_task, "config_task", 3*1024, self, 0, &self->task);
            self->module.state = 1;
        }
    } else {
        assert(self->module.state != 0);
        if (!run && !self->module.stop) {
            ESP_LOGI(TAG, "stopping");
            self->module.stop = 1;
        }
    }
}
