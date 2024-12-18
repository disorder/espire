#ifndef __CHECK_H__
#define __CHECK_H__

#include "config.h"
#include "adc2.h"

typedef enum {
    NONE=0,
    WIFI,
    SPI,
    TEMP,
    RELAY,
    BUTTON,
    OLED,
    USB,
    CO2,
    JTAG,
    NOT_EXPOSED,
} gpio_owner_t;

//extern int gpio_user[40];

#define ADC1_CNT 6
extern int adc1_gpio[ADC1_CNT];
#define ADC2_CNT 10
#define ADC2_WIFI_USABLE 6+2
extern int adc2_gpio[ADC2_CNT];

// 1 for wifi exclusion, 2 for some usable ADC2
void check_init(int wifi);
gpio_owner_t gpio_owner(int gpio);
const char *gpio_owner_str(gpio_owner_t owner);
void check_gpio(int gpio, gpio_owner_t owner);
void check_gpio_clear(int gpio);
void check_report(void *req, int (*func)(void *req, const char *fmt, ...));

#endif /* __CHECK_H__ */
