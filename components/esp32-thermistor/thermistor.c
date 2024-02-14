/*
 * MIT License
 *
 * Copyright (c) 2021 Juan Schiavoni
 * Copyright (c) 2022 Adam Sloboda
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * @file thermistor.c
 * @brief Driver implementation of thermistor component for ESP32 (IDF 5.x).
 */

#include "thermistor.h"

#include "driver/gpio.h"
#include "esp_adc/adc_cali_scheme.h"
#include "adc_cali_schemes.h"
#include "esp_adc/adc_oneshot.h"
#include "hal/efuse_ll.h"
#include "esp_err.h"
#include "math.h"

#include "esp_log.h"
static const char* TAG = "drv_thr";

#define DEFAULT_VREF    1100        // Use adc2_vref_to_gpio() to obtain a better estimate
#define NO_OF_SAMPLES   64          // Amount suggested by espresif for multiple samples.

static adc_cali_handle_t cali_unit1 = NULL;
static adc_cali_handle_t cali_unit2 = NULL;
static const adc_bitwidth_t width = SOC_ADC_RTC_MAX_BITWIDTH;
static const adc_atten_t atten = ADC_ATTEN_DB_11;
static adc_cali_line_fitting_efuse_val_t cali_val;
static adc_oneshot_unit_handle_t unit1 = NULL;
static adc_oneshot_unit_handle_t unit2 = NULL;
adc_oneshot_unit_init_cfg_t init_config1 = {
    .unit_id = ADC_UNIT_1,
    .ulp_mode = false,
};
adc_oneshot_unit_init_cfg_t init_config2 = {
    .unit_id = ADC_UNIT_2,
    .ulp_mode = false,
};
adc_oneshot_chan_cfg_t config = {
    .bitwidth = SOC_ADC_RTC_MAX_BITWIDTH,
};


/**
 * @brief Log the characterization used to linearize the ADC.
 * @param[in] val_type Type of calibration value used in characterization.
 * @return void
 */
static void print_char_val_type(adc_cali_line_fitting_efuse_val_t val_type)
{
    if (val_type == ADC_CALI_LINE_FITTING_EFUSE_VAL_EFUSE_TP) {
        ESP_LOGI(TAG, "Characterized using Two Point Value");
    } else if (val_type == ADC_CALI_LINE_FITTING_EFUSE_VAL_EFUSE_VREF) {
        ESP_LOGI(TAG, "Characterized using eFuse Vref");
    } else {
        ESP_LOGI(TAG, "Characterized using Default Vref");
    }
}

/**
 * @brief Checks if ADC calibration values are burned into eFuse.
 * @param[in] void .
 * @return void
 */
static void check_efuse(void)
{
    adc_cali_scheme_line_fitting_check_efuse(&cali_val);

    // Check if TP is burned into eFuse
    if (adc_cali_scheme_line_fitting_check_efuse(&cali_val) == ESP_OK) {
        ESP_LOGI(TAG, "eFuse Two Point: Supported");
    } else {
        ESP_LOGI(TAG, "Cannot retrieve eFuse Two Point calibration values. Default calibration values will be used.");
    }

#if CONFIG_IDF_TARGET_ESP32
    // Check Vref is burned into eFuse
    if (cali_val != ADC_CALI_LINE_FITTING_EFUSE_VAL_DEFAULT_VREF) {
        ESP_LOGI(TAG, "eFuse Vref: Supported");
    } else {
        ESP_LOGI(TAG, "eFuse Vref: NOT supported");
    }
#endif

#if !CONFIG_IDF_TARGET_ESP32 && !CONFIG_IDF_TARGET_ESP32S2 && !CONFIG_IDF_TARGET_ESP32C3
    #error "For now only support: ESP32/ESP32S2/ESP32C3."
#endif
}

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
// taken from IDF 5.x sources, used only for logging purposes
#define VREF_FORMAT             0
#define VREF_MASK                       0x1F
#define VREF_STEP_SIZE                  7
#define VREF_OFFSET                     1100

static inline int decode_bits(uint32_t bits, uint32_t mask, bool is_twos_compl)
{
    int ret;
    if (bits & (~(mask >> 1) & mask)) {      //Check sign bit (MSB of mask)
        //Negative
        if (is_twos_compl) {
            ret = -(((~bits) + 1) & (mask >> 1));   //2's complement
        } else {
            ret = -(bits & (mask >> 1));    //Sign-magnitude
        }
    } else {
        //Positive
        ret = bits & (mask >> 1);
    }
    return ret;
}

static uint32_t read_efuse_vref(void)
{
    //eFuse stores deviation from ideal reference voltage
    uint32_t ret = VREF_OFFSET;       //Ideal vref
    uint32_t bits = efuse_ll_get_adc_vref();
    ret += decode_bits(bits, VREF_MASK, VREF_FORMAT) * VREF_STEP_SIZE;
    return ret;     //ADC Vref in mV
}
#endif

bool adc_calibration_init(adc_unit_t unit, adc_atten_t atten, adc_bitwidth_t bitwidth, uint32_t default_vref, adc_cali_handle_t *out_handle)
{
    esp_err_t ret = ESP_FAIL;
    adc_cali_handle_t handle = NULL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED

    if (!calibrated) {
        ESP_LOGI(TAG, "calibration scheme version is %s (%d bit)", "Curve Fitting", bitwidth);
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = bitwidth,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }

#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED

if (!calibrated) {
    ESP_LOGI(TAG, "calibration scheme version is %s (%d bit)", "Line Fitting", bitwidth);
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = bitwidth,
            .default_vref = default_vref,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        print_char_val_type(cali_val);
        uint32_t vref = cali_config.default_vref;
        if (cali_val != ADC_CALI_LINE_FITTING_EFUSE_VAL_DEFAULT_VREF) {
            vref = read_efuse_vref();
        }
        ESP_LOGI(TAG, "Vref: %dmV", vref);

        if (ret == ESP_OK) {
            calibrated = true;
        } else if (ret == ESP_ERR_NOT_SUPPORTED) {
            ESP_LOGW(TAG, "calibration fail due to lack of eFuse bits");
        } else {
            assert(false);
        }
    }

#endif

    *out_handle = handle;

    return calibrated;
}

esp_err_t thermistor_init(thermistor_handle_t* th, int gpio, int adc_unit,
                          uint8_t channel, float serial_resistance,
                          float nominal_resistance, float nominal_temperature,
                          float beta_val, float vsource)
{
    assert(adc_unit == ADC_UNIT_1 || adc_unit == ADC_UNIT_2);
    adc_oneshot_unit_handle_t unit = NULL;
    if (adc_unit == ADC_UNIT_1) {
        if (unit1 == NULL) {
            check_efuse();
            ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &unit1));
            assert(adc_calibration_init(adc_unit, atten, width, DEFAULT_VREF, &cali_unit1));
        }
        unit = unit1;
    } else {
        if (unit2 == NULL) {
            check_efuse();
            ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config2, &unit2));
            assert(adc_calibration_init(adc_unit, atten, width, DEFAULT_VREF, &cali_unit2));
        }
        unit = unit2;
    }

    {
        config.atten = atten;
        ESP_ERROR_CHECK(adc_oneshot_config_channel(unit, th->channel, &config));

        th->gpio = gpio;
        th->adc_unit = adc_unit;
        th->channel = channel;
        th->serial_resistance = serial_resistance;
        th->nominal_resistance = nominal_resistance;
        th->nominal_temperature = nominal_temperature;
        th->beta_val = beta_val;
        th->vsource = vsource;
        th->t_resistance = 0;
    }

    return ESP_OK;
}

float thermistor_vout_to_celsius(thermistor_handle_t* th, uint32_t vout)
{
    float steinhart;

    // Rt = R1 * Vout / (Vs - Vout);
    th->t_resistance =  (th->serial_resistance * vout) / (th->vsource - vout);

    steinhart = th->t_resistance / th->nominal_resistance;  // (R/Ro)
    steinhart = log(steinhart);                             // ln(R/Ro)
    steinhart /= th->beta_val;                              // 1/B * ln(R/Ro)
    steinhart += 1.0 / (th->nominal_temperature + 273.15);  // + (1/To)
    steinhart = 1.0 / steinhart;                            // Invert
    steinhart -= 273.15;                                    // convert to C

    return steinhart;
}

uint32_t thermistor_read_vout(thermistor_handle_t* th)
{
    uint32_t adc_reading = 0;
    int value;
    adc_oneshot_unit_handle_t unit = (th->adc_unit == ADC_UNIT_1)? unit1 : unit2;
    adc_cali_handle_t cali = (th->adc_unit == ADC_UNIT_1)? cali_unit1 : cali_unit2;

    // Use multiple samples to stabilize the measured value.
    for (int i = 0; i < NO_OF_SAMPLES; i++) {
        if (adc_oneshot_read(unit, th->channel, &value) == ESP_OK)
            adc_reading += value;
        else // again
            i -= 1;
    }
    adc_reading /= NO_OF_SAMPLES;

    int voltage;
    ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali, adc_reading, &voltage));
    return voltage;
}

float thermistor_get_celsius(thermistor_handle_t* th)
{
    th->vout = thermistor_read_vout(th);
    return thermistor_vout_to_celsius(th, th->vout);
}

float thermistor_celsius_to_fahrenheit(float temp)
{
    return (temp * 1.8) + 32;
}
