idf_component_register(SRCS "source/registro_csv.c"
                       INCLUDE_DIRS "include"
                       REQUIRES sensores estado_sistema
                       PRIV_REQUIRES config_nodo)
