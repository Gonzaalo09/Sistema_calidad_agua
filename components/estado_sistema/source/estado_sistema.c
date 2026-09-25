#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#include "sensores.h"
#include "estado_sistema.h"

static const char *TAG = "estado_sistema";

static estado_fuente_t tabla[NUM_FUENTES];
static SemaphoreHandle_t mutex = NULL;
static const uint32_t MUTEX_TIMEOUT_MS = 100;

esp_err_t estado_sistema_init(void)
{
    if (mutex != NULL)
    {
        return ESP_OK;
    }

    mutex = xSemaphoreCreateMutex();

    if (mutex == NULL)
    {
        ESP_LOGE(TAG, "No se ha podido crear el mutex correctamente");
        return ESP_ERR_NO_MEM;
    }

    memset(tabla, 0, sizeof(tabla));

    tabla[FUENTE_RTC].nombre = "RTC";
    tabla[FUENTE_PANTALLA].nombre = "Pantalla";
    tabla[FUENTE_RED].nombre = "Red";

    for (size_t i = 0; i < NUM_SENSORES; i++)
    {
        tabla[FUENTE_SENSOR_BASE + i].nombre = TABLA_SENSORES[i].nombre_fuente;
    }

    return ESP_OK;
}

/**
 * Primero toma el mutex si puede, si puede copia los valores nuevos si hay y meter todos los nuevos valores como de ts y codigo
 * Si pasan NULL y 0 como valores, se mantienen los antiguos
 */
void estado_sistema_publicar(size_t fuente, const float *valores, uint8_t num_valores, uint32_t ts, estado_codigo_t codigo)
{
    if (mutex == NULL)
    {
        ESP_LOGE(TAG, "Publicar llamada antes que estado_sistema_init");
        return;
    }

    if (fuente >= NUM_FUENTES)
    {
        ESP_LOGE(TAG, "Fuente fuera de rango: %zu", fuente);
        return;
    }

    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE)
    {
        ESP_LOGW(TAG, "Mutex ocupado, no se publica la fuente %zu", fuente);
        return;
    }

    estado_fuente_t *entrada = &tabla[fuente];

    // Los valores solo se tocan si llegan nuevos. Al fallar se pasa NULL, y se conservan los anteriores.
    if ((valores != NULL) && (num_valores > 0))
    {
        uint8_t n = (num_valores > MAX_VALORES) ? MAX_VALORES : num_valores;
        memcpy(entrada->valores, valores, n * sizeof(float));
        entrada->num_valores = n;
    }

    entrada->codigo = codigo;

    if (codigo == ESTADO_OK)
    {
        entrada->ultimo_ts_ok = ts;
        entrada->fallos_consecutivos = 0;
    }
    else
    {
        if (entrada->fallos_consecutivos < UINT16_MAX)
        {
            entrada->fallos_consecutivos++;
        }
    }

    xSemaphoreGive(mutex);
}

/**
 * Se copian todos los campos de la fuente, incluido el array, quien los lee puede trabajar sin bloquear a nadie
 */
bool estado_sistema_leer(size_t fuente, estado_fuente_t *destino)
{
    if ((mutex == NULL) || (destino == NULL))
    {
        return false;
    }

    if (fuente >= NUM_FUENTES)
    {
        return false;
    }

    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE)
    {
        return false;
    }

    *destino = tabla[fuente];

    xSemaphoreGive(mutex);

    return true;
}

const char *estado_sistema_texto(estado_codigo_t codigo)
{
    switch (codigo)
    {
    case ESTADO_OK:
        return "OK";

    case ESTADO_SIN_RESPUESTA:
        return "NO CONTESTA";

    case ESTADO_TRAMA_INVALIDA:
        return "CONTESTA MAL";

    case ESTADO_SIN_HORA:
        return "SIN HORA";

    case ESTADO_SIN_DATOS:
        return "SIN DATOS";

    default:
        return "?";
    }
}

const char *estado_sistema_accion(estado_codigo_t codigo)
{
    switch (codigo)
    {
    case ESTADO_SIN_RESPUESTA:
        return "revisar cable/12V";

    case ESTADO_TRAMA_INVALIDA:
        return "avisar al tecnico";

    case ESTADO_SIN_HORA:
        return "avisar al tecnico";

    default:
        return "";
    }
}

void estado_sistema_hora_legible(uint32_t ts, char *buffer, size_t n)
{
    if ((buffer == NULL) || (n == 0))
    {
        return;
    }

    if (ts == 0)
    {
        snprintf(buffer, n, "--:--"); // Nunca ha funcionado
        return;
    }

    time_t t = (time_t)ts;
    struct tm local;
    gmtime_r(&t, &local);
    snprintf(buffer, n, "%02d:%02d", local.tm_hour, local.tm_min);
}
