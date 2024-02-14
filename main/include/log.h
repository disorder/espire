#ifndef __LOG_H__
#define __LOG_H__

#include <stdarg.h>

void log_init();
int log_add(char *ip, int port);

int log_local(char *fmt, ...);
int log_inet(const char *fmt, va_list args);
extern int (*log_orig)(const char *fmt, va_list vargs);

#endif /* __LOG_H__ */
