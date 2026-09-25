#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "sensores.h"

// Aquí es donde tienes que meter los nuevos sensores si quieres medir uno nuevo.
// No hace falta tocar nada más: NUM_SENSORES se calcula solo al final del archivo.
//
// Antes de añadir un sensor, verifica en SU datasheet:
//   - la dirección Modbus (slave_id), y si viene de fábrica en 1, reprográmala, de momento, luego quiero poner una función que lo haga solo.
//   - los registros: cuál es el primero y cuántos son contiguos
//   - el tipo de dato y, si es float, su orden de bytes (ver cabecera de modbus.c)
const sensor_descrip TABLA_SENSORES[] = {
    // NPK sensor JXBS-3001-NPK-RS
    {.slave_id = 1, .reg_H = 0x00, .reg_L = 0x1E, .num_registros = 3, .tipo = entero16, .num_valores = 3, .nombres_datos = {"Nitrogeno", "Fosforo", "Potasio"}, .nombre_fuente = "NPK"},

    // OD sensor
    {.slave_id = 3, .reg_H = 0x00, .reg_L = 0x01, .num_registros = 4, .tipo = float32_CDAB, .num_valores = 2, .nombres_datos = {"OD_medida", "temperatura"}, .nombre_fuente = "OD"},

    // Ph Sensor
    {.slave_id = 5, .reg_H = 0x00, .reg_L = 0x01, .num_registros = 4, .tipo = float32_CDAB, .num_valores = 2, .nombres_datos = {"pH", "temperatura"}, .nombre_fuente = "pH"},
};

// El compilador cuenta las entradas: no hay que tocar nada al añadir un sensor
const size_t NUM_SENSORES = sizeof(TABLA_SENSORES) / sizeof(TABLA_SENSORES[0]);

_Static_assert(sizeof(TABLA_SENSORES) / sizeof(TABLA_SENSORES[0]) <= MAX_SENSORES,
               "Hay mas sensores en TABLA_SENSORES que MAX_SENSORES: sube MAX_SENSORES en sensores.h");