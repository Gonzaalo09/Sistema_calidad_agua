idf_component_register(
    SRCS "source/RTC.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES i2c_bus esp_driver_i2c
)