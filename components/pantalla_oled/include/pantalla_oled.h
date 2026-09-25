#ifndef PANTALLA_OLED_H
#define PANTALLA_OLED_H

#define PANTALLA_LINEAS 8 // con la fuente 5x8
#define PANTALLA_COLUMNAS 25

/**
 * Estado de la pantalla, para distinguir entre fallo del bus, pantalla que no responde
 * y error interno del driver. La traducción a mensajes para pantalla y trama va en la aplicación.
 */
typedef enum
{
    OLED_OK,
    OLED_BUS_NO_DISPONIBLE, // get_i2c_bus() devolvió null
    OLED_NO_RESPONDE,       // probe, NACK o timeout
    OLED_ERROR_INTERNO      // buffer de transmisión desbordado
} oled_estado_t;

oled_estado_t pantalla_oled_init(void);

oled_estado_t pantalla_oled_estado(void);

void pantalla_oled_texto_prueba(void);

void pantalla_oled_limpiar(void);

void pantalla_oled_texto(uint8_t linea, const char *texto); // linea 0..7

void pantalla_oled_mostrar(void);

#endif