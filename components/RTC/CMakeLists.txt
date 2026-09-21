idf_component_register(
    SRCS "rtc_ds3231.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES I2C_bus esp_driver_i2c
)