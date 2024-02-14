#ifndef __API_H__
#define __API_H__

#include "httpd.h"

void api_init(httpd_t *httpd);
esp_err_t api_reboot(httpd_req_t *req);
int api_key_check(int set_status, httpd_req_t *req, char **ptr_buf, int *ptr_buf_len);

#endif /* __API_H__ */
