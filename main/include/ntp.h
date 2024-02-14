#ifndef __NTP_H__
#define __NTP_H__

#include <time.h>

extern time_t ntp_synced;
extern time_t ntp_start;

void ntp_init(void);

#endif /* __NTP_H__ */
