#ifndef TAREA_POLLING_H
#define TAREA_POLLING_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "estado_sistema.h"

#define PERIODO_MUESTREO_MS 20000 // periodo de la tarea
#define LONGITUD_COLA_CICLOS 4

#define RS485_UART UART_NUM_2 // UART del bus Modbus: la configura main.c y la usa tarea_polling.c

extern QueueHandle_t uart_queue; // Declarada aquí pero definida en main.c

typedef struct ciclo_sensor
{
    estado_codigo_t codigo;
    uint8_t num_valores;
    float valores[MAX_VALORES];
} ciclo_sensor;

typedef struct ciclo
{
    uint32_t ts;
    uint32_t seq;
    estado_codigo_t estado_rtc; // OK o SIN_HORA
    uint8_t num_sensores;
    ciclo_sensor sensor[MAX_SENSORES];
} ciclo;

void tarea_polling(void *parametros);

QueueHandle_t get_cola_lecturas(void);

#endif