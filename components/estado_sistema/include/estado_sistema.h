#ifndef ESTADO_SISTEMA_H
#define ESTADO_SISTEMA_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>


#include "esp_err.h"
#include "sensores.h"
#include "modem.h"

/** @brief Índices de la tabla de estado; son internos y nunca salen del firmware. */
enum 
{
    FUENTE_RTC = 0,
    FUENTE_PANTALLA = 1, 
    FUENTE_RED = 2, // red móvil
    FUENTE_SD = 3,
    FUENTE_SENSOR_BASE = 4, // los sensores van detrás: FUENTE_SENSOR_BASE + i es TABLA_SENSORES[i]
    NUM_FUENTES = FUENTE_SENSOR_BASE + MAX_SENSORES // filas de la tabla (12)
};

/**
 * @brief Código de estado interno. Viaja en la SD y en la trama: solo se añaden valores al final, nunca se reordenan ni se reutilizan.
 */
typedef enum 
{
    ESTADO_SIN_DATOS = 0, // Vale 0 para que una tabla puesta a 0 no se lea como ESTADO_OK
    ESTADO_OK,  
    ESTADO_SIN_RESPUESTA, // Timeout: no contesta nada
    ESTADO_TRAMA_INVALIDA, // contesto, CRC o longitud mal
    ESTADO_SIN_HORA, // no hay hora fiable: el RTC no responde, o se puso una hora provisional al arrancar
    ESTADO_SD_AUSENTE, // La SD no responde (no está o mal contacto)
    ESTADO_SD_SIN_FORMATO, // responde, pero no se puede montar (sin formato, exFAT o dañada)
    ESTADO_SD_LLENA, // SD está llena
    ESTADO_SD_ERROR_ESCRITURA // Estaba montada y falla al escribir
} estado_codigo_t; 

/** @brief Una fila de la tabla: estado de una fuente (RTC, pantalla, red, SD o una petición Modbus). */
typedef struct estado_fuente_t {
    const char *nombre; // Apunta a TABLA_SENSORES o a un literal
    float valores[MAX_VALORES]; // Ultimos valores buenos (no se borran al fallar)
    uint8_t num_valores; // posiciones válidas de valores[] (hasta MAX_VALORES)
    estado_codigo_t codigo; // estado de la última publicación
    uint32_t ultimo_ts_ok; // segundos, UTC + OFFSET_PAIS_S, de la última publicación OK; 0 = nunca
    uint16_t fallos_consecutivos; // satura en UINT16_MAX; vuelve a 0 con un OK
}estado_fuente_t;

/** @brief Registro de red: lleva textos, que las filas (solo floats) no pueden guardar. */
typedef struct registro_red_t {
    bool hay_datos;          // false hasta el primer estado_sistema_publicar_red
    modem_red_t red;         // operador, tecnologia, dBm e IP del ultimo ciclo
    int tx;                  // codigo HTTP o TX_* del ultimo envio
    uint32_t ultimo_ts_200;  // ts del ultimo envio con 200; 0 = nunca
} registro_red_t;

/**
 * @brief Crea el mutex y deja la tabla a cero con los nombres de cada fuente. Llamar en app_main antes de crear tareas.
 * @return ESP_OK (también si ya estaba inicializado) o ESP_ERR_NO_MEM si no se pudo crear el mutex.
 */
esp_err_t estado_sistema_init(void);

/**
 * @brief Publica el estado de una fuente. No hace nada, con un log, si falta el init, la fuente no existe o el mutex está ocupado más de 100 ms.
 *
 * `valores` y `num_valores` solo se aplican si `valores` no es NULL y `num_valores` > 0: al fallar se pasa NULL y 0
 * para conservar los anteriores. `ts` solo se guarda con `codigo` == ESTADO_OK.
 */
void estado_sistema_publicar(size_t fuente, const float *valores, uint8_t num_valores, uint32_t ts, estado_codigo_t codigo); 

/**
 * @brief Copia la fila de una fuente, para trabajar con ella sin bloquear a nadie.
 * @return true si se copió; false si falta el init, `destino` es NULL, la fuente no existe o el mutex está ocupado.
 */
bool estado_sistema_leer(size_t fuente, estado_fuente_t *destino);

/**
 * @brief Publica el estado de la red del ultimo ciclo.
 *
 * @param envio_ok true si tx es el 200 del servidor: solo entonces se actualiza ultimo_ts_200 con ts.
 */
void estado_sistema_publicar_red(const modem_red_t *red, int tx, bool envio_ok, uint32_t ts);

/**
 * @brief Copia el registro de red. Devuelve false si no se puede leer (antes del init o mutex ocupado).
 */
bool estado_sistema_leer_red(registro_red_t *destino);

/** @brief Texto del estado para la pantalla (p. ej. "NO CONTESTA"); "?" si el código no existe. */
const char *estado_sistema_texto(estado_codigo_t codigo); 

/**
 * @brief Acción para quien está en el sitio (p. ej. "revisar cable/12V"), de 23 caracteres como máximo.
 * @return El texto, o "" si el estado no tiene acción.
 */
const char *estado_sistema_accion(estado_codigo_t codigo); 

/**
 * @brief Escribe un timestamp como "hh:mm" (hora del país, sin conversión); "--:--" si ts es 0.
 * @param n Tamaño de `buffer`; con `buffer` NULL o n = 0 no hace nada.
 */
void estado_sistema_hora_legible(uint32_t ts, char *buffer, size_t n); 

#endif