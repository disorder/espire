#include "config.h"
#include "device.h"
#include "ntp.h"
#include "wifi.h"
#include "http.h"
#include "oled.h"
#include "util.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>
#include <unistd.h>

#include "esp_log.h"
static const char *TAG = "shmu";

static task_t *task = NULL;
static int url_i = -1;
static char *urls[] = {
#if 0
    "http://espire:" STR(SHMU_PORT) "/radar",
    "http://espire:" STR(SHMU_PORT) "/eumssk",
    "http://espire:" STR(SHMU_PORT) "/eumseu",
#else
    "radar",
    "eumssk",
    "eumseu",
#endif
    // this entry refreshes screen to original information
    NULL,
};

// TODO remove
// experimental streaming freature, it's more complicated to do right
// when it can be uneven pixels, incomplete lines and running
// into txdata transfers the image is incomplete
// ...also has currently no way of knowing it finished
void shmu_stream(http_request_t *req)
{
    static int done = 0;
    static int has_half = 0;
    static unsigned short half = 0;
    if (req == NULL) {
        done = 0;
        has_half = 0;
        return;
    }
    if (req->bufsize == 0)
        return;
    lv_area_t area = {0};
    area.y1 = done / (160*2);
    area.y2 = area.y1;
    int i = 0;
    int l = 0;

    ESP_LOGW(TAG, "stream %d", req->bufsize);

    if (has_half) {
        area.x1 = (done % (160*2)) / 2;
        area.x2 = area.x1;
        ESP_LOGW(TAG, "%d,%d %d,%d half", area.x1, area.x2, area.y1, area.y2);
        half = half << 8 & req->buf[0];
        i += 1;
        // add old half and new half
        done += i+1;
        // bmp
        area.y1 = 79-area.y1;
        area.y2 = 79-area.y2;
        oled.disp_drv->flush_cb(oled.disp_drv, &area, (lv_color_t *) &half);
        // bmp
        area.y1 = 79-area.y1;
        area.y2 = 79-area.y2;
    }

    if (req->bufsize % 2 != has_half) {
        has_half = 1;
        half = req->buf[req->bufsize-1];
        // ignore
        req->bufsize -= 1;
    } else
        has_half = 0;

    // rest of line
    if (done % (160*2) != 0) {
        area.x1 = (done % (160*2)) / 2;
        if (done + 160*2 >= done-i+req->bufsize) {
            area.x2 = ((done-i+req->bufsize) % (160*2)) / 2;
            area.y2 = area.y1 + 1;
        } else {
            // could do more than one line at once if x1/x2 are aligned
            area.x2 = 160;
            area.y2 = area.y1 + 1;
        }
        l = (area.x2 - area.x1) * 2;
        area.x2 -= 1;
        area.y2 -= 1;
        ESP_LOGW(TAG, "%d,%d %d,%d rest", area.x1, area.x2, area.y1, area.y2);
        // bmp
        area.y1 = 79-area.y1;
        area.y2 = 79-area.y2;
        oled.disp_drv->flush_cb(oled.disp_drv, &area, (lv_color_t *) (req->buf+i));
        // bmp
        area.y1 = 79-area.y1;
        area.y2 = 79-area.y2;
        i += l;
        done += l;
    }

    area.x1 = 0;
    area.x2 = 160;
    area.y1 = done / (160*2);
    area.y2 = area.y1 + ((req->bufsize-i) / (160*2));
    if (area.y1 != area.y2) {
        l = (area.y2 - area.y1) * (160*2);
        area.x2 -= 1;
        area.y2 -= 1;
        ESP_LOGW(TAG, "%d,%d %d,%d full", area.x1, area.x2, area.y1, area.y2);
        // bmp
        area.y1 = 79-area.y1;
        area.y2 = 79-area.y2;
        oled.disp_drv->flush_cb(oled.disp_drv, &area, (lv_color_t *) (req->buf+i));
        // bmp
        area.y1 = 79-area.y1;
        area.y2 = 79-area.y2;
        i += l;
        done += l;
    }

    if (i != req->bufsize) {
        // start of line
        area.x1 = 0;
        area.x2 = ((done-i+req->bufsize) % (160*2)) / 2;
        area.y1 += 1;
        area.y2 = area.y1 + 1;
        l = (area.x2 - area.x1) * 2;
        area.x2 -= 1;
        area.y2 -= 1;
        ESP_LOGW(TAG, "%d,%d %d,%d start", area.x1, area.x2, area.y1, area.y2);
        // bmp, swap?
        area.y1 = 79-area.y1;
        area.y2 = 79-area.y2;
        oled.disp_drv->flush_cb(oled.disp_drv, &area, (lv_color_t *) (req->buf+i));
        // bmp, swap?
        area.y1 = 79-area.y1;
        area.y2 = 79-area.y2;
        i += l;
        done += l;
    }

    ESP_LOGW(TAG, "i=%d", i);
    // TODO failing
    //assert(i != req->bufsize);
    if (i != req->bufsize) ESP_LOGE(TAG, "UNMATCHED");
    // TODO too fast?  txdata transfer > host maximum
    //extern void (*disp_wait_for_pending_transactions)();
    //disp_wait_for_pending_transactions();
}

// TODO remove
void shmu_full(http_request_t *req, int success)
{
    if (success == 0)
        goto CLEANUP;

    ESP_LOGW(TAG, "full %d", req->bufsize);

    lv_area_t a = {
        .x1 = 0,
        .x2 = 160-1,
        .y1 = 0,
        .y2 = 80-1,
    };

    assert(req->bufsize == 160*80*2);

    // with ST7735S swap is always needed to fix little endian
#if 0
    uint16_t *b = (uint16_t *) req->buf;
    int idx = 0;
    for (int i=0; i<160*80; i++) {
#if 1
        // RGB image + BGR mode A8 + swap = good colors
        // ^ simple library had this setup
        // RGB image + RGB mode A0 + no swap = blue and red swapped
        // RGB image + RGB mode A0 + swap = blue and red swapped
        uint16_t c = b[i];
        req->buf[idx++] = c >> 8;
        req->buf[idx++] = c & 0xff;
#else
        // RGB image + RGB mode A0 + swap = blue and red swapped
        // swap red and blue first
        uint16_t _x = b[i] << 11;
        uint16_t _y = b[i] >> 11 & 0b11111;
        b[i] = (b[i] & 0b0000011111100000) | _x | _y;
        // this is probably to fix BMP little endian to hardware
        uint16_t c = b[i];
        req->buf[idx++] = c >> 8;
        req->buf[idx++] = c & 0xff;
        // RRRRRGGGGGGBBBBB
        // LE:  GGGBBBBB RRRRRGGG (BMP LE RGB565 bytes)
        // e.g. 111BBBBB RRRRR111
        // BE:  RRRRRGGG GGGBBBBB

        // RGB mode = red, BGR mode = blue
        //b[i] = 0b00011111 << 8 | 0b00000000;
        // RGB/BGR green
        //b[i] = 0b11100000 << 8 | 0b00000111;
        // RGB mode = blue, BGR mode = red
        // ^ seems that specification names for BGR/RGB modes is wrong?
        //req->buf[i*2] = 0b11111000;
        //req->buf[i*2+1] = 0;
        // zero green in BMP data
        //b[i] = (c & ~0b1110000000000111);
        // green
        //b[i] = 0b1110000000000111;
        // magenta
        //b[i] = 0b0001111111111000;
        // red
        //b[i] = 0b0001111100000000;
        // blue
        //b[i] = 0b0000000011111000;

        // TODO remove
        // BMP source is little endian RGB
        //uint16_t g = (c >> 13) | ((c & 0b111) << 13);
        // green swap did not help
        //b[i] = (c & ~0b1110000000000111) | g;
        //b[i] = (c & ~0b1110000000000111) | (~g & 0b1110000000000111);

        //uint8_t g1 = req->buf[i*2] << 5;
        //uint8_t g2 = req->buf[i*2+1] >> 5;
        //req->buf[i*2] = (req->buf[i*2] & ~0x111) | g2;
        //req->buf[i*2+1] = (req->buf[i*2+1] & 0x11100000) | g1;

        // not good
        //uint16_t g1 = b[i] << 3 & 0b0000011100000000;
        //uint16_t g2 = b[i] >> 3 & 0b0000000011100000;
        //b[i] = (b[i] & 0b1111100000011111) | g1 | g2;
        // bad
        //uint16_t g = ~b[i] & 0b0000011111100000;
        //b[i] = (b[i] & 0b1111100000011111) | g;
#endif
    }
#endif

    for (int y=0; y<80; y++) {
        a.y1 = y;
        a.y2 = y;
#ifndef ST7735S_SIMPLE
        // uses queue so probably causes issues without static buffer
        static uint16_t buf[160];
        memcpy(buf, req->buf + (80-1-a.y1)*160*2, 160*2);
        oled.disp_drv->flush_cb(oled.disp_drv, &a, (lv_color_t *) buf);
#else
        oled.disp_drv->flush_cb(oled.disp_drv, &a, (lv_color_t *) (req->buf + (80-1-a.y1)*160*2));
#endif
    }

CLEANUP:
    if (req->buf) {
        free(req->buf);
        req->buf = NULL;
    }
    // seeing failures http: Failed to allocate memory for output buffer
    if (success == 0) {
        ESP_LOGE(TAG, "heap: %" PRIu32, esp_get_free_heap_size());
        multi_heap_info_t heap = {0};
        heap_caps_get_info(&heap, MALLOC_CAP_8BIT);
        ESP_LOGE(TAG, "heap largest: %u", heap.largest_free_block);
    }
    // use as a flag that worker is not running
    task = NULL;
}

static void shmu_tcp(char *request)
{
    int sock;
    struct sockaddr_in sa;

    if (xSemaphoreTake(esp.sockets, portMAX_DELAY) != pdTRUE)
        return;

    if ((sock = socket(PF_INET, SOCK_STREAM, 0)) == -1) {
        ESP_LOGE(TAG, "socket: %s", strerror(errno));
        goto CLEANUP;
    }

    int optval = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
               (const void *)&optval , sizeof(int));

    bzero((void *) &sa, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(SHMU_PORT);
    if (inet_aton(SHMU_IP, &sa.sin_addr.s_addr) == 0) {
        ESP_LOGW(TAG, "invalid IP: %s", SHMU_IP);
        if ((sa.sin_addr.s_addr = resolve_hostname(SHMU_IP)) == INADDR_ANY) {
            ESP_LOGW(TAG, "could not resolve hostname: %s", SHMU_IP);
            goto CLEANUP;
        }
    }

    if (connect(sock, (struct sockaddr *) &sa, sizeof(sa)) == -1) {
        ESP_LOGE(TAG, "connect: %s", strerror(errno));
        goto CLEANUP;
    }

    write(sock, request, strlen(request));
    shutdown(sock, SHUT_WR);

    // needs to be static or somehow wait until it's written
    //static char buf[160*2];
    #define BUFLINES 2
    static char buf[160*2 * BUFLINES];
    int c = 0;
    int y = 0;
    // centered
    int ox = (oled.w-160) / 2;
    int oy = (oled.h-80) / 2;
    lv_area_t a = {
        .x1 = ox,
        .x2 = ox + 160-1,
        .y1 = oy,
        .y2 = oy + 80-1,
    };
    while (1) {
        int offset = (y % BUFLINES)*160*2;
        ssize_t n = read(sock, buf+c + offset, 160*2 - c);

        if (n <= 0)
            break;

        c += n;
        if (c == 160*2) {
            // reversed for BMP order
            a.y1 = oy + 80-1-y;
            a.y2 = oy + 80-1-y;
            oled.disp_drv->flush_cb(oled.disp_drv, &a, (lv_color_t *) (buf + offset));

            y += 1;
            c = 0;
            if (y == 80)
                break;

#if 0
            // too fast, getting glitches
            if (y % BUFLINES == 0)
                vTaskDelay(1);
#endif
        }
    }

    //shutdown(sock, SHUT_RDWR);
CLEANUP:
    if (sock >= 0)
        close(sock);
    xSemaphoreGive(esp.sockets);
}

void shmu_task(void *pvParameter)
{
#if 0
    static http_request_t req = {
        //.callback_stream = shmu_stream,
        .callback = shmu_full,
        .exclusive = 1,
    };

    req.url = urls[url_i];
    if (req.callback_stream)
        req.callback_stream(NULL);
    https_get(&req);
#else
    // reads and blits line by line with static buffer
    shmu_tcp(urls[url_i]);
    task = NULL;
#endif

    xvTaskDelete(NULL);
}

void shmu(int add)
{
    if (oled.depth != 16 || (!wifi_connected && ntp_synced))
        return;
    if (task != NULL) {
        ESP_LOGI(TAG, "shmu in progress");
        return;
    }

    if (url_i != -1) {
        url_i += add;
        while (url_i < 0)
            url_i += COUNT_OF(urls);
        url_i %= COUNT_OF(urls);
    } else
        url_i = 0;
    if (urls[url_i] == NULL)
        oled_invalidate(&oled);
    else
        xxTaskCreate((void (*)(void*)) shmu_task, "shmu_task", 5*1024, NULL, 2, &task);
}
