#include "config.h"
#include "device.h"
#include "co2.h"
#include "check.h"
#include "driver/uart.h"

#include "esp_log.h"
static const char *TAG = "co2";

static task_t *task = NULL;

int co2_ppm = -1;
uint8_t senseair_id[8] = {0};
uint16_t senseair_abc = 0;

// NDIR flashing about every ~4s
#define CO2_WAIT_S 4
SemaphoreHandle_t co2_mutex = NULL;

// UART0 GPIO 1+3 (USB)
// UART2 GPIO 17+16 (if no PSRAM)
// UART1 GPIO 9+10 (if no SPI flash)
#define S8_TXD_PIN 17
#define S8_RXD_PIN 16
#define S8_UART UART_NUM_2
#define S8_BAUD 9600
#define S8_TIMEOUT_MS 500
#define S8_WAIT_MS 500
#define RX_BUF_SIZE 7

// https://github.com/SFeli/ESP32_S8/blob/master/ESP32_S8_01.c
// Request CO2-Value
uint8_t CO2req[] = {0xFE, 0x44, 0x00, 0x08, 0x02, 0x9F, 0x25};
//uint8_t CO2req[] = {0xFE, 0x04, 0x00, 0x03, 0x00, 0x01, 0xD5, 0xC5};
// Request ABC-Interval in [h] 1f in Hex -> 31 dezimal
uint8_t ABCreq[] = {0xFE, 0x03, 0x00, 0x1F, 0x00, 0x01, 0xA1, 0xC3};
// 0xB4 = 7.5 days
uint8_t ABCenable[] = {0xFE, 0x06, 0x00, 0x1F, 0x00, 0xB4, 0xAC, 0x74};
uint8_t ABCdisable[] = {0xFE, 0x06, 0x00, 0x1F, 0x00, 0x00, 0xAC, 0x03};
// Readout FirmWareversion
uint8_t FWreq[] = {0xFE, 0x04, 0x00, 0x1C, 0x00, 0x01, 0xE4, 0x03};
// Sensor ID hi
uint8_t ID_Hi[] = {0xFE, 0x04, 0x00, 0x1D, 0x00, 0x01, 0xB5, 0xC3};
// Sensor ID lo
uint8_t ID_Lo[] = {0xFE, 0x04, 0x00, 0x1E, 0x00, 0x01, 0x45, 0xC3};

int uart_send(int uart, const char* data, int Req_len)
{
    const int txBytes = uart_write_bytes(uart, data, Req_len);
    return txBytes;
}

unsigned short int ModBus_CRC(unsigned char *buf, int len)
{
    unsigned short int crc = 0xFFFF;
    for (int pos = 0; pos < len; pos++) {
        crc ^= (unsigned short int)buf[pos]; // XOR byte into least sig. byte of crc
        for (int i = 8; i != 0; i--) {       // Loop over each bit
            if ((crc & 0x0001) != 0) {       // If the LSB is set
                crc >>= 1;      // Shift right and XOR 0xA001
                crc ^= 0xA001;
            }
            else                // else LSB is not set
                crc >>= 1;      // Just shift right
        }
    }
    // Note, this number has low and high bytes swapped, so use it accordingly (or swap bytes)
    return crc;
}

static uint8_t *uart_read(int uart, uint8_t *cmd, int len, int read, int wait_ms, int timeout_ms)
{
    static uint8_t resp[RX_BUF_SIZE];
    uint8_t *res = NULL;

    xSemaphoreTake(co2_mutex, portMAX_DELAY);
    uart_flush_input(S8_UART);

    // maybe assert txlen?
    int txlen = uart_write_bytes(uart, cmd, len);
    if (txlen != len) {
        ESP_LOGE(TAG, "sent incomplete command: %d != %d", txlen, len);
        goto CLEANUP;
    }
    vTaskDelay(MS_TO_TICK(wait_ms));

    int rlen = uart_read_bytes(uart, resp, read, MS_TO_TICK(timeout_ms));
    if (rlen != 7) {
        ESP_LOGE(TAG, "unexpected read size: %d", rlen);
        goto CLEANUP;
    }

    uint16_t crc = (uint16_t) resp[6] * 256 + (uint16_t) resp[5];
    int valid = memcmp(&resp[5], &crc, 2);
    if (valid != 0) {
        ESP_LOGE(TAG, "invalid response CRC: response %04hx calculated %04hx", *((uint16_t *) &resp[5]), crc);
        goto CLEANUP;
    }

    res = (uint8_t *) &resp;
CLEANUP:
    xSemaphoreGive(co2_mutex);

    //if (res != NULL) ESP_LOG_BUFFER_HEX(TAG, res, rlen);
    return res;
}

#define READ_U16(buf, i)  (buf[i]*256 + buf[i+1])

void senseair_s8_init()
{
    const uart_config_t uart_config = {
        .baud_rate = S8_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    check_gpio(S8_RXD_PIN, CO2);
    check_gpio(S8_TXD_PIN, CO2);
    // TX buffer is 0, blocking until all data sent
    uart_driver_install(S8_UART, 1024 * 2, 0, 0, NULL, 0);
    uart_param_config(S8_UART, &uart_config);
    uart_set_pin(S8_UART, S8_TXD_PIN, S8_RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    co2_mutex = xSemaphoreCreateMutex();
    int val = senseair_s8_abc();
    if (val != -1) {
        ESP_LOGI(TAG, "ABC period: %d", val);
    }

    val = senseair_s8_fwver();
    if (val != -1) {
        ESP_LOGI(TAG, "FW version: %04x", val);
    }

    ESP_LOGI(TAG, "ID: %08" PRIX32, senseair_s8_id());
}

int senseair_s8_u16(uint8_t *cmd, int len)
{
    uint8_t *resp = NULL;
    resp = uart_read(S8_UART, cmd, len, 7, S8_WAIT_MS, S8_TIMEOUT_MS);
    if (resp == NULL)
        return -1;

    return READ_U16(resp, 3);
}

inline int senseair_s8_abc()
{
    return senseair_s8_u16(ABCreq, sizeof(ABCreq));
}

inline int senseair_s8_abc_enable()
{
    return senseair_s8_u16(ABCenable, sizeof(ABCenable));
}

inline int senseair_s8_abc_disable()
{
    return senseair_s8_u16(ABCdisable, sizeof(ABCdisable));
}

inline int senseair_s8_fwver()
{
    return senseair_s8_u16(FWreq, sizeof(FWreq));
}

inline int senseair_s8_co2_ppm()
{
    co2_ppm = senseair_s8_u16(CO2req, sizeof(CO2req));
    return co2_ppm;
}

inline uint32_t senseair_s8_id()
{
    uint16_t hi = senseair_s8_u16(ID_Hi, sizeof(ID_Hi));
    uint16_t lo = senseair_s8_u16(ID_Lo, sizeof(ID_Lo));
    return (hi<<16) | lo;
}

static int co2_sock = -1;
#define CO2_UDP_PORT 12012
static void co2_send(uint16_t ppm)
{
    if (co2_sock < 0) {
        //return;
        xSemaphoreTake(esp.sockets, portMAX_DELAY);
        if ((co2_sock = socket(PF_INET, SOCK_DGRAM, 0)) == -1) {
            ESP_LOGE(TAG, "socket: %s", strerror(errno));
            xSemaphoreGive(esp.sockets);
            // return from task will crash
            return;
        }
    }

    if (CONTROLLER_SA.sin_addr.s_addr == INADDR_ANY)
        return;

    struct sockaddr_in sa = {0};
    // TODO hardcoded IP
    sa.sin_addr.s_addr = htonl(0x0a000063);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(CO2_UDP_PORT);
    // sending little endian (host order)
    sendto(co2_sock, &ppm, sizeof(ppm),
           MSG_DONTWAIT, (struct sockaddr *) &sa, sizeof(sa));

    sa.sin_addr.s_addr = htonl(0x0a000009);
    sendto(co2_sock, &ppm, sizeof(ppm),
           MSG_DONTWAIT, (struct sockaddr *) &sa, sizeof(sa));
}

void co2_task(void *pvParameter)
{
    //TickType_t last_task = 0;
    //time_t last_task = 0;
    while (1) {
        // we can run this only via request or periodically (and send UDP?)
        co2_ppm = senseair_s8_co2_ppm();
        if (co2_ppm != -1) {
            ESP_LOGI(TAG, "PPM: %d", co2_ppm);
            co2_send(co2_ppm);
            graphite_udp("co2.", esp.dev->hostname, "espire", co2_ppm, 1, 0);
       }

        //last_task = xTaskGetTickCount();
        //time(&last_task);
        _vTaskDelay(S_TO_TICK(CO2_WAIT_S));
    }

    //xvTaskDelete(task);
}

void co2_init()
{
    if (task != NULL)
        return;

    senseair_s8_init();
    // TODO disable ABC - it goes haywire in a week (7.5d max)
    senseair_s8_abc_disable();
    int val = senseair_s8_abc();
    if (val != -1) {
        ESP_LOGI(TAG, "ABC period: %d", val);
    }

    xxTaskCreate((void (*)(void*))co2_task, "co2_task", 2*1024, NULL, 0, &task);
}
