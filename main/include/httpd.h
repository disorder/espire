#ifndef __HTTPD_H__
#define __HTTPD_H__

#include "config.h"
#ifdef HTTPD_SSL
#include "esp_https_server.h"
#else
#include "esp_http_server.h"
#endif
#include "util.h"

typedef struct
{
    module_t module;
    httpd_handle_t server;
#ifdef HTTPD_SSL
    httpd_ssl_config_t config;
#else
    httpd_config_t config;
#endif
    list_t handlers;
} httpd_t;

httpd_t *httpd_new(int port);
void httpd_run(httpd_t *self, int run);
list_t *httpd_register(httpd_t *self, httpd_uri_t *uri);
void httpd_unregister(httpd_t *self, list_t *item);

// \0 terminated string only
#define http_snprintf(REQ, BUF, ...) httpd_resp_send_chunk(REQ, (BUF[0]='\0', snprintf(BUF, sizeof(BUF), __VA_ARGS__), BUF), HTTPD_RESP_USE_STRLEN)
#define http_write(REQ, BUF, LEN) httpd_resp_send_chunk(REQ, BUF, LEN)

esp_err_t http_printf(httpd_req_t *req, const char *format, ...);

#endif /* __HTTPD_H__ */
