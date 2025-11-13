#ifndef __OLED_H__
#define __OLED_H__

#include "widgets.h"
#include "metar.h"
extern display_t oled;

typedef enum {
    HEATING=0,
    CLOCK,
    OWM,
    // TODO widget for viewing all zone temperatures
    ZONES,
    OFF,
    MODE_MAX,
} display_mode_t;

typedef struct {
    display_mode_t mode;
    display_mode_t mode_default;
    // in case we are not sleeping but mode was not restored, otherwise -1
    display_mode_t mode_restore;
    int power_default;
    int power_toggle;
    int power_state;
    int invalidate;
    float external;
    int temp;
    time_t temp_last;
    metar_t *metar;
    time_t metar_last;
    char *owm;
    char *message;

    // TODO initialize from controller
    // TODO read periodically + set on trigger
    float temp_set;
    float temp_mod;
    int temp_pending;
} oled_update_t;

extern oled_update_t oled_update;

int LVGL_ENTER(int block);
void LVGL_EXIT();

void theme_init(display_t *oled);
void oled_ssd1306_init();
void lvgl_monitor_cb(lv_disp_drv_t * disp_drv, uint32_t ms, uint32_t px);
char oled_get_px(uint8_t *buf, int buf_w, int x, int y);

void oled_invalidate(display_t *oled);
void oled_power(display_t *oled, int on);
void oled_invert(display_t *oled, int invert);
void oled_mirror(display_t *oled, int x, int y);
void oled_reset(display_t *oled);
void oled_reinit(display_t *oled);
//void oled_init(display_t *oled);

void tft_st7735s_init(display_t *oled);
void backlight(int level);
void tft_st7789_init(display_t *oled);

#endif /* __OLED_H__ */
