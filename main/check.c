#include "config.h"
#include "check.h"
#include "util.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
static const char *TAG = "check";

int gpio_user[40] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};

int adc1_gpio[ADC1_CNT] = {
    // ADC1 - VP VN 34 35 32 33
    36, 39, 34, 35, 32, 33,
    // not exposed
    //37, 38,
};
int adc2_gpio[ADC2_CNT] = {
    // ADC2 - used by wifi driver
    25, 26, 27, 14, 12, 13,
    // ESP-WROVER-KIT: GPIO 0, 2, 4 and 15 cannot be used due to external connections for different purposes.
    // WROOM32 seems safe for 2 and 15; 4 and 0 seems bogus even disconnected
    // 15: must be HIGH during boot (works with thermistor)
    2, 15,
    // both bogus values out of the box and causing log flood after some time
    // 4: works without initialized wifi
    // 0: bad values even without wifi
    4, 0,
};

// 0 is no wifi used, 1 is strictly no ADC2, 2 is ADC2 with wifi disconnect
static int wifi_mode;

const char *gpio_owner_str(gpio_owner_t owner)
{
    switch (owner) {
    case NONE:
        return "NONE";
    case WIFI:
        return "WIFI";
    case SPI:
        return "SPI";
    case TEMP:
        return "TEMP";
    case RELAY:
        return "RELAY";
    case BUTTON:
        return "BUTTON";
    case OLED:
        return "OLED";
    case USB:
        return "USB";
    case NOT_EXPOSED:
        return "NOT_EXPOSED";
#if JTAG_DEBUG
    case JTAG:
        return "JTAG";
#endif
    default:
        return "unknown";
    }
}

gpio_owner_t gpio_owner(int gpio)
{
    return gpio_user[gpio];
}

void check_gpio_clear(int gpio)
{
    gpio_user[gpio] = NONE;
}

void check_gpio(int gpio, gpio_owner_t owner)
{
    char *s = NULL;
    switch (gpio_user[gpio]) {
    case NONE:
        gpio_user[gpio] = owner;
        return;
    case WIFI:
        s = "WIFI";
        break;
    case SPI:
        s = "SPI";
        break;
    case TEMP:
        s = "TEMP";
        break;
    case RELAY:
        s = "RELAY";
        break;
    case BUTTON:
        s = "BUTTON";
        break;
    case OLED:
        s = "OLED";
        break;
    case USB:
        s = "USB";
        break;
    case NOT_EXPOSED:
        s = "NOT_EXPOSED";
        break;
    default:
        ESP_LOGE(TAG, "%s wants GPIO %d used by unknown %d", gpio_owner_str(owner), gpio, gpio_user[gpio]);
        vTaskEndScheduler();
    }

    ESP_LOGE(TAG, "%s wants GPIO %d used by %s", gpio_owner_str(owner), gpio, s);
    vTaskEndScheduler();
}

// WROOM32, 38 pin board
// https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/gpio.html
// https://randomnerdtutorials.com/esp32-pinout-reference-gpios/
// https://i0.wp.com/randomnerdtutorials.com/wp-content/uploads/2018/08/esp32-pinout-chip-ESP-WROOM-32.png
void check_init(int wifi)
{
    wifi_mode = wifi;
    if (wifi) {
        for (int i=0; i<COUNT_OF(adc2_gpio); i++)
            if (wifi == 1 || i >= ADC2_WIFI_USABLE)
                gpio_user[adc2_gpio[i]] = WIFI;
    }

#if JTAG_DEBUG
    for (int i=12; i<16; i++)
        gpio_user[i] = JTAG;
#endif

    // internal flash, not exposed anyway
    for (int i=6; i<12; i++)
        gpio_user[i] = SPI;

    // ADC1
    gpio_user[37] = NOT_EXPOSED;
    gpio_user[38] = NOT_EXPOSED;
    // USB RX/TX UART0
    // https://docs.espressif.com/projects/esp-at/en/latest/esp32/Get_Started/Hardware_connection.html
    gpio_user[1] = USB;
    gpio_user[3] = USB;

    // 6 pins = 1xNC, 3xGND, 1x3.3V, 1x ChipPU
    gpio_user[20] = NOT_EXPOSED;
    gpio_user[24] = NOT_EXPOSED;
    for (int i=28; i<32; i++)
        gpio_user[i] = NOT_EXPOSED;
}

void check_report()
{
    for (int i=0; i<COUNT_OF(gpio_user); i++) {
        if (gpio_user[i] != NONE)
            ESP_LOGI(TAG, "GPIO %d used by %s", i, gpio_owner_str(gpio_user[i]));
        //else
        //    ESP_LOGI(TAG, "GPIO %d", i);
    }

    for (int i=0; i<COUNT_OF(gpio_user); i++) {
        if (gpio_user[i] == NONE) {
            int adc1 = 0, adc2 = 0, wifi = 0;
            for (int x=0; x<COUNT_OF(adc1_gpio); x++)
                if (adc1_gpio[x] == i)
                    adc1 = 1;
            for (int x=0; x<COUNT_OF(adc2_gpio); x++)
                if (adc2_gpio[x] == i)
                    adc2 = 1;
            for (int x=0; x<ADC2_WIFI_USABLE; x++)
                if (adc2_gpio[x] == i)
                    wifi = 1;
            ESP_LOGI(TAG, "GPIO %d is FREE %s%s %s", i, adc1? "ADC1" : "", adc2? "ADC2" : "", wifi? "WIFI" : "");
        }
    }
}
