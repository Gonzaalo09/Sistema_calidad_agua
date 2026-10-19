/*
 * Estado del nodo: tabla en RAM por fuente (RTC, pantalla, red, SD, sensores) y registro de red, tras un mutex.
 * Módulo pasivo: no es una tarea ni toca hardware. Fuente: ESP-IDF v6.1, FreeRTOS (mutex con herencia de prioridad).
 */
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

static estado_fuente_t tabla[NUM_FUENTES];  // indexada por FUENTE_*
static registro_red_t registro_red;
static SemaphoreHandle_t mutex = NULL;      // mutex y no semáforo binario: la herencia de prioridad evita que polling espere tras una tarea media
static const uint32_t MUTEX_TIMEOUT_MS = 100; // ms; criterio propio

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
    memset(&registro_red, 0, sizeof(registro_red));

    tabla[FUENTE_RTC].nombre = "RTC";
    tabla[FUENTE_PANTALLA].nombre = "Pantalla";
    tabla[FUENTE_RED].nombre = "Red";
    tabla[FUENTE_SD].nombre = "SD";

    // Puntero, no copia: TABLA_SENSORES es const y global
    for (size_t i = 0; i < NUM_SENSORES; i++)
    {
        tabla[FUENTE_SENSOR_BASE + i].nombre = TABLA_SENSORES[i].nombre_fuente;
    }

    return ESP_OK;
}

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

void estado_sistema_publicar_red(const modem_red_t *red, int tx, bool envio_ok, uint32_t ts)
{
    if ((mutex == NULL) || (red == NULL))
    {
        ESP_LOGE(TAG, "publicar_red: llamada antes del init o con puntero nulo");
        return;
    }

    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE)
    {
        ESP_LOGW(TAG, "Mutex ocupado, no se publica la red");
        return;
    }

    registro_red.red = *red;
    registro_red.tx = tx;
    registro_red.hay_datos = true;

    if (envio_ok == true)
    {
        registro_red.ultimo_ts_200 = ts;
    }

    xSemaphoreGive(mutex);
}

bool estado_sistema_leer_red(registro_red_t *destino)
{
    if ((mutex == NULL) || (destino == NULL))
    {
        return false;
    }

    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE)
    {
        return false;
    }

    *destino = registro_red;

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

    case ESTADO_SD_AUSENTE: 
        return "NO DETECTADA";

    case ESTADO_SD_SIN_FORMATO:
        return "SIN FORMATO";
    
    case ESTADO_SD_LLENA: 
        return "LLENA"; 

    case ESTADO_SD_ERROR_ESCRITURA: 
        return "ERROR ESCRITURA";

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

    case ESTADO_SD_AUSENTE:
        return "meter o cambiar SD"; 

    case ESTADO_SD_SIN_FORMATO: 
        return "formatear FAT32 en PC"; 

    case ESTADO_SD_LLENA: 
        return "cambiar tarjeta";

    case ESTADO_SD_ERROR_ESCRITURA: 
        return "cambiar tarjeta";

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
        snprintf(buffer, n, "--:--"); // sin hora o nunca hubo un dato bueno
        return;
    }

    time_t t = (time_t)ts;
    struct tm local;
    gmtime_r(&t, &local); // gmtime_r y no localtime_r: ts ya lleva OFFSET_PAIS_S
    snprintf(buffer, n, "%02d:%02d", local.tm_hour, local.tm_min);
}
