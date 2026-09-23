idf_component_register(SRCS "source/pantalla_oled.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES u8g2 esp_driver_i2c esp_driver_gpio I2C_bus)