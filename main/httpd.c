#include "freertos/portmacro.h"
#define HTTPD_ROOT
#include "config.h"
#include "device.h"
#include "module.h"
#include "httpd.h"
#include "util.h"
#include <stddef.h>

/* based on Simple HTTP Server Example (Public Domain or CC0 licensed) */

#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_eth.h"
//#include "protocol_examples_common.h"
#include "esp_tls_crypto.h"

/* A simple example that demonstrates how to create GET and POST
 * handlers for the web server.
 */

static const char *TAG = "httpd";

#if xCONFIG_EXAMPLE_BASIC_AUTH

typedef struct {
    char    *username;
    char    *password;
} basic_auth_info_t;

#define HTTPD_401      "401 Unauthorized"           /*!< HTTP Response 401 */

static char *http_auth_basic(const char *username, const char *password)
{
    int out;
    char *user_info = NULL;
    char *digest = NULL;
    size_t n = 0;
    asprintf(&user_info, "%s:%s", username, password);
    if (!user_info) {
        ESP_LOGE(TAG, "No enough memory for user information");
        return NULL;
    }
    esp_crypto_base64_encode(NULL, 0, &n, (const unsigned char *)user_info, strlen(user_info));

    /* 6: The length of the "Basic " string
     * n: Number of bytes for a base64 encode format
     * 1: Number of bytes for a reserved which be used to fill zero
    */
    digest = calloc(1, 6 + n + 1);
    if (digest) {
        strcpy(digest, "Basic ");
        esp_crypto_base64_encode((unsigned char *)digest + 6, n, (size_t *)&out, (const unsigned char *)user_info, strlen(user_info));
    }
    free(user_info);
    return digest;
}

/* An HTTP GET handler */
static esp_err_t basic_auth_get_handler(httpd_req_t *req)
{
    char *buf = NULL;
    size_t buf_len = 0;
    basic_auth_info_t *basic_auth_info = req->user_ctx;

    buf_len = httpd_req_get_hdr_value_len(req, "Authorization") + 1;
    if (buf_len > 1) {
        buf = calloc(1, buf_len);
        if (!buf) {
            ESP_LOGE(TAG, "No enough memory for basic authorization");
            return ESP_ERR_NO_MEM;
        }

        if (httpd_req_get_hdr_value_str(req, "Authorization", buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found header => Authorization: %s", buf);
        } else {
            ESP_LOGE(TAG, "No auth value received");
        }

        char *auth_credentials = http_auth_basic(basic_auth_info->username, basic_auth_info->password);
        if (!auth_credentials) {
            ESP_LOGE(TAG, "No enough memory for basic authorization credentials");
            free(buf);
            return ESP_ERR_NO_MEM;
        }

        if (strncmp(auth_credentials, buf, buf_len)) {
            ESP_LOGE(TAG, "Not authenticated");
            httpd_resp_set_status(req, HTTPD_401);
            httpd_resp_set_type(req, "application/json");
            httpd_resp_set_hdr(req, "Connection", "keep-alive");
            httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Hello\"");
            httpd_resp_send(req, NULL, 0);
        } else {
            ESP_LOGI(TAG, "Authenticated!");
            char *basic_auth_resp = NULL;
            httpd_resp_set_status(req, HTTPD_200);
            httpd_resp_set_type(req, "application/json");
            httpd_resp_set_hdr(req, "Connection", "keep-alive");
            asprintf(&basic_auth_resp, "{\"authenticated\": true,\"user\": \"%s\"}", basic_auth_info->username);
            if (!basic_auth_resp) {
                ESP_LOGE(TAG, "No enough memory for basic authorization response");
                free(auth_credentials);
                free(buf);
                return ESP_ERR_NO_MEM;
            }
            httpd_resp_send(req, basic_auth_resp, strlen(basic_auth_resp));
            free(basic_auth_resp);
        }
        free(auth_credentials);
        free(buf);
    } else {
        ESP_LOGE(TAG, "No auth header received");
        httpd_resp_set_status(req, HTTPD_401);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Connection", "keep-alive");
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Hello\"");
        httpd_resp_send(req, NULL, 0);
    }

    return ESP_OK;
}

static httpd_uri_t basic_auth = {
    .uri       = "/basic_auth",
    .method    = HTTP_GET,
    .handler   = basic_auth_get_handler,
};

static void httpd_register_basic_auth(httpd_handle_t server)
{
    basic_auth_info_t *basic_auth_info = calloc(1, sizeof(basic_auth_info_t));
    if (basic_auth_info) {
        basic_auth_info->username = CONFIG_EXAMPLE_BASIC_AUTH_USERNAME;
        basic_auth_info->password = CONFIG_EXAMPLE_BASIC_AUTH_PASSWORD;

        basic_auth.user_ctx = basic_auth_info;
        httpd_register_uri_handler(server, &basic_auth);
    }
}
#endif

static esp_err_t root_get_handler(httpd_req_t *req)
{
    //const char* resp_str = (const char*) req->user_ctx;
    //httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    //httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    const char *hostname = NULL;
    esp_netif_t *esp_netif = NULL;
    esp_netif = esp_netif_next(esp_netif);
    if (esp_netif != NULL) {
        ESP_ERROR_CHECK(esp_netif_get_hostname(esp_netif, &hostname));
    }
    httpd_resp_send(req, hostname, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

#if defined(HTTPD_GET_EXAMPLE) || defined(HTTPD_EXAMPLES)
/* An HTTP GET handler */
static esp_err_t hello_get_handler(httpd_req_t *req)
{
    char*  buf;
    size_t buf_len;

    /* Get header value string length and allocate memory for length + 1,
     * extra byte for null termination */
    buf_len = httpd_req_get_hdr_value_len(req, "Host") + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        /* Copy null terminated value string into buffer */
        if (httpd_req_get_hdr_value_str(req, "Host", buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found header => Host: %s", buf);
        }
        free(buf);
    }

    buf_len = httpd_req_get_hdr_value_len(req, "Test-Header-2") + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        if (httpd_req_get_hdr_value_str(req, "Test-Header-2", buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found header => Test-Header-2: %s", buf);
        }
        free(buf);
    }

    buf_len = httpd_req_get_hdr_value_len(req, "Test-Header-1") + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        if (httpd_req_get_hdr_value_str(req, "Test-Header-1", buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found header => Test-Header-1: %s", buf);
        }
        free(buf);
    }

    /* Read URL query string length and allocate memory for length + 1,
     * extra byte for null termination */
    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found URL query => %s", buf);
            char param[32];
            /* Get value of expected key from query string */
            if (httpd_query_key_value(buf, "query1", param, sizeof(param)) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => query1=%s", param);
            }
            if (httpd_query_key_value(buf, "query3", param, sizeof(param)) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => query3=%s", param);
            }
            if (httpd_query_key_value(buf, "query2", param, sizeof(param)) == ESP_OK) {
                ESP_LOGI(TAG, "Found URL query parameter => query2=%s", param);
            }
        }
        free(buf);
    }

    /* Set some custom headers */
    httpd_resp_set_hdr(req, "Custom-Header-1", "Custom-Value-1");
    httpd_resp_set_hdr(req, "Custom-Header-2", "Custom-Value-2");

    /* Send response with custom headers and body set as the
     * string passed in user context*/
    //const char* resp_str = (const char*) req->user_ctx;
    //httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send(req, "Hi", HTTPD_RESP_USE_STRLEN);

    /* After sending the HTTP response the old HTTP request
     * headers are lost. Check if HTTP request headers can be read now. */
    if (httpd_req_get_hdr_value_len(req, "Host") == 0) {
        ESP_LOGI(TAG, "Request headers lost");
    }
    return ESP_OK;
}
#endif

#if defined(HTTPD_POST_EXAMPLE) || defined(HTTPD_EXAMPLES)
/* An HTTP POST handler */
static esp_err_t echo_post_handler(httpd_req_t *req)
{
    char buf[100];
    int ret, remaining = req->content_len;

    while (remaining > 0) {
        /* Read the data for the request */
        if ((ret = httpd_req_recv(req, buf,
                        MIN(remaining, sizeof(buf)))) <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                /* Retry receiving if timeout occurred */
                continue;
            }
            return ESP_FAIL;
        }

        /* Send back the same data */
        httpd_resp_send_chunk(req, buf, ret);
        remaining -= ret;

        /* Log data received */
        ESP_LOGI(TAG, "=========== RECEIVED DATA ==========");
        ESP_LOGI(TAG, "%.*s", ret, buf);
        ESP_LOGI(TAG, "====================================");
    }

    // End response
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}
#endif

#ifdef HTTPD_PUT_EXAMPLE
/* This handler allows the custom error handling functionality to be
 * tested from client side. For that, when a PUT request 0 is sent to
 * URI /ctrl, the /hello and /echo URIs are unregistered and following
 * custom error handler http_404_error_handler() is registered.
 * Afterwards, when /hello or /echo is requested, this custom error
 * handler is invoked which, after sending an error message to client,
 * either closes the underlying socket (when requested URI is /echo)
 * or keeps it open (when requested URI is /hello). This allows the
 * client to infer if the custom error handler is functioning as expected
 * by observing the socket state.
 */
esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    if (strcmp("/hello", req->uri) == 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "/hello URI is not available");
        /* Return ESP_OK to keep underlying socket open */
        return ESP_OK;
    } else if (strcmp("/echo", req->uri) == 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "/echo URI is not available");
        /* Return ESP_FAIL to close underlying socket */
        return ESP_FAIL;
    }
    /* For any other URI send 404 and close socket */
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Some 404 error message");
    return ESP_FAIL;
}

/* An HTTP PUT handler. This demonstrates realtime
 * registration and deregistration of URI handlers
 */
static esp_err_t ctrl_put_handler(httpd_req_t *req)
{
    httpd_t *self = req->user_ctx;
    assert(self != NULL);
    char buf;
    int ret;

    if ((ret = httpd_req_recv(req, &buf, 1)) <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }

    if (buf == '0') {
        /* URI handlers can be unregistered using the uri string */
        //ESP_LOGI(TAG, "Unregistering /hello and /echo URIs");
        list_t *item = &self->handlers;
        while ((item = list_iter(item)) != NULL) {
            if (strcmp(LIST(httpd_uri_t, item, uri), "/ctrl")) {
                ESP_LOGI(TAG, "unregistering URI %s", LIST(httpd_uri_t, item, uri));
                httpd_unregister_uri(req->handle, LIST(httpd_uri_t, item, uri));
            }
        }
        /* Register the custom error handler */
        httpd_register_err_handler(req->handle, HTTPD_404_NOT_FOUND, http_404_error_handler);
    }
    else {
        //ESP_LOGI(TAG, "Registering /hello and /echo URIs");
        list_t *item = &self->handlers;
        while ((item = list_iter(item)) != NULL) {
            if (strcmp(LIST(httpd_uri_t, item, uri), "/ctrl")) {
                ESP_LOGI(TAG, "registering URI %s", LIST(httpd_uri_t, item, uri));
                httpd_register_uri_handler(req->handle, item->data);
            }
        }
        /* Unregister custom error handler */
        httpd_register_err_handler(req->handle, HTTPD_404_NOT_FOUND, NULL);
    }

    /* Respond with empty body */
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}
#endif

httpd_uri_t httpd_default_handlers[] = {
#if defined(HTTPD_ROOT)
    {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler,
    },
#endif

#if defined(HTTPD_GET_EXAMPLE) || defined(HTTPD_EXAMPLES)
    {
        .uri       = "/hello",
        .method    = HTTP_GET,
        .handler   = hello_get_handler,
    },
#endif

#if defined(HTTPD_POST_EXAMPLE) || defined(HTTPD_EXAMPLES)
    {
        .uri       = "/echo",
        .method    = HTTP_POST,
        .handler   = echo_post_handler,
    },
#endif

#if defined(HTTPD_PUT_EXAMPLE) || defined(HTTPD_EXAMPLES)
    {
        .uri       = "/ctrl",
        .method    = HTTP_PUT,
        .handler   = ctrl_put_handler,
        // .user_ctx = self if .user_ctx is NULL
    },
#endif
};

int httpd_default_handlers_cnt = COUNT_OF(httpd_default_handlers);

#ifdef HTTPD_SSL
#define HTTPD_START httpd_ssl_start
#define HTTPD_STOP httpd_ssl_stop
#define HTTPD_CONFIG self->config.httpd
#else
#define HTTPD_START httpd_start
#define HTTPD_STOP httpd_stop
#define HTTPD_CONFIG self->config
#endif
static esp_err_t start_webserver(httpd_t *self)
{
    assert(self != NULL);

    if (self->server != NULL) {
        assert(self->module.state != 0);
        return ESP_OK;
    }

    // http, control and at least one request
    xSemaphoreTake(esp.sockets, portMAX_DELAY);
    xSemaphoreTake(esp.sockets, portMAX_DELAY);
    xSemaphoreTake(esp.sockets, portMAX_DELAY);

    // Start the httpd server
#ifdef HTTPD_SSL
    ESP_LOGI(TAG, "starting on port %d", self->config.port_secure);
#else
    ESP_LOGI(TAG, "starting on port %d", HTTPD_CONFIG.server_port);
#endif
    esp_err_t res;
    if ((res = HTTPD_START(&self->server, &self->config)) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        list_t *item = &self->handlers;
        while ((item = list_iter(item)) != NULL) {
            httpd_register_uri_handler(self->server, item->data);
        }
        #if xCONFIG_EXAMPLE_BASIC_AUTH
        httpd_register_basic_auth(self->server);
        #endif
        self->module.state = 1;
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return res;
}

static esp_err_t stop_webserver(httpd_t *self)
{
    assert(self != NULL);

    if (self->server == NULL) {
        assert(self->module.state == 0);
        return ESP_OK;
    }

    // http, control and at least one request
    xSemaphoreGive(esp.sockets);
    xSemaphoreGive(esp.sockets);
    xSemaphoreGive(esp.sockets);

    if (self->server != NULL) {
        assert(self->module.state != 0);
        esp_err_t res = HTTPD_STOP(self->server);
        if (res == ESP_OK) {
            self->server = NULL;
            self->module.state = 0;
        }
        return res;
    }
    return ESP_OK;
}

#ifdef xCONFIG_EXAMPLE_CONNECT_WIFI
static void disconnect_handler(void *ptr, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    httpd_t *self = (httpd_t *) ptr;
    assert(self != NULL);
    if (self->server != NULL) {
        ESP_LOGI(TAG, "Stopping webserver");
        if (stop_webserver(self->server) == ESP_OK) {
            self->server = NULL;
        } else {
            ESP_LOGE(TAG, "Failed to stop http server");
        }
    }
}

static void connect_handler(void *self, esp_event_base_t event_base,
                            int32_t event_id, void* event_data)
{
    start_webbserver(self);
}
#endif

static uint16_t count = 0;

httpd_t *httpd_new(int port)
{
    ESP_LOGI(TAG, "new: %d", port);
    httpd_t *self = calloc(1, sizeof(httpd_t));
    assert(self != NULL);

    list_t *list = &self->handlers;
    for (int i=0; i<COUNT_OF(httpd_default_handlers); i++)
        list = list_append(list, &httpd_default_handlers[i]);

#ifdef HTTPD_SSL
    {
        httpd_ssl_config_t sconfig = HTTPD_SSL_CONFIG_DEFAULT();
        //sconfig.transport_mode = HTTPD_SSL_TRANSPORT_INSECURE;

        extern const unsigned char httpd_pem_start[] asm("_binary_httpd_pem_start");
        extern const unsigned char httpd_pem_end[]   asm("_binary_httpd_pem_end");
        sconfig.servercert = httpd_pem_start;
        sconfig.servercert_len = httpd_pem_end - httpd_pem_start;
        // now used for client cert verify?
        //sconfig.cacert_pem = httpd_pem_start;
        //sconfig.cacert_len = httpd_pem_end - httpd_pem_start;

        extern const unsigned char httpd_key_start[] asm("_binary_httpd_key_start");
        extern const unsigned char httpd_key_end[]   asm("_binary_httpd_key_end");
        sconfig.prvtkey_pem = httpd_key_start;
        sconfig.prvtkey_len = httpd_key_end - httpd_key_start;

        memcpy(&self->config, &sconfig, sizeof(httpd_ssl_config_t));

        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        //_config.stack_size = ;
        memcpy(&self->config.httpd, &config, sizeof(httpd_config_t));
    }
#else
    {
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        memcpy(&self->config, &config, sizeof(httpd_config_t));
    }
#endif

    if (port > 0)
        HTTPD_CONFIG.server_port = port;
    HTTPD_CONFIG.lru_purge_enable = true;
    HTTPD_CONFIG.max_uri_handlers = HTTPD_MAX_URI_HANDLERS + list_count(&self->handlers);
#ifdef HTTPD_SSL
    ESP_LOGI(TAG, "lifetime max handlers: %d", HTTPD_CONFIG.max_uri_handlers);
#endif
    // this allows multiple httpd instances
    HTTPD_CONFIG.ctrl_port -= count++;
    ESP_LOGI(TAG, "ctrl_port=%d", HTTPD_CONFIG.ctrl_port);
    //HTTPD_CONFIG.uri_match_fn = httpd_uri_match_wildcard;

    /* Register event handlers to stop the server when Wi-Fi or Ethernet is disconnected,
     * and re-start it upon connection.
     */
#ifdef xCONFIG_EXAMPLE_CONNECT_WIFI
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler, self->server));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, self->server));
#endif // CONFIG_EXAMPLE_CONNECT_WIFI
#ifdef xCONFIG_EXAMPLE_CONNECT_ETHERNET
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &connect_handler, self->server));
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ETHERNET_EVENT_DISCONNECTED, &disconnect_handler, self->server));
#endif // CONFIG_EXAMPLE_CONNECT_ETHERNET

    self->module.name = TAG;
    self->module.type = M_HTTPD;
    self->module.run = (void (*)(void*,int)) httpd_run;
    module_add(&self->module);
    return self;
}

void httpd_run(httpd_t *self, int run)
{
    if (run) {
        start_webserver(self);
    } else {
        stop_webserver(self);
    }
}

list_t *httpd_register(httpd_t *self, httpd_uri_t *uri)
{
    assert(self != NULL);
    ESP_LOGI(TAG, "adding URI %s", uri->uri);
    if (uri->user_ctx == NULL)
        uri->user_ctx = self;
    list_t *new = list_append(&self->handlers, (void*) uri);
    if (self->server != NULL) {
        while (httpd_register_uri_handler(self->server, uri) == ESP_ERR_HTTPD_HANDLERS_FULL) {
            HTTPD_CONFIG.max_uri_handlers += HTTPD_MAX_URI_HANDLERS;
            ESP_LOGW(TAG, "increasing max_uri_handlers to %d", HTTPD_CONFIG.max_uri_handlers);
#ifdef HTTPD_SSL
            ESP_LOGE(TAG, "restart breaks HTTPS server");
            //assert(0 || "httpd restart breaks HTTPS server");
#else
            stop_webserver(self);
            start_webserver(self);
#endif
            break;
        }
    }
    return new;
}

void httpd_unregister(httpd_t *self, list_t *item)
{
    assert(self != NULL);
    ESP_LOGI(TAG, "removing URI %s", LIST(httpd_uri_t, item, uri));
    list_remove(&self->handlers, item);
    if (self->server != NULL) {
        // trusting that it is registered
        httpd_unregister_uri(self->server, LIST(httpd_uri_t, item, uri));
    }
}

esp_err_t http_printf(httpd_req_t *req, const char *format, ...)
{
    // formatted strings are usually not long
    char buf[HTTP_PRINT_BUFSIZE];

    va_list args;
    va_start(args, format);
    int len = vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    if (len >= sizeof(buf)) {
        ESP_LOGE(TAG, "http_printf needs more than %d bytes", HTTP_PRINT_BUFSIZE);
        return ESP_FAIL;
    }

    return httpd_resp_send_chunk(req, buf, len);
}
