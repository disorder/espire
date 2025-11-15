#include <stddef.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "config.h"
#include "device.h"
#include "ntp.h"
#include "util.h"
#include "wifi.h"
#include "nv.h"

#include <sys/time.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_mac.h"
#include "esp_netif.h"

#include "esp_log.h"
static const char *TAG = "util";

size_t format_time(time_t time, char *strftime_buf, int size, char *fmt)
{
    struct tm timeinfo;
    // set in ntp module
    //setenv("TZ", NTP_TZ, 1);
    //tzset();
    localtime_r(&time, &timeinfo);
    return strftime(strftime_buf, size, fmt, &timeinfo);
}

char *set_time(char *dt)
{
    struct tm tm = {0};
    char *fmt = "%Y-%m-%dT%H:%M:%S";
    char *ret = strptime(dt, fmt, &tm);
    ESP_LOGW(TAG, "%ssetting time %s: %04d-%02d-%02d %02d:%02d:%02d",
             (ret == NULL) ? "not " : "",
             dt, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);
    if (ret == NULL)
        return ret;

    //ESP_LOGW(TAG, "setting time %s: %d-%d-%d %d:%d:%d",
    //         dt, tm.tm_year, tm.tm_mon, tm.tm_mday,
    //         tm.tm_hour, tm.tm_min, tm.tm_sec);
    time_t t = mktime(&tm);
    struct timeval tv = {t, 0};
    struct timezone *tz = NULL;
    ntp_synced = 0;
    api_synced = t;
    settimeofday(&tv, tz);
    return ret;
}

inline void print_mac(char *mac) {
    ESP_LOGI(TAG, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
}

char mac[6] = {0};
char *get_mac()
{
    esp_efuse_mac_get_default((uint8_t *) &mac);
    ESP_ERROR_CHECK(esp_read_mac((uint8_t *) &mac, ESP_MAC_WIFI_STA));
    return (char *) &mac;
}

void read_mac(char *out, int colons)
{
    get_mac();
    //unsigned char mac_local_base[6] = {0};
    //unsigned char mac_uni_base[6] = {0};
    //esp_derive_local_mac(mac_local_base, mac_uni_base);
    //printf("Local Address: ");
    //print_mac(mac_local_base);
    //printf("\nUni Address: ");
    //print_mac(mac_uni_base);
    //printf("MAC Address: ");
    //print_mac(mac_base);
    if (out != NULL) {
        if (colons)
            snprintf(out, 13+5, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
        else
            snprintf(out, 13, "%02X%02X%02X%02X%02X%02X", mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    } else
        print_mac((char *) &mac);
}

// this is lowlevel function, use honstame_handler from auto
void hostname_set(char *hostname)
{
    esp_netif_t *esp_netif = NULL;
    esp_netif = esp_netif_next(esp_netif);
    if (esp_netif != NULL) {
        ESP_ERROR_CHECK(esp_netif_set_hostname(esp_netif, hostname));
        const char *_hostname = NULL;
        ESP_ERROR_CHECK(esp_netif_get_hostname(esp_netif, &_hostname));
        if (_hostname != NULL)
            ESP_LOGE(TAG, "hostname: %s", _hostname);
    }
}

//static int list_mutex = 0;
//#define LIST_ENTER(x) {while (list_mutex) vTaskDelay(1); list_mutex += 1;}
//#define LIST_EXIT(x) {list_mutex = 0;}
#define LIST_ENTER(x) LIST_ENTER_(&list_mutex, 0)
#define LIST_EXIT(x) LIST_EXIT_(&list_mutex)
// ideally per list
static SemaphoreHandle_t list_mutex = NULL;

static inline int LIST_ENTER_(SemaphoreHandle_t *mutex, int nonblock)
{
    if (*mutex == NULL) {
        *mutex = xSemaphoreCreateMutex();
    }
    return xSemaphoreTake(*mutex, (nonblock)? 0 : portMAX_DELAY) == pdTRUE;
}

static inline void LIST_EXIT_(SemaphoreHandle_t *mutex)
{
    assert(*mutex != NULL);
    xSemaphoreGive(*mutex);
}


inline list_t *list_last(list_t *item)
{
    assert(item != NULL);
    while (item->next != NULL)
        item = item->next;
    return item;
}

inline int list_count(list_t *item)
{
    assert(item != NULL);

    int cnt = 0;
    for (; item->next != NULL; item = item->next, cnt++)
        ;
    return cnt;
}

list_t *list_new(void *data)
{
    assert(data != NULL);
    list_t *new = calloc(1, sizeof(list_t));
    assert(new != NULL);
    new->data = data;
    return new;
}

list_t *list_append(list_t *list, void *data)
{
    assert(list != NULL);
    list_t *new = list_new(data);
    LIST_ENTER(list);
    list_t *last = list_last(list);
    last->next = new;
    LIST_EXIT(list);
    return new;
}

list_t *list_prepend(list_t *list, void *data)
{
    assert(list != NULL);
    list_t *new = list_new(data);
    LIST_ENTER(list);
    new->next = list->next;
    list->next = new;
    LIST_EXIT(list);
    return new;
}

int list_remove(list_t *list, list_t *find)
{
    assert(list != NULL);
    assert(find != NULL);
    LIST_ENTER(list);
    list_t *item = list;
    while (item->next != NULL) {
        if (item->next == find) {
            item->next = find->next;
            LIST_EXIT(list);
            free(find);
            return 1;
        }
        item = item->next;
    }
    LIST_EXIT(list);
    return 0;
}

// for iterating list in while
inline list_t *list_iter(list_t *list)
{
    assert(list != NULL);
    return list->next;
}

list_t *list_find(list_t *list, void *data)
{
    assert(list != NULL);
    list_t *item = list;
    while (item->next != NULL) {
        if (item->next->data == data)
            return item->next;
        item = item->next;
    }
    return NULL;
}

// for use by opaque iterator (iterator is list)
// inline = linker error
list_t *iter_next_list(list_t *iter, void **pdata)
{
    assert(iter != NULL);
    assert(pdata != NULL);
    if (iter->next != NULL) {
        *pdata = iter->next->data;
        return iter->next;
    }

    *pdata = NULL;
    return NULL;
}

// opaque iterator from list
inline iter_t iter_next(iter_t iter, void **data)
{
    return iter_next_list((list_t *) iter, data);
}

// for opaque lists it can be counted from iterator without iterating
inline int iter_count(iter_t iter)
{
    assert(iter != NULL);
    return list_count(iter);
}

inline array_t *array_new()
{
    array_t *new = calloc(1, sizeof(array_t));
    assert(new != NULL);
    return new;
}

inline void array_append(array_t *arr, void *item)
{
    assert(arr != NULL);
    if (arr->count+1 > arr->max) {
        ESP_LOGW(TAG, "growing array from %d", arr->max);
        arr->data = reallocarray(arr->data, arr->max+10, sizeof(void*));
        assert(arr->data != NULL);
        arr->max += 10;
    }
    arr->data[arr->count++] = item;
}

inline void array_remove(array_t *arr, int i)
{
    assert(arr != NULL);
    assert(i < arr->count);
    assert(i >= 0);
    memmove(arr->data[i], arr->data[i+1], (arr->count - i) * sizeof(void*));
    arr->count -= 1;
}

list_t tasks = {0};

void xvTaskDelete(task_t *data)
{
//    return vTaskDelete(data);

    list_t *item = NULL;
    if (data == NULL) {
        TaskHandle_t task = xTaskGetCurrentTaskHandle();
        list_t *iter = &tasks;
        while ((iter = list_iter(iter)) != NULL) {
            if (LIST(task_t, iter, task) == task) {
                item = iter;
                data = iter->data;
                break;
            }
        }
    } else {
        item = list_find(&tasks, data);
    }
    assert(item != NULL);
    assert(data != NULL);
    assert(data->task != NULL);

    ESP_LOGE(TAG, "-%s %" PRIx32, data->name, (uint32_t) data->task);
    assert(list_remove(&tasks, item) != 0);
    TaskHandle_t task = data->task;
    free(data);

    vTaskDelete(task);
}

BaseType_t xxTaskCreate(    TaskFunction_t pvTaskCode,
                            const char * const pcName,
                            configSTACK_DEPTH_TYPE usStackDepth,
                            void *pvParameters,
                            UBaseType_t uxPriority,
                            task_t **pxCreatedTask)
{
//    return xTaskCreate(pvTaskCode, pcName,
//                       8192,//usStackDepth,
//                       pvParameters, uxPriority, pxCreatedTask);

    task_t *data = calloc(1, sizeof(task_t));
    assert(data != NULL);
    // with network logging we should have at least 3*1024 everywhere
    assert(usStackDepth >= 2*1024);
    if (usStackDepth < 3*1024)
        usStackDepth = 3*1024;
    BaseType_t res = xTaskCreate(pvTaskCode,
                                 pcName,
                                 usStackDepth,
                                 pvParameters,
                                 uxPriority,
                                 &data->task);
    // TODO seen this fail with owm after boot
    if (data->task == NULL) {
        ESP_LOGE(TAG, "heap: %" PRIu32, esp_get_free_heap_size());
        multi_heap_info_t heap = {0};
        heap_caps_get_info(&heap, MALLOC_CAP_8BIT);
        ESP_LOGE(TAG, "heap largest: %u", heap.largest_free_block);
    }
    assert(data->task != NULL);
    strncpy(data->name, pcName, sizeof(data->name)-1);
    list_prepend(&tasks, data);
    ESP_LOGE(TAG, "+%s %" PRIu32, data->name, (uint32_t) data->task);
    if (pxCreatedTask != NULL)
        *pxCreatedTask = data;
    return res;
}

// it's list_t* but keep iter type opaque
inline iter_t task_iter()
{
    return (iter_t) &tasks;
}

inline iter_t task_next(iter_t iter, task_t **data)
{
    return iter_next(iter, (void**) data);
}

// buf and out need to be both padded size
int aes_cbc(int enc, mbedtls_aes_context *aes, char *iv, int len, char *buf, int bufsiz, char *out) {
    assert(bufsiz == AES_PADDED_SIZE(len));

    if (enc) {
        uint8_t pkc5_value = (17 - (bufsiz % 16));
        for (int i = len; i < bufsiz; i++) {
            buf[i] = pkc5_value;
        }
    }

    int ret = mbedtls_aes_crypt_cbc(aes, enc? MBEDTLS_AES_ENCRYPT : MBEDTLS_AES_DECRYPT, bufsiz,
                                    (uint8_t *) iv,
                                    (uint8_t *) buf,
                                    (uint8_t *) out);

    //ESP_LOG_BUFFER_HEX("cbc_encrypt", out, bufsiz);
    return ret;
}

#include "mbedtls/base64.h"

/*
char *b64_encode(char *data, size_t len)
{
    size_t enclen = B64_ENC_LEN(len);
    uint8_t *enc = malloc(enclen+1);
    assert(enc != NULL);
    mbedtls_base64_encode(enc, enclen, &enclen, (uint8_t *) data, len);
    enc[enclen] = '\0';
    return enclen;
}
*/

int b64_decode(char *data, size_t len, char *dec, size_t declen)
{
    assert(dec != NULL);
    int k = 0;
    if (data[len-1] == '=') {
        for (k=1; k<len-1; k++) {
            assert(k < 3);
            if (data[len-1-k] != '=')
                break;
        }
    }
    assert(declen >= B64_DEC_LEN(len)-k);
    assert(dec != NULL);
    mbedtls_base64_decode((uint8_t *) dec, declen, &declen, (uint8_t *) data, len);
    return declen;
}

inline int init_sa(char *ip, int port, struct sockaddr_in *sa)
{
    if (port < 0 || port >= 65536) {
        ESP_LOGW(TAG, "invalid port: %d", port);
        return 0;
    }

    if (inet_aton(ip, &sa->sin_addr.s_addr) == 0) {
        ESP_LOGW(TAG, "invalid IP: %s", ip);
        if ((sa->sin_addr.s_addr = resolve_hostname(ip)) == INADDR_ANY) {
            ESP_LOGW(TAG, "could not resolve hostname: %s", ip);
            return 0;
        }
    }

    sa->sin_port = htons(port);
    sa->sin_family = AF_INET;
    return 1;
}

#include <netdb.h>
uint32_t resolve_hostname(char *host)
{
    struct hostent *phe;
    struct hostent he;
    char tmpbuf[100];
    int i, herr;
    if (((i = gethostbyname_r(host, &he, tmpbuf, sizeof(tmpbuf), &phe, &herr) ) != 0 ) || (phe == NULL)) {
        ESP_LOGI(TAG, "gethostbyname_r failed: %s", strerror(herr));
        return INADDR_ANY;
    } else {
        return ((struct in_addr*) phe->h_addr)->s_addr;
    }
}

void wall_clock_wait(int s, TickType_t wait)
{
    // TODO ticks seem to work with auto sleep?
    if (esp.pm) {
        _vTaskDelay(S_TO_TICK(s));
        return;
    }
    time_t end;
    time(&end);
    end += s;

    time_t now;
    while (1) {
        time(&now);
        if (now >= end)
            break;
        _vTaskDelay(wait);
    }
}

void wall_clock_wait_until_ms(uint32_t ms, TickType_t wait)
{
    struct timeval tv_end;
    gettimeofday(&tv_end, NULL);
    tv_end.tv_sec += ms/1000;
    tv_end.tv_usec += (ms % 1000) * 1000;

    struct timeval tv_now;
    while (1) {
        gettimeofday(&tv_now, NULL);
        if (tv_now.tv_sec >= tv_end.tv_sec && tv_now.tv_sec >= tv_end.tv_sec)
            break;
        _vTaskDelay(wait);
    }
}

void wall_clock_wait_until(struct timeval tv_end, TickType_t wait)
{
    struct timeval tv_now;
    while (1) {
        gettimeofday(&tv_now, NULL);
        if (tv_now.tv_sec >= tv_end.tv_sec && tv_now.tv_sec >= tv_end.tv_sec)
            break;

        _vTaskDelay(wait);
    }
}

struct sockaddr_in GRAPHITE_SA = {0};
static int g_sock_udp = -1;

int graphite_init(char *host, int port)
{
    const char *TAG = "graphite";
    int ret = 0;
    uint16_t nv;

    if (port == 0) {
        port = GRAPHITE_UDP_PORT_DEFAULT;
        if (nv_read_u16("graphite.port", &nv) == ESP_OK)
            if (port != nv) {
                port = nv;
                ret = 1;
            }
    }

    if (host == NULL) {
        size_t size;
        if (nv_read_str("graphite.ip", &host, &size) != ESP_OK) {
            ret = init_sa(GRAPHITE_IP_DEFAULT, port, &GRAPHITE_SA);
        } else {
            ret = init_sa(host, port, &GRAPHITE_SA);
            free(host);
            host = NULL;
        }
    } else {
        ret = init_sa(host, port, &GRAPHITE_SA);
    }

    if (ret != 0 && g_sock_udp != -1) {
        close(g_sock_udp);
        g_sock_udp = -1;
        xSemaphoreGive(esp.sockets);
    }

    ESP_LOGI(TAG, "init: %s:%d: %d", (host)? host : GRAPHITE_IP_DEFAULT, port, ret);
    return ret;
}

int graphite_udp(char *prefix, char *metric, char *tag, float val, int now, time_t ts)
{
    const char *TAG = "graphite";
    int ret = 0;
    //WIFI_ADD(xTaskGetCurrentTaskHandle());

    if (g_sock_udp == -1) {
        graphite_init(NULL, 0);
        // don't block
        if (xSemaphoreTake(esp.sockets, 0/*S_TO_TICK(1)*/) == pdFALSE) {
            ret = 4;
            ESP_LOGE(TAG, "no sockets available");
            goto CLEANUP;
        }
        if ((g_sock_udp = socket(PF_INET, SOCK_DGRAM, 0)) == -1) {
            ESP_LOGE(TAG, "socket: %s", strerror(errno));
            xSemaphoreGive(esp.sockets);
            ret = 1;
            goto CLEANUP;
        }
    }

    char buf[64];
    int len;
    if (!now && (ntp_synced || api_synced)) {
        time_t now = ts;
        if (!now)
            time(&now);
        len = snprintf(buf, sizeof(buf), "%s%s;tag1=%s %.2f %jd\n", prefix, metric, tag, val, now);
    } else
        len = snprintf(buf, sizeof(buf), "%s%s;tag1=%s %.2f\n", prefix, metric, tag, val);

    if (len < 0) {
        ESP_LOGE(TAG, "failed to format graphite data: %s%s, %s, %.2f, %d, %jd", prefix, metric, tag, val, now, ts);
        ret = 2;
        goto CLEANUP;
    }

    ESP_LOGI(TAG, "%s", buf);
    ssize_t s = sendto(g_sock_udp, &buf, len,
                       MSG_DONTWAIT, (struct sockaddr *) &GRAPHITE_SA, sizeof(GRAPHITE_SA));
    if (s == -1) {
        ESP_LOGE(TAG, "graphite errno %s", strerror(errno));
        ret = 3;
        goto CLEANUP;
    }

CLEANUP:
    //WIFI_DEL(xTaskGetCurrentTaskHandle());
    return ret;
}
