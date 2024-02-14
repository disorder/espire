#include "config.h"
#include "device.h"
#include "ftp.h"
#include "wifi.h"
#include "adc2.h"

#include "esp_log.h"
static const char *TAG = "ftp";

// callback needs to call free(req->buf) if not NULL
void ftp_get_task(ftp_request_t *req)
{
    xSemaphoreTake(esp.sockets, portMAX_DELAY);
    xSemaphoreTake(esp.sockets, portMAX_DELAY);
    WIFI_ADD(xTaskGetCurrentTaskHandle());

    netbuf *nbctrl = NULL;
    int ret = 0;

    if ((ret = FtpConnect(req->host, req->port, &nbctrl)) == 0) {
        ESP_LOGI(TAG, "failed to connect %s:%d", req->host, req->port);
        goto CLEANUP;
    }

    char *user = "anonymous";
    if (req->user != NULL)
        user = req->user;
    char *pass = "";
    if (req->password != NULL)
        pass = req->password;
    if ((ret = FtpLogin(user, pass, nbctrl)) == 0) {
        ESP_LOGI(TAG, "failed to login %s:%d", req->host, req->port);
        goto CLEANUP;
    }

    if (req->path)
        if ((ret = FtpChdir(req->path, nbctrl)) == 0) {
            ESP_LOGI(TAG, "failed to cd %s:%d %s", req->host, req->port, req->path);
            goto CLEANUP;
        }

    // TYPE I
    char mode = FTPLIB_IMAGE;
    unsigned int size;
    if ((ret = FtpSize(req->filename, &size, mode, nbctrl)) == 0)
        goto CLEANUP;

    ESP_LOGI(TAG, "%s/%s size: %d", req->path, req->filename, size);
    char *data = malloc(size + req->pad);
    if (data == NULL) {
        ret = 0;
        goto CLEANUP;
    }

    netbuf *nbdata = NULL;
    if ((ret = FtpAccess(req->filename, FTPLIB_FILE_READ, mode, nbctrl, &nbdata)) == 0) {
        ESP_LOGI(TAG, "failed to get %s:%d %s/%s", req->host, req->port, req->path, req->filename);
        goto CLEANUP;
    }

    // read at once, ret will be size
    if ((ret = FtpRead(data, size, nbdata)) == 0) {
        ESP_LOGI(TAG, "failed to read %s:%d %s/%s", req->host, req->port, req->path, req->filename);
        goto CLEANUP;
    }

    // possibly realloc to smaller size
    req->bufsize = ret;
    req->buf = data;
    if (req->pad)
        data[req->bufsize] = '\0';
    ret = (size == ret);

CLEANUP:
    if (nbdata != NULL)
        FtpClose(nbdata);
    if (nbctrl != NULL)
        FtpQuit(nbctrl);

    xSemaphoreGive(esp.sockets);
    xSemaphoreGive(esp.sockets);

    req->callback(req, ret);
    WIFI_DEL(xTaskGetCurrentTaskHandle());

    //xvTaskDelete(req->task);
    xvTaskDelete(NULL);
}

void ftp_get(ftp_request_t *req)
{
    // TODO seeing stuck ftp task after some time
    if (req->port == 0)
        req->port = 21;
    ESP_LOGI(TAG, "ftp_get %s:%d %s/%s", req->host, req->port, req->path, req->filename);
    assert(req->callback);

    // TODO this can probably leave wifi_count +1
    // based on configuration wifi_run() can cancel connection after timeout
    if (wifi_run(0, 0, NULL)) {
        // failed - cancelled
        //ESP_LOGW(TAG, "wifi connection cancelled");
        if (req->callback != NULL)
            req->callback(req, 0);
        return;
    } else if (!wifi_connected) {
        // failed - may connect later
        ESP_LOGW(TAG, "wifi connection timeouted");
        // ...changed to add=0
        // decrease because we're cancelling task
        //WIFI_DEL();
        if (req->callback != NULL)
            req->callback(req, 0);
        return;
    }

    xxTaskCreate((void (*)(void*)) ftp_get_task, "ftp_get_task", 6*1024, req, 0, &req->task);
}
