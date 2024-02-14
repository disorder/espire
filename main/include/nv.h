#ifndef __NV_H__
#define __NV_H__

#include "nvs_flash.h"
#include "esp_err.h"

extern uint32_t nv_writes;

void nv_dump(char *label, char *namespace, int values);
void nv_init();
void nv_close();
esp_err_t nv_commit();
esp_err_t nv_remove(char *key);
esp_err_t nv_erase();

esp_err_t nv_read_u8(char *key, uint8_t *value);
esp_err_t nv_read_i8(char *key, int8_t *value);
esp_err_t nv_read_u16(char *key, uint16_t *value);
esp_err_t nv_read_i16(char *key, int16_t *value);
esp_err_t nv_read_u32(char *key, uint32_t *value);
esp_err_t nv_read_i32(char *key, int32_t *value);
esp_err_t nv_read_u64(char *key, uint64_t *value);
esp_err_t nv_read_i64(char *key, int64_t *value);
esp_err_t nv_read_str(char *key, char **value, size_t *len);
esp_err_t nv_read_blob(char *key, void **value, size_t *len);
esp_err_t nv_read_str_size(char *key, size_t *size);
esp_err_t nv_read_blob_size(char *key, size_t *size);

esp_err_t nv_write_u8(char *key, uint8_t value);
esp_err_t nv_write_i8(char *key, int8_t value);
esp_err_t nv_write_u16(char *key, uint16_t value);
esp_err_t nv_write_i16(char *key, int16_t value);
esp_err_t nv_write_u32(char *key, uint32_t value);
esp_err_t nv_write_i32(char *key, int32_t value);
esp_err_t nv_write_u64(char *key, uint64_t value);
esp_err_t nv_write_i64(char *key, int64_t value);

esp_err_t nv_write_str(char *key, char *value);
esp_err_t nv_write_blob(char *key, void *value, size_t len, int is_str);


typedef union
{
    uint8_t u8;
    int8_t i8;
    uint16_t u16;
    int16_t i16;
    uint32_t u32;
    int32_t i32;
    uint64_t u64;
    int64_t i64;
    char *str;
    void *blob;
} nv_value_t;

typedef struct
{
    nvs_type_t type;
    size_t size;
    nv_value_t value;
} nv_data_t;

// tries to read all types
esp_err_t nv_read_any(char *key, nv_data_t **ptr);
void nv_data_free(nv_data_t *data);

#endif /* __NV_H__ */
