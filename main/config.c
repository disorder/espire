#include "config.h"
#include "device.h"
#include "auto.h"
#include "wifi.h"
#include "temp.h"
#include "oled.h"
#include "nv.h"
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
static const char *TAG = "config";

// all of this could be moved to system_t
char WIFI_SSID[32] = CONFIG_ESP_WIFI_SSID;
char WIFI_PASSWORD[64] = CONFIG_ESP_WIFI_PASSWORD;
char HOSTNAME[member_size(heating_t, name)] = "";
char CONTROLLER_IP[15+1] = CONFIG_ESP_CONTROLLER_IP;
struct sockaddr_in CONTROLLER_SA = {0};

uint16_t HEATING_UDP_PORT = CONFIG_ESP_HEATING_UDP_PORT;
char *HEATING_UDP_SECRET = NULL;
char HEATING_UDP_KEY_B64[44] = CONFIG_ESP_HEATING_UDP_KEY_B64;
char HEATING_UDP_IV_B64[24] = CONFIG_ESP_HEATING_UDP_IV_B64;

// TODO load more from nvs
void config_init(int booting)
{
    char *ssid = NULL;
    char *password = NULL;
    size_t size = 0;

    size = 0;
    nv_read_str(WIFI_SSID_KEY, &ssid, &size);
    size = 0;
    nv_read_blob(WIFI_PASSWORD_KEY, (void **) &password, &size);
    password = NULL;
    // TODO process all generics?
    if (ssid != NULL)
        ESP_LOGE(TAG, "setting ssid: %s", ssid);
    if (password != NULL)
      ESP_LOGE(TAG, "setting password");
    if (ssid || password)
        wifi_update(ssid, password, 0);
    free(ssid);
    free(password);

    char *value = NULL;
    size = 0;
    nv_read_str("hostname", &value, &size);
    if (value != NULL) {
        strncpy(HOSTNAME, value, (size < sizeof(HOSTNAME))? size : sizeof(HOSTNAME));
        HOSTNAME[sizeof(HOSTNAME) - 1] = '\0';
        free(value);
    } else if (esp.dev->hostname != NULL) {
        size = strlen(esp.dev->hostname);
        strncpy(HOSTNAME, esp.dev->hostname, (size < sizeof(HOSTNAME))? size : sizeof(HOSTNAME));
    }
    esp.dev->hostname = HOSTNAME;

    value = NULL;
    size = 0;
    nv_read_str("controller_ip", &value, &size);
    if (value != NULL) {
        strncpy(CONTROLLER_IP, value, (size < sizeof(CONTROLLER_IP))? size : sizeof(CONTROLLER_IP));
        CONTROLLER_IP[sizeof(CONTROLLER_IP) - 1] = '\0';
        free(value);
    }
    init_sa(CONTROLLER_IP, 0, &CONTROLLER_SA);

#if HEATING_UDP_ENC
    nv_read_u16("th.udp.port", &HEATING_UDP_PORT);

    value = NULL;
    size = 0;
    nv_read_str("th.udp.secret", (void*) &value, &size);
    if (value == NULL) {
        HEATING_UDP_SECRET = strndup(CONFIG_ESP_HEATING_UDP_SECRET, sizeof(CONFIG_ESP_HEATING_UDP_SECRET));
        assert(HEATING_UDP_SECRET != NULL);
    } else
        HEATING_UDP_SECRET = value;

    value = NULL;
    size = 0;
    nv_read_blob("th.udp.key", (void*) &value, &size);
    memcpy(HEATING_UDP_KEY_B64, value, (size < sizeof(HEATING_UDP_KEY_B64))? size : sizeof(HEATING_UDP_KEY_B64));
    free(value);

    value = NULL;
    size = 0;
    nv_read_blob("th.udp.iv", (void*) &value, &size);
    memcpy(HEATING_UDP_IV_B64, value, (size < sizeof(HEATING_UDP_IV_B64))? size : sizeof(HEATING_UDP_IV_B64));
    free(value);

    th_aes_init();
#endif

    // devices can default to different display modes or power off state
    uint8_t u8;
    if (nv_read_u8("mode_default", &u8) == ESP_OK)
        oled_update.mode_default = u8 % MODE_MAX;
    else
        oled_update.mode_default = HEATING;

    if (nv_read_u8("oled_power", &u8) == ESP_OK)
        oled_update.power_default = u8;
    else
        oled_update.power_default = 1;
    oled_update.power_state = oled_update.power_default;
}
