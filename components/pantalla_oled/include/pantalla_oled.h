#ifndef PANTALLA_OLED_H
#define PANTALLA_OLED_H

#include <stdint.h>

#define PANTALLA_LINEAS 8     // líneas de texto: 64 px / 8 px de la fuente u8g2_font_5x8_tf
#define PANTALLA_COLUMNAS 25  // caracteres por línea: 128 px / 5 px de la fuente u8g2_font_5x8_tf (25,6 -> 25)

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

/**
 * @brief Registra el SSD1306 (0x3C) en el bus I2C, comprueba que responde y lo arranca con u8g2.
 *
 * Llamar tras i2c_bus_init y antes de crear tareas. Tarda ~300 ms (reset de u8g2).
 *
 * @return OLED_OK, OLED_BUS_NO_DISPONIBLE (sin bus), OLED_NO_RESPONDE o OLED_ERROR_INTERNO.
 */
oled_estado_t pantalla_oled_init(void);

/** @brief Estado que dejó la última operación (init, o el último frame dibujado). */
oled_estado_t pantalla_oled_estado(void);

/** @brief Pantalla de prueba de bring-up (regla de 25 columnas y acentos); no hace nada si el estado no es OLED_OK. */
void pantalla_oled_texto_prueba(void);

/**
 * @brief Empieza un frame: pone el estado a OLED_OK y borra el buffer. Sin pantalla registrada, no hace nada.
 *
 * Cada frame se juzga por sí mismo: un fallo puntual del bus no deja la pantalla muerta hasta reiniciar.
 */
void pantalla_oled_limpiar(void);

/**
 * @brief Escribe una línea de texto UTF-8 en el buffer, sin enviarla. `linea` 0..7; fuera de rango, se ignora.
 *        No hace nada si el frame ya ha fallado.
 */
void pantalla_oled_texto(uint8_t linea, const char *texto);

/** @brief Envía el buffer a la pantalla. No hace nada si el frame ya ha fallado. */
void pantalla_oled_mostrar(void);

#endif