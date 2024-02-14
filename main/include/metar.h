#ifndef __METAR_H__
#define __METAR_H__

#include "module.h"

typedef struct {
    //TickType_t last;
    time_t last;
    //char name[25];
    uint16_t station;
    float ta_2m;
    float rh;
    float pr_1h;
    //float pa;
    // wind
    //float ws_avg;
    //float wd_avg;
} shmu_t;

typedef struct metar metar_t;
struct metar {
    module_t module;
    //TickType_t last_task;
    time_t last_task;

    char *buf;
    char report_time[7+1];
    time_t time;
    char decoded[256];

    char icao[5];
    char *url;
    char *url_decode;
    task_t *task;
    int celsius;
    int dew;
    float rh;
    float wind_speed;
    char *wind_from;
    char *wind_to;
    float wind_gust;
    int pressure;
    // from decoded url, which is just for testing
    int humidity;
    //TickType_t last;
    time_t last;
    // forecast data
    int has_forecast;
    metar_t *forecast;
    shmu_t shmu;
};

metar_t *metar_new(char *icao, uint16_t station);
void metar_run(metar_t *self, int run);

#endif /* __UTIL_H__ */
