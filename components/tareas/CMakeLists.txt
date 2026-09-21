idf_component_register(SRCS "source/tarea_polling.c"
                             "source/tarea_transmision.c"
                        INCLUDE_DIRS "include"
                        REQUIRES modbus sensores esp_driver_uart freertos)
