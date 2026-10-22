idf_component_register(SRCS "source/tiempo.c" 
                        INCLUDE_DIRS "include" 
                        PRIV_REQUIRES RTC config_nodo modem esp_timer)