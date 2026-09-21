#ifndef TAREA_POLLING
#define TAREA_POLLING

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#define PERIODO_MUESTREO_MS 4000

extern QueueHandle_t uart_queue; //definida aquí pero declarada en main.c  

void tarea_polling(void* parametros);

QueueHandle_t get_cola_lecturas(void); 

#endif