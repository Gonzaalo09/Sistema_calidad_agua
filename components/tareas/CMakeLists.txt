idf_component_register(SRCS "source/tarea_polling.c"
                             "source/tarea_transmision.c"
                             "source/tarea_rtc.c"
                             "source/tarea_pantalla.c"
                             "source/registro_csv.c"
                        INCLUDE_DIRS "include"
                        REQUIRES modbus sensores esp_driver_uart freertos estado_sistema
                        PRIV_REQUIRES RTC pantalla_oled config_nodo tarjeta_sd)
