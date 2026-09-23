#ifndef PANTALLA_OLED
#define PANTALLA_OLED

typedef enum {
    OLED_OK,
    OLED_BUS_NO_DISPONIBLE, //get_i2c_bus() devolvió null
    OLED_NO_RESPONDE, // probe, NACK o timeout
    OLED_ERROR_INTERNO // buffer de transmisión desbordado
} oled_estado_t;

oled_estado_t patalla_oled_init(void);

oled_estado_t pantalla_oled_estado(void); 

void pantalla_oled_texto_prueba(void); 

#endif