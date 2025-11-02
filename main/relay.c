#include "relay.h"
#include "check.h"
#include "driver/gpio.h"
#include "esp_log.h"
static const char *TAG = "relay";

// with wifi there are
// 10 outputs + 4 thermistors OR
//  8 outputs + 6 thermistors
// with wifi disconnect there are
// 10 outputs + 10 thermistors (2 ADC1, 8 ADC2)
int relay_pins[] = {
    // safe for output
    18, 19, 21, 22, 23,
    // uart
    16, 17,
    // high at boot
    5,
    // debug output at boot - doesn't respond?
    //1,
    // ADC2
    // 2, 12, 13, 14, 15, 25, 26, 27, ... 0, 4
    // ADC1 + 4 input only
    32, 33,
};

void relay_init_gpio(int gpio, int v5)
{
    ESP_LOGI(TAG, "Enabling GPIO %d", gpio);
    check_gpio(gpio, RELAY);
    if (!v5)
        gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
}

void relay_init_pin(int pin, int v5)
{
    relay_init_gpio(relay_pins[pin], v5);
}

void relay_set_gpio(int gpio, int state)
{
    ESP_LOGI(TAG, "Setting GPIO %d to %d", gpio, state);
    gpio_set_level(gpio, state);
}

void relay_set_gpio_5v(int gpio, int state)
{
    // for this relay 3.3 high is not enough
    if (state) {
        gpio_set_level(gpio, state);
        gpio_reset_pin(gpio);
    } else {
        gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
        gpio_set_level(gpio, state);
    }
}

void relay_reset_gpio(int gpio)
{
    ESP_LOGI(TAG, "Resetting GPIO %d", gpio);
    gpio_reset_pin(gpio);
}

void relay_set_pin(int pin, int state)
{
    relay_set_gpio(relay_pins[pin], state);
}

void relay_set_pin_5v(int pin, int state)
{
    relay_set_gpio_5v(relay_pins[pin], state);
}

void relay_init(int count, int v5)
{
    ESP_LOGI(TAG, "init: %d GPIOs", count);
    for (int i=0; i<count; i++)
        relay_init_pin(i, v5);
}
