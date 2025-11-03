#include "device.h"
#include "esp_system.h"
#include "http.h"
#include "wifi.h"
#include "check.h"

/* based on ESP HTTP Client Example (Public Domain or CC0 licensed) */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/atomic.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
//#include "protocol_examples_common.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"

#include "esp_log.h"
static const char *TAG = "http";

#define EVT_REQ       (((http_request_t *) evt->user_data))
#define EVT_PAD       (((http_request_t *) evt->user_data)->pad)
#define EVT_BUF       (((http_request_t *) evt->user_data)->buf)
#define EVT_BUFSIZE   (((http_request_t *) evt->user_data)->bufsize)
#define EVT_BUFDYN    (((http_request_t *) evt->user_data)->bufdyn)
#define EVT_BUFSTATIC (((http_request_t *) evt->user_data)->bufstatic)
#define EVT_STREAM    (((http_request_t *) evt->user_data)->callback_stream)
#define EVT_REMAINING (((http_request_t *) evt->user_data)->remaining)
esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    //static char *output_buffer;  // Buffer to store response of http request from event handler
    //static int output_len;       // Stores number of bytes read

    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);

            if (EVT_REQ->auth_type == HTTP_AUTH_TYPE_DIGEST) {
                // this is not data
            } else if (EVT_STREAM != NULL) {
                EVT_BUF = evt->data;
                EVT_BUFSIZE = evt->data_len;
                EVT_STREAM((http_request_t *) evt->user_data);
            } else if (EVT_BUFSTATIC == 0) {
                int chunked = esp_http_client_is_chunked_response(evt->client);
                if (EVT_BUF == NULL) {
                    int len = esp_http_client_get_content_length(evt->client);

                    if (chunked) {
                        // will reallocate for more
                        len = (evt->data_len > 1024-EVT_PAD)? evt->data_len : (1024-EVT_PAD);
                        EVT_BUFDYN = len;
                        ESP_LOGD(TAG, "Allocating %d", EVT_BUFDYN);
                    } else {
                        ESP_LOGD(TAG, "Allocating %d", len);
                        EVT_REMAINING = len;
                    }
                    EVT_BUF = (char *) malloc(len + EVT_PAD);
                    EVT_BUFSIZE = 0;
                    if (EVT_BUF == NULL) {
                        ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
                        EVT_REMAINING += evt->data_len;
                        return ESP_FAIL;
                    }
                } else if (EVT_BUFDYN > 0 && evt->data_len > EVT_BUFDYN-EVT_BUFSIZE) {
                    EVT_BUFDYN += (evt->data_len > 1024)? evt->data_len : 1024;
                    ESP_LOGD(TAG, "Growing to %d", EVT_BUFDYN);
                    char *new = realloc(EVT_BUF, EVT_BUFDYN+EVT_PAD);
                    if (new == NULL) {
                        ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
                        EVT_REMAINING += evt->data_len - EVT_BUFDYN-EVT_BUFSIZE;
                        return ESP_FAIL;
                    }
                    EVT_BUF = new;
                }
                assert(chunked == 0 || EVT_BUFDYN >= evt->data_len + EVT_BUFSIZE);
                memcpy(EVT_BUF + EVT_BUFSIZE, evt->data, evt->data_len);
                EVT_BUFSIZE += evt->data_len;
                // we can't count remaining with chunked
                if (EVT_BUFDYN == 0)
                    EVT_REMAINING -= evt->data_len;
            } else {
                int len = EVT_BUFSTATIC - EVT_BUFSIZE;
                if (len > evt->data_len)
                    len = evt->data_len;

                memcpy(EVT_BUF + EVT_BUFSIZE, evt->data, len);
                EVT_BUFSIZE += len;

                if (len < evt->data_len) {
                    EVT_REMAINING += evt->data_len - len;
                    return ESP_FAIL;
                }
            }

            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            if (EVT_STREAM != NULL) {
                EVT_BUF = NULL;
                EVT_BUFSIZE = 0;
            }
            if (EVT_BUFDYN) {
                // shrink
                char *new = realloc(EVT_BUF, EVT_BUFSIZE + EVT_PAD);
                if (new != NULL) {
                    EVT_BUF = new;
                    EVT_BUFDYN = EVT_BUFSIZE + EVT_PAD;
                }
            }
            if (EVT_BUF != NULL && EVT_PAD) {
                EVT_BUF[EVT_BUFSIZE] = '\0';
            }
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            if (EVT_REQ->auth_type == HTTP_AUTH_TYPE_DIGEST) {
                // real data follows
                EVT_REQ->auth_type = 0;
            }

            int mbedtls_err = 0;
            esp_err_t err = esp_tls_get_and_clear_last_error((esp_tls_error_handle_t)evt->data, &mbedtls_err, NULL);
            if (err != 0) {
                ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
                ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            }
            return ESP_FAIL;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
            break;
    }
    return ESP_OK;
}

// this is required because https requests (ota, auto) can starve OWM (cJSON)
// can't use mutex, that would require creating task first (with stack)
static uint32_t occupied = 0;
static uint32_t exclusive = 0;

inline void https_enter(int excl)
{
    if (excl) {
        while (1) {
            if (occupied == 0 && Atomic_CompareAndSwap_u32(&exclusive, 1, 0) == 1)
                return;
            _vTaskDelay(MS_TO_TICK(100));
        }
    } else  {
        while (1) {
            if (Atomic_CompareAndSwap_u32(&exclusive, 1, 0) == 1) {
                Atomic_Increment_u32(&occupied);
                // exclusive was not set
                exclusive = 0;
                return;
            }
            _vTaskDelay(MS_TO_TICK(100));
        }
    }
 }

inline void https_leave(int excl)
{
    if (excl)
        exclusive = 0;
    else
        Atomic_Decrement_u32(&occupied);
}

// callback needs to call free(req->buf) if not NULL (not callback_stream)
void https_get_task(http_request_t *req)
{
    xSemaphoreTake(esp.sockets, portMAX_DELAY);
    WIFI_ADD(xTaskGetCurrentTaskHandle());

    esp_http_client_config_t config = {
        .url = req->url,
        .event_handler = _http_event_handler,
        .cert_pem = req->cert_pem,
        .skip_cert_common_name_check = req->skip_cert_common_name_check,
        .timeout_ms = 5000,
        .user_data = req,
        .username = req->username,
        .password = req->password,
        .auth_type = req->auth_type,
    };
    if (!config.skip_cert_common_name_check && !config.cert_pem)
        config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTPS Status = %d, content_length = %lld",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
        //assert(esp_http_client_get_content_length(client) == req->bufsize);
    } else {
        ESP_LOGE(TAG, "Error perform http request %s", esp_err_to_name(err));
    }

    xSemaphoreGive(esp.sockets);

    req->client = client;
    if (req->callback)
        req->callback(req, err == ESP_OK && req->remaining == 0);
    WIFI_DEL(xTaskGetCurrentTaskHandle());
    esp_http_client_cleanup(client);

    https_leave(req->exclusive);
    //xvTaskDelete(req->task);
    xvTaskDelete(NULL);
}

void https_get(http_request_t *req)
{
    ESP_LOGI(TAG, "https_get %s", req->url);
    assert(req->callback || req->callback_stream);

    // TODO this can probably leave wifi_count +1
    // based on configuration wifi_run() can cancel connection after timeout
    if (wifi_run(0, 0, NULL)) {
        // failed - cancelled
        //ESP_LOGW(TAG, "wifi connection cancelled");
        if (req->callback != NULL)
            req->callback(req, 0);
        return;
    } else if (!wifi_connected) {
        // failed - may connect later
        ESP_LOGW(TAG, "wifi connection timeouted");
        // ...changed to add=0
        // decrease because we're cancelling task
        //WIFI_DEL();
        if (req->callback != NULL)
            req->callback(req, 0);
        return;
    }

    https_enter(req->exclusive);
    xxTaskCreate((void (*)(void*)) https_get_task, "https_get_task", 3*1024, req, 0, &req->task);
}

/*
void http_init(void)
{
    ESP_LOGI(TAG, "init");
    /
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    //ESP_ERROR_CHECK(esp_netif_init());
    //ESP_ERROR_CHECK(esp_event_loop_create_default());
}
*/
