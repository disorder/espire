#include "config.h"
#include "device.h"
#include "module.h"
#include "auto.h"
#include "wifi.h"
#include "temp.h"
#include "api.h"
#include "heating.h"
#include "relay.h"
#include "driver/gpio.h"
#include "ping.h"
#include "httpd.h"
#include "nv.h"
#include "heap.h"
#include "oled.h"
#include "co2.h"
#include "widgets.h"
#include "button.h"
#include "ntp.h"
#include "log.h"
#include "util.h"

#include "ota.h"
#include "esp_ota_ops.h"

#include <stdarg.h>
#include <inttypes.h>

#include "nvs_flash.h"
#include "esp_log.h"
static const char *TAG = "api";

#include "metar.h"

esp_err_t api_help(httpd_req_t *req);

static esp_err_t api_module(httpd_req_t *req)
{
    char *buf = NULL;
    int buf_len;
    if (!api_key_check(1, req, &buf, &buf_len))
        goto CLEANUP;
    size_t module_id = 0;

    if (buf_len > 1) {
        {//if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            char state[2];
            int run;
            if (httpd_query_key_value(buf, "run", (char *) state, sizeof(state)) == ESP_OK) {
                run = atoi(state);
            } else {
                httpd_resp_set_status(req, "400 Bad Request - run");
                goto CLEANUP;
            }

            char id[11];
            if (httpd_query_key_value(buf, "id", (char *) id, sizeof(id)) == ESP_OK) {
                module_id = atoi(id);
                iter_t iter = module_iter();
                module_t *m;
                while ((iter = module_next(iter, &m)) != NULL) {
                    if (m == (void *) module_id) {
                        m->run(m, run);
                        goto CLEANUP;
                    }
                }
                httpd_resp_set_status(req, "404 Not Found - id");
                goto CLEANUP;
            }

            char name[16];
            if (httpd_query_key_value(buf, "name", (char *) name, sizeof(name)) == ESP_OK) {
                int count = 0;
                iter_t iter = module_iter();
                module_t *m;
                while ((iter = module_next(iter, &m)) != NULL) {
                    if (module_match_name(m, (char *) name)) {
                        m->run(m, run);
                        goto CLEANUP;
                    }
                }
                if (count == 0)
                    httpd_resp_set_status(req, "404 Not Found");
                goto CLEANUP;
            } else {
                httpd_resp_set_status(req, "400 Bad Request - id/name");
            }
        }
    }

CLEANUP:
    if (buf != NULL)
        free(buf);
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t api_controller(httpd_req_t *req)
{
    char *buf = NULL;
    int buf_len;
    if (!api_key_check(1, req, &buf, &buf_len))
        goto CLEANUP;

    if (buf_len > 1) {
        {//if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            char value[sizeof(CONTROLLER_IP)];
            if (httpd_query_key_value(buf, "value", (char *) value, sizeof(value)) == ESP_OK) {
                controller_ip_handler(NULL, value);
            } else {
                httpd_resp_set_status(req, "400 Bad Request - value");
                goto CLEANUP;
            }
        }
    }

CLEANUP:
    if (buf != NULL)
        free(buf);
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// this is used to set thermostat hostname which doubles as heating zone name
static esp_err_t api_hostname(httpd_req_t *req)
{
    char *buf = NULL;
    int buf_len;
    int set = 0;
    if (!api_key_check(1, req, &buf, &buf_len))
        goto CLEANUP;

    char value[member_size(heating_t, name)] = "";
    if (buf_len > 1) {
        {//if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            if (httpd_query_key_value(buf, "value", (char *) value, sizeof(value)) == ESP_OK) {
                // postpone
                set = 1;
            } else {
                httpd_resp_set_status(req, "400 Bad Request - value");
                goto CLEANUP;
            }
        }
    }

CLEANUP:
    if (buf != NULL)
        free(buf);
    httpd_resp_send(req, NULL, 0);
    if (set) {
        hostname_handler(NULL, value);
        // needed to apply to openwrt DHCP/DNS
        _wifi_stop_sta(1);
        _wifi_start_sta(1);
    }
    return ESP_OK;
}


static esp_err_t api_temp_zone(httpd_req_t *req)
{
    char *buf = NULL;
    int buf_len;
    int auth = 0;
    if (api_key_check(1, req, &buf, &buf_len))
        auth = 1;

    char name[member_size(heating_t, name)] = "";
    if (buf_len > 1) {
        {//if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            if (httpd_query_key_value(buf, "name", (char *) name, sizeof(name)) == ESP_OK) {
            } else {
                //httpd_resp_set_status(req, "400 Bad Request - name");
                //goto CLEANUP;
                goto PRINT;
            }

            if (!auth)
                goto CLEANUP;

            char adc[3] = "";
            if (httpd_query_key_value(buf, "adc", (char *) adc, sizeof(adc)) == ESP_OK) {
                temp_zone_adc(name, atoi(adc));
            }

            char relay[3] = "";
            if (httpd_query_key_value(buf, "relay", (char *) relay, sizeof(relay)) == ESP_OK) {
                heating_relay(name, atoi(relay));
            }

            if (adc[0] || relay[0])
                goto CLEANUP;
        }
    }

PRINT:
    for (int i=0; i<COUNT_OF(temp_zones); i++) {
        if (name[0] != '\0' && strncmp(temp_zones[i].name, name, sizeof(temp_zones[i].name)) != 0)
            continue;
        if (temp_zones[i].name[0] == '\0')
            continue;
        http_printf(req, "temp_zone_adc=%s==%d\n", temp_zones[i].name, temp_zones[i].adc);
    }

    iter_t iter = heating_iter();
    heating_t *data;
    while ((iter = heating_next(iter, &data)) != NULL) {
        http_printf(req, "heating_relay=%s=%d\n", data->name, data->relay);
        if (data->c > 0) {
            int last = HEATING_LAST_VAL_I(data);
            http_printf(req, "temp=%.1f\n", data->vals[last]);
            http_printf(req, "vals=");
            // from oldest to newest
            for (int i=data->c - 1; i>=0; i--)
                http_printf(req, "%.1f ", data->vals[(last+COUNT_OF(data->vals)-i) % COUNT_OF(data->vals)]);
            http_printf(req, "\n");
        }
        http_printf(req, "val=%.1f\n", data->val);
        http_printf(req, "set=%.1f\n", data->set);
        http_printf(req, "fix=%.1f\n", data->fix);
        http_printf(req, "state=%d\n", data->state == HEATING_ON);
        http_printf(req, "triggered=%d\n", data->triggered);
        http_printf(req, "change=%d\n", data->change);
    }

CLEANUP:
    if (buf != NULL)
        free(buf);
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t api_heating_hc_get(httpd_req_t *req)
{
    char *buf = NULL;
    int buf_len;
    if (!api_key_check(1, req, &buf, &buf_len))
        goto CLEANUP;

    if (buf_len > 1) {
        {//if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            // at least "http://123.456.789.abc:12345/hc?value=X"
            // more available by using autoconfiguration
            char url[4+1+2 + 3+1+3+1+3+1+3 + 1+5 +1+ 2+1+5+1+1 +1];
            if (httpd_query_key_value(buf, "url", (char *) url, sizeof(url)) == ESP_OK) {
                if (!heating_hc_url_set(url)) {
                    httpd_resp_set_status(req, "500 Failed - " HEATING_HC_URL_KEY);
                    goto CLEANUP;
                }
            } else {
                goto PRINT;
            }
        }
    }

PRINT:
    /* can be read using nv, not useful here */
    char *value = heating_hc_url_get();
    if (value != NULL) {
        http_printf(req, "%s=%s", HEATING_HC_URL_KEY, value);
        free(value);
    } else
        httpd_resp_set_status(req, "404 Not Found - " HEATING_HC_URL_KEY);

CLEANUP:
    if (buf != NULL)
        free(buf);
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t api_heating_temp_get(httpd_req_t *req)
{
    char *buf = NULL;
    int buf_len;
    // no authorization required
    api_key_check(0, req, &buf, &buf_len);

    heating_t *data = NULL;
    if (buf_len > 1) {
        {//if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            char name[member_size(heating_t, name)] = "";
            if (httpd_query_key_value(buf, "name", (char *) name, sizeof(name)) == ESP_OK) {
                data = heating_find(name, 0);
                if (data == NULL) {
                    httpd_resp_set_status(req, "404 Not Found - name");
                    goto CLEANUP;
                } else {
                    // TODO curl: Illegal or missing hexadecimal sequence in chunked-encoding
                    http_printf(req, "name=%s\n", data->name);
                    http_printf(req, "val=%.1f\n", data->val);
                    http_printf(req, "set=%.1f\n", data->set);
                }
            } else {
                httpd_resp_set_status(req, "400 Bad Request - name");
                goto CLEANUP;
            }
        }
    } else {
        iter_t iter = heating_iter();
        while ((iter = heating_next(iter, &data)) != NULL) {
            http_printf(req, "name=%s\n", data->name);
            http_printf(req, "val=%.1f\n", data->val);
            http_printf(req, "set=%.1f\n", data->set);
            http_printf(req, "relay=%d\n", data->relay);
        }
    }

CLEANUP:
    if (buf != NULL)
        free(buf);

    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t api_heating_temp_set(httpd_req_t *req)
{
    char *buf = NULL;
    int buf_len;
    if (!api_key_check(1, req, &buf, &buf_len))
        goto CLEANUP;

    if (buf_len > 1) {
        {//if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            char name[member_size(heating_t, name)] = "";
            if (httpd_query_key_value(buf, "name", (char *) name, sizeof(name)) == ESP_OK) {
            } else {
                httpd_resp_set_status(req, "400 Bad Request - name");
                goto CLEANUP;
            }

            httpd_resp_set_status(req, "404 Not Found - val/set");
            char temp[3+1+5 +1];
            if (httpd_query_key_value(buf, "val", (char *) temp, sizeof(temp)) == ESP_OK) {
                float tempf = strtof(temp, NULL);
                heating_temp_val(name, tempf, 1);
                httpd_resp_set_status(req, "200 OK");
            }

            if (httpd_query_key_value(buf, "set", (char *) temp, sizeof(temp)) == ESP_OK) {
                float setf = strtof(temp, NULL);
                heating_temp_set(name, setf, 1);
                httpd_resp_set_status(req, "200 OK");
            }

            if (httpd_query_key_value(buf, "fix", (char *) temp, sizeof(temp)) == ESP_OK) {
                float fixf = strtof(temp, NULL);
                heating_temp_fix(name, fixf, 1);
                httpd_resp_set_status(req, "200 OK");
            }
       }
    }

    // unnecessary for update
    /*
    if (data == NULL) {
        iter_t iter = heating_iter();
        while ((iter = heating_next(iter, &data)) != NULL) {
            http_printf(req, "name=%s\n", data->name);
            http_printf(req, "val=%.1f\n", data->val);
            http_printf(req, "set=%.1f\n", data->set);
            http_printf(req, "fix=%.1f\n", data->fix);
        }
    } else {
        http_printf(req, "name=%s\n", data->name);
        http_printf(req, "val=%.1f\n", data->val);
        http_printf(req, "set=%.1f\n", data->set);
        http_printf(req, "fix=%.1f\n", data->fix);
    }
    */

CLEANUP:
    if (buf != NULL)
        free(buf);

    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t api_co2_ppm_get(httpd_req_t *req)
{
    char *buf = NULL;
    int buf_len;
    // no authorization required
    api_key_check(0, req, &buf, &buf_len);
    //http_printf(req, "%d", co2_ppm);
    http_printf(req, "%d", senseair_s8_co2_ppm());

    if (buf != NULL)
        free(buf);

    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t api_co2_abc_get(httpd_req_t *req)
{
    char *buf = NULL;
    int buf_len;
    // no authorization required
    api_key_check(0, req, &buf, &buf_len);
    http_printf(req, "%d", senseair_s8_abc());

    if (buf != NULL)
        free(buf);

    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t api_co2_get(httpd_req_t *req)
{
    char *buf = NULL;
    int buf_len;
    // no authorization required
    api_key_check(0, req, &buf, &buf_len);
    http_printf(req, "FW version: %04x\n", senseair_s8_fwver());
    http_printf(req, "ID: %08X\n", senseair_s8_id());

    if (buf != NULL)
        free(buf);

    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// TODO keeping as a template for post
static esp_err_t api_module_post(httpd_req_t *req)
{
    char *buf = NULL;
    int qbuf_len;
    char *qbuf;
    if (!api_key_check(1, req, &qbuf, &qbuf_len))
        goto CLEANUP;

    int ret, remaining = req->content_len;
    buf = malloc(remaining+1);
    assert(buf != NULL);
    buf[remaining] = '\0';

    while (remaining > 0) {
        /* Read the data for the request */
        if ((ret = httpd_req_recv(req,
                                  buf + (req->content_len - remaining),
                                  remaining)) <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                /* Retry receiving if timeout occurred */
                continue;
            }
            free(buf);
            return ESP_FAIL;
        }

        remaining -= ret;
    }

    char *name = buf;
    char *state = strchrnul(name, '=');
    if (state[0] != '\0') {
        state[0] = '\0';
        state += 1;
        int run = atoi(state);
        iter_t iter = module_iter();
        module_t *m;
        while ((iter = module_next(iter, &m)) != NULL)
            if (module_match_name(m, name))
                m->run(m, run);
    } else {
        goto CLEANUP;
    }

CLEANUP:
    if (qbuf != NULL)
        free(qbuf);
    if (buf != NULL)
        free(buf);
    // End response
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t api_button(httpd_req_t *req)
{
    char *buf = NULL;
    int buf_len;
    if (!api_key_check(1, req, &buf, &buf_len))
        goto CLEANUP;

    if (buf_len > 1) {
        {//if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            char name[7];

            char id[2];
            button_enum_t data = BUTTON_MAX;
            if (httpd_query_key_value(buf, "id", (char *) id, sizeof(id)) == ESP_OK) {
                data = (button_enum_t) atoi(id);
            } else if (httpd_query_key_value(buf, "name", (char *) name, sizeof(name)) == ESP_OK) {
                if (strcmp(name, "toggle") == 0)
                    data = TOGGLE;
                else if (strcmp(name, "+") == 0)
                    data = PLUS;
                else if (strcmp(name, "-") == 0)
                    data = MINUS;
            } else {
                goto CLEANUP;
                httpd_resp_set_status(req, "400 Bad Request - name/id");
            }

            // by default it's short press start
            char on[2] = "1";
            char changed[2] = "1";
            char repeats[2] = {0};
            char longs[2] = {0};
            httpd_query_key_value(buf, "on", (char *) on, sizeof(on));
            httpd_query_key_value(buf, "changed", (char *) changed, sizeof(changed));
            httpd_query_key_value(buf, "repeats", (char *) repeats, sizeof(repeats));
            httpd_query_key_value(buf, "longs", (char *) longs, sizeof(longs));
            button_t b = {
                .state = atoi(on)? BUTTON_STATE_ON : BUTTON_STATE_OFF,
                .changed = atoi(changed),
                .reps = atoi(repeats),
                .longs = atoi(longs),
                .data = (void*) data,
            };
            b.repeated = b.reps > 0;
            b.longpress = b.longs > 0;
            ESP_LOGI(TAG, "triggered button %s(%d): on=%d changed=%d reps(%d)=%d longs(%d)=%d", name, data, b.state == BUTTON_STATE_ON, b.changed, b.repeated, b.reps, b.longpress, b.longs);
            button_handler(&b);
        }
    }

CLEANUP:
    if (buf != NULL)
        free(buf);
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t api_loglevel(httpd_req_t *req)
{
    int buf_len;
    char *buf = NULL;
    if (!api_key_check(1, req, &buf, &buf_len))
        goto CLEANUP;

    if (buf_len > 1) {
        {//if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            char level[2];
            if (httpd_query_key_value(buf, "level", (char *) level, sizeof(level)) != ESP_OK) {
                httpd_resp_set_status(req, "400 Bad Request - level");
                goto CLEANUP;
            }

            char tag[50];
            if (httpd_query_key_value(buf, "tag", (char *) tag, sizeof(tag)) == ESP_OK) {
                esp_log_level_set(tag, atoi(level));
            } else {
                httpd_resp_set_status(req, "400 Bad Request - tag");
                goto CLEANUP;
            }
        }
    }

CLEANUP:
    if (buf != NULL)
        free(buf);
    // End response
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t api_log(httpd_req_t *req)
{
    int buf_len;
    char *buf = NULL;
    if (!api_key_check(1, req, &buf, &buf_len))
        goto CLEANUP;

    if (buf_len > 1) {
        {//if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            char ip[4*3+3+1];
            if (httpd_query_key_value(buf, "ip", (char *) ip, sizeof(ip)) != ESP_OK) {
                httpd_resp_set_status(req, "400 Bad Request - ip");
                goto CLEANUP;
            }

            char port[5+1];
            if (httpd_query_key_value(buf, "port", (char *) port, sizeof(port)) == ESP_OK) {
                log_add(ip, atoi(port));
            } else {
                httpd_resp_set_status(req, "400 Bad Request - port");
                goto CLEANUP;
            }
        }
    }

CLEANUP:
    if (buf != NULL)
        free(buf);
    // End response
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

esp_err_t api_reboot(httpd_req_t *req)
{
    char *reason = (req == NULL) ? "internal" : "external";
    ESP_LOGE(TAG, "reboot: %s", reason);
    //httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    //esp_restart();
    esp_system_abort(reason);
    return ESP_OK;
}

esp_err_t api_ota(httpd_req_t *req)
{
    char *reason = (req == NULL) ? "internal" : "external";
    ESP_LOGE(TAG, "ota: %s", reason);

    int buf_len;
    char *buf = NULL;
    int authorized = api_key_check(0, req, &buf, &buf_len);

    if (buf_len > 1) {
        {//if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            char force[1+1];
            if (httpd_query_key_value(buf, "force", (char *) force, sizeof(force)) == ESP_OK) {
                int value = atoi(force);
                // only allow force with key
                if (value) {
                    if (authorized)
                        ota_force = value;
                    else {
                        httpd_resp_set_status(req, "401 Unauthorized");
                        goto CLEANUP;
                    }
                }
            } else {
                httpd_resp_set_status(req, "400 Bad Request - force");
                goto CLEANUP;
            }
        }
    }

CLEANUP:
    if (buf != NULL)
        free(buf);

    httpd_resp_send_chunk(req, NULL, 0);
    ota_main();
    return ESP_OK;
}

#define PRINT_FIELD_STR(DATA, PREFIX, FIELD) http_printf(req, PREFIX "." #FIELD "=%s\n", DATA.FIELD)
esp_err_t api_version(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_app_desc_t info;
    if (esp_ota_get_partition_description(running, &info) != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        goto CLEANUP;
    }

    PRINT_FIELD_STR(info, "app", version);
    PRINT_FIELD_STR(info, "app", project_name);
    PRINT_FIELD_STR(info, "app", date);
    PRINT_FIELD_STR(info, "app", time);
    PRINT_FIELD_STR(info, "app", idf_ver);
    http_printf(req, "app.app_elf_sha256=");
    for (int i=0; i< sizeof(info.app_elf_sha256); i++)
        http_printf(req, "%02x", info.app_elf_sha256[i]);
    http_printf(req, "\n");
    http_printf(req, "app.secure_version=%"PRIu32"\n", info.secure_version);

CLEANUP:
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t api_heap(httpd_req_t *req)
{
    //httpd_resp_set_status(req, "200 OK");
    //httpd_resp_set_type(req, "text/plain");
    //httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    http_printf(req, "%zu", esp_get_free_heap_size());
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

typedef struct __attribute__((__packed__)) {             // Total: 54 bytes
    uint16_t  type;             // Magic identifier: 0x4d42
    uint32_t  size;             // File size in bytes
    uint16_t  reserved1;        // Not used
    uint16_t  reserved2;        // Not used
    uint32_t  offset;           // Offset to image data in bytes from beginning of file (54 bytes)
    // DIB header
    uint32_t  dib_header_size;  // DIB Header size in bytes (40 bytes)
    int32_t   width_px;         // Width of the image
    int32_t   height_px;        // Height of image
    uint16_t  num_planes;       // Number of color planes
    uint16_t  bits_per_pixel;   // Bits per pixel
    uint32_t  compression;      // Compression type
    uint32_t  image_size_bytes; // Image size in bytes
    int32_t   x_resolution_ppm; // Pixels per meter
    int32_t   y_resolution_ppm; // Pixels per meter
    uint32_t  num_colors;       // Number of colors
    uint32_t  important_colors; // Important colors

    uint8_t black[4], white[4];
} BMPHeader;
static esp_err_t api_screenshot(httpd_req_t *req)
{
    if (oled.scr == NULL) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_send(req, NULL, 0);
        goto CLEANUP;
    }

    int buf_len;
    char *buf;
    if (!api_key_check(1, req, &buf, &buf_len))
        goto CLEANUP;

    // apparently can't take snapshot of whole screen
    /*
    lv_img_dsc_t* snapshot;
    snapshot = lv_snapshot_take(oled.scr, LV_IMG_CF_INDEXED_1BIT);
    if (snapshot == NULL) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        goto CLEANUP;
    }
    */

    //int W = 128, H = 64, BPP = 1;
    #define W oled.w
    #define H oled.h
    #define BPP oled.depth
    int SIZE = W * H * BPP / 8;
    // some values preset to 1 bit image
    BMPHeader h = {
        .type = 0x4d42,
        //.size = sizeof(BMPHeader) + snapshot->data_size),
        .size = sizeof(BMPHeader) + SIZE,//*8,
        .offset = sizeof(BMPHeader),
        .dib_header_size = 40,
        //.width_px = snapshot->header.w,
        //.height_px = snapshot->header.h,
        .width_px = W,
        .height_px = H,
        .num_planes = 1,
        .bits_per_pixel = BPP,//8,//BPP,
        .compression = 0,
        //.image_size_bytes = snapshot->data_size,
        .image_size_bytes = SIZE,// * 8,
        // whatever ppm, don't have DPI for display, this is SSD1306
        .x_resolution_ppm = 130 * 39.3701,
        .y_resolution_ppm = 130 * 39.3701,
        .num_colors = (BPP==1)? 2 : 65536,
        .important_colors = 0,

        .black = {0x00,0x00,0x00,0x00},
        .white = {0xff,0xff,0xff,0x00},
    };
    extern char *screenshot;
    if (screenshot != NULL) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        goto CLEANUP;
    }
    char *data = (oled.depth == 1)? calloc(1, SIZE*2) : calloc(1, SIZE + oled.w*oled.depth/8);
    assert(data != NULL);
    screenshot = data;
    oled_invalidate(&oled);

    TickType_t start = xTaskGetTickCount();
    while (screenshot != NULL && xTaskGetTickCount() - start < MS_TO_TICK(1000))
        _vTaskDelay(MS_TO_TICK(100));
    if (screenshot != NULL) {
        ESP_LOGE(TAG, "screenshot timeouted");
        screenshot = NULL;
    }
    /*
    // 1 byte per pixel (buffer of size*(1+8))
    for (int x=oled.w-1; x >= 0; x--) {
        for (int y=0; y < oled.h; y++) {
            char val = oled_get_px((uint8_t *) data, oled.w, x, y);
            data[SIZE + x + (oled.h - 1 - y)*oled.w] = val;
        }
    }
    */

    if (oled.depth == 1) {
        // 1 bit per pixel
        /*
        for (int x=0; x < oled.w; x++) {
            for (int y=0; y < oled.h; y++) {
                char val = oled_get_px((uint8_t *) data, oled.w, x, y);
                if (val)
                    data[SIZE + (x + (oled.h - 1 - y)*oled.w)/8] |= 1<< (7 - (x % 8));
            }
        }
        */
        for (int i=0; i<SIZE; i++) {
            int x = i % 128;
            int yb = 8*(i/128);
            for (int bit=0; bit<8; bit++) {
                int y = bit + yb;
                int mask = 1 << bit;
                if (data[i] & mask)
                    data[SIZE + (x + (oled.h - 1 - y)*oled.w)/8] |= 1 << (7 - (x % 8));
            }
        }
    } else {
        int y2 = oled.h - 1;
        for (int y1=0; y1 < y2; y1++, y2--) {
            memcpy(&data[SIZE], &data[y1*oled.w*oled.depth/8], oled.w*oled.depth/8);
            memcpy(&data[y1*oled.w*oled.depth/8], &data[y2*oled.w*oled.depth/8], oled.w*oled.depth/8);
            memcpy(&data[y2*oled.w*oled.depth/8], &data[SIZE], oled.w*oled.depth/8);
        }
    }

    httpd_resp_set_type(req, "image/bmp");
    // BMP is little endian, ESP32 is little endian
    httpd_resp_send_chunk(req, (char*) &h, sizeof(h));
    if (oled.depth == 1) {
        //httpd_resp_send_chunk(req, data+SIZE, SIZE*8);
        httpd_resp_send_chunk(req, data+SIZE, SIZE);
    } else {
        httpd_resp_send_chunk(req, data, SIZE);
    }
    //httpd_resp_send_chunk(req, (char*) snapshot->data, snapshot->data_size);
    free(data);
    //lv_snapshot_free(snapshot);

CLEANUP:
    if (buf != NULL)
        free(buf);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t api_oled(httpd_req_t *req)
{
    int buf_len;
    char *buf = NULL;
    if (!api_key_check(1, req, &buf, &buf_len))
        goto CLEANUP;

    if (buf_len > 1) {
        {//if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            char op[7];
            if (httpd_query_key_value(buf, "op", (char *) op, sizeof(op)) == ESP_OK) {
            } else {
                httpd_resp_set_status(req, "400 Bad Request - op");
                goto CLEANUP;
            }

            // max message size
            static char value[200];
            if (httpd_query_key_value(buf, "value", (char *) value, sizeof(value)) == ESP_OK) {
                if (strncmp(op, "mode", sizeof(op)) == 0) {
                    int mode = atoi(value) % MODE_MAX;
                    if (mode != oled_update.mode) {
                        oled_update.mode = mode;
                        oled_update.invalidate = 1;
                    }
                } else if (strncmp(op, "msg", sizeof(op)) == 0) {
                    oled_update.message = value;
                    oled_update.invalidate = 1;
                } else if (strncmp(op, "invert", sizeof(op)) == 0) {
                    int invert = atoi(value);
                    oled_invert(&oled, invert);
                } else if (strncmp(op, "mirror", sizeof(op)) == 0) {
                    // 0,0 - any ASCII character works
                    value[1] = '\0';
                    int x = atoi(value);
                    int y = atoi(value+2);
                    oled_mirror(&oled, x, y);
                }  else if (strncmp(op, "power", sizeof(op)) == 0) {
                    int on = atoi(value);
                    oled_update.power_state = on;
                }  else if (strncmp(op, "power_force", sizeof(op)) == 0) {
                    // i'm cautious about using this, i've seen issues
                    // that froze httpd
                    int on = atoi(value);
                    oled_power(&oled, on);
                } else {
                    httpd_resp_set_status(req, "400 Bad Request - op");
                }
            } else if (strncmp(op, "reinit", sizeof(op)) == 0) {
                oled_reinit(&oled);
            } else if (strncmp(op, "reset", sizeof(op)) == 0) {
                oled_reset(&oled);
            } else
                httpd_resp_set_status(req, "404 Not Found - op");
            goto CLEANUP;
        }
    }

CLEANUP:
    if (buf != NULL)
        free(buf);
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t api_debug(httpd_req_t *req)
{
    int buf_len;
    char *buf = NULL;
    if (!api_key_check(1, req, &buf, &buf_len))
        goto CLEANUP;

    char *reason = (req == NULL) ? "internal" : "external";
    ESP_LOGE(TAG, "debug: %s", reason);

    if (oled.disp != NULL) {
        if (oled.disp->driver->monitor_cb)
            oled.disp->driver->monitor_cb = NULL;
        else
            oled.disp->driver->monitor_cb = lvgl_monitor_cb;
        http_printf(req, "oled.lvgl_monitor_cb = %x\n", oled.disp->driver->monitor_cb);
    }

CLEANUP:
    if (buf != NULL)
        free(buf);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t api_nvdump(httpd_req_t *req)
{
    int buf_len;
    char *buf;
    if (!api_key_check(1, req, &buf, &buf_len))
        goto CLEANUP;

    // can't set before 404
    //httpd_resp_set_type(req, "text/plain");

    if (buf_len > 1) {
        {//if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            char key[NVS_KEY_NAME_MAX_SIZE];
            if (httpd_query_key_value(buf, "key", key, sizeof(key)) == ESP_OK) {
                nv_data_t *d = NULL;
                nv_read_any(key, &d);
                switch (d->type) {
                case NVS_TYPE_U8:
                    http_printf(req, "%"PRIu8, d->value.u8);
                    break;
                case NVS_TYPE_I8:
                    http_printf(req, "%"PRIi8, d->value.i8);
                    break;
                case NVS_TYPE_U16:
                    http_printf(req, "%"PRIu16, d->value.u16);
                    break;
                case NVS_TYPE_I16:
                    http_printf(req, "%"PRIi16, d->value.i16);
                    break;
                case NVS_TYPE_U32:
                    http_printf(req, "%"PRIu32, d->value.u32);
                    break;
                case NVS_TYPE_I32:
                    http_printf(req, "%"PRIi32, d->value.i32);
                    break;
                case NVS_TYPE_U64:
                    http_printf(req, "%"PRIu64, d->value.u64);
                    break;
                case NVS_TYPE_I64:
                    http_printf(req, "%"PRIi64, d->value.i64);
                    break;
                case NVS_TYPE_STR:
                    http_write(req, d->value.str, d->size - 1);
                    break;
                case NVS_TYPE_BLOB:
                    http_write(req, d->value.blob, d->size);
                    break;
                case NVS_TYPE_ANY:
                    httpd_resp_set_status(req, "404 Not Found");
                    httpd_resp_send(req, NULL, 0);
                }
                nv_data_free(d);
                free(d);
            }
        }
    }

CLEANUP:
    if (buf != NULL)
        free(buf);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t api_mac(httpd_req_t *req)
{
    char *mac = get_mac();
    http_printf(req, "%02X:%02X:%02X:%02X:%02X:%02X",
                mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t api_time_set(httpd_req_t *req)
{
    int buf_len;
    char *buf = NULL;
    if (!api_key_check(1, req, &buf, &buf_len))
        goto CLEANUP;

    if (buf_len > 1) {
        {//if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            char force[1+1];
            int override = 0;
            if (httpd_query_key_value(buf, "force", (char *) force, sizeof(force)) == ESP_OK) {
                override = atoi(force);
            }

            if (!override && ntp_synced)
                goto CLEANUP;

            // YYYY-MM-DDThh:mm:ss without timezone
            char dt[4+1+2+1+2 +1+ 2+1+2+1+2 +1];
            if (httpd_query_key_value(buf, "dt", (char *) dt, sizeof(dt)) == ESP_OK) {
                char *ret = set_time(dt);
                if (ret != NULL)
                    goto CLEANUP;
            }
            httpd_resp_set_status(req, "400 Bad Request - dt");
        }
    }

CLEANUP:
    if (buf != NULL)
        free(buf);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}


static esp_err_t api_gpio(httpd_req_t *req)
{
    check_report((void *) req, (int (*)(void *, const char *, ...)) &http_printf);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t api_gpio_set(httpd_req_t *req)
{
    int buf_len;
    char *buf = NULL;
    if (!api_key_check(1, req, &buf, &buf_len))
        goto CLEANUP;

    if (buf_len > 1) {
        {//if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            char pin[2+1];
            int gpio = 0;
            if (httpd_query_key_value(buf, "gpio", (char *) pin, sizeof(pin)) == ESP_OK) {
                gpio = atoi(pin);
            } else {
                httpd_resp_set_status(req, "400 Bad Request - gpio");
                goto CLEANUP;
            }

            char mode[4+1];
            if (httpd_query_key_value(buf, "mode", (char *) mode, sizeof(mode)) == ESP_OK) {
                ESP_LOGI(TAG, "gpio %d mode %s", gpio, mode);
                if (strncmp(mode, "0", sizeof(mode)) == 0) {
                    gpio_set_direction(gpio, GPIO_MODE_DISABLE);
                } else if (strncmp(mode, "i", sizeof(mode)) == 0) {
                    gpio_set_direction(gpio, GPIO_MODE_INPUT);
                } else if (strncmp(mode, "o", sizeof(mode)) == 0) {
                    gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
                } else if (strncmp(mode, "ood", sizeof(mode)) == 0) {
                    gpio_set_direction(gpio, GPIO_MODE_OUTPUT_OD);
                } else if (strncmp(mode, "iood", sizeof(mode)) == 0) {
                    gpio_set_direction(gpio, GPIO_MODE_INPUT_OUTPUT_OD);
                } else if (strncmp(mode, "io", sizeof(mode)) == 0) {
                    gpio_set_direction(gpio, GPIO_MODE_INPUT_OUTPUT);
                } else {
                    httpd_resp_set_status(req, "400 Bad Request - mode");
                    goto CLEANUP;
                }
            }/* else {
                httpd_resp_set_status(req, "400 Bad Request - mode");
                goto CLEANUP;
            }*/

            char pull[1+1];
            if (httpd_query_key_value(buf, "pullup", (char *) pull, sizeof(pull)) == ESP_OK) {
                ESP_LOGI(TAG, "gpio %d pullup %d", gpio, atoi(pull));
                if (atoi(pull))
                    gpio_pullup_en(gpio);
                else
                    gpio_pullup_dis(gpio);
            }

            if (httpd_query_key_value(buf, "pulldown", (char *) pull, sizeof(pin)) == ESP_OK) {
                ESP_LOGI(TAG, "gpio %d pulldown %d", gpio, atoi(pull));
                if (atoi(pull))
                    gpio_pulldown_en(gpio);
                else
                    gpio_pulldown_dis(gpio);
            }

            char level[1+1];
            if (httpd_query_key_value(buf, "level", (char *) level, sizeof(level)) == ESP_OK) {
                relay_set_gpio(gpio, atoi(level));
            }

            if (httpd_query_key_value(buf, "level5", (char *) level, sizeof(level)) == ESP_OK) {
                relay_set_gpio_5v(gpio, atoi(level));
            }
        }
    }

CLEANUP:
    if (buf != NULL)
        free(buf);
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t api_stats(httpd_req_t *req)
{
    int buf_len;
    char *buf = NULL;
    // no authorization required
    api_key_check(0, req, &buf, &buf_len);

    httpd_resp_set_type(req, "text/plain");

    time_t now;
    time(&now);
    char timestr[4+1+2+1+2 +1+ 2+1+2+1+2 +1] = "";
    format_time(now, timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S");
    http_printf(req, "timestamp=%lld\n", now);
    http_printf(req, "time=%s\n", timestr);
    uint64_t runtime;
    if (ntp_synced)
        runtime = now - ntp_start + ntp_synced;
    else {
        TickType_t tick_now = xTaskGetTickCount();
        runtime = (TICK_TO_MS(tick_now) + esp.slept_ms) / 1000;
    }
    http_printf(req, "runtime=%lld\n", runtime);
    if (ntp_synced)
        http_printf(req, "runtime.synced=%lld\n", now - ntp_start);
    http_printf(req, "runtime.pm=%d\n", esp.pm);
    if (esp.sleep != NULL) {
        http_printf(req, "runtime.sleep_ms=%lld\n", esp.slept_ms);
        http_printf(req, "runtime.sleep=%d%%\n", (esp.slept_ms / 10) / runtime);
    }
    http_printf(req, "activity=%" PRIi32 "\n", now - esp.activity);
    http_printf(req, "ntp.start=%lld\n", ntp_start);
    http_printf(req, "nv.writes=%" PRIu32 "\n", nv_writes);
    http_printf(req, "net.online=%d\n", ping_online.connected);
    http_printf(req, "net.last_timestamp=%" PRIu64 "\n", (uint64_t) ping_online.last);
    if (ping_online.last > 0) {
        format_time(ping_online.last, timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S");
        http_printf(req, "net.last_time=%s\n", timestr);
    }

    http_printf(req, "heap.estimate_s=%f\n", heap_estimate_s);
    http_printf(req, "heap.estimate_d=%f\n", heap_estimate_s/(24.0*60*60));
    //http_printf(req, "heap.free=%zu\n", esp_get_free_heap_size());
    http_printf(req, "heap.first_free=%zu\n", heap_first_free);
    //http_printf(req, "heap.free=%zu\n", heap_caps_get_free_size(MALLOC_CAP_8BIT));
    //http_printf(req, "heap.total=%zu\n", heap_caps_get_total_size(MALLOC_CAP_8BIT));
    multi_heap_info_t heap = {0};
    heap_caps_get_info(&heap, MALLOC_CAP_8BIT);
#define PRINT_FIELD(DATA, PREFIX, FIELD) http_printf(req, PREFIX "." #FIELD "=%zu\n", DATA.FIELD)
    PRINT_FIELD(heap, "heap", total_free_bytes);
    PRINT_FIELD(heap, "heap", total_allocated_bytes);
    PRINT_FIELD(heap, "heap", largest_free_block);
    PRINT_FIELD(heap, "heap", minimum_free_bytes);
    PRINT_FIELD(heap, "heap", allocated_blocks);
    PRINT_FIELD(heap, "heap", free_blocks);
    PRINT_FIELD(heap, "heap", total_blocks);

    extern SemaphoreHandle_t lvgl_mutex;
    if (lvgl_mutex != NULL)
        http_printf(req, "lvgl.mutex.count=%d\n", uxSemaphoreGetCount(lvgl_mutex));
    if (oled.mutex_i2c != NULL)
        http_printf(req, "i2c.mutex.count=%d\n", uxSemaphoreGetCount(oled.mutex_i2c));

    http_printf(req, "socket.max=%d\n", CONFIG_LWIP_MAX_SOCKETS);
    http_printf(req, "socket.free=%d\n", uxSemaphoreGetCount(esp.sockets));

    http_printf(req, "task.total=%zu\n", uxTaskGetNumberOfTasks());
    http_printf(req, "task.managed=%zu\n", list_count(&tasks));

    iter_t itask = task_iter();
    task_t *task;
    while ((itask = task_next(itask, &task)) != NULL) {
        http_printf(req, "task.%s(%x).stack_min=%zu\n", task->name, task->task, uxTaskGetStackHighWaterMark(task->task));
    }

    nvs_stats_t nvs_stats;
    nvs_get_stats(NULL, &nvs_stats);
    PRINT_FIELD(nvs_stats, "nvs", used_entries);
    PRINT_FIELD(nvs_stats, "nvs", free_entries);
    PRINT_FIELD(nvs_stats, "nvs", total_entries);
    PRINT_FIELD(nvs_stats, "nvs", namespace_count);
    module_t *m;
    iter_t imod = module_iter();
    while ((imod = module_next(imod, &m)) != NULL)
        http_printf(req, "module %d type=%d name=%s state=%d\n", m, m->type, (m->name == NULL)? "(null)" : m->name, m->state);

//CLEANUP:
    if (buf != NULL)
        free(buf);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

// no blobs, format same as auto.c
static esp_err_t api_export(httpd_req_t *req)
{
    int buf_len;
    char *buf;
    if (!api_key_check(1, req, &buf, &buf_len))
        goto CLEANUP;

    httpd_resp_set_type(req, "text/plain");
    nvs_iterator_t iter = NULL;
    // counting on this namespace to be used by nv module
    esp_err_t res = nvs_entry_find(NVS_LABEL, NVS_NAMESPACE, NVS_TYPE_ANY, &iter);

    while (res == ESP_OK) {
        nvs_entry_info_t info;
        nvs_entry_info(iter, &info);

#define READ(TYPE, FN, INFO)                                            \
        TYPE value_ ## FN;                                              \
        ESP_ERROR_CHECK(nv_read_ ## FN(INFO.key, & value_ ## FN));      \

        switch (info.type) {
        case NVS_TYPE_U8:
            READ(uint8_t, u8, info);
            http_printf(req, "%s=%"PRIu8"\n", info.key, value_u8);
            break;
        case NVS_TYPE_I8:
            READ(int8_t, i8, info);
            http_printf(req, "%s=%"PRIi8"\n", info.key, value_i8);
            break;
        case NVS_TYPE_U16:
            READ(uint16_t, u16, info);
            http_printf(req, "%s=%"PRIu16"\n", info.key, value_u16);
            break;
        case NVS_TYPE_I16:
            READ(int16_t, i16, info);
            http_printf(req, "%s=%"PRIi16"\n", info.key, value_i16);
            break;
        case NVS_TYPE_U32:
            READ(uint32_t, u32, info);
            http_printf(req, "%s=%"PRIu32"\n", info.key, value_u32);
            break;
        case NVS_TYPE_I32:
            READ(int32_t, i32, info);
            http_printf(req, "%s=%"PRIi32"\n", info.key, value_i32);
            break;
        case NVS_TYPE_U64:
            READ(uint64_t, u64, info);
            http_printf(req, "%s=%"PRIu64"\n", info.key, value_u64);
            break;
        case NVS_TYPE_I64:
            READ(int64_t, i64, info);
            http_printf(req, "%s=%"PRIi64"\n", info.key, value_i64);
            break;
        case NVS_TYPE_STR:
            size_t size_str = 0;
            char *value_str;
            ESP_ERROR_CHECK(nv_read_str(info.key, &value_str, &size_str));
            http_printf(req, "%s=%s\n", info.key, value_str);
            free(value_str);
            break;
        case NVS_TYPE_BLOB:
            size_t size_blob = 0;
            ESP_ERROR_CHECK(nv_read_blob_size(info.key, &size_blob));
            //ESP_LOGW(TAG, "blob(%d) ignored", size_blob);
            http_printf(req, "#%s=blob[%d] use /nvdump?key=%s to retrieve\n", info.key, size_blob, info.key);
// not sure if this can cause issues in HTTP output
// no binary data is currently used in project namespace
#if 0
            char *value_blob;
            size_blob = 0;
            ESP_ERROR_CHECK(nv_read_blob(info.key, (void *) &value_blob, &size_blob));
            http_printf(req, "#%s=raw[%s]\n", info.key, value_blob);
            free(value_blob);
#endif
            break;
        case NVS_TYPE_ANY:
            ESP_LOGW(TAG, "found ANY type");
            http_printf(req, "#%s=ANY type\n", info.key);
            break;
        }

        res = nvs_entry_next(&iter);
    }
    nvs_release_iterator(iter);

CLEANUP:
    if (buf != NULL)
        free(buf);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

// this call should be for oneliners, use POST for full config
static esp_err_t api_auto(httpd_req_t *req)
{
    char *buf = NULL;
    int buf_len;
    if (!api_key_check(1, req, &buf, &buf_len))
        goto CLEANUP;

    if (buf_len > 1) {
        {//if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            // 2048 causes stack overflow, 1024 was ok so far
            // not parsed so can't use newlines, big size is pointless
            char config[128];
            if (httpd_query_key_value(buf, "config", (char *) config, sizeof(config)) == ESP_OK) {
                config_apply(NULL, (char *) config, strlen(config), 1, 1);
            }
        }
    }

CLEANUP:
    if (buf != NULL)
        free(buf);
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t api_auto_post(httpd_req_t *req)
{
    char *buf = NULL;
    int qbuf_len;
    char *qbuf;
    int auth = 0;
    if (api_key_check(1, req, &qbuf, &qbuf_len))
        auth = 1;

    int ret, remaining = req->content_len;
    buf = malloc(remaining+1);
    assert(buf != NULL);
    buf[remaining] = '\0';

    while (remaining > 0) {
        /* Read the data for the request */
        if ((ret = httpd_req_recv(req,
                                  buf + (req->content_len - remaining),
                                  remaining)) <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                /* Retry receiving if timeout occurred */
                continue;
            }
            free(buf);
            return ESP_FAIL;
        }

        remaining -= ret;
    }

    // post with curl -X POST --data-binary @filename to preserve newlines
    config_apply(NULL, buf, strlen(buf), 1, auth);

//CLEANUP:
    if (qbuf != NULL)
        free(qbuf);
    if (buf != NULL)
        free(buf);
    // End response
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static httpd_uri_t api_uris[] = {
    {
        .uri       = "/help",
        .method    = HTTP_GET,
        .handler   = api_help,
    },
    {
        .uri       = "/reboot",
        .method    = HTTP_GET,
        .handler   = api_reboot,
    },
    {
        .uri       = "/version",
        .method    = HTTP_GET,
        .handler   = api_version,
    },
    {
        .uri       = "/controller",
        .method    = HTTP_GET,
        .handler   = api_controller,
    },
    {
        .uri       = "/hostname",
        .method    = HTTP_GET,
        .handler   = api_hostname,
    },
    {
        .uri       = "/heating/hc",
        .method    = HTTP_GET,
        .handler   = api_heating_hc_get,
    },
    {
        .uri       = "/temp/zone",
        .method    = HTTP_GET,
        .handler   = api_temp_zone,
    },
    {
        .uri       = "/temp/set",
        .method    = HTTP_GET,
        .handler   = api_heating_temp_set,
    },
    {
        .uri       = "/temp/get",
        .method    = HTTP_GET,
        .handler   = api_heating_temp_get,
    },
    {
        .uri       = "/co2/ppm",
        .method    = HTTP_GET,
        .handler   = api_co2_ppm_get,
    },
    {
        .uri       = "/co2/abc",
        .method    = HTTP_GET,
        .handler   = api_co2_abc_get,
    },
    {
        .uri       = "/co2",
        .method    = HTTP_GET,
        .handler   = api_co2_get,
    },
    {
        .uri       = "/ota",
        .method    = HTTP_GET,
        .handler   = api_ota,
    },
    {
        .uri       = "/export",
        .method    = HTTP_GET,
        .handler   = api_export,
    },
    {
        .uri       = "/auto",
        .method    = HTTP_GET,
        .handler   = api_auto,
    },
    {
        .uri       = "/auto",
        .method    = HTTP_POST,
        .handler   = api_auto_post,
    },
    // some damage could be done by abusing this
    {
        .uri       = "/loglevel",
        .method    = HTTP_GET,
        .handler   = api_loglevel,
    },
    {
        .uri       = "/log",
        .method    = HTTP_GET,
        .handler   = api_log,
    },
    {
        .uri       = "/module",
        .method    = HTTP_GET,
        .handler   = api_module,
    },
    {
        .uri       = "/module",
        .method    = HTTP_POST,
        .handler   = api_module_post,
    },
    {
        .uri       = "/heap",
        .method    = HTTP_GET,
        .handler   = api_heap,
    },
    {
        .uri       = "/stats",
        .method    = HTTP_GET,
        .handler   = api_stats,
    },
    {
        .uri       = "/time/set",
        .method    = HTTP_GET,
        .handler   = api_time_set,
    },
    {
        .uri       = "/oled",
        .method    = HTTP_GET,
        .handler   = api_oled,
    },
    {
        .uri       = "/screenshot",
        .method    = HTTP_GET,
        .handler   = api_screenshot
    },
    {
        .uri       = "/button",
        .method    = HTTP_GET,
        .handler   = api_button,
    },
    {
        .uri       = "/debug",
        .method    = HTTP_GET,
        .handler   = api_debug,
    },
    // unrestricted access to NVS_LABEL NVS_NAMESPACE
    {
        .uri       = "/nvdump",
        .method    = HTTP_GET,
        .handler   = api_nvdump,
    },
    {
        .uri       = "/gpio/set",
        .method    = HTTP_GET,
        .handler   = api_gpio_set,
    },
    {
        .uri       = "/gpio",
        .method    = HTTP_GET,
        .handler   = api_gpio,
    },
    {
        .uri       = "/mac",
        .method    = HTTP_GET,
        .handler   = api_mac,
    },
};

void api_init(httpd_t *httpd)
{
    for (int i=0; i<COUNT_OF(api_uris); i++)
        httpd_register(httpd, &api_uris[i]);
}

esp_err_t api_help(httpd_req_t *req)
{
    for (int i=0; i<httpd_default_handlers_cnt; i++) {
        http_printf(req, "%d %s\n", httpd_default_handlers[i].method, httpd_default_handlers[i].uri);
    }
    for (int i=0; i<COUNT_OF(api_uris); i++) {
        http_printf(req, "%d %s\n", api_uris[i].method, api_uris[i].uri);
    }
    http_printf(req, "configuration:\n");
    list_t *iter = &auto_singleton->handlers;
    while ((iter = list_iter(iter)) != NULL) {
        http_printf(req, "%s is_blob=%d\n", LIST(auto_handler_t, iter, name), LIST(auto_handler_t, iter, is_blob));
    }
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

int api_key_check(int set_status, httpd_req_t *req, char **ptr_buf, int *ptr_buf_len)
{
    int buf_len;
    char *buf = NULL;
    int authorized = 0;

    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        if (buf == NULL) {
            httpd_resp_set_status(req, "507 Insufficient Storage");
            *ptr_buf = NULL;
            *ptr_buf_len = 0;
            return 0;
        }

        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            //ESP_LOGI(TAG, "Found URL query => %s", buf);
            // TODO possible ESP_ERR_HTTPD_RESULT_TRUNC
            char api_key[50];

            if (httpd_query_key_value(buf, "apikey", (char *) api_key, sizeof(api_key)) == ESP_OK) {
                if (strcmp(api_key, API_KEY) == 0)
                    authorized = 1;
            }
        } else {
            if (strlen(API_KEY) == 0)
                authorized = 1;
        }
    }

    if (set_status && !authorized)
        httpd_resp_set_status(req, "401 Unauthorized");

    // count successful authorization as activity preventing sleep
    if (authorized)
        time(&esp.activity);

    *ptr_buf_len = buf_len;
    *ptr_buf = buf;
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return authorized;
}
