/*
 * Tabla de sensores del bus RS-485. Fuentes: datasheets JXCT JXBS-3001-NPK-RS y REMOND (OD, pH RMD-ISHP105),
 * mapas de registros y reprogramación de dirección.
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "sensores.h"

// Único sitio a tocar para añadir un sensor: NUM_SENSORES se calcula al final del archivo.
//
// Antes de añadir un sensor, verificar en SU datasheet:
//   - la dirección Modbus (slave_id); si viene de fábrica en 1 hay que reprogramarla a mano (el firmware aún no escribe registros)
//   - los registros: cuál es el primero y cuántos son contiguos
//   - el tipo de dato y, si es float, su orden de bytes (ver decodificar_datos en modbus.c)
const sensor_descrip TABLA_SENSORES[] = {
    // NPK sensor JXBS-3001-NPK-RS
    //{.slave_id = 1, .reg_H = 0x00, .reg_L = 0x1E, .num_registros = 3, .tipo = entero16, .num_valores = 3, .nombres_datos = {"Nitrogeno", "Fosforo", "Potasio"}, .nombre_fuente = "NPK"},

    // OD sensor
    //{.slave_id = 3, .reg_H = 0x00, .reg_L = 0x01, .num_registros = 4, .tipo = float32_CDAB, .num_valores = 2, .nombres_datos = {"OD_medida", "temperatura"}, .nombre_fuente = "OD"},

    // pH Sensor
    {.slave_id = 5, .reg_H = 0x00, .reg_L = 0x01, .num_registros = 4, .tipo = float32_CDAB, .num_valores = 2, .nombres_datos = {"pH", "temperatura"}, .nombre_fuente = "pH"},   
};

// El compilador cuenta las entradas: no hay que tocar nada al añadir un sensor
const size_t NUM_SENSORES = sizeof(TABLA_SENSORES) / sizeof(TABLA_SENSORES[0]);

_Static_assert(sizeof(TABLA_SENSORES) / sizeof(TABLA_SENSORES[0]) <= MAX_SENSORES,
               "Hay mas sensores en TABLA_SENSORES que MAX_SENSORES: sube MAX_SENSORES en sensores.h");