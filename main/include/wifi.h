#ifndef __WIFI_H__
#define __WIFI_H__

extern int wifi_connected;

#include "check.h"

void wifi_init(void);
// zero for success
int wifi_run(int add, int nonblock, TaskHandle_t owner);
void wifi_check();
void wifi_update(char *ssid, char *password, int apply);

// call wifi_run(), these are for use by ADC2_WAIT
void _wifi_start_sta(int nonblock);
void _wifi_stop_sta(int force);

#endif /* __WIFI_H__ */
