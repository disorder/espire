#include "config.h"
#include "nv.h"
#include <string.h>

#include "esp_netif.h"
#include "esp_err.h"
#include "esp_log.h"
static const char *TAG = "nv";

static nvs_handle_t handle = {0};
uint32_t nv_writes = 0;

void nv_init()
{
    ESP_LOGW(TAG, "init namespace %s", NVS_NAMESPACE);
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle));
}

void nv_close()
{
    nvs_close(handle);
}

esp_err_t nv_commit()
{
    ESP_LOGI(TAG, "commit");
    return nvs_commit(handle);
}

esp_err_t nv_remove(char *key)
{
    ESP_LOGI(TAG, "remove '%s'", key);
    return nvs_erase_key(handle, key);
}

esp_err_t nv_erase()
{
    ESP_LOGI(TAG, "erase");
    return nvs_erase_all(handle);
}

#define GETTER(TYPE, FN) \
    esp_err_t nv_read_ ## FN(char *key, TYPE *value)                    \
    {                                                                   \
        ESP_LOGI(TAG, "%s namespace %s read %s %s", NVS_LABEL, NVS_NAMESPACE, #FN, key); \
        return nvs_get_ ## FN(handle, key, value);                      \
    }

GETTER(uint8_t, u8);
GETTER(uint16_t, u16);
GETTER(uint32_t, u32);
GETTER(uint64_t, u64);
GETTER(int8_t, i8);
GETTER(int16_t, i16);
GETTER(int32_t, i32);
GETTER(int64_t, i64);

esp_err_t nv_read_str_size(char *key, size_t *size)
{
    assert(size != NULL);
    return nvs_get_str(handle, key, NULL, size);
}

esp_err_t nv_read_blob_size(char *key, size_t *size)
{
    assert(size != NULL);
    return nvs_get_blob(handle, key, NULL, size);
}

// if len=0 it will be allocated
esp_err_t nv_read_str(char *key, char **value, size_t *len)
{
    assert(value != NULL);
    assert(len != NULL);
    char *data = *value;
    if (*len == 0) {
        esp_err_t res = nvs_get_str(handle, key, NULL, len);
        if (res != ESP_OK)
            return res;
        data = malloc((*len)+1);
        data[*len] = '\0';
        *value = data;
    }
    assert(data != NULL);
    ESP_LOGI(TAG, "%s namespace %s read %s(%d) %s", NVS_LABEL, NVS_NAMESPACE, "str", *len, key);
    return nvs_get_str(handle, key, data, len);
}

// if len=0 it will be allocated
esp_err_t nv_read_blob(char *key, void **value, size_t *len)
{
    assert(value != NULL);
    assert(len != NULL);
    char *data = *value;
    if (*len == 0) {
        esp_err_t res = nvs_get_blob(handle, key, NULL, len);
        if (res != ESP_OK)
            return res;
        data = malloc((*len)+1);
        data[*len] = '\0';
        *value = data;
    }
    assert(data != NULL);
    ESP_LOGI(TAG, "%s namespace %s read %s(%d) %s", NVS_LABEL, NVS_NAMESPACE, "blob", *len, key);
    return nvs_get_blob(handle, key, data, len);
}

#define SETTER(TYPE, FN)                                                \
    esp_err_t nv_write_ ## FN(char *key, TYPE value)                    \
    {                                                                   \
        TYPE stored;                                                    \
        nvs_get_ ## FN(handle, key, &stored);                           \
        if (stored != value) {                                          \
            nv_writes += 1;                                                \
            ESP_LOGE(TAG, "%s namespace %s write %s %s", NVS_LABEL, NVS_NAMESPACE, #FN, key); \
            return nvs_set_ ## FN(handle, key, value);                  \
        }                                                               \
        return ESP_OK;                                                  \
    }

SETTER(uint8_t, u8);
SETTER(uint16_t, u16);
SETTER(uint32_t, u32);
SETTER(uint64_t, u64);
SETTER(int8_t, i8);
SETTER(int16_t, i16);
SETTER(int32_t, i32);
SETTER(int64_t, i64);

esp_err_t nv_write_str(char *key, char *value)
{
    size_t size = -1;
    esp_err_t res = nvs_get_str(handle, key, NULL, &size);
    if (res == ESP_ERR_NVS_NOT_FOUND)
        goto WRITE;

    // prevent write of the same value
    int cmp = 1;
    // reported size includes \0
    if (size == strlen(value)+1) {
        char *nv_value = malloc(size);
        assert(nv_value != NULL);
        nv_value[0] = '\0';
        nvs_get_str(handle, key, nv_value, &size);
        if (nv_value != NULL) {
            cmp = strcmp(nv_value, value);
            free(nv_value);
        }
    }

    if (cmp == 0) {
        ESP_LOGI(TAG, "%s namespace %s skip %s(%d) %s", NVS_LABEL, NVS_NAMESPACE, "str", strlen(value), key);
        return ESP_OK;
    }

WRITE:
    // error level to track flash wear
    nv_writes += 1;
    ESP_LOGE(TAG, "%s namespace %s write %s(%d) %s", NVS_LABEL, NVS_NAMESPACE, "str", strlen(value), key);
    return nvs_set_str(handle, key, value);
}

// len can be unused when is_str=1
esp_err_t nv_write_blob(char *key, void *value, size_t len, int is_str)
{
    if (is_str) // blob stored without \0
        len = strlen(value);

    size_t size = -1;
    esp_err_t res = nvs_get_blob(handle, key, NULL, &size);
    if (res == ESP_ERR_NVS_NOT_FOUND)
        goto WRITE;

    // prevent write of the same value
    int cmp = size+len;
    if (cmp != 0) {
        char *nv_value = malloc(size);
        assert(nv_value != NULL);
        nvs_get_blob(handle, key, nv_value, &size);
        if (nv_value != NULL) {
            cmp = memcmp(nv_value, value, size);
            free(nv_value);
        }
    }

    if (cmp == 0) {
        ESP_LOGI(TAG, "%s namespace %s skip %s(%d) %s", NVS_LABEL, NVS_NAMESPACE, "str", strlen(value), key);
        return ESP_OK;
    }

WRITE:
    // error level to track flash wear
    nv_writes += 1;
    ESP_LOGE(TAG, "%s namespace %s write %s(%d) %s", NVS_LABEL, NVS_NAMESPACE, "blob", len, key); \
    return nvs_set_blob(handle, key, value, len);
}

void nv_data_free(nv_data_t *data)
{
    assert(data != NULL);
    if (data->type == NVS_TYPE_BLOB) {
        free(data->value.blob);
        data->value.blob = NULL;
    }
    else if (data->type == NVS_TYPE_BLOB) {
        free(data->value.str);
        data->value.str = NULL;
    }
    data->type = NVS_TYPE_ANY;
}

esp_err_t nv_read_any(char *key, nv_data_t **ptr)
{
    assert(ptr != NULL);
    nv_data_t *data = *ptr;
    if (data == NULL)
        data = *ptr = calloc(1, sizeof(nv_data_t));
    assert(data != NULL);
    // automatically free if already used
    nv_data_free(data);
    esp_err_t res;

    res = nv_read_blob(key, &data->value.blob, &data->size);
    if (res == ESP_OK) {
        data->type = NVS_TYPE_BLOB;
        return res;
    }

    res = nv_read_str(key, &data->value.str, &data->size);
    if (res == ESP_OK) {
        data->type = NVS_TYPE_STR;
        return res;
    }

#define READ_VALUE(FN, TYPE) {                          \
        res = nv_read_## FN(key, &data->value.FN);      \
        if (res == ESP_OK) {                            \
            data->type = NVS_TYPE_ ## TYPE;             \
            return res;                                 \
        }                                               \
    }
    READ_VALUE(u8, U8);
    READ_VALUE(i8, I8);
    READ_VALUE(u16, U16);
    READ_VALUE(i16, I16);
    READ_VALUE(u32, U32);
    READ_VALUE(i32, I32);
    READ_VALUE(u64, U64);
    READ_VALUE(i64, I64);

    return ESP_FAIL;
}

void nv_dump(char *label, char *namespace, int values)
{
    nvs_iterator_t iter = NULL;
    esp_err_t res = nvs_entry_find(label, namespace, NVS_TYPE_ANY, &iter);
    while (res == ESP_OK) {
        nvs_entry_info_t info;
        nvs_entry_info(iter, &info);

#define NVS_ITEM_INFO(TYPE, i) case TYPE: ESP_LOGW(TAG, "ns %-15s key %-15s type=%d=%s", (i).namespace_name, (i).key, (i).type, #TYPE); break;
        switch (info.type) {
            NVS_ITEM_INFO(NVS_TYPE_I8, info);
            NVS_ITEM_INFO(NVS_TYPE_U8, info);
            NVS_ITEM_INFO(NVS_TYPE_U16, info);
            NVS_ITEM_INFO(NVS_TYPE_I16, info);
            NVS_ITEM_INFO(NVS_TYPE_U32, info);
            NVS_ITEM_INFO(NVS_TYPE_I32, info);
            NVS_ITEM_INFO(NVS_TYPE_U64, info);
            NVS_ITEM_INFO(NVS_TYPE_I64, info);
            NVS_ITEM_INFO(NVS_TYPE_STR, info);
            NVS_ITEM_INFO(NVS_TYPE_BLOB, info);
            NVS_ITEM_INFO(NVS_TYPE_ANY, info);
        }
        if (!values) {
            res = nvs_entry_next(&iter);
            continue;
        }

        nvs_handle_t h;
//            ESP_LOGE(TAG, "%s", #TYPE);
#define READ_AND_LOG(TYPE, FN, INFO, log) {       \
            TYPE value_ ## FN;                                           \
            ESP_ERROR_CHECK(nvs_open(INFO.namespace_name, NVS_READONLY, &h));            \
            ESP_ERROR_CHECK(res = nvs_get_ ## FN(h, INFO.key, & value_ ## FN)); \
            nvs_close(h);                                               \
            if (log)                                                    \
                ESP_LOGW(TAG, "%" PRI ## FN, value_ ## FN);                       \
        }
        switch (info.type) {
        case NVS_TYPE_U8:
            //uint8_t x;
            //ESP_ERROR_CHECK(nvs_open(info.namespace_name, NVS_READONLY, &h));
            //ESP_ERROR_CHECK(res = nvs_get_u8(h, info.key, &x));
            //nvs_close(h);
            //ESP_LOGE(TAG, "%d", x);

            READ_AND_LOG(uint8_t, u8, info, 1);
            break;
        case NVS_TYPE_I8:
            READ_AND_LOG(int8_t, i8, info, 1);
            break;
        case NVS_TYPE_U16:
            READ_AND_LOG(uint16_t, u16, info, 1);
            break;
        case NVS_TYPE_I16:
            READ_AND_LOG(int16_t, i16, info, 1);
            break;
        case NVS_TYPE_U32:
            READ_AND_LOG(uint32_t, u32, info, 1);
            break;
        case NVS_TYPE_I32:
            READ_AND_LOG(int32_t, i32, info, 1);
            break;
        case NVS_TYPE_U64:
            READ_AND_LOG(uint64_t, u64, info, 1);
            break;
        case NVS_TYPE_I64:
            READ_AND_LOG(int64_t, i64, info, 1);
            break;
        case NVS_TYPE_STR:
            size_t value_str_size;
            ESP_ERROR_CHECK(nvs_open(info.namespace_name, NVS_READONLY, &h));
            res = nvs_get_str(h, info.key, NULL, &value_str_size);
            char *value_str = malloc(value_str_size + 1);
            assert(value_str != NULL);
            value_str[value_str_size] = '\0';
            res = nvs_get_str(h, info.key, value_str, &value_str_size);
            nvs_close(h);
            ESP_LOGW(TAG, "str(%d) %s", value_str_size, value_str);
            free(value_str);
            break;
        case NVS_TYPE_BLOB:
            size_t value_blob_size;
            ESP_ERROR_CHECK(nvs_open(info.namespace_name, NVS_READONLY, &h));
            res = nvs_get_blob(h, info.key, NULL, &value_blob_size);
            nvs_close(h);
            ESP_LOGW(TAG, "blob(%d) ignored", value_blob_size);
            break;
        case NVS_TYPE_ANY:
            ESP_LOGW(TAG, "found ANY type");
            break;
        }

        res = nvs_entry_next(&iter);
    }
    nvs_release_iterator(iter);
}
