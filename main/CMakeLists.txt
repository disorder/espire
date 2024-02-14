set(COMPONENT_SRCDIRS ".")
set(COMPONENT_ADD_INCLUDEDIRS include)
list(APPEND COMPONENT_REQUIRES esp32-thermistor)
list(APPEND COMPONENT_REQUIRES ftplib)
list(APPEND COMPONENT_REQUIRES driver nvs_flash esp_eth esp-tls esp_http_server esp_https_server esp_http_client esp_wifi)
list(APPEND COMPONENT_REQUIRES esp_lcd esp_timer lvgl)
list(APPEND COMPONENT_REQUIRES json)
list(APPEND COMPONENT_REQUIRES app_update esp_https_ota)
list(APPEND COMPONENT_ADD_INCLUDEDIRS $ENV{IDF_PATH}/examples/common_components/protocol_examples_common/include)

file(GLOB_RECURSE sources "*.c")
idf_component_register(SRCS ${sources} INCLUDE_DIRS "include"
    EMBED_TXTFILES "../certs/httpd.pem" "../certs/httpd.key"
)

#register_component()
