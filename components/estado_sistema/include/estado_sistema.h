#ifndef ESTADO_SISTEMA_H
#define ESTADO_SISTEMA_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>


#include "esp_err.h"
#include "sensores.h"

enum 
{
    FUENTE_RTC = 0,
    FUENTE_PANTALLA = 1, 
    FUENTE_RED = 2, //Red mobil 
    FUENTE_SENSOR_BASE = 3, 
    NUM_FUENTES = FUENTE_SENSOR_BASE + MAX_SENSORES // 11
};

// Vocabulario de estado, es el codigo interno
typedef enum 
{
    ESTADO_SIN_DATOS = 0, // Vale 0 para que una tabla puesta a 0 no se lea como ESTADO_OK
    ESTADO_OK,  
    ESTADO_SIN_RESPUESTA, // Timeout: no contesta nada 
    ESTADO_TRAMA_INVALIDA, // contesto, CRC o longitud mal
    ESTADO_SIN_HORA // El RTC no responde
} estado_codigo_t; 

//Una fila de la tabla
typedef struct estado_fuente_t {
    const char *nombre; // Apunta a TABLA_SENSORES o a un literal
    float valores[MAX_VALORES]; // Ultimos valores buenos
    uint8_t num_valores; // Cuantos de los 8 son validos, puedes tener hasta 8 valores en un mismo sensor, tienes que saber cuantos poner
    estado_codigo_t codigo; 
    uint32_t ultimo_ts_ok; // Hora en Peru en segundos, 0 = nunca. Luego ya se decide como mostrar después
    uint16_t fallos_consecutivos; 
}estado_fuente_t;

esp_err_t estado_sistema_init(void);

void estado_sistema_publicar(size_t fuente, const float *valores, uint8_t num_valores, uint32_t ts, estado_codigo_t codigo); 

bool estado_sistema_leer(size_t fuente, estado_fuente_t *destino);

const char *estado_sistema_texto(estado_codigo_t codigo); 

const char *estado_sistema_accion(estado_codigo_t codigo); 

void estado_sistema_hora_legible(uint32_t ts, char *buffer, size_t n); 

#endif