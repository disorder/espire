#ifndef __BUTTON_H__
#define __BUTTON_H__

#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// device specific but this could be a useful base
typedef enum {
    MINUS=0,
    PLUS,
    TOGGLE,
    BUTTON_MAX,
} button_enum_t;

#define BUTTON_STATE_OFF (!BUTTON_STATE_ON)

typedef struct button button_t;
struct button
{
    int gpio;
    SemaphoreHandle_t sem;
    // current state
    int state;
    TickType_t change;
    // debounce
    TickType_t last;

    // useful information for handler
    // changed = 1 ...simple action
    // changed = 0 ...repeat or long press
    // reps/longs is a number of continuous repeats
    int changed;
    int repeated;
    int longpress;
    int reps;
    int longs;

    void (*handler)(button_t *);
    // custom data for caller use
    void *data;
};

void button_init(int *gpios, int count, void (*handler)(button_t *b));
void button_add(button_t *b);
void button_run();
// device specific
void button_handler(button_t *b);

#endif /* __BUTTON_H__ */
