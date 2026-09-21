#include <string.h>    


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
            printf("%s = %.3f @ %lu\n", lectura.nombre, lectura.valor, lectura.timestamp);
        } else {
            printf("%s: lectura fallida\n", lectura.nombre);
        }
    }
}