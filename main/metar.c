#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include <string.h>
#include <math.h>
#include "metar.h"
#include "http.h"
#include "ftp.h"
#include "util.h"
#include "oled.h"
#include "wifi.h"
#include "ntp.h"
#include "ping.h"
#include "module.h"
#include "config.h"

#include "esp_log.h"
static const char *TAG = "metar";

//static char metar_url[] = "https://tgftp.nws.noaa.gov/data/observations/metar/decoded/XXXX.TXT";
static char shmu_url[] = "http://meteo.shmu.sk/customer/home/opendata/?observations;date=DD.MM.YYYY:ZZ";
#if METAR_FTP != 0
static char metar_host[] = "tgftp.nws.noaa.gov";
static char metar_path_decode[] = "/data/observations/metar/stations";
static char metar_file[] = "XXXX.TXT";
#else
static char metar_url_decode[] = "https://tgftp.nws.noaa.gov/data/observations/metar/stations/XXXX.TXT";
#endif

metar_t *metar_new(char *icao, uint16_t station)
{
    ESP_LOGI(TAG, "new: %s", icao);
    metar_t *self = calloc(1, sizeof(metar_t));
    assert(self != NULL);
    strncpy(self->icao, icao, sizeof(self->icao));

    self->module.name = TAG;
    self->module.type = M_METAR;
    self->module.run = (void (*)(void*,int)) metar_run;
    self->module.offline = 3;
    // only required for shmu
    self->module.ntp = 1;

    //self->url = strdup(metar_url);
    //assert(self->url != NULL);
    //memcpy(self->url + sizeof(metar_url)-9, icao, 4);

#if METAR_FTP == 0
    self->url_decode = strdup(metar_url_decode);
    assert(self->url_decode != NULL);
    memcpy(self->url_decode + sizeof(metar_url_decode)-9, icao, 4);
#endif

    if (station > 0)
        self->shmu.station = station;

    module_add(&self->module);
    return self;
}

void metar_cb(http_request_t *req, int success)
{
    if (!success)
        goto CLEANUP;
    if (!req->client || esp_http_client_get_status_code(req->client) != 200) {
        goto CLEANUP;
    }
    ESP_LOGI(TAG, "processing decoded");
    size_t req_len = req->bufsize;

    metar_t *self = req->data;
    assert(self != NULL);
    char rh_str[] = "Relative Humidity: ";
    char *rh = memmem(req->buf, req_len, &rh_str, sizeof(rh_str)-1);
    if (rh != NULL) {
        rh += sizeof(rh_str)-1;
        int max = req->buf + req_len - rh;
        // memchr version
        char *rh_end = memchr(rh, '%', max);
        if (rh_end == NULL) {
            // unexpected
            ESP_LOGE(TAG, "%s %d: %s", self->icao, req_len, req->buf);
        } else {
            rh_end[0] = '\0';
            self->humidity = atoi(rh);
            ESP_LOGW(TAG, "%s RH: %d%%", self->icao, self->humidity);
        }
        /*
        for (char *rh_end=rh; max>0; max--, rh_end++) {
            if (rh_end[0] == '\0') {
                // unexpected
                ESP_LOGE(TAG, "%d: %s", req_len, req->buf);
                break;
            }
            if (rh_end[0] == '%') {
                rh_end[0] = '\0';
                self->humidity = atoi(rh);
                ESP_LOGW(TAG, "RH: %d%%", humidity);
                break;
            }
        }
        */
    }
CLEANUP:
    ESP_LOGI(TAG, "cleanup");
    if (req->buf != NULL)
        free(req->buf);
    free(req);
}

char *metar_token(char *start, char *stop)
{
    char *end = strchrnul(start, ' ');
    char *end2 = strchrnul(start, '\n');
    end = end2 < end? end2 : end;
    if (end - start == 0)
        return NULL;
    end[0] = '\0';
    return end;
}

typedef enum {
    TEMPERATURE = 0,
    PRESSURE,
    FORECAST,
    WEATHER,
    WIND,
    CLOUD,
    RUNWAY,

    DESCRIPTOR,
    ANY,
} match_type_t;

typedef struct
{
    match_type_t type;
    char *match;
    char *description;
} metar_match_t;


metar_match_t m_match[] = {
    /*
    {
        .type = RUNWAY,
        .match = "//",
        .description = "Runway",
    },
    */
    {
        .type = DESCRIPTOR,
        .match = "RMK",
        .description = "Remarks",
    },
    // more data follows
    {
        .type = FORECAST,
        .match = "BECMG",
        .description = "Becoming",
    },
    {
        .type = FORECAST,
        .match = "TEMPO",
        //.description = "Significant variations in 1h",
        .description = "<1h",
    },
    {
        .type = FORECAST,
        .match = "INTER",
        //.description = "Significant variations in 30min",
        .description = "<30min",
    },

    {
        .type = TEMPERATURE,
        .match = "/",
        .description = "Temperature",
    },

    {
        .type = WEATHER,
        .match = "NOSIG",
        //.description = "No significant changes expected",
        .description = "Stable",
    },
    {
        // seen in BECMG
        .type = WEATHER,
        .match = "NSW",
        .description = "No significant weather",
    },

    {
        .type = WIND,
        .match = "KT",
        .description = "Wind",
    },
    {
        .type = WIND,
        .match = "MPS",
        .description = "Wind",
    },

    {
        .type = CLOUD,
        .match = "CAVOK",
        //.description = "Ceiling and visibility OK",
        .description = "OK",
    },

    {
        .type = CLOUD,
        .match = "SKC",
        .description = "No cloud",
    },
    {
        .type = CLOUD,
        .match = "NCD",
        .description = "Nil Cloud detected",
        // automated METAR station has not detected any cloud, either due to a lack of it, or due to an error in the sensors
    },
    {
        .type = CLOUD,
        .match = "CLR",
        .description = "Clear",
        // No clouds below 12,000 ft (3,700 m) (U.S.) or 25,000 ft (7,600 m) (Canada)",
        // , used mainly within North America and indicates a station that is at least partly automated[13][14]
    },
    {
        .type = CLOUD,
        .match = "NSC",
        .description = "No (nil) significant cloud",
        // , i.e., none below 5,000 ft (1,500 m) and no TCU or CB. Not used in North America.
    },
    {
        .type = CLOUD,
        .match = "FEW",
        .description = "Few 1-2/8",
        // = 1–2 oktas
    },
    {
        .type = CLOUD,
        .match = "SCT",
        .description = "Scattered 3-4/8",
        // = 3–4 oktas
    },
    {
        .type = CLOUD,
        .match = "BKN",
        .description = "Broken 5-7/8",
        // = 5–7 oktas
    },
    {
        .type = CLOUD,
        .match = "OVC",
        .description = "Overcast",
        // = 8 oktas, i.e., full cloud coverage
    },
    {
        .type = CLOUD,
        .match = "VV",
        .description = "Vertical Visibility",
        //= Clouds cannot be seen because of fog or heavy precipitation, so vertical visibility is given instead.
    },

    {
        .type = WEATHER,
        .match = "SH",
        .description = "Showers",
    },
    {
        .type = WEATHER,
        .match = "TS",
        .description = "Thunderstorm",
    },
    {
        .type = WEATHER,
        .match = "Thunder",
        .description = "Thunderstorm",
    },
    {
        .type = WEATHER,
        .match = "DZ",
        .description = "Drizzle",
    },
    {
        .type = WEATHER,
        .match = "RA",
        .description = "Rain",
    },
    {
        .type = WEATHER,
        .match = "SN",
        .description = "Snow",
    },
    {
        .type = WEATHER,
        .match = "SG",
        .description = "Snow Grains",
    },
    {
        .type = WEATHER,
        .match = "GS",
        .description = "Small Hail",
    },
    {
        .type = WEATHER,
        .match = "GR",
        .description = "Hail",
    },
    {
        .type = WEATHER,
        .match = "PL",
        .description = "Ice Pellets",
    },
    {
        .type = WEATHER,
        .match = "IC",
        .description = "Ice Crystals",
    },
    {
        .type = WEATHER,
        .match = "UP",
        .description = "Unknown Precipitation",
    },

    {
        .type = WEATHER,
        .match = "FG",
        .description = "Fog",
    },
    {
        .type = WEATHER,
        .match = "BR",
        .description = "Mist",
    },
    {
        .type = WEATHER,
        .match = "HZ",
        .description = "Haze",
    },
    {
        .type = WEATHER,
        .match = "VA",
        .description = "Volcanic Ash",
    },
    {
        .type = WEATHER,
        .match = "DU",
        .description = "Widespread Dust",
    },
    {
        .type = WEATHER,
        .match = "FU",
        .description = "Smoke",
    },
    {
        .type = WEATHER,
        .match = "SA",
        .description = "Sand",
    },
    {
        .type = WEATHER,
        .match = "PY",
        .description = "Spray",
    },

    {
        .type = WEATHER,
        .match = "SQ",
        .description = "Squall",
    },
    {
        .type = WEATHER,
        .match = "PO",
        .description = "Dust",
    },
    {
        .type = WEATHER,
        .match = "DS",
        .description = "Duststorm",
    },
    {
        .type = WEATHER,
        .match = "SS",
        .description = "Sandstorm",
    },
    {
        .type = WEATHER,
        .match = "FC",
        .description = "Funnel Cloud",
    },

    // after everything else
    /* parsing variable direction after first WIND
    {
        .type = WIND,
        .match = "V",
        .description = "Wind",
    },
    */
};

metar_match_t m_cloud[] = {
    {
        .type = CLOUD,
        .match = "TCU",
        .description = "Towering cumulus cloud",
        //, e.g., SCT016TCU
    },
    {
        .type = CLOUD,
        .match = "CB",
        .description = "Cumulonimbus cloud",
        // , e.g., FEW015CB
    },
};

metar_match_t m_desc[] = {
    {
        .type = DESCRIPTOR,
        .match = "-",
        .description = "Light",
    },
    {
        .type = DESCRIPTOR,
        .match = "+",
        .description = "Heavy",
    },
    {
        .type = DESCRIPTOR,
        .match = "VC",
        .description = "Vicinity",
    },
    {
        .type = DESCRIPTOR,
        .match = "RE",
        .description = "Recent",
    },
    {
        .type = DESCRIPTOR,
        .match = "MI",
        .description = "Shallow",
    },
    {
        .type = DESCRIPTOR,
        .match = "PR",
        .description = "Partial",
    },
    {
        .type = DESCRIPTOR,
        .match = "BC",
        .description = "Patches",
    },
    /*
    {
        .type = DESCRIPTOR,
        .match = "DR",
        .description = "Low drifting below eye level",
    },
    {
        .type = DESCRIPTOR,
        .match = "BL",
        .description = "Blowing at or above eye level",
    },
    */
    /*
    {
        .type = DESCRIPTOR,
        .match = "SH",
        .description = "Showers",
    },
    {
        .type = DESCRIPTOR,
        .match = "TS",
        .description = "Thunderstorm",
    },
    */
    {
        .type = DESCRIPTOR,
        .match = "FZ",
        .description = "Freezing",
    },

    {
        .type = DESCRIPTOR,
        .match = "DSNT",
        .description = "Distant",
    },
    {
        .type = DESCRIPTOR,
        .match = "CONS",
        .description = "Continuous",
    },

    // seeing TSRA, but TS is main match
    {
        .type = DESCRIPTOR, // PRECIPITATION
        .match = "RA",
        .description = "Rain",
    },
};

metar_match_t *metar_parse(char *token, match_type_t type)
{
    for (int i=0; i<COUNT_OF(m_match); i++) {
        if (type == ANY || type == m_match[i].type)
            if (strstr(token, m_match[i].match)) {
                ESP_LOGD(TAG, "%s: %s", token, m_match[i].description);
                return &m_match[i];
            }
    }
    return NULL;
}

metar_match_t *metar_parse_desc(char *token)
{
    for (int i=0; i<COUNT_OF(m_desc); i++) {
        if (strstr(token, m_desc[i].match)) {
            ESP_LOGI(TAG, "%s: %s", token, m_desc[i].description);
            return &m_desc[i];
        }
    }
    return NULL;
}
metar_match_t *metar_parse_cloud(char *token)
{
    for (int i=0; i<COUNT_OF(m_cloud); i++) {
        if (strstr(token, m_cloud[i].match)) {
            ESP_LOGI(TAG, "%s: %s", token, m_cloud[i].description);
            return &m_cloud[i];
        }
    }
    return NULL;
}

#define T0 273.15
#define E0 0.611
#define LdivRv 5423
#define Ex(x) (E0 * exp((LdivRv) * ((1.0/T0) - (1.0/(x+T0)))))
#define RH(T, Td) (100 * (Ex(Td) / Ex(T)))
// https://www.omnicalculator.com/physics/relative-humidity
// doesn't work right in C
#define Ex_(x) exp((17.625 * x) / (243.04 + x))
#define RH_(T, Td) (100.0 * (Ex_(Td) / Ex_(T)))

/*
static double rh(float T, float Td)
{
    // https://iridl.ldeo.columbia.edu/dochelp/QA/Basic/dewpoint.html
    // °C to K
    T += T0;
    Td += T0;
    double E = E0 * exp((LdivRv) * ((1/T0) - (1/Td)));
    double Es = E0 * exp((LdivRv) * ((1/T0) - (1/T)));
    return 100 * (E/Es);
}
*/

typedef enum {
    UNKNOWN = 0,
    N,
    NNE,
    NE,
    ENE,
    E,
    ESE,
    SE,
    SSE,
    S,
    SSW,
    SW,
    WSW,
    W,
    WNW,
    NW,
    NNW,
    VARIABLE,
    WIND_MAX,
} wind_dir_t;

static char wind_dirs[WIND_MAX][4] = {
    "?",
    "N",
    "NNE",
    "NE",
    "ENE",
    "E",
    "ESE",
    "SE",
    "SSE",
    "S",
    "SSW",
    "SW",
    "WSW",
    "W",
    "WNW",
    "NW",
    "NNW",
    "VAR",
};

static inline wind_dir_t wind_dir(int dir)
{
    wind_dir_t dire;
    if (dir >= 349 || dir <= 11)
        dire = N;
    else if (dir > 11 && dir < 34)
        dire = NNE;
    else if (dir >= 34 && dir <= 56)
        dire = NE;
    else if (dir > 56 && dir < 79)
        dire = ENE;
    else if (dir >= 79 && dir <= 101)
        dire = E;
    else if (dir > 101 && dir < 124)
        dire = ESE;
    else if (dir >= 124 && dir <= 146)
        dire = SE;
    else if (dir > 146 && dir < 169)
        dire = SSE;
    else if (dir >= 169 && dir <= 191)
        dire = S;
    else if (dir > 191 && dir < 214)
        dire = SSW;
    else if (dir > 214 && dir <= 236)
        dire = SW;
    else if (dir >= 236 && dir < 259)
        dire = WSW;
    else if (dir > 259 && dir <= 281)
        dire = W;
    else if (dir >= 281 && dir < 304)
        dire = WNW;
    else if (dir > 304 && dir <= 326)
        dire = NW;
    else if (dir >= 326 && dir < 349)
        dire = NNW;
    else
        dire = UNKNOWN;
    return dire;
}

#define KNOT_KPH 1.852
#define MPS_KPH 3.6
#define metar_decoded_add(self, args...) snprintf(self->decoded+strlen(self->decoded), sizeof(self->decoded)-strlen(self->decoded), args)

char *metar_decode(metar_t *self, char *buf, size_t len, metar_t *parent)
{
    assert(self != NULL);
    char *stop = buf + len;
    char *token = memmem(buf, len, self->icao, strlen(self->icao));
    if (token == NULL) {
        ESP_LOGE(TAG, "no location '%s' (%d): %s", self->icao, len, buf);
        return NULL;
    }
    char *next;

    int i = 0;
    self->report_time[0] = '\0';
    self->decoded[0] = '\0';
    if (self->shmu.last) {
        metar_decoded_add(self, " %.1f %.1f%%", self->shmu.ta_2m, roundf(self->shmu.rh), roundf(self->shmu.pr_1h));
        float pr_1h = round(self->shmu.pr_1h);
        if (pr_1h > 0)
            metar_decoded_add(self, " %.0fmm", pr_1h);
    }

    if (self->has_forecast) {
        assert(self->forecast != NULL);
        // only metar_t, buf is shared with parent (self)
        self->has_forecast = 0;
        free(self->forecast);
        self->forecast = NULL;
    }
    while (token < stop) {
        next = metar_token(token, stop);
        if (next == NULL)
            break;
        if (next - token == 0)
            break;

        if (i == 0)
            ESP_LOGI(TAG, "code(%d): '%s'", next-token, token);
        else if (i == 1) {
            ESP_LOGI(TAG, "%s time(%d): '%s'", self->icao, next-token, token);
            strncpy(self->report_time, token, next-token);
        } else {
            if (/*token[0] == 'R' ||*/ strstr(token, "//"))
                ; // runway, also RERA
            else if (token[0] == 'Q') {
                self->pressure = atoi(token+1);
                ESP_LOGW(TAG, "%s hpa: %d", self->icao, self->pressure);
            } else {
                // multiple wind - ends with mps kt
                // visibility - can be just number, CAVOK
                // snow - can start with +-
                // temperature - can start with M, has one /
                metar_match_t *m = metar_parse(token, ANY);

                if (m == NULL)
                    ESP_LOGI(TAG, "%s token(%d): '%s'", self->icao, next-token, token);
                else {
                    metar_match_t *d;
                    switch (m->type) {
                    case FORECAST:
                        ESP_LOGI(TAG, "%s Forecast: %s", self->icao, m->description);
                        metar_decoded_add(self, " %s", token);
                        if (parent != NULL)
                            break;

                        // treat rest as another metar without location
                        metar_t *forecast = calloc(1, sizeof(metar_t));
                        assert(forecast != NULL);
                        next = metar_decode(forecast, next, stop-next, self);
                        self->forecast = forecast;
                        self->has_forecast = 1;
                        // not newly allocated but parent->buf
                        forecast->buf = token;
                        break;
                    case TEMPERATURE:
                        int is_temp = 1;
                        for (char *s=token; s[0] != '\0'; s++)
                            if (!(isdigit((int)s[0]) || s[0] == 'M' || s[0] == '/')) {
                                is_temp = 0;
                            }
                        if (is_temp) {
                            char *dewp = NULL;
                            for (char *s=token; s[0] != '\0'; s++) {
                                if (s[0] == 'M') {
                                    s[0] = '-';
                                } else if (s[0] == '/') {
                                    // no need to replace /
                                    //s[0] = '\0';
                                    dewp = s+1;
                                }
                            }
                            int T = atoi(token);
                            int Td = atoi(dewp);
                            self->celsius = T;
                            self->dew = Td;
                            self->rh = RH(T, Td);
                            ESP_LOGW(TAG, "%s Temperature: %d/%d %.1f%% %.1f%%", self->icao, T, Td, RH(T, Td), RH_(T, Td));
                        }
                        break;
                    case CLOUD:
                        d = metar_parse_cloud(token);
                        if (d) {
                            ESP_LOGW(TAG, "%s Clouds: %s %s", self->icao, d->description, m->description);
                            metar_decoded_add(self, " %s %s", d->description, m->description);
                        } else {
                            ESP_LOGW(TAG, "%s Clouds: %s", self->icao, m->description);
                            metar_decoded_add(self, " %s", m->description);
                        }
                        break;
                    case WIND:
                        if (token[0] == 'P') { // >100
                            if (m->match[0] == 'M') { // MPS
                                ESP_LOGW(TAG, "%s Wind: >360 km/h %s", self->icao, token);
                                metar_decoded_add(self, " >360 km/h");
                                self->wind_speed = 360;
                            } else {
                                ESP_LOGI(TAG, "%s Wind: >100 kt %s", self->icao, token);
                                self->wind_speed = 185;
                                metar_decoded_add(self, " >185 km/h");
                            }
                        } else {
                            float gust = -1;
                            for (char *s=token; s[0] != '\0'; s++) {
                                if (s[0] == 'G') {
                                    s[0] = '\0';
                                    gust = (float) atoi(s+1);
                                    char *gust_end = strchrnul(s+1, 'K');
                                    if (gust_end[0] == '\0')
                                        gust_end = strchrnul(s+1, 'M');
                                    if (gust_end[0] == 'K') {
                                        gust *= KNOT_KPH;
                                    } else if (gust_end[0] == 'M') {
                                        gust *= MPS_KPH;
                                    } else {
                                        // unexpected
                                        ESP_LOGW(TAG, "%s Wind: unexpected gust '%s'", self->icao, s+1);
                                        gust *= -1;
                                    }
                                }
                            }
                            self->wind_gust = gust;
                            float speed = (float) atoi(token+3);
                            switch (m->match[0]) {
                            case 'M': // MPS
                                speed *= MPS_KPH;
                                break;
                            case 'K': // KT
                                speed *= KNOT_KPH;
                                break;
                            default:
                                ESP_LOGE(TAG, "%s Wind: not implemented '%s'", self->icao, token);
                                speed *= -1;
                            }
                            self->wind_speed = speed;
                            token[3] = '\0';
                            int dir = -1;
                            if (token[0] != 'V') { // VRB
                                dir = atoi(token);
                                wind_dir_t dire = wind_dir(dir);
                                ESP_LOGW(TAG, "%s Wind: %.0f km/h gust %.0f %s %d", self->icao, roundf(speed), roundf(gust), wind_dirs[dire], dir);
                                self->wind_from = wind_dirs[dire];
                            } else
                                ESP_LOGW(TAG, "%s Wind: var %.0f km/h gust %.0f (%d)", self->icao, roundf(speed), roundf(gust));
                            if (next[1+3] == 'V') {
                                // hacky but we can depend it goes after wind
                                // ' xxxVxxx' => ' xxx\0xxx\0'
                                //next[1+3] = '\0';
                                next[1+3+1+3] = '\0';
                                int from = atoi(next+1);
                                int to = atoi(next+1+3+1);
                                //next[1+3] = 'V';
                                // eat the token
                                ESP_LOGI(TAG, "%s eating %s", self->icao, next+1);
                                next = metar_token(next+1, stop);
                                wind_dir_t frome = wind_dir(from);
                                wind_dir_t toe = wind_dir(to);
                                ESP_LOGW(TAG, "%s Wind: var %s-%s %d-%d", self->icao, wind_dirs[frome], wind_dirs[toe], from, to);
                                self->wind_from = wind_dirs[frome];
                                self->wind_to = wind_dirs[toe];
                            }
                        }
                        break;
                    default:
                        d = metar_parse_desc(token);
                        // OVC => VC false positive
                        // 2/3SM => temp. false positive
                        // DSNT => SN
                        if (d) {
                            ESP_LOGW(TAG, "%s %s %s", self->icao, d->description, m->description);
                            metar_decoded_add(self, " %s %s", d->description, m->description);
                        }
                        else {
                            ESP_LOGW(TAG, "%s %s", self->icao, m->description);
                            metar_decoded_add(self, " %s", m->description);
                        }
                    }
                }
            }
        }

        // skip NUL
        token = next + 1;
        if (token[0] == '\0')
            break;
        i += 1;
    }
    time(&self->time);
    // add period in case update finished sooner than metar
    //self->last = xTaskGetTickCount() + S_TO_TICK(METAR_PERIOD_S);
    time(&self->last);
    self->last += METAR_PERIOD_S;
    return token;
}


void shmu_decode(shmu_t *self, char *buf, size_t len, metar_t *metar)
{
    char station[5+1];
    snprintf(station, sizeof(station), "%d", self->station);

    if (len < sizeof(station-1))
        return;
    char *line = memmem(buf, len, station, strlen(station));
    if (line == NULL)
        return;

    char *saveptr;
    // end of obs_stn
    strtok_r(line, ";", &saveptr);
    // cccc; name; lat; lon; elev;
    for (int i=0; i<5; i++)
        if (strtok_r(NULL, ";", &saveptr) == NULL)
            return;
    // date
    char *token = strtok_r(NULL, ";", &saveptr);
    // don't check correctness
    if (token == NULL)
        return;

    float ta_2m, rh, pr_1h;
    // ta_2m
    token = strtok_r(NULL, ";", &saveptr);
    if (token == NULL)
        return;
    if (strchrnul(token, 'n')[0] != '\0') // spaces and "null"
        return;
    ta_2m = strtof(token, NULL);

    // pa
    token = strtok_r(NULL, ";", &saveptr);
    if (token == NULL)
        return;
    //pa = strtof(token, NULL);

    // rh
    token = strtok_r(NULL, ";", &saveptr);
    if (token == NULL)
        return;
    rh = strtof(token, NULL);

    // pr_1h
    token = strtok_r(NULL, ";", &saveptr);
    if (token == NULL)
        return;
    pr_1h = strtof(token, NULL);

    //char *ws_avg = strtok_r(NULL, ";", &saveptr);
    //char *wd_avg = strtok_r(NULL, ";", &saveptr);

    self->ta_2m = ta_2m;
    //self->pa = pa;
    self->rh = rh;
    self->pr_1h = pr_1h;
    //self->last = xTaskGetTickCount();
    time(&self->last);

    if (metar) {
        metar->celsius = ta_2m;
        metar->rh = rh;
    }
    ESP_LOGI(TAG, "shmu: %.1f %.1f%% %.2fmm", self->ta_2m, rh, pr_1h);
}

void shmu_decode_cb(http_request_t *req, int success)
{
    // static buffer overflow will have success=0
    if (!success && req->buf == NULL)
        goto CLEANUP;

    if (!req->client || esp_http_client_get_status_code(req->client) != 200) {
        goto CLEANUP;
    }

    metar_t *self = req->data;
    assert(self != NULL);

    ESP_LOGI(TAG, "processing shmu: %d", self->shmu.station);
    size_t req_len = req->bufsize;

    shmu_decode(&self->shmu, req->buf, req_len, self);

CLEANUP:
    ESP_LOGI(TAG, "shmu cleanup");
    if (req->buf != NULL)
        free(req->buf);
    free(req);
}

//static char test[] = "LZIB 051853Z 04011KT 1/2SM VCTS +SN FZFG BKN003 OVC010 M02/M02 A3006 RMK AO2 TSB40 SLP176 P0002 T10171017=";
#if METAR_FTP != 0
void metar_decode_cb(ftp_request_t *req, int success)
#else
void metar_decode_cb(http_request_t *req, int success)
#endif
{
    if (!success)
        goto CLEANUP;
#if METAR_FTP == 0
    if (!req->client || esp_http_client_get_status_code(req->client) != 200) {
        goto CLEANUP;
    }
#endif
    ESP_LOGD(TAG, "processing encoded: %s", req->buf);
    size_t req_len = req->bufsize;

    metar_t *self = req->data;
    assert(self != NULL);
    //req->buf = &test; req_len = sizeof(test);
    metar_decode(self, req->buf, req_len, NULL);

    // replace nulls with spaces to get almost original data
    for (int i=0; i<req_len-1; i++)
        if (req->buf[i] == '\0')
            req->buf[i] = ' ';
    if (self->buf != NULL) {
        free(self->buf);
    }
    // replace with new buf
    self->buf = req->buf;

    // only if req->pad=1
    //ESP_LOGD(TAG, "metar %s", self->buf);
    if (self->has_forecast) {
        assert(self->forecast);
        // separate forecast part (or don't, oled is too small for 2 lines)
        //self->forecast->buf[-1] = '\0';
        ESP_LOGD(TAG, "forecast %s", self->forecast->buf);
    }

    char time[4+1+2+1+2 +1+ 2+1+2+1+2 +1] = "";
    format_time(self->time, time, sizeof(time), "%Y-%m-%d %H:%M:%S");
    ESP_LOGD(TAG, "time %s", time);

    // right now we scroll decoded+forecast, metar
    // or we can scroll forecast, decoded
    if (self->has_forecast) {
        metar_decoded_add(self, " %s", self->forecast->decoded);
    }
    oled_update.metar = self;

CLEANUP:
    ESP_LOGI(TAG, "cleanup");
    // save encoded data until next time
    //if (req->buf != NULL)
    //    free(req->buf);
    free(req);
}

void metar_task(metar_t *self)
{
    assert(self != NULL);
    /*
    http_request_t req = {
        .url = (char *) &metar_url,
        .callback = metar_cb,
    };
    http_request_t req_decode = {
        .url = (char *) &metar_url_decode,
        .callback = metar_decode_cb,
    };
    */
    char shmu_url_decode[sizeof(shmu_url)];
    char filename[sizeof(metar_file)];
    strncpy(filename, metar_file, sizeof(filename));
    memcpy(filename, self->icao, strlen(self->icao));

    // already failed to update
    //if (xTaskGetTickCount() - self->last >= S_TO_TICK(METAR_PERIOD_S))
    time_t now;
    if (time(&now) - self->last >= METAR_PERIOD_S)
        self->last_task = 0;

    while (!self->module.stop) {
        if (self->last_task != 0) {
            //vTaskDelayUntil(&self->last_task, S_TO_TICK(METAR_PERIOD_S));
            wall_clock_wait(METAR_PERIOD_S, S_TO_TICK(1));
        }
        /*
        http_request_t *req = calloc(1, sizeof(http_request_t));
        assert(req != NULL);
        req->data = self;
        req->url = self->url;
        req->callback = metar_cb;
        https_get(req);
        */

        if (self->shmu.station > 0 && ntp_synced) {
            http_request_t *req_shmu = calloc(1, sizeof(http_request_t));
            assert(req_shmu != NULL);
            req_shmu->data = self;
            memcpy(shmu_url_decode, shmu_url, sizeof(shmu_url));
            char *date = shmu_url_decode + sizeof(shmu_url) - sizeof("DD.MM.YYYY:ZZ");
            time_t t = time(NULL);
            struct tm tmp;
            gmtime_r(&t, &tmp);
            snprintf(date, sizeof("DD.MM.YYYY:ZZ"), "%02d.%02d.%04d:%02d",
                     tmp.tm_mday, tmp.tm_mon+1, tmp.tm_year+1900, tmp.tm_hour);
            ESP_LOGD(TAG, "shmu: %s", shmu_url_decode);
            req_shmu->url = shmu_url_decode;
            req_shmu->callback = shmu_decode_cb;
#ifdef SHMU_STATIC
            req_shmu->buf = malloc(SHMU_STATIC);
            if (req_shmu->buf != NULL) {
                req_shmu->bufstatic = SHMU_STATIC - 1;
                req_shmu->buf[SHMU_STATIC - 1] = '\0';
                https_get(req_shmu);
                _vTaskDelay(S_TO_TICK(1));
            } else {
                free(req_shmu);
            }
#else
            https_get(req_shmu);
            // this will help to get it rendered sooner
            _vTaskDelay(S_TO_TICK(1));
#endif
        }

        #if METAR_FTP != 0
        ftp_request_t *req_decode = calloc(1, sizeof(ftp_request_t));
        assert(req_decode != NULL);
        req_decode->data = self;
        req_decode->host = metar_host;
        req_decode->path = metar_path_decode;
        req_decode->filename = filename;
        req_decode->callback = metar_decode_cb;
        ftp_get(req_decode);
        #else
        http_request_t *req_decode = calloc(1, sizeof(http_request_t));
        assert(req_decode != NULL);
        req_decode->data = self;
        req_decode->url = self->url_decode;
        req_decode->callback = metar_decode_cb;
        https_get(req_decode);
        #endif

        //self->last_task = xTaskGetTickCount();
        time(&self->last_task);
        //_vTaskDelay(S_TO_TICK(METAR_PERIOD_S));
        //wall_clock_wait(METAR_PERIOD_S, S_TO_TICK(1));
    }

    task_t *task = self->task;
    self->module.state = 0;
    self->task = NULL;
    self->module.stop = 0;
    xvTaskDelete(task);
}

void metar_run(metar_t *self, int run)
{
    if (run && self->module.offline != 0)
        if (ping_online.connected == 0 || !ntp_synced) {
            return;
        }
    assert(self != NULL);
    if (self->task == NULL) {
        assert(self->module.state == 0);
        if (run) {
            ESP_LOGI(TAG, "starting");
            xxTaskCreate((void (*)(void*))metar_task, "metar_task", 2*1024, self, 0, &self->task);
            self->module.state = 1;
        }
    } else {
        assert(self->module.state != 0);
        if (!run && !self->module.stop) {
            ESP_LOGI(TAG, "stopping");
            self->module.stop = 0;
        }
    }
}
