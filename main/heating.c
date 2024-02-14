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
        data->val = NAN;
        data->set = NAN;
        char key[5+member_size(heating_t, name)] = "tset.";
        strncpy(key+5, data->name, strlen(data->name));
        size_t size = 0;
        char *set = NULL;
        nv_read_str(key, &set, &size);
        if (set != NULL) {
            data->set = strtof(set, NULL);
            free(set);
        }
        temp_zone_init(name);
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
    th_zone_t *zone = temp_zone_find(data->name);
    if (zone == NULL) {
        ESP_LOGE(TAG, "unknown zone %s", data->name);
        return;
    }

    if (zone->relay < 0) {
        ESP_LOGE(TAG, "no relay for %s", data->name);
        return;
    }

    if (data->val >= 45.0 || data->val <= 0.0) {
        // something's wrong with data, close
        relay_set_gpio(zone->relay, !HEATING_ON);
        return;
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
    int state = (set > data->val)? HEATING_ON : !HEATING_ON;
    // this can happen if sensor goes bad or is not connected
    if (isnanf(data->val))
        state = !HEATING_ON;
    if (data->state == state) {
        relay_set_gpio(zone->relay, state);
        return;
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

    relay_set_gpio(zone->relay, state);

    if (data->state != state) {
        data->state = state;
        data->change = now;
        data->triggered = 0;
    }
}

heating_t *heating_temp_val(char *name, float val)
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
        if (data->vals[i] < val)
            val = data->vals[i];

    if (val != data->val)
        oled_update.temp = 1;
    data->prev = data->val;
    data->val = val;

    ESP_LOGI(TAG, "saving temp val '%s'=%.1f => %.1f", name, data->prev, data->val);
    heating_action(data);
    return data;
}

// this function is not restricted by HEATING_TEMP_MAX
// can be manually used with HTTP request and apikey (if set)
heating_t *heating_temp_set(char *name, float set)
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
        char value[2+1+1 +1];
        if (snprintf(value, sizeof(value), "%.1f", value) > 0)
            nv_write_str(key, value);

        heating_action(data);
    } else
        ESP_LOGI(TAG, "ignoring temp set '%s'=%.1f", name, set);
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
    // this is all very ugly, datagram payload format is
    // 1 byte = ? or ! or #
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
            ESP_LOGI(TAG, "invalid datagram from 0x%08x", claddr.sin_addr);
            continue;
        }
#endif

        if (dec[0] == '#') {
            // ugly hack but better than stuck httpd without any remote reboot
            api_reboot(NULL);
        }

        dec[1+member_size(heating_t, name)-1] = '\0';
        if (dec[0] != '*')
            data = heating_find(dec+1, dec[0] == '!');
        if (data == NULL && dec[0] != '*')
            continue;

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
                data = NULL;
                if (iter != NULL)
                    iter = heating_next(iter, &data);
             }
        } else if (dec[0] == '!') {
            time(&oled_update.temp_last);
            typeof(data->val) val;
            typeof(data->set) set;
            memcpy(&val, dec+1+member_size(heating_t, name), sizeof(data->val));
            memcpy(&set, dec+1+member_size(heating_t, name)+sizeof(data->set), sizeof(data->set));
            // to avoid tampering, controller will only accept set, not val
            // controller is collecting data and updating val
            if (!esp.dev->controller && !isnan(val)) {
                if (val != data->val)
                    oled_update.temp = 1;
                data->val = val;
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
                    // or update keep mod value updated if not pending
                    // (not sure about that behaviour)
                    oled_update.temp_mod = set;
                }
            }
        } else {
            ESP_LOGW(TAG, "received unknown request type '%c' from 0x%08x", dec[0], claddr.sin_addr);
        }
    }
}

static void thermostat_update(void *pvParameter)
{
    while (1) {
        if (oled_update.temp_pending)
            th_send('!', esp.dev->hostname, NAN, oled_update.temp_set);
        //th_send('?', esp.dev->hostname, 0, 0);
        th_send('*', NULL, 0, 0);

        _vTaskDelay((oled_update.temp_pending)? S_TO_TICK(1) : S_TO_TICK(5));
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

    xxTaskCreate((void (*)(void*))thermostat_udp, "th_udp", 2*1024, NULL, 0, NULL);
    if (!esp.dev->controller)
        xxTaskCreate((void (*)(void*))thermostat_update, "th_update", 2*1024, NULL, 0, NULL);
}
