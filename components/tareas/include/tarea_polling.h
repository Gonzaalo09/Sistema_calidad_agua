#ifndef TAREA_POLLING_H
#define TAREA_POLLING_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "estado_sistema.h"

#define PERIODO_MUESTREO_MS 20000 // ms entre ciclos; valor de pruebas, el nodo final medirá cada 10 min
#define LONGITUD_COLA_CICLOS 4    // ciclos que caben en cola_lecturas (~336 B cada uno); criterio propio

#define RS485_UART UART_NUM_2 // UART del bus Modbus: la configura main.c y la usa tarea_polling.c

extern QueueHandle_t uart_queue; // Cola de eventos de la UART de RS485_UART; declarada aquí, definida en main.c

/** @brief Resultado de un sensor en un ciclo. */
typedef struct ciclo_sensor
{
    estado_codigo_t codigo;       // OK o el código de fallo de ese sensor
    uint8_t num_valores;          // posiciones válidas de valores[]; 0 si el sensor falló
    float valores[MAX_VALORES];   // magnitudes, en el orden de nombres_datos de TABLA_SENSORES
} ciclo_sensor;

/** @brief Una vuelta de tarea_polling: hora, número de ciclo y todos los sensores. Es el elemento de cola_lecturas. */
typedef struct ciclo
{
    uint32_t ts;                // segundos, UTC + OFFSET_PAIS_S; 0 si el RTC falló. Marca el inicio del ciclo
    uint32_t seq;               // de config_nodo_siguiente_seq; 0 = sin número
    estado_codigo_t estado_rtc; // OK o SIN_HORA
    uint8_t num_sensores;       // NUM_SENSORES en este ciclo
    ciclo_sensor sensor[MAX_SENSORES]; // la posición i corresponde a TABLA_SENSORES[i]
} ciclo;

/**
 * @brief Tarea que lee por Modbus cada sensor de TABLA_SENSORES y encola un ciclo por vuelta.
 *
 * Crea cola_lecturas al arrancar. Si la cola está llena, el ciclo se pierde con un log.
 *
 * @param parametros No se usa
 */
void tarea_polling(void *parametros);

/**
 * @brief Cola de ciclos de la que consume tarea_transmision.
 * @return El handle, o NULL si tarea_polling aún no ha arrancado.
 */
QueueHandle_t get_cola_lecturas(void);

#endif