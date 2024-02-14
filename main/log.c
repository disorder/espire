#include "config.h"
#include "device.h"
#include "log.h"
#include "wifi.h"
#include "util.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>

#include "esp_log.h"
static const char *TAG = "log";

// ideally it can withstand boot until wifi is connected without realloc
#define LOG_MAX (32*1024)
#define LOG_SIZE (16*1024)
#define LOG_GROW_ADD 1024
// enough for steady connection
#define LOG_SHRINK_SIZE 1024
// both smaller possibility of loss and smaller possible loss
#define CHUNK 1024

//vprintf_like_t *log_orig;
int (*log_orig)(const char *fmt, va_list vargs) = NULL;

static int log_size = 0;
static char *log_buf = NULL;
static char *log_ptr = NULL;

typedef struct {
    char *ip;
    int port;
    struct sockaddr_in sa;
} log_info_t;

static log_info_t default_targets[] = {
#if defined(LOG_IP) && defined(LOG_PORT)
    {
        .ip = LOG_IP,
        .port = LOG_PORT,
    },
#endif
#if defined(LOG_IP2) && defined(LOG_PORT2)
    {
        .ip = LOG_IP2,
        .port = LOG_PORT2,
    },
#endif
};

list_t log_targets = {0};

static SemaphoreHandle_t mutex = NULL;
static int LOG_ENTER(int nonblock)
{
    if (mutex == NULL) {
        mutex = xSemaphoreCreateMutex();
        assert(mutex != NULL);
    }

    return xSemaphoreTake(mutex, (nonblock)? 0 : portMAX_DELAY) == pdTRUE;
}

static void LOG_EXIT()
{
    xSemaphoreGive(mutex);
}

static inline int log_socket()
{
    static int sock = -1;
    if (sock >= 0)
        return sock;

    /*
    if (inet_aton(log_ip, &sa.sin_addr.s_addr) == 0) {
        return -1;
    }
    sa.sin_port = htons(log_port);
    sa.sin_family = AF_INET;
    */

    int ret;
    // SOCK_STREAM unnecessary and this works for multiple targets
    xSemaphoreTake(esp.sockets, portMAX_DELAY);
    if ((ret = socket(PF_INET, SOCK_DGRAM, 0)) == -1) {
        int e = errno;
        log_local("socket() %d: %s\n", e, strerror(e));
        xSemaphoreGive(esp.sockets);
        return -2;
    }

    /*
    int flags = fcntl(ret, F_GETFL, 0);
    assert(flags != -1);
    assert(fcntl(ret, F_SETFL, flags | O_NONBLOCK) == 0);

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 10 * 1000;
    setsockopt(ret, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    setsockopt(ret, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    if (connect(ret, (struct sockaddr *) &sa, sizeof(sa)) != 0) {
        assert(close(ret) == 0);
        return -3;
    }
    */

    sock = ret;
    return sock;
}

static void log_task(void *pvParameter)
{
    while (1) {
        int sock = log_socket();
        if (sock >= 0 && log_ptr > log_buf && wifi_connected) {
            char *log_send = NULL;
            int size = log_ptr-log_buf;

            if (list_count(&log_targets) == 0)
                goto SKIP;

            LOG_ENTER(0);
            log_send = malloc(size);
            if (log_send != NULL) {
                memcpy(log_send, log_buf, size);
                log_ptr = log_buf;
            }
            LOG_EXIT();

        SKIP:
            // shrink when connected
            int new_size = LOG_SHRINK_SIZE;
            if (log_size > new_size) {
                LOG_ENTER(0);
                char *shrunk = realloc(log_buf, new_size);
                if (shrunk != NULL) {
                    log_size = new_size;
                    log_buf = shrunk;
                    log_ptr = log_buf;
                }
                LOG_EXIT();
                if (log_buf == shrunk)
                    ESP_LOGW(TAG, "buffer shrunk to %d", new_size);
            }

            // this call could hang when disconnected
            if (log_send != NULL) {
                list_t *item = &log_targets;
                while ((item = list_iter(item)) != NULL) {
                    log_info_t *l = (log_info_t *) item->data;
                    if (l->sa.sin_addr.s_addr == 0) {
                        assert(l->ip != NULL);
                        // it's probably a hostname
                        if (!wifi_connected || init_sa(l->ip, l->port, &l->sa) == 0)
                            continue;
                    }
                    /*
                    sendto(sock, log_send, size, MSG_DONTWAIT,
                           (struct sockaddr *) &LIST(log_info_t, item, sa),
                           sizeof(LIST(log_info_t, item, sa)));
                    */
                    for (int i=0; i<(size/CHUNK)+1; i++)
                        sendto(sock, log_send + i*CHUNK,
                               ((i+1)*CHUNK > size)? size % CHUNK : CHUNK,
                               MSG_DONTWAIT,
                               (struct sockaddr *) &LIST(log_info_t, item, sa),
                               sizeof(LIST(log_info_t, item, sa)));
                    // whatever could get out, everything else is lost now
                }
                free(log_send);
            }
        }

        //_vTaskDelay(MS_TO_TICK(100));
        // TODO increased for sleep
        _vTaskDelay(MS_TO_TICK(1000));
    }
}

int log_inet(const char *fmt, va_list vargs)
{
    log_orig(fmt, vargs);

    LOG_ENTER(0);
    int ret = vsnprintf(log_ptr, (log_buf+log_size) - log_ptr, fmt, vargs);
    if (ret > 0 && log_ptr+ret < log_buf+log_size - 100)
        log_ptr += ret;
    else if (log_size >= LOG_MAX) {
        // that's too much
    } else {
        // grow
        int new_size = log_size + LOG_GROW_ADD;
        char *grown = realloc(log_buf, new_size);
        if (grown != NULL) {
            log_local("log buffer(%d) grown to %d\n", log_size, new_size);
            log_ptr = grown + (log_ptr - log_buf);
            log_buf = grown;
            log_size = new_size;
        } else {
            LOG_EXIT();
            log_local("log buffer(%d) failed to increase\n", log_size);
            return -1;
        }

        // one more attempt, if it's longer than 1024 then you deserve it
        ret = vsnprintf(log_ptr, (log_buf+log_size) - log_ptr, fmt, vargs);
        if (ret > 0)
            log_ptr += ret;

        if (log_size >= LOG_MAX)
            log_local("log buffer(%d) limited to %d\n", log_size, LOG_MAX);
    }

    LOG_EXIT();
    return ret;
}

int log_local(char *fmt, ...)
{
    assert(log_orig != NULL);
    va_list args;
    va_start(args, fmt);
    int ret = log_orig(fmt, args);
    va_end(args);
    return ret;
}

int log_add(char *ip, int port)
{
    log_info_t *t = calloc(1, sizeof(log_info_t));
    assert(t != NULL);

    if (!init_sa(ip, port, &t->sa))
        return 0;

    // this is used only in init_sa, don't save - only in hardcoded entries
    //t->ip = strdup(ip);
    //assert(t->ip != NULL);
    //t->port = port;
    list_prepend(&log_targets, t);
    return 1;
}

void log_init()
{
    if (log_orig != NULL)
        return;
    log_orig = esp_log_set_vprintf(log_inet);

    for (int i=0; i<COUNT_OF(default_targets); i++) {
        log_info_t *t = &default_targets[i];

        // resolve later if needed
        init_sa(t->ip, t->port, &t->sa);

        list_prepend(&log_targets, t);
        ESP_LOGI(TAG, "logging to %s:%d", t->ip, t->port);
    }

    // we need more at boot and then we can shrink
    log_size = LOG_SIZE;
    log_buf = malloc(log_size);
    log_ptr = log_buf;
    assert(log_buf != NULL);

    xxTaskCreate((void (*)(void*))log_task, "log_task", 5*1024, NULL, 0, NULL);
}
