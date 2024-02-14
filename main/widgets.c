#include "widgets.h"
#include "metar.h"
#include <math.h>
#include <malloc.h>
#include <lvgl.h>
#include <assert.h>
#include "font/small5x3.h"
#include "esp_log.h"

//static const char *TAG = "widgets";

extern display_t oled;

void oled_clock(int locked, char *date, char *time)
{
    if (oled.scr == NULL)
        return;

    if (!locked)
        LVGL_ENTER(0);
    static lv_obj_t *ltime = NULL, *ldate = NULL;

    if (ldate == NULL) {
        ldate = lv_label_create(oled.scr);
        assert(ldate != NULL);
        ltime = lv_label_create(oled.scr);
        assert(ltime != NULL);
        static lv_style_t tstyle;
        lv_style_init(&tstyle);
        lv_style_set_text_font(&tstyle, &lv_font_montserrat_44);
        lv_obj_add_style(ltime, &tstyle, 0);
        static lv_style_t dstyle;
        lv_style_init(&dstyle);
        lv_style_set_text_font(&dstyle, &small5x3);
        lv_obj_add_style(ldate, &dstyle, 0);

        lv_obj_set_width(ltime, oled.w);
        lv_obj_set_width(ldate, oled.w);
        lv_obj_align(ltime, LV_ALIGN_CENTER, 0, -2);
        lv_obj_align(ldate, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_text_align(ltime, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_align(ldate, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_label_set_text(ltime, "");
        lv_label_set_text(ldate, "");
    }
    if (ldate != NULL) {
        switch (oled_update.mode %= MODE_MAX) {
        case CLOCK:
            lv_obj_clear_flag(ldate, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ltime, LV_OBJ_FLAG_HIDDEN);
            break;
        default:
            lv_obj_add_flag(ldate, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ltime, LV_OBJ_FLAG_HIDDEN);
        }

        if (date != NULL)
            lv_label_set_text(ldate, date);
        if (time != NULL)
            lv_label_set_text(ltime, time);
    }
    if (!locked)
        LVGL_EXIT();
}

void oled_temp(int locked, float room, float set, float mod)
{
    if (oled.scr == NULL)
        return;

    if (!locked)
        LVGL_ENTER(0);
    static lv_obj_t *l0 = NULL, *l1 = NULL, *l2 = NULL;

    static lv_style_t style;
    static lv_style_t style1;
    if (l1 == NULL) {
        l0 = lv_label_create(oled.scr);
        assert(l0 != NULL);
        l1 = lv_label_create(oled.scr);
        assert(l1 != NULL);
        l2 = lv_label_create(oled.scr);
        assert(l2 != NULL);
        lv_style_init(&style);
        lv_style_set_text_font(&style, &small5x3);
        lv_obj_add_style(l0, &style, 0);
        lv_style_init(&style1);
        lv_style_set_text_font(&style1, &lv_font_unscii_8);
        lv_obj_add_style(l1, &style1, 0);

        lv_obj_set_width(l0, oled.w/2);
        lv_obj_set_width(l1, oled.w/2);
        lv_obj_set_width(l2, oled.w/2);
        lv_obj_set_style_text_align(l0, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_align(l1, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_align(l2, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_label_set_text(l0, "");
        lv_label_set_text(l1, "");
        lv_label_set_text(l2, "");
    }

    if (l1 != NULL) {
        switch (oled_update.mode %= MODE_MAX) {
        case CLOCK:
            lv_style_set_text_font(&style1, &small5x3);
            //lv_obj_add_style(l1, &style1, 0);
            lv_obj_align(l1, LV_ALIGN_TOP_LEFT, 0, +3+3);
            lv_obj_align(l2, LV_ALIGN_TOP_RIGHT, 0, +3);
            lv_obj_add_flag(l0, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(l1, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(l2, LV_OBJ_FLAG_HIDDEN);
            break;
        case HEATING:
            lv_style_set_text_font(&style1, &lv_font_unscii_8);
            //lv_obj_add_style(l1, &style1, 0);
            lv_obj_align(l0, LV_ALIGN_LEFT_MID, 0, 1);
            lv_obj_align(l1, LV_ALIGN_LEFT_MID, 0, -7);
            lv_obj_align(l2, LV_ALIGN_RIGHT_MID, 0, -7);
            lv_obj_clear_flag(l0, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(l1, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(l2, LV_OBJ_FLAG_HIDDEN);
            break;
        default:
            lv_obj_add_flag(l0, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(l1, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(l2, LV_OBJ_FLAG_HIDDEN);
        }

        char buf[2+2 +1 +1];
        if (!isnanf(set) && fabs(set - mod) < 0.05) {
            lv_label_set_text(l0, "");
            snprintf(buf, sizeof(buf), "%.1f", set);
        } else {
            if (isnanf(set)) {
                lv_label_set_text(l0, "N/A");
                snprintf(buf, sizeof(buf), "%.1f", mod);
            } else {
                snprintf(buf, sizeof(buf), "%.1f", set);
                lv_label_set_text(l0, buf);
                if (oled_update.temp_pending && oled_update.temp_mod == oled_update.temp_set)
                    // signalize that the value is being sent
                    snprintf(buf, sizeof(buf), "%.1f", mod);
                else
                    snprintf(buf, sizeof(buf), "%.1f_", mod);
            }
        }
        lv_label_set_text(l1, buf);
        if (isnanf(room))
            lv_label_set_text(l2, "N/A");
        else {
            snprintf(buf, sizeof(buf), "%.1f", room);
            lv_label_set_text(l2, buf);
        }
    }
    if (!locked)
        LVGL_EXIT();
}

// line from bottom = 2 if both scrollers are at the bottom
void oled_metar(int locked, metar_t *metar, int bottom_line)
{
    if (oled.scr == NULL)
        return;

    if (!locked)
        LVGL_ENTER(0);
    static lv_obj_t *label = NULL, *temp = NULL;

    if (label == NULL) {
        label = lv_label_create(oled.scr);
        assert(label != NULL);
        temp = lv_label_create(oled.scr);
        assert(temp != NULL);
        static lv_style_t style;
        lv_style_init(&style);
        lv_style_set_text_font(&style, &small5x3);
        lv_obj_add_style(label, &style, 0);
        lv_obj_add_style(temp, &style, 0);

        lv_obj_set_width(label, oled.w);
        lv_obj_align(label, LV_ALIGN_BOTTOM_RIGHT, 0, -7*bottom_line);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

        lv_obj_set_width(temp, oled.w/2);
        lv_obj_align(temp, LV_ALIGN_RIGHT_MID, 0, 2);
        lv_obj_set_style_text_align(temp, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_label_set_text(label, "N/A");
        lv_label_set_text(temp, "");
    }

    if (label != NULL) {
        switch (oled_update.mode %= MODE_MAX) {
        case HEATING:
            lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(temp, LV_OBJ_FLAG_HIDDEN);
            break;
        default:
            lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(temp, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (metar == NULL) {
        if (!locked)
            LVGL_EXIT();
        return;
    }

    static int last = 0;
    if (label != NULL && last != metar->last) {
        last = metar->last;
        // temp rh report_time wind/gust wind_from-wind_to
        char data[3 +1+ 3+1 +1+ 5 +1+ 3+1+3+2 +1+ 3+1+3 +1+ 4+4 +1];
        char *buf = data;
        char *end = data + sizeof(data);
        buf += snprintf(buf, end-buf, "%d %.0f%%", metar->celsius, roundf(metar->rh));
        lv_label_set_text(temp, data);
        buf = data;
        buf += snprintf(buf, end-buf, "%s ", metar->report_time+2);
        if (metar->wind_gust < 0)
            buf += snprintf(buf, end-buf, "%.0f km/h", metar->wind_speed);
        else
            buf += snprintf(buf, end-buf, "%.0f-%.0f km/h", metar->wind_speed, metar->wind_gust);
        if (metar->wind_to)
            buf += snprintf(buf, end-buf, " %s-%s", metar->wind_from, metar->wind_to);
        else if (metar->wind_from)
            buf += snprintf(buf, end-buf, " %s", metar->wind_from);
        // removed " hPa" to make more space
        buf += snprintf(buf, end-buf, " %d", metar->pressure);
        lv_label_set_text(label, data);
    }
    if (!locked)
        LVGL_EXIT();
}

// call only in mutex
static lv_obj_t *scroll_label(int align, int text_align, int yoffset)
{
    assert(oled.scr != NULL);
    lv_obj_t *label = lv_label_create(oled.scr);
    assert(label != NULL);

    lv_style_t *style = calloc(1, sizeof(lv_style_t));
    lv_style_init(style);
    //lv_style_set_text_color(style, lv_color_black());
    lv_style_set_text_font(style, &small5x3);
    //lv_obj_set_style_anim_speed(style, 1, LV_PART_MAIN);
    lv_obj_add_style(label, style, 0);

    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
#define LV_LABEL_DEF_SCROLL_SPEED(obj) (lv_disp_get_dpi(lv_obj_get_disp(obj)) / 3)
    //lv_obj_set_style_anim_speed(label, LV_LABEL_DEF_SCROLL_SPEED(label), LV_PART_MAIN);
    lv_obj_set_style_anim_speed(label, 20, LV_PART_MAIN);
    lv_obj_set_width(label, oled.w);
    // doesn't matter where, if it's always long enough to scroll
    lv_obj_set_style_text_align(label, text_align, LV_PART_MAIN);
    lv_obj_align(label, align, 0, yoffset);
    lv_label_set_text(label, "");
    return label;
}

void oled_bottom_scroll1(int locked, char *text)
{
    if (oled.scr == NULL)
        return;

    if (!locked)
        LVGL_ENTER(0);
    static lv_obj_t *label = NULL;
    if (label == NULL) {
        label = scroll_label(LV_ALIGN_BOTTOM_MID, LV_TEXT_ALIGN_CENTER, -7);
    }

    if (label != NULL) {
        switch (oled_update.mode %= MODE_MAX) {
        case HEATING:
            lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
            break;
        default:
            lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
        }

        if (text != NULL)
            lv_label_set_text(label, text);
    }
    if (!locked)
        LVGL_EXIT();
}

void oled_bottom_scroll0(int locked, char *text)
{
    if (oled.scr == NULL)
        return;

    if (!locked)
        LVGL_ENTER(0);
    static lv_obj_t *label = NULL;

    if (label == NULL) {
        label = scroll_label(LV_ALIGN_BOTTOM_LEFT, LV_TEXT_ALIGN_LEFT, 0);
    }

    if (label != NULL) {
        switch (oled_update.mode %= MODE_MAX) {
        case HEATING:
            lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
            break;
        default:
            lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
        }

        if (text != NULL)
            lv_label_set_text(label, text);
    }
    if (!locked)
        LVGL_EXIT();
}

/*
static lv_obj_t *nlabel = NULL;
void oled_network_align(int locked, lv_align_t align, int x, int y)
{
    if (nlabel == NULL)
        return;

    if (!locked)
        LVGL_ENTER(0);
    lv_obj_align(nlabel, align, x, y);
    if (!locked)
        LVGL_EXIT();
}
*/

void oled_network(int locked, int wifi, int ping)
{
    if (oled.scr == NULL)
        return;

    if (!locked)
        LVGL_ENTER(0);
    static lv_obj_t *nlabel = NULL;
    if (nlabel == NULL) {
        nlabel = lv_label_create(oled.scr);
        assert(nlabel != NULL);
        static lv_style_t style;
        lv_style_init(&style);
        lv_style_set_text_font(&style, &small5x3);
        //lv_style_set_text_font(&style, &lv_font_montserrat_14);
        //lv_style_set_text_font(&style, &lv_font_unscii_8);
        lv_obj_add_style(nlabel, &style, 0);

        lv_obj_set_width(nlabel, 8);
        lv_label_set_text(nlabel, "");
    }

    if (nlabel != NULL) {
        switch (oled_update.mode %= MODE_MAX) {
        case OWM:
            lv_obj_align(nlabel, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
            break;
        case CLOCK:
            //lv_obj_align(nlabel, LV_ALIGN_BOTTOM_RIGHT, 0, -1);
            //lv_obj_align(nlabel, LV_ALIGN_CENTER, 1, 2);
            lv_obj_align(nlabel, LV_ALIGN_BOTTOM_MID, 1, -7);
            //oled_network_align(LV_ALIGN_BOTTOM_RIGHT, 0, 0);
            break;
        default:
            lv_obj_align(nlabel, LV_ALIGN_CENTER, 0, 0);
            //oled_network_align(LV_ALIGN_CENTER, 0, 0);
        }

        char status[4] = "X?\0\0";
        if (wifi)
            status[0] = 'Y';
        if (ping == -1) // not pinging
            status[1] = '.';
        else if (ping) // connected
            status[1] = '|';
        else // disconnected
            status[1] = ':';

        // no temperature update over minute (controller is probably offline)
        time_t now;
        if (time(&now) >= oled_update.temp_last + 60)
            status[2] = ':';
        else
            status[2] = '\0';
        // maybe !/i for online/no controller and vice versa but that's dense

        //lv_label_set_text(label, LV_SYMBOL_WIFI);
        lv_label_set_text(nlabel, status);
    }
    if (!locked)
        LVGL_EXIT();
}

void oled_top_right(int locked, char *text)
{
    if (oled.scr == NULL)
        return;

    if (!locked)
        LVGL_ENTER(0);
    static lv_obj_t *label = NULL;
    if (label == NULL) {
        label = lv_label_create(oled.scr);
        assert(label != NULL);
        //static lv_style_t style;
        //lv_style_init(&style);
        //lv_style_set_text_color(&style, lv_color_black());
        ////lv_style_set_text_font(&style, &small5x3);
        //lv_obj_add_style(label, &style, 0);

        // date width
        lv_obj_set_width(label, 8*8);
        lv_obj_align(label, LV_ALIGN_TOP_RIGHT, -1, 0);
        lv_label_set_text(label, "");
    }

    if (label != NULL) {
        switch (oled_update.mode %= MODE_MAX) {
        case HEATING:
            lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
            break;
        default:
            lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
        }

        if (text != NULL)
            lv_label_set_text(label, text);
    }
    if (!locked)
        LVGL_EXIT();
}

void oled_top_left(int locked, char *text)
{
    if (oled.scr == NULL)
        return;
    if (!locked)
        LVGL_ENTER(0);
    static lv_obj_t *label = NULL;

    if (label == NULL) {
        label = lv_label_create(oled.scr);
        assert(label != NULL);
        static lv_style_t style;
        lv_style_init(&style);
        lv_style_set_text_font(&style, &small5x3);
        lv_obj_add_style(label, &style, 0);

        // date width
        lv_obj_set_width(label, 10*4);
        // there is actually one more column of pixels so leave it at x=0
        lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 1);
        lv_label_set_text(label, "");
    }

    if (label != NULL) {
        switch (oled_update.mode %= MODE_MAX) {
        case HEATING:
            lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
            break;
        default:
            lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
        }

        if (text != NULL)
            lv_label_set_text(label, text);
    }
    if (!locked)
        LVGL_EXIT();
}

void oled_message(int locked, char *text)
{
    if (oled.scr == NULL)
        return;

    if (!locked)
        LVGL_ENTER(0);
    static lv_obj_t *label = NULL;

    if (label == NULL) {
        label = scroll_label(LV_ALIGN_TOP_MID, LV_TEXT_ALIGN_CENTER, 10);
    }

    if (label != NULL) {
        switch (oled_update.mode %= MODE_MAX) {
        case OWM:
            lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
            break;
        case CLOCK:
            lv_obj_align(label, LV_TEXT_ALIGN_CENTER, 0, 4);
            lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
            break;
        default:
            lv_obj_align(label, LV_TEXT_ALIGN_CENTER, 0, 12);
            lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
        }

        if (text != NULL)
            lv_label_set_text(label, text);
    }
    if (!locked)
        LVGL_EXIT();
}

void oled_owm(int locked, char *text)
{
    if (oled.scr == NULL)
        return;
    if (!locked)
        LVGL_ENTER(0);
    static lv_obj_t *label = NULL;

    if (label == NULL) {
        label = lv_label_create(oled.scr);
        assert(label != NULL);
        static lv_style_t style;
        lv_style_init(&style);
        lv_style_set_text_font(&style, &small5x3);
        lv_obj_add_style(label, &style, 0);

        // date width
        lv_obj_set_width(label, oled.w);
        // there is actually one more column of pixels so leave it at x=0
        lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_label_set_text(label, "");
    }

    if (label != NULL) {
        switch (oled_update.mode %= MODE_MAX) {
        case OWM:
            lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
            break;
        default:
            lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
        }

        if (text != NULL)
            lv_label_set_text(label, text);
    }
    if (!locked)
        LVGL_EXIT();
}
