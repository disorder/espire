#ifndef __HEAP_H__
#define __HEAP_H__

#include "freertos/FreeRTOS.h"
#include "util.h"
#include <stdint.h>

extern uint32_t heap_first_free;
extern uint32_t heap_first_t_free;
extern time_t heap_first_t;
extern double heap_estimate_s;

void heap_init(int (*handler)(uint32_t, uint32_t));

#endif /* __HEAP_H__ */
