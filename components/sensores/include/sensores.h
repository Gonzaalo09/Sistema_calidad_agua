#ifndef SENSORES_H
#define SENSORES_H

#include <stdio.h>

#include "modbus.h"

#define MAX_SENSORES 8   // filas máximas de TABLA_SENSORES; criterio propio, dimensiona el ciclo y estado_sistema
#define MAX_VALORES  8   // magnitudes máximas por sensor; criterio propio, dimensiona el ciclo

/**
 * @brief Descriptor de un sensor Modbus: qué pedirle y cómo llamar a sus datos.
 */
typedef struct sensor_descrip {
    uint8_t slave_id;      // dirección Modbus del esclavo (datasheet del sensor, o la reprogramada)
    uint8_t reg_H, reg_L;  // dirección del primer registro: byte alto y bajo (mapa de registros del datasheet)
    uint8_t num_registros; // registros de 16 bits que se piden en una sola petición
    TipoDato tipo;         // codificación de los valores (ver modbus.h)
    uint8_t num_valores;   // magnitudes que se extraen de la respuesta (<= MAX_VALORES)
    char nombres_datos[MAX_VALORES][16]; // nombre de cada magnitud (<= 15 caracteres); puede llevar '_'
    char nombre_fuente[16]; // nombre del sensor en pantalla, CSV y trama: <= 4 caracteres, único, sin '_'

} sensor_descrip;

/** @brief Sensores del bus, uno por fila; el único sitio a tocar para añadir uno. */
extern const sensor_descrip TABLA_SENSORES[]; 
/** @brief Número de filas de TABLA_SENSORES, calculado con sizeof. */
extern const size_t NUM_SENSORES; 
#endif