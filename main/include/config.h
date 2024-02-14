#ifndef __CONFIG_H__
#define __CONFIG_H__

#include "heap.h"
#include "sdkconfig.h"
#include "stdint.h"

//#define JTAG_DEBUG 1
#define CONTROLLER_SLEEP 1

#ifndef CONFIG_ESP_HEATING_UDP_ENCRYPT
#define HEATING_UDP_ENC 0
#else
#define HEATING_UDP_ENC 1
#endif
//#define HEATING_UDP_SECRET CONFIG_ESP_HEATING_UDP_SECRET
//#define HEATING_UDP_KEY_B64 CONFIG_ESP_HEATING_UDP_KEY_B64
//#define HEATING_UDP_IV_B64 CONFIG_ESP_HEATING_UDP_IV_B64
//#define HEATING_UDP_PORT CONFIG_ESP_HEATING_UDP_PORT
extern char *HEATING_UDP_SECRET;
extern char HEATING_UDP_KEY_B64[44];
extern char HEATING_UDP_IV_B64[24];
extern uint16_t HEATING_UDP_PORT;

#define HTTPD_SSL
#define API_KEY "test"
//#define CONFIG_ESP_CONTROLLER_IP "127.0.0.1"
extern char CONTROLLER_IP[15+1];
extern struct sockaddr_in CONTROLLER_SA;
#define HEATING_TEMP_MAX 25.0
// when client device and controller not initialized, avoid NAN for temp_mod
#define HEATING_TEMP_DEFAULT 15.0
#define HOSTNAME_DEFAULT "dummy"
#include "heating.h"
extern char HOSTNAME[member_size(heating_t, name)];
// for Normally Closed heating is off when relay signal is low
// per-relay NC/NO is not implemented
#define HEATING_ON 1

// TODO
//#define LOG_IP "10.0.0.9"
//#define LOG_PORT 8001
#define LOG_IP2 "nas"
#define LOG_PORT2 8001

#define NVS_LABEL "nvs"
#define NVS_NAMESPACE "espire"

#define PING_ONLINE_IP CONFIG_ESP_PING_ONLINE_IP

// this is for debugging purposes, finds any device after i2c address failed
//#define I2C_SCAN 1
#ifndef I2C_SCAN
#define I2C_SCAN 0
#endif
#define I2C_HOST 0
#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22
// currently only 128x64 SSD1306 is supported
#define OLED_I2C_ADDR 0x3C
#define SSD1306_CLOCK_HZ (400 * 1000)
#define SSD1306_PIN_RST -1
#define SSD1306_H_RES 128
#define SSD1306_V_RES 64
// enable both for convenience
#define OLED_SSD1306
#define LCD_ST7735S
// 1.3" 240x240 GMT130, SPI_TFT_SPI_MODE = 3 (not 2)
//#define LCD_ST7789_1
// 2.4" 240x320... PORTRAIT=LANDSCAPE, inverted colors
//#define LCD_ST7789_2
// alternative driver (uses synchronous write)
//#define ST7735S_SIMPLE

//#define LVGL_TICK_PERIOD_MS    2
// scrolls nice and produces less heat, 100 is too choppy
#define LVGL_TICK_PERIOD_MS 50
// 1000 is too much lag when switching modes
#define LVGL_TICK_PERIOD_MS_SLOW 200

// only use for formatted chunked responses
#define HTTP_PRINT_BUFSIZE 100
// adding more handlers will require httpd restart and will add the same amount
#ifdef HTTPD_SSL
// restart seems to break SSL, getting two handshakes and a failure
#define HTTPD_MAX_URI_HANDLERS 30
#else
#define HTTPD_MAX_URI_HANDLERS 10
#endif

// to allow self-signed certificates
#define AUTO_HTTPS_INSECURE CONFIG_ESP_AUTO_HTTPS_INSECURE
#define OTA_HTTPS_INSECURE CONFIG_ESP_OTA_HTTPS_INSECURE
// or to instead use same certificate as HTTPD
#define AUTO_HTTPS_INTERNAL CONFIG_ESP_AUTO_HTTPS_INTERNAL
#define OTA_HTTPS_INTERNAL CONFIG_ESP_OTA_HTTPS_INTERNAL

#ifndef CONFIG_ESP_AUTO_AUTH_BASIC
#define CONFIG_ESP_AUTO_AUTH_BASIC 0
#endif
#define AUTO_AUTH_BASIC CONFIG_ESP_AUTO_AUTH_BASIC

#ifndef CONFIG_ESP_OTA_AUTH_BASIC
#define CONFIG_ESP_OTA_AUTH_BASIC 0
#endif
#define OTA_AUTH_BASIC CONFIG_ESP_OTA_AUTH_BASIC

#define OTA_FIRMWARE_URL CONFIG_ESP_OTA_FIRMWARE_URL
#define OTA_URL_USER CONFIG_ESP_OTA_USER
#define OTA_URL_PASSWORD CONFIG_ESP_OTA_PASSWORD

// TODO AUTO and OTA would be better triggered manually = period 0
#define AUTO_CONFIG_URL CONFIG_ESP_AUTO_CONFIG_URL
#define AUTO_CONFIG_PERIOD_S CONFIG_ESP_AUTO_CONFIG_PERIOD_S
#define AUTO_CONFIG_URL_USER CONFIG_ESP_AUTO_CONFIG_URL_USER
#define AUTO_CONFIG_URL_PASSWORD CONFIG_ESP_AUTO_CONFIG_URL_PASSWORD

//#define ADC2_MUTEX_BYPASS
#define RELAY_CNT CONFIG_ESP_RELAY_CNT

#define BUTTON_DEBOUNCE_MS CONFIG_ESP_BUTTON_DEBOUNCE_MS
#define BUTTON_REPEAT_MS CONFIG_ESP_BUTTON_REPEAT_MS
#define BUTTON_LONG_MS CONFIG_ESP_BUTTON_LONG_MS
// PULLUP ON=0, PULLDOWN ON=1
#define BUTTON_STATE_ON 0

// https request can use 30-40K per request
#define METAR_FTP 1
#define METAR_LOCATION CONFIG_ESP_METAR_LOCATION
#define SHMU_STATION CONFIG_ESP_SHMU_STATION
#define METAR_PERIOD_S CONFIG_ESP_METAR_PERIOD_S
// it's a hack but stop reading 6K answer if we look for first lines only
#define SHMU_STATIC 400
#define SHMU_PORT CONFIG_ESP_SHMU_HTTP_PORT
#define SHMU_IP "espire"

#define OWM_API_KEY CONFIG_ESP_OWM_API_KEY
#define OWM_LAT CONFIG_ESP_OWM_LAT
#define OWM_LON CONFIG_ESP_OWM_LON

#define TEMP_PERIOD_S CONFIG_ESP_TEMP_PERIOD_S
// force wifi disconnect after some time
#define TEMP_FORCE_WIFI_MS CONFIG_ESP_TEMP_FORCE_WIFI_MS

#define HEAP_PERIOD_S CONFIG_ESP_HEAP_PERIOD_S
#define HEAP_PERIOD_INFO_S CONFIG_ESP_HEAP_PERIOD_INFO_S
#define HEAP_WARN_DECREASE_B CONFIG_ESP_HEAP_WARN_DECREASE_B
#define HEAP_REBOOT_B CONFIG_ESP_HEAP_REBOOT_B

#define CONFIG_ESP_WIFI_AUTH_WPA2_PSK 1
#define WIFI_COUNTRY_CODE CONFIG_ESP_WIFI_COUNTRY_CODE

//#define WIFI_SSID CONFIG_ESP_WIFI_SSID
//#define WIFI_PASSWORD CONFIG_ESP_WIFI_PASSWORD
extern char WIFI_SSID[32];
extern char WIFI_PASSWORD[64];
#define WIFI_CHECK_MS (WIFI_RETRY_WAIT_MS+5000)
// ideally this would block and gracefully fail but it could block
// other tasks forever if there are perpetual attempts to take wifi
//#define WIFI_MAXIMUM_RETRY -1
//#define WIFI_RETRY_CANCEL_MS 0
// 0 would block waiting infinitely with portMAX_DELAY
#define WIFI_RETRY_WAIT_MS CONFIG_ESP_WIFI_RETRY_WAIT_MS
// if -1 this will retry indefinitely
#define WIFI_MAXIMUM_RETRY CONFIG_ESP_WIFI_MAX_RETRY_CNT
// cancel failing connection (tasks can check wifi_connected)
// this will make sure failing connection won't block other tasks
#define WIFI_RETRY_CANCEL_MS CONFIG_ESP_WIFI_RETRY_CANCEL_MS

#define NTP_SERVER1 CONFIG_ESP_NTP_SERVER1
#define NTP_SERVER2 CONFIG_ESP_NTP_SERVER2
#define NTP_TZ CONFIG_ESP_TZ

// example for wifi connect/disconnect handler, not required
//#define CONFIG_EXAMPLE_CONNECT_WIFI

typedef struct {
    float serial_resistance;
    float nominal_resistance;
    float nominal_temperature;
    float beta_val;
    float vsource;
} thermistor_t;

typedef struct {
    int cnt;
    thermistor_t *th;
    char *label;
} th_def_t;

#ifdef INCLUDE_THERMISTORS
// NTC 2.7k with 2.7k series R
/*
static thermistor_t th_2k7 = {
    .serial_resistance = 2700,
    .nominal_resistance = 2700,
    .nominal_temperature = 25,
    .beta_val = 4250, //3950
    .vsource = 3300,
};
*/

// NTC 4.7k MF52AT
static thermistor_t th_4k7 = {
    .serial_resistance = 2700,
    .nominal_resistance = 4700,
    .nominal_temperature = 25,
    .beta_val = 3950,
    .vsource = 3300,
};
#endif

void config_init();

#endif /* __CONFIG_H__ */
