set(COMPONENT_ADD_INCLUDEDIRS include)
set(COMPONENT_SRCS "thermistor.c")
set(COMPONENT_REQUIRES esp_adc driver)
register_component()

