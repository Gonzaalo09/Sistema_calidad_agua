#ifndef SENSORES_H
#define SENSORES_H

#include <stdio.h>

#include "modbus.h"

#define MAX_SENSORES 8   // máximo de sensores: filas de TABLA_SENSORES
#define MAX_VALORES  8   // máximo de magnitudes que devuelve un sensor

typedef struct sensor_descrip {
    uint8_t slave_id;
    uint8_t reg_H, reg_L; 
    uint8_t num_registros; 
    TipoDato tipo;  
    uint8_t num_valores;
    char nombres_datos[MAX_VALORES][16]; 
    char nombre_fuente[16]; 

} sensor_descrip;

extern const sensor_descrip TABLA_SENSORES[]; 
extern const size_t NUM_SENSORES; 
#endif