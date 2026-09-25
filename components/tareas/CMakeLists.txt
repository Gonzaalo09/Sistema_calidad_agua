idf_component_register(SRCS "source/tarea_polling.c"
                             "source/tarea_transmision.c"
                             "source/tarea_rtc.c"
                             "source/tarea_pantalla.c"
                        INCLUDE_DIRS "include"
                        REQUIRES modbus sensores esp_driver_uart freertos
                        PRIV_REQUIRES RTC estado_sistema pantalla_oled)
