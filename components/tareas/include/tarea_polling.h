#ifndef TAREA_POLLING_H
#define TAREA_POLLING_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_err.h"

#define PERIODO_MUESTREO_MS 20000 // ms entre ciclos; valor de pruebas, el nodo final medirá cada 10 min
#define LONGITUD_COLA_CICLOS 4    // ciclos que caben en cola_lecturas (~336 B cada uno); criterio propio

/**
 * @brief Configura el UART2 del RS-485 y crea cola_lecturas. Llamar desde app_main antes de crear las tareas.
 * @return ESP_OK, o el error del driver UART; ESP_ERR_NO_MEM si no se pudo crear la cola.
 */
esp_err_t tarea_polling_init(void);

/**
 * @brief Tarea que lee por Modbus cada sensor de TABLA_SENSORES y encola un ciclo por vuelta.
 *
 * Necesita que tarea_polling_init() haya terminado bien. Si la cola está llena, el ciclo se pierde con un log.
 *
 * @param parametros No se usa
 */
void tarea_polling(void *parametros);

/**
 * @brief Cola de ciclos de la que consume tarea_transmision.
 * @return El handle, o NULL si tarea_polling_init() no se ha llamado.
 */
QueueHandle_t get_cola_lecturas(void);

#endif