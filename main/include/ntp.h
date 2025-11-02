#ifndef __NTP_H__
#define __NTP_H__

#include <time.h>

extern time_t ntp_synced;
extern time_t api_synced;
#define dt_synced (ntp_synced || api_synced)
extern time_t ntp_start;

void ntp_init(void);

#endif /* __NTP_H__ */
