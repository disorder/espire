#ifndef __DUMMY_H__
#define __DUMMY_H__

#include "module.h"

typedef struct {
    module_t module;
    // custom module data
    task_t *task;
} dummy_t;

dummy_t *dummy_new();
void dummy_run(dummy_t *self, int run);

#endif /* __DUMMY_H__ */
