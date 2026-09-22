#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"

#include "sensores.h"
#include "tarea_polling.h"

void tarea_transmision(void* parametros){
    while(1){
        lectura_sensor lectura; 
        xQueueReceive(get_cola_lecturas(), &lectura, portMAX_DELAY);

        if (lectura.valido == true){
            if (lectura.timestamp == 0)
            {
                printf("%s = %.3f @ SIN HORA (RTC no disponible)\n", lectura.nombre, lectura.valor);
            }else
            {
                time_t ts = (time_t)lectura.timestamp;
                struct tm local;
                //Combiertes el valor en segundos a UTC peruana para leerlo bien por pantalla
                gmtime_r(&ts, &local);

                printf("%s = %.3f @ %04d-%02d-%02d %02d:%02d:%02d\n",
                       lectura.nombre, lectura.valor,
                       local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                       local.tm_hour, local.tm_min, local.tm_sec);
            }
        } else {
            printf("%s: lectura fallida\n", lectura.nombre);
        }
    }
}