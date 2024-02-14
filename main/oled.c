#include "FreeRTOSConfig.h"
#include "config.h"
#include "sdkconfig.h"
#include "util.h"
#include "oled.h"
#include "check.h"

// based on example (licensed CC0)

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "oled";

#define SSD1306_BSIZE 20
#define ST7735S_BSIZE 10
#define ST7789_BSIZE 10

#define ST7789_1_H_RES 240
#define ST7789_1_V_RES 240

#define ST7789_2_H_RES 320
#define ST7789_2_V_RES 240

#ifdef LCD_ST7735S
#define LCD_HOST SPI2_HOST
// for landscape
#define ST7735S_OFFSETX 1
#define ST7735S_OFFSETY 26
#define ST7735S_H_RES 160
#define ST7735S_V_RES 80
#define CONFIG_MOSI_GPIO 23 // SDA
#define CONFIG_SCLK_GPIO 18 // SCL
#define CONFIG_TFT_CS_GPIO 16

#define CONFIG_DC_GPIO 17
//#define CONFIG_LV_DISP_PIN_DC CONFIG_DC_GPIO
//#define ST7735S_DC CONFIG_DC_GPIO
#define CONFIG_RESET_GPIO 5
//#define ST7735S_RST CONFIG_RESET_GPIO
//#define CONFIG_LV_DISP_PIN_RST CONFIG_RESET_GPIO
#define CONFIG_BL_GPIO 19
#define MISO_GPIO -1
#define XPT_CS_GPIO -1
#define XPT_IRQ_GPIO -1
#define TFT_MODEL 0x7735

#ifdef ST7735S_SIMPLE
#include "ili9340.h"
static TFT_t tft = {0};
// Using SPI2 in the example, as it also supports octal modes on some targets
#else // SIMPLE

//#define LV_HOR_RES DISP_H_RES
#include "st7735s.h"
#include "disp_spi.h"
#include "lvgl_helpers.h"
#endif // !SIMPLE
#endif

#if defined(LCD_ST7789_1) || defined(LCD_ST7789_2)
#include "st7789.h"
#include "disp_spi.h"
#include "lvgl_helpers.h"
#endif

// not using pwm so this is just bloated way to set gpio, also something turns it back on
//#define LV_BACKLIGHT
#ifdef LV_BACKLIGHT
#include "esp_lcd_backlight.h"
const disp_backlight_config_t bckl_config = {
    .gpio_num = CONFIG_LV_DISP_PIN_BCKL,
#if defined CONFIG_LV_DISP_BACKLIGHT_PWM
    .pwm_control = true,
#else
    .pwm_control = false,
#endif
#if defined CONFIG_LV_BACKLIGHT_ACTIVE_LVL
    .output_invert = false, // Backlight on high
#else
    .output_invert = true, // Backlight on low
#endif
    .timer_idx = 0,
    .channel_idx = 0 // @todo this prevents us from having two PWM controlled displays
};
disp_backlight_h *bl = NULL;
#endif // LV_BACKLIGHT

#define OLED_LCD_PIXEL_CLOCK_HZ    SSD1306_CLOCK_HZ
#define OLED_PIN_NUM_SDA           I2C_SDA_PIN
#define OLED_PIN_NUM_SCL           I2C_SCL_PIN
#define OLED_PIN_NUM_RST           SSD1306_PIN_RST
#define OLED_I2C_HW_ADDR           OLED_I2C_ADDR

// The pixel number in horizontal and vertical
#ifdef OLED_SSD1306
#define _DISP_H_RES             SSD1306_H_RES
#define _DISP_V_RES             SSD1306_V_RES
#elif defined LCD_ST7789_1
#define _DISP_H_RES             ST7789_1_H_RES
#define _DISP_V_RES             ST7789_1_V_RES
#elif defined LCD_ST7789_2
#define _DISP_H_RES             ST7789_2_H_RES
#define _DISP_V_RES             ST7789_2_V_RES
#else
#define _DISP_H_RES             ST7735S_H_RES
#define _DISP_V_RES             ST7735S_V_RES
#endif
// Bit number used to represent command and parameter
#define SSD1306_CTRL_PHASE_BYTES 1
#define SSD1306_DC_BIT_OFFSET 6
#define SSD1306_CMD_BITS 8
#define SSD1306_PARAM_BITS 8
#define OLED_CTRL_PHASE_BYTES       SSD1306_CTRL_PHASE_BYTES
#define OLED_DC_BIT_OFFSET          SSD1306_DC_BIT_OFFSET
// same for st7735s
#define OLED_LCD_CMD_BITS           SSD1306_CMD_BITS
#define OLED_LCD_PARAM_BITS         SSD1306_PARAM_BITS

// LVGL is not thread safe
SemaphoreHandle_t lvgl_mutex = NULL;

display_t oled = {
    .w = _DISP_H_RES,
    .h = _DISP_V_RES,
    .mutex_i2c = NULL,
    .disp_drv = NULL,
    .disp = NULL,
    .scr = NULL,
    .power = 0,
    .panel_handle = NULL,
};

static lv_theme_t * th = NULL;

void theme_init(display_t *oled)
{
    if (oled->disp == NULL)
        return;

    assert(th == NULL);
    th = lv_theme_mono_init(oled->disp, true, &lv_font_unscii_8);
    /*
    th = lv_theme_default_init(
        oled->disp,
        lv_color_white(), lv_color_white(),
        true,
        &lv_font_unscii_8
    );
    */
    assert(th != NULL);
    lv_disp_set_theme(oled->disp, th);
}

static void oled_task(void *pvParameter)
{
    while (1) {
        // raise the task priority of LVGL and/or reduce the handler period can improve the performance
        //_vTaskDelay(pdMS_TO_TICKS(10));
        //_vTaskDelay(pdMS_TO_TICKS(LVGL_TICK_PERIOD_MS));
        _vTaskDelay(pdMS_TO_TICKS(oled.lvgl_tick_timer_ms));
        // The task running lv_timer_handler should have lower priority than that running `lv_tick_inc`
        LVGL_ENTER(0);
        lv_timer_handler();
        LVGL_EXIT();
    }
}

static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_disp_drv_t *disp_driver = (lv_disp_drv_t *)user_ctx;
    lv_disp_flush_ready(disp_driver);
    return false;
}

inline char oled_get_px(uint8_t *buf, int buf_w, int x, int y)
{
    uint16_t byte_index = x + (( y >> 3 ) * buf_w);
    uint8_t  bit_index  = y & 0x7;

    return (buf[byte_index] >> bit_index) & 1;
}

// seeing glitches in the image often and sometimes on screen
char *screenshot = NULL;
static void example_lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    static int seen = 0;
    // TODO this was tailored to oled, lvgl drew 16 lines at once
    //int seenmask = 1 << (area->y2 / 16);
    int seenmask = 1 << (area->y2 / 10);
    if (screenshot != NULL && (seen & seenmask) == 0 &&
        area->x1 == 0 && (area->x1 != area->x2) &&
        area->x2 == oled.w-1
    ) {
        ESP_LOGD(TAG, "screenshot %d = %d %d %d %d", seen, area->x1, area->x2, area->y1, area->y2);
        // this only works if area width = oled.w otherwise use oled_get_px
        // assert(area->x2 == oled.w-1 && area->x1 == 0);
        char *dest = screenshot + (area->y1 * oled.w * oled.depth / 8);
        memcpy(dest, color_map, (area->y2 - area->y1 + 1) * oled.w*oled.depth/8);

        /*
        for (int x=0; x<=area->x2-area->x1; x++) {
            for (int y=0; y<=area->y2-area->y1; y++) {
                // buf_w is mostly full width, but seen smaller
                char val = oled_get_px((uint8_t *) color_map,
                                       area->x2-area->x1+1, x, y);
                //screenshot[(area->x1+x) + (area->y1+y)*oled.w] = val;
                // BMP is reversed
                screenshot[area->x1+x + (oled.h-1-y-area->y1)*oled.w] = val;
            }
        }
        */

        // this doesn't always happen in order
        seen |= seenmask;
        // TODO this was tailored to oled, lvgl drew 16 lines at once
        //if (seen == (1 << (_DISP_V_RES/16)) - 1) {
        if (seen == (1 << (oled.h/10)) - 1) {
            ESP_LOGE(TAG, "screenshot rendered");
            screenshot = NULL;
            seen = 0;
        }
    }
    // NOTE
    // seeing x1=x2=0, this surely means not even one column but this does
    // not seem to be the reason for glitches in the first column
    if (area->x1 != area->x2) {
        // copy a buffer's content to a specific area of the display
        display_t *oled = (display_t *) drv->user_data;
        if (oled->mutex_i2c != NULL)
            xSemaphoreTake(oled->mutex_i2c, portMAX_DELAY);
        // any panel with esp-idf driver
        if (oled->panel_handle) {
            int offsetx1 = area->x1;
            int offsetx2 = area->x2;
            int offsety1 = area->y1;
            int offsety2 = area->y2;
            esp_err_t err = esp_lcd_panel_draw_bitmap(oled->panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);
            if (err != ESP_OK)
                ESP_LOGE(TAG, "i2c/spi error %d", err);
            // TODO let's see what happens when blocked
            // ESP_FAIL - broken OLED
            // ESP_ERR_TIMEOUT Operation timeout because the bus is busy.
            lv_disp_flush_ready(drv);
        } else {
#ifdef LCD_ST7735S
#ifndef ST7735S_SIMPLE
            st7735s_flush(drv, area, color_map);
#else
            int w = area->x2 - area->x1 + 1;
            for (int y=0; y<=area->y2-area->y1; y++)
                lcdDrawMultiPixels(&tft, area->x1, area->y1+y, w, (uint16_t*) (color_map + y*w));
            lv_disp_flush_ready(drv);
#endif
#elif defined(LCD_ST7789_1) || defined(LCD_ST7789_2)
            st7789_flush(drv, area, color_map);
#endif
        }
        if (oled->mutex_i2c != NULL)
            xSemaphoreGive(oled->mutex_i2c);
    } else {
        lv_disp_flush_ready(drv);
    }
}

static void ssd1306_lvgl_set_px_cb(lv_disp_drv_t *disp_drv, uint8_t *buf, lv_coord_t buf_w, lv_coord_t x, lv_coord_t y,
                                   lv_color_t color, lv_opa_t opa)
{
    uint16_t byte_index = x + (( y >> 3 ) * buf_w);
    uint8_t  bit_index  = y & 0x7;

    if ((color.full == 0) && (LV_OPA_TRANSP != opa)) {
        buf[byte_index] |= (1 << bit_index);
    } else {
        buf[byte_index] &= ~(1 << bit_index);
    }
}

static void ssd1306_lvgl_rounder(lv_disp_drv_t *disp_drv, lv_area_t *area)
{
    area->y1 = area->y1 & (~0x7);
    area->y2 = area->y2 | 0x7;
}

static void example_increase_lvgl_tick(void *arg)
{
    /* Tell LVGL how many milliseconds has elapsed */
    //lv_tick_inc(LVGL_TICK_PERIOD_MS);
    lv_tick_inc(oled.lvgl_tick_timer_ms);
}

void lvgl_monitor_cb(lv_disp_drv_t * disp_drv, uint32_t ms, uint32_t px)
{
    ESP_LOGI(TAG, "%"PRIu32" px refreshed in %"PRIu32" ms", px, ms);
}

#if I2C_SCAN
static void i2c_scan(int host)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    for (int addr=1; addr<127; addr++) {
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, 1);
        i2c_master_stop(cmd);
        esp_err_t ret = i2c_master_cmd_begin(host, cmd, MS_TO_TICK(100));
        if (ret == ESP_OK)
            ESP_LOGI(TAG, "I2C device 0x%x found", addr);
    }
    i2c_cmd_link_delete(cmd);
}
#endif

static int i2c_test(int host, int addr)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, 1);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(host, cmd, MS_TO_TICK(100));
    i2c_cmd_link_delete(cmd);
    ESP_LOGI(TAG, "I2C device 0x%x: %d", addr, ret == ESP_OK);
    return (ret == ESP_OK);
}

static void i2c_init(int host)
{
    gpio_set_pull_mode(21, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(22, GPIO_PULLUP_ONLY);
    if (gpio_owner(OLED_PIN_NUM_SDA) == OLED)
        check_gpio_clear(OLED_PIN_NUM_SDA);
    if (gpio_owner(OLED_PIN_NUM_SCL) == OLED)
        check_gpio_clear(OLED_PIN_NUM_SCL);

    if (gpio_owner(OLED_PIN_NUM_SDA) != NONE ||
        gpio_owner(OLED_PIN_NUM_SCL) != NONE) {
        ESP_LOGE(TAG, "I2C pins are not available");
        return;
    }
    check_gpio(OLED_PIN_NUM_SDA, OLED);
    check_gpio(OLED_PIN_NUM_SCL, OLED);

    ESP_LOGI(TAG, "Initialize I2C bus");
    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = OLED_PIN_NUM_SDA,
        .scl_io_num = OLED_PIN_NUM_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = OLED_LCD_PIXEL_CLOCK_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config(host, &i2c_conf));
    ESP_ERROR_CHECK(i2c_driver_install(host, I2C_MODE_MASTER, 0, 0, 0));
}

int oled_ssd1306_i2c(display_t *oled)
{
    static lv_disp_drv_t disp_drv;      // contains callback functions
    if (oled->mutex_i2c == NULL) {
        oled->mutex_i2c = xSemaphoreCreateMutex();
    }
    assert(oled->mutex_i2c != NULL);

    i2c_init(I2C_HOST);
    if (!i2c_test(I2C_HOST, OLED_I2C_HW_ADDR)) {
        ESP_LOGE(TAG, "I2C device not found");
#if I2C_SCAN
        i2c_scan(I2C_HOST);
#endif
        i2c_driver_delete(I2C_HOST);
        return 0;
    }

    ESP_LOGI(TAG, "Install panel IO");
    esp_lcd_panel_io_handle_t io_handle;
    esp_lcd_panel_io_i2c_config_t io_config = {
        .dev_addr = OLED_I2C_HW_ADDR,
        .control_phase_bytes = OLED_CTRL_PHASE_BYTES,
        .dc_bit_offset = OLED_DC_BIT_OFFSET,
        .lcd_cmd_bits = OLED_LCD_CMD_BITS,
        .lcd_param_bits = OLED_LCD_CMD_BITS,
        .on_color_trans_done = notify_lvgl_flush_ready,
        .user_ctx = &disp_drv,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)I2C_HOST, &io_config, &io_handle));

    ESP_LOGI(TAG, "Install SSD1306 panel driver");
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .bits_per_pixel = 1,
        .color_space = ESP_LCD_COLOR_SPACE_MONOCHROME,
        .reset_gpio_num = OLED_PIN_NUM_RST,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(io_handle, &panel_config, &panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    oled->disp_drv = &disp_drv;
    esp_lcd_panel_io_handle_t old_io_handle = oled->io_handle;
    esp_lcd_panel_handle_t old_panel_handle = oled->panel_handle;
    oled->io_handle = io_handle;
    oled->panel_handle = panel_handle;
    if (old_panel_handle) {
        oled->disp_drv->user_data = oled;
        xSemaphoreTake(oled->mutex_i2c, portMAX_DELAY);
        esp_lcd_panel_del(old_panel_handle);
        esp_lcd_panel_io_del(old_io_handle);
        xSemaphoreGive(oled->mutex_i2c);
    }

    return 1;
}

void oled_ssd1306_init(display_t *oled)
{
    if (lvgl_mutex == NULL) {
        lvgl_mutex = xSemaphoreCreateMutex();
    }
    assert(lvgl_mutex != NULL);
    static lv_disp_draw_buf_t disp_buf; // contains internal graphic buffer(s) called draw buffer(s)

    if (oled->panel_handle == NULL)
        if (!oled_ssd1306_i2c(oled))
            return;

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();
    // alloc draw buffers used by LVGL
    // it's recommended to choose the size of the draw buffer(s) to be at least 1/10 screen sized
    oled->w = SSD1306_H_RES;
    oled->h = SSD1306_V_RES;
    lv_color_t *buf1 = malloc(oled->w * SSD1306_BSIZE * sizeof(lv_color_t));
    assert(buf1);
    lv_color_t *buf2 = malloc(oled->w * SSD1306_BSIZE * sizeof(lv_color_t));
    assert(buf2);
    // initialize LVGL draw buffers
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, oled->w * SSD1306_BSIZE);

    ESP_LOGI(TAG, "Register display driver to LVGL");
    assert(oled->disp_drv != NULL);
    lv_disp_drv_init(oled->disp_drv);
    oled->disp_drv->hor_res = oled->w;
    oled->disp_drv->ver_res = oled->h;
    oled->disp_drv->flush_cb = example_lvgl_flush_cb;
    oled->disp_drv->draw_buf = &disp_buf;
    oled->disp_drv->user_data = oled;
    oled->disp_drv->rounder_cb = ssd1306_lvgl_rounder;
    oled->disp_drv->set_px_cb = ssd1306_lvgl_set_px_cb;
    // these are not working?
    //oled->disp_drv.rotated = LV_DISP_ROT_180;
    //lv_disp_set_rotation(disp, LV_DISP_ROT_180);
    //oled->disp_drv.sw_rotate = LV_DISP_ROT_180;
    // this works, remembers state until power down
    //esp_lcd_panel_mirror(oled->panel_handle, true, true);
    esp_lcd_panel_invert_color(oled->panel_handle, true);
    //esp_lcd_panel_disp_on_off(oled->panel_handle, 1);

    //oled->disp_drv->monitor_cb = lvgl_monitor_cb;
    lv_disp_t *disp = oled->disp;
    if (disp == NULL) {
        disp = lv_disp_drv_register(oled->disp_drv);
    }

    oled->disp = disp;
    oled->scr = lv_disp_get_scr_act(disp);
    oled->depth = 1;

    ESP_LOGI(TAG, "Install LVGL tick timer");
    // Tick interface for LVGL (using esp_timer to generate 2ms periodic event)
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &example_increase_lvgl_tick,
        .name = "lvgl_tick"
    };
    if (oled->lvgl_tick_timer == NULL) {
        oled->lvgl_tick_timer_ms = LVGL_TICK_PERIOD_MS;
        ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &oled->lvgl_tick_timer));
        ESP_ERROR_CHECK(esp_timer_start_periodic(oled->lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000));
    }

    // oled_task priority = 3 < CONFIG_FREERTOS_TIMER_TASK_PRIORITY = 4
    // any drawing task at least 4 too?
    xxTaskCreate((void (*)(void*)) oled_task, "oled_task", 8*1024, NULL, 3, NULL);
}

inline int LVGL_ENTER(int nonblock)
{
    assert(lvgl_mutex != NULL);
    return xSemaphoreTake(lvgl_mutex, (nonblock)? 0 : portMAX_DELAY) == pdTRUE;
}

inline void LVGL_EXIT()
{
    assert(lvgl_mutex != NULL);
    xSemaphoreGive(lvgl_mutex);
}

void backlight(int level)
{
#ifdef ST7735S_SIMPLE
#if CONFIG_BL_GPIO >= 0
    //level?  lcdBacklightOn(&tft) : lcdBacklightOff(&tft);
    gpio_set_level(CONFIG_BL_GPIO, level? 1 : 0);
#endif
#else
#ifdef LV_BACKLIGHT
    disp_backlight_set(bl, level);
#else
#if CONFIG_LV_DISP_PIN_BCKL >= 0
    gpio_set_level(CONFIG_LV_DISP_PIN_BCKL, level? 1 : 0);
#endif
#endif
#endif
}

void oled_power(display_t *oled, int on)
{
    if (oled->panel_handle) {
        ESP_LOGI(TAG, "power=%d", on);
        esp_lcd_panel_disp_on_off(oled->panel_handle, on);
        oled->power = on? 1 : 0;
    } else {
#if CONFIG_BL_GPIO >= 0 || CONFIG_LV_DISP_PIN_BCKL >= 0
        ESP_LOGI(TAG, "backlight=%d", on);
        backlight(on? 100 : 0);
        oled->power = on? 1 : 0;
#endif
    }
}

void oled_invert(display_t *oled, int invert)
{
    if (oled->panel_handle == NULL)
        return;
    ESP_LOGI(TAG, "invert=%d", invert);
    esp_lcd_panel_invert_color(oled->panel_handle, invert);
}

void oled_mirror(display_t *oled, int x, int y)
{
    if (oled->panel_handle == NULL)
        return;
    // 1,1 is useful and then can't flip back until power off
    // in various mirrored modes until power off
    ESP_LOGI(TAG, "mirror x=%d y=%d", x, y);
    esp_lcd_panel_mirror(oled->panel_handle, x, y);
}

void oled_invalidate(display_t *oled)
{
    if (oled->scr == NULL)
        return;
    LVGL_ENTER(0);
    lv_obj_invalidate(oled->scr);
    LVGL_EXIT();
    // this is not exactly necessary but maybe it speeds things up
    // ...it probably triggers callback immediately and takes mutex
    assert(oled->disp != NULL);
    lv_refr_now(oled->disp);
}

void oled_reinit(display_t *oled)
{
    if (oled->io_handle == NULL) {
        //ESP_LOGE(TAG, "reinit: no I/O handle");
        //return;
        oled_ssd1306_init(oled);
        return;
    }

    i2c_driver_delete(I2C_HOST);
    oled_ssd1306_i2c(oled);
    if (lvgl_mutex != NULL && uxSemaphoreGetCount(lvgl_mutex) == 0) {
        // end stuck flush and release mutex
        // TODO still not sure what happens now - no leak and no stuck task?
        notify_lvgl_flush_ready(oled->io_handle, NULL, oled->disp_drv);
        xQueueReset(lvgl_mutex);
    }
    // it's the same static drv
    //lv_disp_drv_update(oled->disp, oled->disp_drv);
    // TODO doesn't seem to work after reinit at least on 1 oled?
    oled_power(oled, oled->power);
}

void oled_reset(display_t *oled)
{
    ESP_ERROR_CHECK(esp_lcd_panel_reset(oled->panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(oled->panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(oled->panel_handle, oled->power));
}

/*
void oled_init(display_t *oled)
{
    //if (oled->scr != NULL) {
    //    lv_obj_clean(oled->scr);
    //}
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(oled->panel_handle, true));
    oled->power = 1;
}
*/

// initialization silently fails if there is no hw
// experimental support as (color) alternative to SSD1306
#ifdef LCD_ST7735S
void tft_st7735s_init(display_t *oled)
{
    oled->w = ST7735S_H_RES;
    oled->h = ST7735S_V_RES;

#ifdef ST7735S_SIMPLE
    check_gpio(CONFIG_BL_GPIO, OLED);
    gpio_reset_pin(CONFIG_BL_GPIO);
    gpio_set_direction(CONFIG_BL_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(CONFIG_BL_GPIO, 1);
#else
    // seems to be optional
    check_gpio(CONFIG_LV_DISP_PIN_BCKL, OLED);
    gpio_reset_pin(CONFIG_LV_DISP_PIN_BCKL);
    gpio_set_direction(CONFIG_LV_DISP_PIN_BCKL, GPIO_MODE_OUTPUT);
    gpio_set_level(CONFIG_LV_DISP_PIN_BCKL, 1);
#endif

#ifdef ST7735S_SIMPLE
    // simpler driver
    spi_master_init(
        &tft,
        CONFIG_MOSI_GPIO, CONFIG_SCLK_GPIO,
        CONFIG_TFT_CS_GPIO, CONFIG_DC_GPIO,
        CONFIG_RESET_GPIO, CONFIG_BL_GPIO,
        MISO_GPIO, XPT_CS_GPIO, XPT_IRQ_GPIO);
    check_gpio(CONFIG_MOSI_GPIO, OLED);
    check_gpio(CONFIG_SCLK_GPIO, OLED);
    check_gpio(CONFIG_TFT_CS_GPIO, OLED);
    check_gpio(CONFIG_DC_GPIO, OLED);
    check_gpio(CONFIG_RESET_GPIO, OLED);

    lcdInit(&tft, TFT_MODEL, ST7735S_H_RES, ST7735S_V_RES, ST7735S_OFFSETX, ST7735S_OFFSETY);
#else
    // after replacing partial with full featured component, this  library
    // seems to eat as much as +4K of heap

    // pins from menuconfig
    check_gpio(CONFIG_LV_DISP_SPI_MOSI, OLED);
    check_gpio(CONFIG_LV_DISP_SPI_CLK, OLED);
    check_gpio(CONFIG_LV_DISP_SPI_CS, OLED);
    check_gpio(CONFIG_LV_DISP_PIN_DC, OLED);
    // seems to have software reset too
    check_gpio(CONFIG_LV_DISP_PIN_RST, OLED);

#ifdef LV_BACKLIGHT
    bl = disp_backlight_new(&bckl_config);
    disp_backlight_set(bl, 100);
#endif

    spi_bus_config_t buscfg = {
        .sclk_io_num = CONFIG_SCLK_GPIO,
        .mosi_io_num = CONFIG_MOSI_GPIO,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        // what is the size if this is not set?
        //.max_transfer_sz = oled->w * ST7735S_BSIZE * sizeof(lv_color_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));
    disp_spi_add_device(LCD_HOST);
    st7735s_init();

    // library all in one init call
    //lvgl_driver_init();
#endif

    // avoid issues with multiple transactions
    if (oled->mutex_i2c == NULL) {
        oled->mutex_i2c = xSemaphoreCreateMutex();
    }
    assert(oled->mutex_i2c != NULL);
    if (lvgl_mutex == NULL) {
        lvgl_mutex = xSemaphoreCreateMutex();
    }
    assert(lvgl_mutex != NULL);
    static lv_disp_draw_buf_t disp_buf; // contains internal graphic buffer(s) called draw buffer(s)

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();
    // alloc draw buffers used by LVGL
    // it's recommended to choose the size of the draw buffer(s) to be at least 1/10 screen sized
    lv_color_t *buf1 = malloc(oled->w * ST7735S_BSIZE * sizeof(lv_color_t));
    assert(buf1);
    lv_color_t *buf2 = malloc(oled->w * ST7735S_BSIZE * sizeof(lv_color_t));
    assert(buf2);
    // initialize LVGL draw buffers
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, oled->w * ST7735S_BSIZE);

    ESP_LOGI(TAG, "Register display driver to LVGL");
    static lv_disp_drv_t disp_drv;      // contains callback functions
    oled->disp_drv = &disp_drv;
    assert(oled->disp_drv != NULL);
    lv_disp_drv_init(oled->disp_drv);
    oled->disp_drv->hor_res = oled->w;
    oled->disp_drv->ver_res = oled->h;
#ifdef ST7735S_SIMPLE
    oled->disp_drv->flush_cb = example_lvgl_flush_cb;
#else
    //oled->disp_drv->flush_cb = st7735s_flush;
    // for screenshot support
    oled->disp_drv->flush_cb = example_lvgl_flush_cb;
#endif
    oled->disp_drv->draw_buf = &disp_buf;
    oled->disp_drv->user_data = oled;

    //oled->disp_drv->monitor_cb = lvgl_monitor_cb;
    lv_disp_t *disp = oled->disp;
    if (disp == NULL) {
        disp = lv_disp_drv_register(oled->disp_drv);
    }

    oled->disp = disp;
    //lv_disp_set_rotation(disp, LV_DISP_ROT_90);
    oled->scr = lv_disp_get_scr_act(disp);
    oled->depth = 16;

    ESP_LOGI(TAG, "Install LVGL tick timer");
    // Tick interface for LVGL (using esp_timer to generate 2ms periodic event)
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &example_increase_lvgl_tick,
        .name = "lvgl_tick"
    };
    if (oled->lvgl_tick_timer == NULL) {
        oled->lvgl_tick_timer_ms = LVGL_TICK_PERIOD_MS;
        ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &oled->lvgl_tick_timer));
        ESP_ERROR_CHECK(esp_timer_start_periodic(oled->lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000));
    }

    // oled_task priority = 3 < CONFIG_FREERTOS_TIMER_TASK_PRIORITY = 4
    // any drawing task at least 4 too?
    xxTaskCreate((void (*)(void*)) oled_task, "oled_task", 8*1024, NULL, 3, NULL);
}
#endif // LCD_ST7735S


#if defined(LCD_ST7789_1) || defined(LCD_ST7789_2)
#define LCD_HOST SPI2_HOST
#define CONFIG_MOSI_GPIO 23 // SDA
#define CONFIG_SCLK_GPIO 18 // SCL
#define CONFIG_TFT_CS_GPIO 16

#define CONFIG_DC_GPIO 17
#define CONFIG_RESET_GPIO 5
#define CONFIG_BL_GPIO 19

#define EXAMPLE_LCD_PIXEL_CLOCK_HZ (20 * 1000 * 1000)
#define EXAMPLE_LCD_BK_LIGHT_ON_LEVEL 1
#define EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL !EXAMPLE_LCD_BK_LIGHT_ON_LEVEL
#define EXAMPLE_PIN_NUM_DATA0          CONFIG_MOSI_GPIO /*!< for 1-line SPI, this also refered as MOSI */
#define EXAMPLE_PIN_NUM_PCLK           CONFIG_SCLK_GPIO
#ifdef LCD_ST7789_2
#define EXAMPLE_PIN_NUM_CS CONFIG_TFT_CS_GPIO
#else
// small one doesn't have CS
#define EXAMPLE_PIN_NUM_CS -1
#endif
#define EXAMPLE_PIN_NUM_DC             CONFIG_DC_GPIO
#define EXAMPLE_PIN_NUM_RST            CONFIG_RESET_GPIO
#define EXAMPLE_PIN_NUM_BK_LIGHT       CONFIG_BL_GPIO
// ST7789_2 has pin
#define MISO_GPIO -1

// To speed up transfers, every SPI transfer sends a bunch of lines. This define
// specifies how many. More means more memory use, but less overhead for setting
// up / finishing transfers. Make sure 240 is dividable by this.
#define CONFIG_EXAMPLE_LCD_FLUSH_PARALLEL_LINES 1
#define PARALLEL_LINES CONFIG_EXAMPLE_LCD_FLUSH_PARALLEL_LINES

//#endif // LCD_ST7789_*
//#if defined(LCD_ST7789_1) || defined(LCD_ST7789_2)

// TODO
int tft_st7789_spi(display_t *oled)
{
    static lv_disp_drv_t disp_drv;

    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << EXAMPLE_PIN_NUM_BK_LIGHT
    };
    // Initialize the GPIO of backlight
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));

    spi_bus_config_t buscfg = {
        .sclk_io_num = EXAMPLE_PIN_NUM_PCLK,
        .mosi_io_num = EXAMPLE_PIN_NUM_DATA0,
        .miso_io_num = MISO_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        //.max_transfer_sz = PARALLEL_LINES * oled->w * 2 + 8
        .max_transfer_sz = oled->w * ST7789_BSIZE * sizeof(lv_color_t),
    };

    // Initialize the SPI bus
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = EXAMPLE_PIN_NUM_DC,
        .cs_gpio_num = EXAMPLE_PIN_NUM_CS,
        .pclk_hz = EXAMPLE_LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = OLED_LCD_CMD_BITS,
        .lcd_param_bits = OLED_LCD_PARAM_BITS,
#ifdef LCD_ST7789_1 // no CS pin
        .spi_mode = 3,
#else
        .spi_mode = 0,
#endif
        .trans_queue_depth = 10,
        .user_ctx = &disp_drv,
    };
#if CONFIG_EXAMPLE_LCD_SPI_8_LINE_MODE
    io_config.spi_mode = 3;  // using mode 3 to simulate Intel 8080 timing
    io_config.flags.octal_mode = 1;
#endif
    // Attach the LCD to the SPI bus
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = EXAMPLE_PIN_NUM_RST,
        .color_space = ESP_LCD_COLOR_SPACE_BGR,
        .bits_per_pixel = 16,
    };

    // Initialize the LCD configuration
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));

    // Turn off backlight to avoid unpredictable display on the LCD screen while initializing
    // the LCD panel driver. (Different LCD screens may need different levels)
    ESP_ERROR_CHECK(gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL));

    // Reset the display
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));

    // Initialize LCD panel
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    // Turn on the screen
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    // Swap x and y axis (Different LCD screens may need different options)
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_handle, true));

    // Turn on backlight (Different LCD screens may need different levels)
    //ESP_ERROR_CHECK(gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, EXAMPLE_LCD_BK_LIGHT_ON_LEVEL));

    // no reinit implemented
    oled->disp_drv = &disp_drv;
    oled->io_handle = io_handle;
    oled->panel_handle = panel_handle;
    return 1;
}

// seems slow with 320x240, causing i2c reinit trigger and metar/heating is not rendering
// using 3rd party library was more successful
#ifdef ST7789_IDF
void tft_st7789_init(display_t *oled)
#else
void idf_tft_st7789_init(display_t *oled)
#endif
{
    // avoid issues with multiple transactions
    if (oled->mutex_i2c == NULL) {
        oled->mutex_i2c = xSemaphoreCreateMutex();
    }
    assert(oled->mutex_i2c != NULL);
    if (lvgl_mutex == NULL) {
        lvgl_mutex = xSemaphoreCreateMutex();
    }
    assert(lvgl_mutex != NULL);
    static lv_disp_draw_buf_t disp_buf; // contains internal graphic buffer(s) called draw buffer(s)

#ifdef LCD_ST7789_1
    oled->w = ST7789_1_H_RES;
    oled->h = ST7789_1_V_RES;
#elif defined(LCD_ST7789_2)
    oled->w = ST7789_2_H_RES;
    oled->h = ST7789_2_V_RES;
#endif

    if (oled->panel_handle == NULL)
        if (!tft_st7789_spi(oled))
            return;

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();
    // alloc draw buffers used by LVGL
    // it's recommended to choose the size of the draw buffer(s) to be at least 1/10 screen sized
    lv_color_t *buf1 = malloc(oled->w * ST7789_BSIZE * sizeof(lv_color_t));
    assert(buf1);
    lv_color_t *buf2 = malloc(oled->w * ST7789_BSIZE * sizeof(lv_color_t));
    assert(buf2);
    // initialize LVGL draw buffers
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, oled->w * ST7789_BSIZE);

    ESP_LOGI(TAG, "Register display driver to LVGL");
    assert(oled->disp_drv != NULL);
    lv_disp_drv_init(oled->disp_drv);
    oled->disp_drv->hor_res = oled->w;
    oled->disp_drv->ver_res = oled->h;
    oled->disp_drv->flush_cb = example_lvgl_flush_cb;
    oled->disp_drv->draw_buf = &disp_buf;
    oled->disp_drv->user_data = oled;
    // these are not working?
    //oled->disp_drv.rotated = LV_DISP_ROT_180;
    //lv_disp_set_rotation(disp, LV_DISP_ROT_180);
    //oled->disp_drv.sw_rotate = LV_DISP_ROT_180;
    // this works, remembers state until power down
#ifdef LCD_ST7789_1
    esp_lcd_panel_mirror(oled->panel_handle, true, false);
#endif
#ifdef LCD_ST7789_2
    esp_lcd_panel_mirror(oled->panel_handle, false, true);
    esp_lcd_panel_invert_color(oled->panel_handle, true);
#endif
    //esp_lcd_panel_disp_on_off(oled->panel_handle, 1);

    //oled->disp_drv->monitor_cb = lvgl_monitor_cb;
    lv_disp_t *disp = oled->disp;
    if (disp == NULL) {
        disp = lv_disp_drv_register(oled->disp_drv);
    }

    oled->disp = disp;
    oled->scr = lv_disp_get_scr_act(disp);
    oled->depth = 16;

    ESP_LOGI(TAG, "Install LVGL tick timer");
    // Tick interface for LVGL (using esp_timer to generate 2ms periodic event)
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &example_increase_lvgl_tick,
        .name = "lvgl_tick"
    };
    if (oled->lvgl_tick_timer == NULL) {
        oled->lvgl_tick_timer_ms = LVGL_TICK_PERIOD_MS;
        ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &oled->lvgl_tick_timer));
        ESP_ERROR_CHECK(esp_timer_start_periodic(oled->lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000));
    }

    // oled_task priority = 3 < CONFIG_FREERTOS_TIMER_TASK_PRIORITY = 4
    // any drawing task at least 4 too?
    xxTaskCreate((void (*)(void*)) oled_task, "oled_task", 8*1024, NULL, 3, NULL);
}


#ifndef ST7789_IDF
void tft_st7789_init(display_t *oled)
#else
void lib_tft_st7789_init(display_t *oled)
#endif
{
#ifdef LCD_ST7789_1
    oled->w = ST7789_1_H_RES;
    oled->h = ST7789_1_V_RES;
#elif defined(LCD_ST7789_2)
    oled->w = ST7789_2_H_RES;
    oled->h = ST7789_2_V_RES;
#endif

    // pins from menuconfig
    check_gpio(CONFIG_LV_DISP_SPI_MOSI, OLED);
    check_gpio(CONFIG_LV_DISP_SPI_CLK, OLED);
    check_gpio(CONFIG_LV_DISP_SPI_CS, OLED);
    check_gpio(CONFIG_LV_DISP_PIN_DC, OLED);
    // seems to have software reset too
    check_gpio(CONFIG_LV_DISP_PIN_RST, OLED);
    // seems to be optional
    check_gpio(CONFIG_LV_DISP_PIN_BCKL, OLED);

    gpio_reset_pin(CONFIG_LV_DISP_PIN_BCKL);
    gpio_set_direction(CONFIG_LV_DISP_PIN_BCKL, GPIO_MODE_OUTPUT);
    gpio_set_level(CONFIG_LV_DISP_PIN_BCKL, 1);
#ifdef LV_BACKLIGHT
    bl = disp_backlight_new(&bckl_config);
    disp_backlight_set(bl, 100);
#endif

    spi_bus_config_t buscfg = {
        .sclk_io_num = CONFIG_SCLK_GPIO,
        .mosi_io_num = CONFIG_MOSI_GPIO,
        .miso_io_num = MISO_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = oled->w * ST7789_BSIZE * sizeof(lv_color_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));
    disp_spi_add_device(LCD_HOST);
    st7789_init();

    // library all in one init call
    //lvgl_driver_init();

    // avoid issues with multiple transactions
    if (oled->mutex_i2c == NULL) {
        oled->mutex_i2c = xSemaphoreCreateMutex();
    }
    assert(oled->mutex_i2c != NULL);
    if (lvgl_mutex == NULL) {
        lvgl_mutex = xSemaphoreCreateMutex();
    }
    assert(lvgl_mutex != NULL);
    static lv_disp_draw_buf_t disp_buf; // contains internal graphic buffer(s) called draw buffer(s)

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();
    // alloc draw buffers used by LVGL
    // it's recommended to choose the size of the draw buffer(s) to be at least 1/10 screen sized
    lv_color_t *buf1 = malloc(oled->w * ST7789_BSIZE * sizeof(lv_color_t));
    assert(buf1);
    lv_color_t *buf2 = malloc(oled->w * ST7789_BSIZE * sizeof(lv_color_t));
    assert(buf2);
    // initialize LVGL draw buffers
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, oled->w * ST7789_BSIZE);

    ESP_LOGI(TAG, "Register display driver to LVGL");
    static lv_disp_drv_t disp_drv;      // contains callback functions
    oled->disp_drv = &disp_drv;
    assert(oled->disp_drv != NULL);
    lv_disp_drv_init(oled->disp_drv);
    oled->disp_drv->hor_res = oled->w;
    oled->disp_drv->ver_res = oled->h;
    //oled->disp_drv->flush_cb = st7789_flush;
    // for screenshot support
    oled->disp_drv->flush_cb = example_lvgl_flush_cb;
    oled->disp_drv->draw_buf = &disp_buf;
    oled->disp_drv->user_data = oled;

    //oled->disp_drv->monitor_cb = lvgl_monitor_cb;
    lv_disp_t *disp = oled->disp;
    if (disp == NULL) {
        disp = lv_disp_drv_register(oled->disp_drv);
    }

    oled->disp = disp;
    //lv_disp_set_rotation(disp, LV_DISP_ROT_90);
    oled->scr = lv_disp_get_scr_act(disp);
    oled->depth = 16;

    ESP_LOGI(TAG, "Install LVGL tick timer");
    // Tick interface for LVGL (using esp_timer to generate 2ms periodic event)
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &example_increase_lvgl_tick,
        .name = "lvgl_tick"
    };
    if (oled->lvgl_tick_timer == NULL) {
        oled->lvgl_tick_timer_ms = LVGL_TICK_PERIOD_MS;
        ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &oled->lvgl_tick_timer));
        ESP_ERROR_CHECK(esp_timer_start_periodic(oled->lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000));
    }

    // oled_task priority = 3 < CONFIG_FREERTOS_TIMER_TASK_PRIORITY = 4
    // any drawing task at least 4 too?
    xxTaskCreate((void (*)(void*)) oled_task, "oled_task", 8*1024, NULL, 3, NULL);
}

#endif // LCD_ST7789_*
