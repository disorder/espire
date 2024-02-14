#ifndef __HTTP_H__
#define __HTTP_H__

#include "module.h"
#include "esp_http_client.h"

// used by ota too
void https_enter(int exclusive);
void https_leave(int exclusive);

typedef struct http_request http_request_t;
struct http_request
{
    char *url;
    char *cert_pem;
    bool skip_cert_common_name_check;
    char *username;
    char *password;
    esp_http_client_auth_type_t auth_type;
    esp_http_client_handle_t client;
    task_t *task;
    void (*callback)(http_request_t *req, int success);
    void (*callback_stream)(http_request_t *req);
    int exclusive;
    // event handler actually does not stop on ESP_FAIL
    int remaining;
    // pad=1 will allocate more bytes and end with '\0'
    // useful for debugging
    int pad;
    char *buf;
    size_t bufsize;
    // allocated size for chunked responses
    size_t bufdyn;
    // this is useful if you're interested in part of longer response
    size_t bufstatic;

    // custom data
    void *data;
};

//void http_init();
void https_get(http_request_t *req);

#endif /* __HTTP_H__ */
