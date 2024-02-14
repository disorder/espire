#ifndef __WIDGETS_H__
#define __WIDGETS_H__

#include <lvgl.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_lcd_panel_io.h"
#include "esp_timer.h"
#include "metar.h"

typedef struct {
    int w;
    int h;
    int depth;
    int power;
    esp_timer_handle_t lvgl_tick_timer;
    int lvgl_tick_timer_ms;
    lv_obj_t *scr;
    lv_disp_t *disp;
    lv_disp_drv_t *disp_drv;
    SemaphoreHandle_t mutex_i2c;
    esp_lcd_panel_io_handle_t io_handle;
    esp_lcd_panel_handle_t panel_handle;
} display_t;

#include "oled.h"

void oled_clock(int locked, char *date, char *time);
void oled_temp(int locked, float room, float set, float mod);
void oled_metar(int locked, metar_t *metar, int bottom_line);
void oled_bottom_scroll0(int locked, char *text);
void oled_bottom_scroll1(int locked, char *text);
void oled_network(int locked, int wifi, int ping);
void oled_top_right(int locked, char *text);
void oled_top_left(int locked, char *text);
void oled_message(int locked, char *text);
void oled_owm(int locked, char *text);
void oled_co2(int locked, int ppm);

#endif /* __WIDGETS_H__ */
