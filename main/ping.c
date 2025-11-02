#include "config.h"
#include "device.h"
#include "module.h"
#include "ping.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "ping/ping_sock.h"
#include "esp_event.h"
#include "esp_log.h"
static const char *TAG = "ping";

ping_t ping_online = {0};
int ping_timeout = 0;

// based on https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/icmp_echo.html

#ifdef PING_TEST
static void test_on_ping_success(esp_ping_handle_t hdl, ping_t *ping)
{
    // optionally, get callback arguments
    // const char* str = (const char*) args;
    // printf("%s\r\n", str); // "foo"
    uint8_t ttl;
    uint16_t seqno;
    uint32_t elapsed_time, recv_len;
    ip_addr_t target_addr;
    esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TTL, &ttl, sizeof(ttl));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
    esp_ping_get_profile(hdl, ESP_PING_PROF_SIZE, &recv_len, sizeof(recv_len));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed_time, sizeof(elapsed_time));
    ESP_LOGI(TAG, "%d bytes from %s icmp_seq=%d ttl=%d time=%d ms\n",
           recv_len, inet_ntoa(target_addr.u_addr.ip4), seqno, ttl, elapsed_time);
}

static void test_on_ping_timeout(esp_ping_handle_t hdl, void *args)
{
    uint16_t seqno;
    ip_addr_t target_addr;
    esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
    ESP_LOGI(TAG, "From %s icmp_seq=%d timeout\n", inet_ntoa(target_addr.u_addr.ip4), seqno);
}

static void test_on_ping_end(esp_ping_handle_t hdl, void *args)
{
    uint32_t transmitted;
    uint32_t received;
    uint32_t total_time_ms;

    esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST, &transmitted, sizeof(transmitted));
    esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &received, sizeof(received));
    esp_ping_get_profile(hdl, ESP_PING_PROF_DURATION, &total_time_ms, sizeof(total_time_ms));
    ESP_LOGI(TAG, "%d packets transmitted, %d received, time %dms\n", transmitted, received, total_time_ms);
}
#endif

static void on_ping_success(esp_ping_handle_t hdl, ping_t *ping)
{
    ESP_LOGI(TAG, "success");
#ifdef PING_TEST
    test_on_ping_success(hdl, ping);
#endif
    ping->connected = 1;
    time(&ping->last);
    ping->timeout = 0;
    module_offline(ping->timeout);
    return;
}

static void on_ping_timeout(esp_ping_handle_t hdl, ping_t *ping)
{
    ESP_LOGI(TAG, "timeout");
#ifdef PING_TEST
    test_on_ping_timeout(hdl, ping);
#endif
    ping->connected = 0;
    ping->timeout += 1;
    module_offline(ping->timeout);
    return;
}

static void on_ping_end(esp_ping_handle_t hdl, ping_t *ping)
{
    ESP_LOGI(TAG, "end");
#ifdef PING_TEST
    test_on_ping_end(hdl, ping);
#endif
    ping->connected = -1;
    ping->timeout = 0;
    module_offline(0);
    return;
}

void ping_add(ping_t *ping, char *addr)
{
    assert(ping != NULL);
    assert(addr != NULL);
    ping->module.name = TAG;
    ping->module.type = M_PING;
    ping->module.run = (void (*)(void*,int)) ping_run;

    /* convert URL to IP address */
    ip_addr_t target_addr = {0};
    //struct addrinfo hint;
    //struct addrinfo *res = NULL;
    //memset(&hint, 0, sizeof(hint));
    //memset(&target_addr, 0, sizeof(target_addr));
    //getaddrinfo("www.espressif.com", NULL, &hint, &res);
    //struct in_addr addr4 = ((struct sockaddr_in *) (res->ai_addr))->sin_addr;
    //inet_addr_to_ip4addr(ip_2_ip4(&target_addr), &addr4);
    //freeaddrinfo(res);
    ipaddr_aton(addr, &target_addr);

    esp_ping_config_t ping_config = ESP_PING_DEFAULT_CONFIG();
    ping_config.target_addr = target_addr;          // target IP address
    ping_config.count = ESP_PING_COUNT_INFINITE;    // ping in infinite mode, esp_ping_stop can stop it
    ping_config.interval_ms = 1000;
    ping_config.task_stack_size = 3*1024;
    memcpy(&ping->ping_config, &ping_config, sizeof(ping->ping_config));

    /* set callback functions */
    ping->cbs.on_ping_success = (void (*)(void*,void*)) on_ping_success;
    ping->cbs.on_ping_timeout = (void (*)(void*,void*)) on_ping_timeout;
    ping->cbs.on_ping_end = (void (*)(void*,void*)) on_ping_end;
    ping->cbs.cb_args = ping;

    esp_ping_new_session(&ping->ping_config, &ping->cbs, &ping->ping);
    ESP_LOGW(TAG, "%s", addr);
    module_add(&ping->module);
}

void ping_run(ping_t *self, int run)
{
    assert(self != NULL);
    if (run) {
        if (self->module.state == 0) {
            if (xSemaphoreTake(esp.sockets, S_TO_TICK(2)) != pdTRUE)
                return;
            esp_ping_start(self->ping);
            self->module.state = 1;
        }
    } else {
        if (self->module.state != 0) {
            esp_ping_stop(self->ping);
            xSemaphoreGive(esp.sockets);
            self->connected = -1;
            self->module.state = 0;
        }
    }
}

void ping_online_init(char *addr)
{
    ping_add(&ping_online, addr);
    ping_online.module.run(&ping_online, 1);
}
