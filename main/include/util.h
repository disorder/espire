#ifndef __UTIL_H__
#define __UTIL_H__
//#define MS_TO_TICK(x) ((x)/portTICK_PERIOD_MS)
#define MS_TO_TICK(x) pdMS_TO_TICKS((x))
//#define TICK_TO_MS(x) ((x)*portTICK_PERIOD_MS)
#define TICK_TO_MS(x) pdTICKS_TO_MS((x))
//#define pdTICKS_TO_MS( xTicks )   ( ( TickType_t ) ( ( uint64_t ) ( xTicks ) * 1000 / configTICK_RATE_HZ ) )
#define S_TO_TICK(x) MS_TO_TICK((x)*1000)
// integer
//#define TICK_TO_S(x) (TICK_TO_MS(x)/1000)

#define COUNT_OF(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))
#define container_of(ptr, type, member)                                        \
  ({                                                                           \
    const typeof(((type *)0)->member) *__mptr = (ptr);                         \
    (type *)((char *)__mptr - offsetof(type, member));                         \
  })
#define member_size(type, member) sizeof(((type *)0)->member)

#define STR_(x) #x
#define STR(x) STR_(x)

#define LIST(type, x, field) ((type *) x->data)->field
#define ITEM_IS(item, ptr) (item->data == (void *) ptr)
typedef struct list list_t;
struct list
{
    void *data;
    list_t *next;
};

int list_count(list_t *item);
list_t *list_last(list_t *item);
list_t *list_new(void *data);
list_t *list_append(list_t *list, void *data);
list_t *list_prepend(list_t *list, void *data);
int list_remove(list_t *list, list_t *item);
list_t *list_iter(list_t *list);
list_t *list_find(list_t *list, void *data);

// for use by opaque iterator
typedef void *iter_t;
iter_t iter_next(iter_t iter, void **data);

typedef struct
{
    int count;
    int max;
    void **data;
} array_t;

array_t *array_new();
void array_append(array_t *arr, void *item);
void array_remove(array_t *arr, int i);

void print_mac(char *mac);
void read_mac(char *mac, int colons);
extern char mac[6];
char *get_mac();
void hostname_set(char *hostname);

#include <time.h>
size_t format_time(time_t time, char *strftime_buf, int size, char *fmt);
char *set_time(char *dt);

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

typedef struct
{
    char name[20];
    TaskHandle_t task;
} task_t;

extern list_t tasks;
iter_t task_iter();
iter_t task_next(iter_t iter, task_t **data);
void xvTaskDelete(task_t *task);
BaseType_t xxTaskCreate(TaskFunction_t pvTaskCode, const char *const pcName,
                        configSTACK_DEPTH_TYPE usStackDepth, void *pvParameters,
                        UBaseType_t uxPriority, task_t **pxCreatedTask);

//#define xxTaskCreate(args...) xTaskCreate(args)
//#define xvTaskDelete(task) vTaskDelete(task)

#include "mbedtls/aes.h"

#define AES_PADDED_SIZE(len) ((len < 16) ? 16 : (((len-1) / 16 + 1) * 16))
void aes_init(mbedtls_aes_context *aes, char *aes_key, int len);
int aes_cbc(int enc, mbedtls_aes_context *aes, char *iv, int len, char *buf, int bufsiz, char *out);

#define B64_ENC_LEN(n) (((4 * n / 3) + 3) & ~3)
#define B64_DEC_LEN(n) (3*n/4)
int b64_decode(char *data, size_t len, char *dec, size_t declen);

#include <sys/socket.h>
int init_sa(char *ip, int port, struct sockaddr_in *sa);
uint32_t resolve_hostname(char *host);

void wall_clock_wait(int s, TickType_t wait);
void wall_clock_wait_until_ms(uint32_t ms, TickType_t wait);
void wall_clock_wait_until(struct timeval tv_end, TickType_t wait);

#define _vTaskDelay(x) vTaskDelay(x)
#define __vTaskDelay(x) {                                                \
        vTaskDelay(x);                                                  \
        ESP_LOGW(TAG, "delay %dms ended %s", TICK_TO_MS(x), __func__);  \
    }

#endif /* __UTIL_H__ */
