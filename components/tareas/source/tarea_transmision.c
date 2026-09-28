#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "esp_log.h"

#include "sensores.h"
#include "tarea_polling.h"
#include "estado_sistema.h"
#include "config_nodo.h"
#include "tarjeta_sd.h"

static const char *TAG = "transmision";

void tarea_transmision(void *parametros)
{
    ciclo lectura;

    while (1)
    {
        xQueueReceive(get_cola_lecturas(), &lectura, portMAX_DELAY);

        // Cabecera del ciclo
        if (lectura.ts == 0)
        {
            printf("--- %s seq %lu @ SIN HORA (RTC no disponible) ---\n", config_nodo_id(), (unsigned long)lectura.seq);
        }
        else
        {
            time_t t = (time_t)lectura.ts;
            struct tm local;
            char fecha[20];
            // Combiertes el valor en segundos a UTC peruana para leerlo bien por pantalla
            // ts ya lleva sumado OFFSET_PAIS_S, asi que gmtime_r da hora de Peru
            gmtime_r(&t, &local);
            strftime(fecha, sizeof(fecha), "%Y-%m-%d %H:%M:%S", &local);
            printf("--- %s seq %lu @ %s ---\n", config_nodo_id(), (unsigned long)lectura.seq, fecha);
        }

        for (uint8_t i = 0; i < lectura.num_sensores; i++)
        {
            const sensor_descrip *d = &TABLA_SENSORES[i];
            const ciclo_sensor *s = &lectura.sensor[i];

            if (s->codigo == ESTADO_OK)
            {
                for (uint8_t j = 0; j < s->num_valores; j++)
                {
                    printf("%s_%s = %.3f\n", d->nombre_fuente, d->nombres_datos[j], s->valores[j]);
                }
            }
            else
            {
                printf("%s: %s\n", d->nombre_fuente, estado_sistema_texto(s->codigo));
            }
        }
        // vTaskDelay(pdMS_TO_TICKS(120000)); // PRUEBA cola llena: quitar si no se quiere hacer
        //  Para mirar el tamaño de la pila que queda libre
        ESP_LOGI(TAG, "pila libre minima: %u B", (unsigned)uxTaskGetStackHighWaterMark(NULL));
        tarjeta_sd_prueba_escritura(); // PROVISIONAL paso 4
    }
}
