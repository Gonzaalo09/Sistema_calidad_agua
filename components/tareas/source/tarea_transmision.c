#include <stdio.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#include "sensores.h"
#include "tarea_polling.h"
#include "estado_sistema.h"
#include "config_nodo.h"
#include "tarjeta_sd.h"
#include "registro_csv.h"

#define MAX_ARCHIVOS_DIA 9 // _2 .. _9 si cambian los sensores el mismo dia

static const char *TAG = "transmision";

/**
 * @brief Traduccion del estado del driver de la SD al codigo de estado_sistema.
 */
static estado_codigo_t sd_a_estado(sd_estado_t est)
{
    switch (est)
    {
    case SD_EST_OK:
        return ESTADO_OK;

    case SD_EST_SIN_FORMATO:
        return ESTADO_SD_SIN_FORMATO;

    case SD_EST_LLENA:
        return ESTADO_SD_LLENA;

    case SD_EST_ERROR_ESCRITURA:
        return ESTADO_SD_ERROR_ESCRITURA;

    case SD_EST_AUSENTE:
    case SD_EST_ERROR_BUS: // solo con pines mal configurados; no hay codigo mejor
    default:
        return ESTADO_SD_AUSENTE;
    }
}

/**
 * @brief Imprime el ciclo en la consola con los nombres de columna.
 */
static void imprimir_ciclo(const ciclo *lectura)
{
    if (lectura->ts == 0)
    {
        printf("--- %s seq %lu @ SIN HORA (RTC no disponible) ---\n",
               config_nodo_id(), (unsigned long)lectura->seq);
    }
    else
    {
        time_t t = (time_t)lectura->ts;
        struct tm local;
        char fecha[20];

        // ts ya lleva sumado OFFSET_PAIS_S, asi que gmtime_r da la hora del pais
        gmtime_r(&t, &local);
        strftime(fecha, sizeof(fecha), "%Y-%m-%d %H:%M:%S", &local);
        printf("--- %s seq %lu @ %s ---\n", config_nodo_id(), (unsigned long)lectura->seq, fecha);
    }

    for (uint8_t i = 0; i < lectura->num_sensores; i++)
    {
        const sensor_descrip *d = &TABLA_SENSORES[i];
        const ciclo_sensor *s = &lectura->sensor[i];

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
}

/**
 * @brief Escribe el ciclo en el archivo del dia. Si el archivo del dia ya
 *        existe con otra cabecera (han cambiado los sensores), usa _2, _3...
 */
static sd_estado_t guardar_en_sd(const ciclo *lectura, const char *cabecera, int tx)
{
    // static: fuera de la pila de la tarea
    static char linea[TAM_LINEA_CSV];
    static char base[TAM_RUTA_CSV];
    static char ruta[TAM_RUTA_CSV + 16]; // Tiene que caber lo que se añade de base más el peor de los casos(+16) _<número>.csv
    static char primera[TAM_CABECERA_CSV];

    if (registro_csv_linea(lectura, tx, linea, sizeof(linea)) == 0)
    {
        // No es un fallo de la tarjeta: queda en el log y como hueco en seq
        ESP_LOGE(TAG, "Linea CSV demasiado larga: ciclo %lu no guardado", (unsigned long)lectura->seq);
        return SD_EST_OK;
    }

    registro_csv_ruta(lectura->ts, base, sizeof(base));

    bool elegida = false;

    for (unsigned k = 1; k <= MAX_ARCHIVOS_DIA; k++)
    {
        if (k == 1)
        {
            snprintf(ruta, sizeof(ruta), "%s.csv", base);
        }
        else
        {
            snprintf(ruta, sizeof(ruta), "%s_%u.csv", base, k);
        }

        bool existe = false;
        sd_estado_t est = tarjeta_sd_primera_linea(ruta, primera, sizeof(primera), &existe);

        if (est != SD_EST_OK)
        {
            return est;
        }

        if ((existe == false) || (strcmp(primera, cabecera) == 0))
        {
            elegida = true;
            break;
        }
    }

    if (elegida == false)
    {
        ESP_LOGE(TAG, "Ninguno de los %d archivos del dia tiene la cabecera actual", MAX_ARCHIVOS_DIA);
        return SD_EST_ERROR_ESCRITURA;
    }

    sd_estado_t est = tarjeta_sd_escribir_linea(ruta, cabecera, linea);

    if (est == SD_EST_OK)
    {
        ESP_LOGI(TAG, "Ciclo %lu guardado en %s", (unsigned long)lectura->seq, ruta);
    }

    return est;
}

/**
 * @brief Publica el estado de la SD en estado_sistema. Con OK, valores[0] es el % libre.
 */
static void publicar_sd(sd_estado_t est, uint32_t ts)
{
    if (est != SD_EST_OK)
    {
        estado_sistema_publicar(FUENTE_SD, NULL, 0, 0, sd_a_estado(est));
        return;
    }

    uint8_t pct = 0;

    if (tarjeta_sd_espacio(&pct) == SD_EST_OK)
    {
        float valor = (float)pct;
        estado_sistema_publicar(FUENTE_SD, &valor, 1, ts, ESTADO_OK);
    }
    else
    {
        estado_sistema_publicar(FUENTE_SD, NULL, 0, ts, ESTADO_OK);
    }
}

void tarea_transmision(void *parametros)
{
    // Tamaño del buf que se pasa a anadir()
    static char cabecera[TAM_CABECERA_CSV];
    ciclo lectura;

    // TABLA_SENSORES es const: la cabecera se genera una sola vez
    if (registro_csv_cabecera(cabecera, sizeof(cabecera)) == 0)
    {
        ESP_LOGE(TAG, "Cabecera CSV demasiado larga: no se guardara nada en la SD");
    }

    while (1)
    {
        xQueueReceive(get_cola_lecturas(), &lectura, portMAX_DELAY);

        imprimir_ciclo(&lectura);

        int tx = TX_NO_INTENTADO; // fase 5: aqui se transmite y tx toma el resultado

        if (cabecera[0] != '\0')
        {
            sd_estado_t est = guardar_en_sd(&lectura, cabecera, tx);
            publicar_sd(est, lectura.ts);
        }
        //  Para mirar el tamaño de la pila que queda libre
        ESP_LOGI(TAG, "pila libre minima: %u B", (unsigned)uxTaskGetStackHighWaterMark(NULL));
    }
}