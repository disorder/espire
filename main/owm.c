#include "config.h"
#include "http.h"
#include "oled.h"
#include <stdlib.h>
#include <time.h>
#include "cJSON.h"

#include "esp_log.h"
static const char *TAG = "owm";

//static TickType_t last = 0;
static time_t last = 0;
static task_t *task = NULL;

int owm_parse(char *out, int outsize, char *buf, int len)
{
    cJSON *json = cJSON_ParseWithLength(buf, len);
    if (json == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            // this can fail on allocation too, about 75k is ok
            // seen fail with heap largest = 24576
            buf[len-1] = '\0';
            ESP_LOGE(TAG, "Error before: %s\n", error_ptr);
            //ESP_LOGE(TAG, "Full: %s\n", buf);
            ESP_LOGE(TAG, "heap: %zu", esp_get_free_heap_size());
            multi_heap_info_t heap = {0};
            heap_caps_get_info(&heap, MALLOC_CAP_8BIT);
            ESP_LOGE(TAG, "heap largest: %zu", heap.largest_free_block);
        }
        return 0;
    }

    const cJSON *list;
    list = cJSON_GetObjectItemCaseSensitive(json, "list");
    const cJSON *item;
    int mday = 0;
    //int cnt;
    int temp_max = 100;
    int temp_min = -100;
    int cloud_min = 101;
    int cloud_max = -1;
    float wind_min = 1000;
    float wind_max = -1;
    float rain3h[24/3] = {-1,-1,-1,-1,-1,-1,-1,-1};
    int pressure1 = 0, pressure2 = 0;
    char dow[3 +1+ 2 +1];
    int ret = 0;
    cJSON_ArrayForEach(item, list) {
        cJSON *dt = cJSON_GetObjectItemCaseSensitive(item, "dt");

        if (!cJSON_IsNumber(dt))
            goto CLEANUP;

        struct tm dt_tm;
        time_t t = dt->valueint;
        gmtime_r(&t, &dt_tm);
        //char tbuf[4+1+2+1+2 +1+ 2+1+2 +1];
        //strftime(tbuf,sizeof(tbuf), "%Y-%m-%d %H%M", &dt_tm);
        //ESP_LOGI(TAG, "%lld %s", t, tbuf);

        if (mday != 0 && dt_tm.tm_mday != mday) {
            ret = snprintf(out, outsize, "%s: %d/%d %3d-%3d%% %d-%d\n", dow, temp_max, temp_min, cloud_min, cloud_max, pressure1, pressure2);
            if (ret > 0) {
                assert(ret <= outsize);
                out += ret;
                outsize -= ret;
            }
            for (int i=0; i<sizeof(rain3h)/sizeof(float); i++) {
                if (rain3h[i] > 0) {
                    ret = snprintf(out, outsize, "%d:%.1f ", 3*i, rain3h[i]);
                    if (ret > 0) {
                        assert(ret <= outsize);
                        out += ret;
                        outsize -= ret;
                    }
                    rain3h[i] = -1;
                }
            }
            ret = snprintf(out, outsize, "%.0f-%.0f km/h\n", wind_min, wind_max);
            if (ret > 0) {
                assert(ret <= outsize);
                out += ret;
                outsize -= ret;
            }
        }
        strftime(dow, sizeof(dow), "%a %d", &dt_tm);

        if (mday == 0 || dt_tm.tm_mday != mday) {
            mday = dt_tm.tm_mday;
            mday = dt_tm.tm_mday;
            temp_min = 100;
            temp_max = -100;
            wind_min = 1000;
            wind_max = -1;
            cloud_min = 101;
            cloud_max = -1;
            pressure1 = 0;
            pressure2 = 0;
        }

        const cJSON *main;
        const cJSON *clouds;
        //const cJSON *weather;
        const cJSON *rain;
        const cJSON *wind;
        //int humidity;
        main = cJSON_GetObjectItemCaseSensitive(item, "main");
        clouds = cJSON_GetObjectItemCaseSensitive(item, "clouds");
        //weather = cJSON_GetObjectItemCaseSensitive(item, "weather");
        rain = cJSON_GetObjectItemCaseSensitive(item, "rain");
        wind = cJSON_GetObjectItemCaseSensitive(item, "wind");
        cJSON *val;
        if (clouds != NULL) {
            if (dt_tm.tm_hour >= 12 && dt_tm.tm_hour <= 15) {
                val = cJSON_GetObjectItemCaseSensitive(clouds, "all");
                if (cJSON_IsNumber(val)) {
                    int cloud = val->valueint;
                    //ESP_LOGI(TAG, "%3d%%", cloud);
                    if (cloud > cloud_max)
                        cloud_max = cloud;
                    if (cloud < cloud_min)
                        cloud_min = cloud;
                }
            }
        }

        if (main != NULL) {
            val = cJSON_GetObjectItemCaseSensitive(main, "temp");
            if (cJSON_IsNumber(val)) {
                int temp = val->valueint - 273;
                //ESP_LOGI(TAG, " %d", temp);
                if (temp > temp_max)
                    temp_max = temp;
                if (temp < temp_min)
                    temp_min = temp;
            }

            /*
            val = cJSON_GetObjectItemCaseSensitive(main, "humidity");
            if (cJSON_IsNumber(val)) {
                //ESP_LOGI(TAG, " %3d%%", val->valueint);
                humidity = val->valueint;
            }
            */

            val = cJSON_GetObjectItemCaseSensitive(main, "pressure");
            if (cJSON_IsNumber(val)) {
                //ESP_LOGI(TAG, " %d", val->valueint);
                if (pressure1 == 0)
                    pressure1 = val->valueint;
                pressure2 = val->valueint;
            }
        }

        /*
        if (weather != NULL) {
            cJSON *witem;
            cJSON_ArrayForEach(witem, weather) {
                val = cJSON_GetObjectItemCaseSensitive(witem, "description");
                if (cJSON_IsString(val)) {
                    //ESP_LOGI(TAG, " %s", val->valuestring);
                }
            }
        }
        */

        if (rain != NULL) {
            val = cJSON_GetObjectItemCaseSensitive(rain, "3h");
            if (cJSON_IsNumber(val)) {
                rain3h[(dt_tm.tm_hour / 3) % 8] = val->valuedouble;
            }
        }

        if (wind != NULL) {
            val = cJSON_GetObjectItemCaseSensitive(wind, "gust");
            if (cJSON_IsNumber(val)) {
                float gust = val->valuedouble * 3.6;
                if (gust > wind_max)
                    wind_max = gust;
                if (gust < wind_min)
                    wind_min = gust;
            }
        }
    }
CLEANUP:
    cJSON_Delete(json);
    return 1;
}

static char owm_buf[33*11];
void owm_task_cb(http_request_t *req, int success)
{
    if (!success)
        goto CLEANUP;
    if (!req->client || esp_http_client_get_status_code(req->client) != 200) {
        goto CLEANUP;
    }

    if (esp_http_client_get_content_length(req->client) != req->bufsize) {
        ESP_LOGW(TAG, "content size mismatch %lld != %d", esp_http_client_get_content_length(req->client), req->bufsize);
        // this happens when we get disconnected
        goto CLEANUP;
    }

    // seeing NULL and 0 size
    //assert(req->buf != NULL);
    if (owm_parse(owm_buf, sizeof(owm_buf), req->buf, req->bufsize)) {
        oled_update.owm = owm_buf;
        ESP_LOGI(TAG, "%s", owm_buf);
        last = xTaskGetTickCount();
    }

CLEANUP:
    ESP_LOGI(TAG, "cleanup");
    if (req->buf != NULL)
        free(req->buf);
    free(req);
}

static char owm_url[] = "http://api.openweathermap.org/data/2.5/forecast?lat=" OWM_LAT "&lon=" OWM_LON "&appid=" OWM_API_KEY "&unit=metric";

void owm_task(void *pvParameter)
{
    //TickType_t last_task = 0;
    time_t last_task = 0;
    while (1) {
        if (last_task != 0) {
            time_t now;
            //if (last == 0 || last + S_TO_TICK(60*60) >= xTaskGetTickCount())
            if (last == 0 || last + 60 >= time(&now))
                //_vTaskDelay(S_TO_TICK(60));
                wall_clock_wait(60, S_TO_TICK(1));
            else
                // 3 hour windows, 1 hour period is fine
                //vTaskDelayUntil(&last_task, S_TO_TICK(60*60));
                wall_clock_wait(60*60, S_TO_TICK(10));
        }

        http_request_t *req = calloc(1, sizeof(http_request_t));
        assert(req != NULL);
        req->url = owm_url;
        req->callback = owm_task_cb;
        req->pad = 1;
        // fails if there is too little memory so prevent other requests
        req->exclusive = 1;
        https_get(req);

        //last_task = xTaskGetTickCount();
        time(&last_task);
        //_vTaskDelay(S_TO_TICK(...));
    }

    //xvTaskDelete(task);
}

void owm_init()
{
    if (task != NULL || sizeof(OWM_API_KEY) == 0)
        return;

    xxTaskCreate((void (*)(void*))owm_task, "owm_task", 2*1024, NULL, 0, &task);
}
