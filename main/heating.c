#include "config.h"
#include "device.h"
#include "heating.h"
#include "temp.h"
#include "relay.h"
#include "http.h"
#include "oled.h"
#include "api.h"
#include "nv.h"
#include "util.h"

#include <string.h>
#include <math.h>

#include "esp_log.h"

static const char *TAG = "heating";

static list_t zones = {0};
static char *heating_hc_url = NULL;
static int hc_url_reload = 0;
static int hc_status = 0;

heating_t *heating_find(char *name, int create)
{
    list_t *item = &zones;
    heating_t *data = NULL;

    if (name[0] == '\0'){
        ESP_LOGE(TAG, "empty name");
        return NULL;
    }

    if (strlen(name) >= sizeof(data->name)){
        ESP_LOGE(TAG, "name too long: %s", name);
        return NULL;
    }

    while ((item = list_iter(item)) != NULL) {
        if (strncmp(LIST(heating_t, item, name), name, sizeof(data->name)) == 0) {
            data = (heating_t *) item->data;
            break;
        }
    }

    if (data == NULL && create) {
        ESP_LOGI(TAG, "adding '%s'", name);
        data = calloc(1, sizeof(heating_t));
        assert(data != NULL);
        strncpy(data->name, name, sizeof(data->name));
        data->valid = xTaskGetTickCount();
        data->val = NAN;
        data->set = NAN;
        for (int i=0; i<COUNT_OF(data->vals); i++)
            data->vals[i] = NAN;
        data->relay = -1;
        data->state = !HEATING_ON;
        char key[5+member_size(heating_t, name)] = "tset.";
        strncpy(key+5, data->name, strlen(data->name));
        size_t size = 0;
        char *set = NULL;
        nv_read_str(key, &set, &size);
        if (set != NULL) {
            data->set = strtof(set, NULL);
            free(set);
        }
        //temp_zone_init(name);
        list_prepend(&zones, data);
    }

    return data;
}

inline iter_t heating_iter()
{
    return (iter_t) &zones;
}

inline iter_t heating_next(iter_t iter, heating_t **zone)
{
    return iter_next(iter, (void**) zone);
}

static void heating_action(heating_t *data)
{
    /*
    th_zone_t *zone = temp_zone_find(data->name);
    if (zone == NULL) {
        ESP_LOGE(TAG, "unknown zone '%s'", data->name);
        return;
    }
    */

    // heating is not available
    int state = !HEATING_ON;
    if (!hc_status) {
        goto ACTION;
    }

    if (data->relay < 0) {
        ESP_LOGE(TAG, "no relay for '%s'", data->name);
        return;
    }

    if (data->val >= 45.0 || data->val <= 0.0) {
        // something's wrong with data, close
        state = !HEATING_ON;
        goto ACTION;
    }

    float set = data->set;
    if (isnanf(data->set))
        set = HEATING_TEMP_DEFAULT;

    // with all levels of filtering and softening, in 5 minutes the jump
    // can be as much as 0.6 or maybe more
    // TODO what is the best moment to detect trigger on/off
#define HEATING_MIN_TIME_AFTER 0
#define HEATING_MIN_TIME_RUN 0
#define HEATING_IMMEDIATE_DIFF_ON 0.1
#define HEATING_IMMEDIATE_DIFF_OFF 0.1
    state = (set > data->val)? HEATING_ON : !HEATING_ON;
    // this can happen if sensor goes bad or is not connected
    if (isnanf(data->val))
        state = !HEATING_ON;
    if (data->state == state) {
        goto ACTION;
    }

    time_t now;
    time(&now);
    if (data->triggered == 0)
        data->triggered = now;

    float diff = data->val - set;
    if (state != HEATING_ON && diff < HEATING_IMMEDIATE_DIFF_OFF) {
        if (data->change + HEATING_MIN_TIME_AFTER > now)
            return;
    }

    if (state == HEATING_ON && diff > -1.0*HEATING_IMMEDIATE_DIFF_ON) {
        if (data->change + HEATING_MIN_TIME_RUN > now)
            return;
    }

    if (data->state != state) {
        data->change = now;
        data->triggered = 0;
    }

ACTION:
    data->state = state;

#ifdef RELAY_3V3
    relay_init_gpio(data->relay);
    relay_set_gpio(data->relay, state);
#else
    relay_set_gpio_5v(data->relay, state);
#endif
}

static void th_send(int req, char *name, float val, float set);
heating_t *heating_temp_val(char *name, float val, int apply)
{
    heating_t *data = heating_find(name, 1);
    if (data == NULL)
        return NULL;

    // circular buffer, select lowest value
    data->vals[data->i] = val;
    data->i = (data->i + 1) % COUNT_OF(data->vals);
    if (data->c < COUNT_OF(data->vals))
        data->c += 1;
    for (int i=0; i<data->c; i++)
        if (!isnanf(data->vals[i]) && data->vals[i] < val)
            val = data->vals[i];

    if (val != data->val)
        oled_update.temp = 1;
    data->prev = data->val;
    data->val = val;
    data->valid = xTaskGetTickCount();

    ESP_LOGI(TAG, "saving temp val '%s'=%.1f => %.1f", name, data->prev, data->val);
    // measuring was supposed to happen on controller
    // but now measuring can be anywhere
    if (temp_zone_find(data->name) != NULL) {
        ESP_LOGW(TAG, "sending update '%s'=%.1f", data->name, data->val);
        th_send('!', data->name, data->val, NAN);
    }

    if (apply)
        heating_action(data);
    return data;
}

// can be manually used with HTTP request and apikey (if set)
heating_t *heating_temp_set(char *name, float set, int apply)
{
    heating_t *data = heating_find(name, 1);
    if (data == NULL)
        return NULL;

    if (set <= HEATING_TEMP_MAX + .1) {
        if (set != data->set)
            oled_update.temp = 1;
        data->set = set;

        ESP_LOGI(TAG, "saving temp set '%s'=%.1f", name, set);
        // only "xx.x"
        char key[5+member_size(heating_t, name)] = "tset.";
        strncpy(key+5, data->name, strlen(data->name));
        // +1 char allow negative to disable heating
        char value[1+2+1+1 +1];
        if (snprintf(value, sizeof(value), "%.1f", set) > 0)
            nv_write_str(key, value);

        if (apply)
            heating_action(data);
    } else
        ESP_LOGI(TAG, "ignoring temp set '%s'=%.1f", name, set);
    return data;
}

int heating_hc_url_set(char *url)
{
    if (url == NULL)
        return 0;

    ESP_LOGI(TAG, "saving API URL: %s", url);
    esp_err_t res = nv_write_str(HEATING_HC_URL_KEY, url);
    if (heating_hc_url != NULL)
        hc_url_reload = 1;

    if (res != ESP_OK)
        ESP_LOGI(TAG, "writing failed '%s'=%s", HEATING_HC_URL_KEY, url);

    return 1;
}

// needs to be freed
char *heating_hc_url_get()
{
    char *url = NULL;
    size_t size = 0;
    esp_err_t res = nv_read_str(HEATING_HC_URL_KEY, &url, &size);
    if (res != ESP_OK) {
        if (url != NULL) {
            free(url);
            url = NULL;
        }
    }

    return url;
}

// relay=-1 will only reset relay
heating_t *heating_relay(char *name, int relay)
{
    int len = strlen(name);
    if (len >= member_size(heating_t, name)) {
        ESP_LOGE(TAG, "zone name too long: '%s'", name);
        return NULL;
    }
    if (name[0] == '\0') {
        ESP_LOGE(TAG, "zone name too short");
        return NULL;
    }

    char rkey[5+member_size(th_zone_t, name)] = "rpin.";
    strncpy(rkey+5, name, len);
    if (relay == -1) {
        nv_remove(rkey);
    } else {
        int8_t rval = (int8_t) relay;
        nv_write_i8(rkey, rval);
    }

    // zone->relay should be dropped and command move to heating_relay()
    heating_t *data = heating_find(name, 1);
    if (relay == -1) {
        ESP_LOGI(TAG, "clearing heating relay %s", name);
    } else if (data->relay != relay) {
        ESP_LOGI(TAG, "setting heating %s relay %d (was %d)", name, relay, data->relay);
    }
    data->relay = relay;

    return data;
}

// this will leak APIKEY periodically, only solution is digest auth
// or no authorization (or no proactive updates)
/*
void heating_set(heating_t *data)
{
    char url[100] = "http://";
    snprintf(url, sizeof(url), "http://%s/temp/set?apikey=" API_KEY "&name=%s&set=%.1f&val=%.1f",
             data->name, data->name, data->set, data->val);

    // add https_get
    ESP_LOGE(TAG, "%s", url);
}
*/


// TODO autoconfig, maybe http api
#if HEATING_UDP_ENC
static char UDP_KEY[32];
static char UDP_IV[16];
#define UDP_SECRET HEATING_UDP_SECRET
// type + zone+\0 + 2xfloat + secret (or maybe just crc32)
#define HEATING_DATA_SIZE (1+member_size(heating_t, name)+member_size(heating_t, val)+member_size(heating_t, set))
#define HEATING_DGRAM_SIZE AES_PADDED_SIZE(HEATING_DATA_SIZE + strlen(UDP_SECRET)+1)
static mbedtls_aes_context udp_aes = {0};
#endif

// buf should be HEATING_DGRAM_SIZE if HEATING_UDP_ENC
static int th_prepare(char *buf, int req, char *name, float val, float set)
{
    //char buf[1+member_size(heating_t, name)];
    buf[0] = req;
    int len = 1+member_size(heating_t, name)+sizeof(val)+sizeof(set);

    buf[0] = req;

    if (name != NULL) {
        int len = strlen(name);
        memcpy(buf+1, name, len);
        buf[1+len] = '\0';
        len = 1+len+1;
    }

    if (req == '!') {
        memcpy(buf+1+member_size(heating_t, name), &val, sizeof(val));
        memcpy(buf+1+member_size(heating_t, name)+sizeof(val), &set, sizeof(set));
        len = 1+member_size(heating_t, name)+sizeof(val)+sizeof(set);
    }

#if !HEATING_UDP_ENC
    return len;

#else
    //int inlen = len + strlen(UDP_SECRET);
    //int padded = AES_PADDED_SIZE(inlen);
    char in[HEATING_DGRAM_SIZE];
    memcpy(in, buf, len);
    memcpy(in + len, UDP_SECRET, strlen(UDP_SECRET)+1);

    char iv[16];
    memcpy(iv, UDP_IV, sizeof(iv));
    aes_cbc(1, &udp_aes, iv, HEATING_DGRAM_SIZE, in, sizeof(in), buf);
    //ESP_LOG_BUFFER_CHAR(TAG, in, HEATING_DGRAM_SIZE);
    //ESP_LOG_BUFFER_HEX(TAG, buf, HEATING_DGRAM_SIZE);
    return HEATING_DGRAM_SIZE;
#endif
}

static int th_sock = -1;

static void th_send(int req, char *name, float val, float set)
{
    if (th_sock < 0)
        return;

    if (CONTROLLER_SA.sin_addr.s_addr == INADDR_ANY)
        return;

    struct sockaddr_in sa = {0};
    sa.sin_addr = CONTROLLER_SA.sin_addr;
    sa.sin_family = AF_INET;
    sa.sin_port = htons(HEATING_UDP_PORT);
#if !HEATING_UDP_ENC
    char buf[1+member_size(heating_t, name)+sizeof(val)+sizeof(set)];
#else
    char buf[HEATING_DGRAM_SIZE];
#endif
    int len = th_prepare(buf, req, name, val, set);
    sendto(th_sock, buf, len,
           MSG_DONTWAIT, (struct sockaddr *) &sa, sizeof(sa));
}

static void thermostat_udp(void *pvParameter)
{
    struct sockaddr_in sa, claddr;

    xSemaphoreTake(esp.sockets, portMAX_DELAY);
    if ((th_sock = socket(PF_INET, SOCK_DGRAM, 0)) == -1) {
        ESP_LOGE(TAG, "socket: %s", strerror(errno));
        xSemaphoreGive(esp.sockets);
        // return from task will crash
        return;
    }

    int optval = 1;
    setsockopt(th_sock, SOL_SOCKET, SO_REUSEADDR,
               (const void *)&optval , sizeof(int));

    bzero((void *) &sa, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(HEATING_UDP_PORT);
    sa.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(th_sock, (struct sockaddr *) &sa, sizeof(sa)) == -1) {
        ESP_LOGE(TAG, "bind: %s", strerror(errno));
        xSemaphoreGive(esp.sockets);
        // return from task will crash
        return;
    }

    socklen_t claddrlen = sizeof(claddr);
    int n;
    heating_t *data;
#if !HEATING_UDP_ENC
    char buf[1 + member_size(heating_t, name) + sizeof(data->val)+sizeof(data->set)];
#define dec buf
#else
    char buf[HEATING_DGRAM_SIZE];
    char dec[HEATING_DGRAM_SIZE];
#endif
    // thudp.py is useful for debugging:
    // # = reboot, ! = set, * = get all zones, ? = get zone name
    // this is all very ugly, datagram payload format is
    // 1 byte = * or ? or ! or #
    // sizeof name (including '\0')
    // sizeof val (little endian)
    // sizeof set (little endian)
    // secret
    while (1) {
        //bzero(buf, sizeof(buf));
        n = recvfrom(th_sock, buf, sizeof(buf), 0,
                     (struct sockaddr *) &claddr, &claddrlen);

        if (n < 0)
            continue;

#if HEATING_UDP_ENC
        if (n != HEATING_DGRAM_SIZE)
            continue;

        char iv[16];
        memcpy(iv, UDP_IV, sizeof(iv));
        aes_cbc(0, &udp_aes, iv, n, buf, sizeof(dec), dec);
        if (memcmp(dec+HEATING_DATA_SIZE, UDP_SECRET, strlen(UDP_SECRET)+1) != 0) {
            ESP_LOGI(TAG, "invalid datagram from 0x%08" PRIx32, claddr.sin_addr.s_addr);
            continue;
        }
#endif

        if (dec[0] == '#') {
            // ugly hack but better than stuck httpd without any remote reboot
            api_reboot(NULL);
        }

        dec[1+member_size(heating_t, name)-1] = '\0';
        if (dec[0] != '*') {
            data = heating_find(dec+1, dec[0] == '!');
            if (data == NULL)
                continue;
        }

        if (dec[0] == '?' || dec[0] == '*') {
            iter_t iter = NULL;
            if (dec[0] == '*') {
                iter = heating_iter();
                iter = heating_next(iter, &data);
            }

            while (data != NULL) {
                th_prepare(dec, '!', data->name, data->val, data->set);
                // send back to listening port
                claddr.sin_port = htons(HEATING_UDP_PORT);
#if !HEATING_UDP_ENC
                n = sendto(th_sock, buf, 1+member_size(heating_t, name)+sizeof(data->val)+sizeof(data->set),
                           MSG_DONTWAIT, (struct sockaddr *) &claddr, claddrlen);
#else
                n = sendto(th_sock, dec, HEATING_DGRAM_SIZE,
                           MSG_DONTWAIT, (struct sockaddr *) &claddr, claddrlen);
#endif
                if (n < 0)
                    ESP_LOGE(TAG, "sendto: %s", strerror(errno));

                ESP_LOGD(TAG, "sending '%s'", data->name);
                if (dec[0] == '?')
                    break;
                data = NULL;
                if (iter != NULL)
                    iter = heating_next(iter, &data);
             }
        } else if (dec[0] == '!') {
            time(&oled_update.temp_last);
            typeof(data->val) val = NAN;
            typeof(data->set) set;
            memcpy(&set, dec+1+member_size(heating_t, name)+sizeof(data->set), sizeof(data->set));
            memcpy(&val, dec+1+member_size(heating_t, name), sizeof(data->val));

            th_zone_t *zone = temp_zone_find(data->name);
            int local = !esp.dev->controller && (strncmp(data->name, esp.dev->hostname, member_size(heating_t, name)) == 0);

            if (esp.dev->controller) {
                int apply = 0;
                if (!isnan(set)) {
                    heating_temp_set(data->name, set, 0);
                    apply = 1;
                }
                if (zone == NULL) {
                    heating_temp_val(data->name, val, 0);
                    apply = 1;
                }
                // remotely measured, apply and enact
                // but original proposal was to measure on controller only
                if (zone == NULL && apply)
                    heating_action(data);
                // no display to handle
                continue;
            }

            // non-local zones on client device
            if (!local) {
                data->val = val;
                data->set = set;
                continue;
            }

            if (!isnan(val)) {
                if (val != data->val) {
                    data->val = val;
                    oled_update.temp = 1;
                }
            }

            if (!isnan(set)) {
                if (set <= HEATING_TEMP_MAX + .1)
                    data->set = set;
                if (set == oled_update.temp_set) {
                    oled_update.temp_pending = 0;
                    oled_update.temp = 1;
                }
                if (isnanf(oled_update.temp_mod) || !oled_update.temp_pending) {
                    // initialize UI with controller value
                    // or update keep set value updated if not pending
                    // if update comes from controller mod will be editable
                    oled_update.temp_set = set;
                    if (isnanf(oled_update.temp_mod))
                        oled_update.temp_mod = oled_update.temp_set;
                }
            }
        } else {
            ESP_LOGW(TAG, "received unknown request type '%c' from 0x%08" PRIx32, dec[0], claddr.sin_addr.s_addr);
        }
    }
}

static void thermostat_update(void *pvParameter)
{
    while (1) {
        if (oled_update.temp_pending)
            th_send('!', esp.dev->hostname, NAN, oled_update.temp_set);
        //th_send('?', esp.dev->hostname, 0, 0);
        // requesting all zones from controller - good for displaying
        th_send('*', NULL, 0, 0);

        _vTaskDelay((oled_update.temp_pending)? S_TO_TICK(1) : S_TO_TICK(5));
    }
}

void heating_api_cb(http_request_t *req, int success)
{
    if (!success)
        goto FAIL;
    if (!req->client || esp_http_client_get_status_code(req->client) != 200) {
        goto FAIL;
    }

    // it doesn't matter
    ESP_LOGI(TAG, "EBUS sent");
    goto CLEANUP;

    if (esp_http_client_get_content_length(req->client) != req->bufsize) {
        ESP_LOGW(TAG, "content size mismatch %lld != %d", esp_http_client_get_content_length(req->client), req->bufsize);
        // this happens when we get disconnected
        goto CLEANUP;
    }

    // seeing NULL and 0 size
    //assert(req->buf != NULL);
FAIL:
    // TODO could have some data in the response or UDP updates?
    // this means if API is down, all relays should be off
    // we can't switch heating on/off so close everything
    hc_status = 0;
    iter_t iter = heating_iter();
    heating_t *data;
    while ((iter = heating_next(iter, &data)) != NULL)
        heating_action(data);
    ESP_LOGE(TAG, "EBUS failed: %s", req->url);
CLEANUP:
    ESP_LOGI(TAG, "cleanup");
    if (req->buf != NULL)
        free(req->buf);
    free(req);
}

// with this period we can share turning heating on/off
#define CHECK_PERIOD_S 10
// reboot will prevent aging, timestamp can't be relied on
#define MAX_AGE_S (OFFLINE_REBOOT-(2*CHECK_PERIOD_S))
static void thermostat_aging(void *pvParameter)
{
    while (1) {
        int on_cnt = 0;
        TickType_t tick_now = xTaskGetTickCount();
        iter_t iter = heating_iter();
        heating_t *data;
        while ((iter = heating_next(iter, &data)) != NULL) {
            // TODO maybe configurable time via API?
            if (tick_now > data->valid + S_TO_TICK(MAX_AGE_S)) {
                // this is very important - maybe we should do it at boot
                ESP_LOGW(TAG, "temperature for '%s' is too old", data->name);
                // start erasing circular buffer
                heating_temp_val(data->name, NAN, 0);
                // enforce action
                data->prev = data->val;
                data->val = NAN;
                heating_action(data);
                /*
#ifdef RELAY_3V3
                relay_init_gpio(gpio);
                relay_set_gpio(gpio, !HEATING_ON);
#else
                relay_set_gpio_5v(gpio, !HEATING_ON);
#endif
                */
            } else {
                //ESP_LOGI(TAG, "temperature for '%s' is ok %d", data->name, data->valid);
            }

            if (data->state == HEATING_ON)
                ++on_cnt;
        }

        if (hc_url_reload) {
            free(heating_hc_url);
            heating_hc_url = NULL;
            hc_url_reload = 0;
        }

        if (heating_hc_url == NULL)
            heating_hc_url = heating_hc_url_get();

        if (heating_hc_url == NULL) {
            ESP_LOGE(TAG, "heating API URL is not set: %s", HEATING_HC_URL_KEY);
        } else {
            http_request_t *req = calloc(1, sizeof(http_request_t));
            assert(req != NULL);

            // TODO digest auth
            // off/auto/day/night - 0/1 will let API decide
            heating_hc_url[strlen(heating_hc_url)-1] = '0' + ((on_cnt)? 1 : 0);
            req->url = heating_hc_url;
            req->callback = heating_api_cb;
            //req->pad = 1;
            req->exclusive = 1;
            https_get(req);
        }

        _vTaskDelay(S_TO_TICK(CHECK_PERIOD_S));
    }
}

#if HEATING_UDP_ENC
void th_aes_init()
{
    b64_decode(HEATING_UDP_KEY_B64, sizeof(HEATING_UDP_KEY_B64), UDP_KEY, sizeof(UDP_KEY));
    b64_decode(HEATING_UDP_IV_B64, sizeof(HEATING_UDP_IV_B64), UDP_IV, sizeof(UDP_IV));
    /*
    static int inited = 0;
    if (inited)
        mbedtls_aes_free(&udp_aes);
    mbedtls_aes_init(&udp_aes);
    inited = 1;
    */
    mbedtls_aes_setkey_enc(&udp_aes, (unsigned char *) UDP_KEY, 8*sizeof(UDP_KEY));
}
#endif

void thermostat_init()
{
#if HEATING_UDP_ENC
    ESP_LOGI(TAG, "encryption enabled");
    mbedtls_aes_init(&udp_aes);
    th_aes_init();
#endif

    // initialize all known relays
    nvs_iterator_t iter = NULL;
    esp_err_t res = nvs_entry_find(NVS_LABEL, NVS_NAMESPACE, NVS_TYPE_I8, &iter);
    while (res == ESP_OK) {
        nvs_entry_info_t info;
        nvs_entry_info(iter, &info);

        if (strncmp(info.key, "rpin.", sizeof("rpin.")-1) != 0)
            goto NEXT;

        nvs_handle_t h;
        int8_t gpio;
        ESP_ERROR_CHECK(nvs_open(info.namespace_name, NVS_READONLY, &h));
        ESP_ERROR_CHECK(res = nvs_get_i8(h, info.key, &gpio));
        nvs_close(h);
        if (gpio_owner(gpio)) {
            ESP_LOGW(TAG, "skipping used %s GPIO %d", info.key, gpio);
            goto NEXT;
        }
        ESP_LOGW(TAG, "initializing %s GPIO %d", info.key, gpio);

        // with aging this is unnecessary, aging will take care of it
        // but it depends on default action being turning off
        // and not crashing before it can happen
        // (hw boot resets pin anyway but software reboot/abort does not)
        /*
#ifdef RELAY_3V3
        relay_init_gpio(gpio);
        relay_set_gpio(gpio, !HEATING_ON);
#else
        relay_set_gpio_5v(gpio, !HEATING_ON);
#endif
        */

        // create zone
        heating_relay(&info.key[5], gpio);
        // there is no guarantee someone will send anything
        // but there is default action on NAN...
        //heating_action(data);

    NEXT:
        res = nvs_entry_next(&iter);
    }
    nvs_release_iterator(iter);

    xxTaskCreate((void (*)(void*))thermostat_udp, "th_udp", 2*1024, NULL, 0, NULL);
    if (!esp.dev->controller)
        xxTaskCreate((void (*)(void*))thermostat_update, "th_update", 2*1024, NULL, 0, NULL);
    else
        xxTaskCreate((void (*)(void*))thermostat_aging, "th_aging", 2*1024, NULL, 0, NULL);
}
