/*
 * Arranque del nodo: inicializa UART2 (RS-485), estado, NVS, I2C, RTC, OLED, SD y módem, y crea las tareas.
 * Fuentes: ESP-IDF v6.1, "UART"; DS3231 datasheet (Analog Devices), registro de estado 0Fh (OSF).
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"

#include "modbus.h"
#include "tarea_polling.h"
#include "tarea_transmision.h"
#include "i2c_bus.h"
#include "RTC.h"
#include "tarea_rtc.h"
#include "pantalla_oled.h"
#include "estado_sistema.h"
#include "tarea_pantalla.h"
#include "config_nodo.h"
#include "tarjeta_sd.h"
#include "modem.h"

static const char *TAG = "main";

// 1 = en cada arranque el RTC se pone a la hora de compilación (andamiaje). Desplegar siempre con 0.
#define FORZAR_HORA 0
 
static struct tm hora_compilacion_a_utc(void)
{
    static const char *meses[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    char mes_str[4];
    int dia, anio, hora, min, seg;

    sscanf(__DATE__, "%3s %d %d", mes_str, &dia, &anio);
    sscanf(__TIME__, "%d:%d:%d", &hora, &min, &seg);

    int mes = 1;
    // Conviertes de string que es como te lo da a numero
    for (int i = 0; i < 12; i++)
    {
        if (strcmp(mes_str, meses[i]) == 0)
        {
            mes = i + 1;
            break;
        }
    }

    struct tm t_local = {0};
    t_local.tm_year = anio - 1900;
    t_local.tm_mon = mes - 1;
    t_local.tm_mday = dia;
    t_local.tm_hour = hora;
    t_local.tm_min = min;
    t_local.tm_sec = seg;

    // TZ no está fijada -> mktime interpreta t_local como si ya fuera UTC.
    // t_local es hora LOCAL (la del PC al compilar), así que restamos
    // OFFSET_PAIS_S (definida en tarea_rtc.h) para obtener UTC real.
    // mktime pasa de structm a numero
    time_t epoch_local = mktime(&t_local);
    time_t epoch_utc = epoch_local - OFFSET_PAIS_S;

    struct tm utc;
    // gmtime_r toma el número ts(segundos desde 1970) y lo descompone en año,mes,día...
    gmtime_r(&epoch_utc, &utc);
    return utc;
}
// ============ FIN ANDAMIAJE TEMPORAL ============

void app_main(void)
{
    ESP_ERROR_CHECK(tarea_polling_init());

    esp_err_t err_sistema = estado_sistema_init();
    if (err_sistema != ESP_OK)
    {
        ESP_LOGE(TAG, "Fallo creando el estado del sistema");
    }

    cfg_nodo_estado_t est_cfg = config_nodo_init();

    if (est_cfg != CFG_NODO_OK)
    {
        ESP_LOGW(TAG, "Configuracion del nodo incompleta (%d)", est_cfg);
    }

    esp_err_t err_bus = i2c_bus_init();
    if (err_bus != ESP_OK)
    {
        ESP_LOGE(TAG, "Fallo creando el bus I2C, RTC y pantalla no disponibles");
        estado_sistema_publicar(FUENTE_RTC, NULL, 0, 0, ESTADO_SIN_RESPUESTA);
        estado_sistema_publicar(FUENTE_PANTALLA, NULL, 0, 0, ESTADO_SIN_RESPUESTA);
    }
    else
    {
        i2c_bus_scan();

        esp_err_t err_rtc = rtc_ds3231_init();
        if (err_rtc != ESP_OK)
        {
            ESP_LOGW(TAG, "RTC no disponible al arrancar");
            estado_sistema_publicar(FUENTE_RTC, NULL, 0, 0, ESTADO_SIN_RESPUESTA);
        }
        else
        {
            // Diagnóstico de campo: se lee ANTES de cualquier escritura en el RTC (datasheet: 0Eh bit 7 = EOSC, 0Fh bit 7 = OSF)
            struct tm utc_arranque;
            char fecha[20];
            uint8_t control;
            uint8_t estado;

            if (rtc_leer_hora(&utc_arranque) == RTC_OK)
            {
                strftime(fecha, sizeof(fecha), "%Y-%m-%d %H:%M:%S", &utc_arranque);
            }
            else
            {
                snprintf(fecha, sizeof(fecha), "ilegible");
            }

            if (rtc_leer_control_estado(&control, &estado) == RTC_OK)
            {
                ESP_LOGI(TAG, "RTC al arrancar: %s UTC, 0Eh=0x%02X (EOSC=%d), 0Fh=0x%02X (OSF=%d)",
                         fecha, control, (control >> 7) & 1, estado, (estado >> 7) & 1);
            }
            else
            {
                ESP_LOGW(TAG, "RTC al arrancar: %s UTC, registros 0Eh y 0Fh ilegibles", fecha);
            }
        }

        // OSF=1: el oscilador se paró y la hora no es fiable. Solo se comprueba aquí, al arrancar.
        rtc_estado_t est_osc = rtc_comprobar_oscilador();
        if (FORZAR_HORA)
        {
            struct tm utc = hora_compilacion_a_utc();
            ESP_LOGW(TAG, "Fijando hora PROVISIONAL (compilación)");
            rtc_estado_t est_fijar = rtc_fijar_hora(&utc);
            if (est_fijar != RTC_OK)
            {
                ESP_LOGE(TAG, "rtc_fijar_hora ha fallado (código %d)", est_fijar);
            }
            estado_sistema_publicar(FUENTE_RTC, NULL, 0, 0, ESTADO_SIN_HORA);
        }
        else if (est_osc == RTC_OSCILADOR_PARADO)
        {
            ESP_LOGW(TAG, "OSF=1: hora no fiable, no se escribe");
            estado_sistema_publicar(FUENTE_RTC, NULL, 0, 0, ESTADO_SIN_HORA);
        }
        else if (est_osc == RTC_OK)
        {
            ESP_LOGI(TAG, "OSF=0: se conserva la hora del RTC");
        }
        else
        {
            ESP_LOGW(TAG, "No se pudo comprobar el oscilador (código %d)", est_osc);
        }
    }
    oled_estado_t est_oled = pantalla_oled_init();
    if (est_oled != OLED_OK)
    {
        ESP_LOGW(TAG, "Pantalla no disponible (%d)", est_oled);
        estado_sistema_publicar(FUENTE_PANTALLA, NULL, 0, 0, ESTADO_SIN_RESPUESTA);
    }
    else
    {
        estado_sistema_publicar(FUENTE_PANTALLA, NULL, 0, 0, ESTADO_OK);
        // pantalla_oled_texto_prueba();
    }

    sd_estado_t est_sd = tarjeta_sd_init();

    if (est_sd == SD_EST_OK)
    {
        tarjeta_sd_info();
    }
    else
    {
        ESP_LOGW(TAG, "SD no disponible al arrancar (%d)", est_sd);
    }

    
    if (modem_init() != MODEM_OK)
    {
        ESP_LOGE(TAG, "Fallo configurando el UART1 o el PWRKEY del modem");
    }

    // Prioridades: el muestreo (5) no debe esperar al módem (3); RTC y pantalla (2) son de apoyo.
    // Pila libre mínima medida con 6144 B en transmisión: ~3300 B (AC3D04). Criterio: subir si baja de 1 KB.
    xTaskCreate(tarea_polling, "polling", 4096, NULL, 5, NULL);
    xTaskCreate(tarea_transmision, "transmision", 6144, NULL, 3, NULL);
    xTaskCreate(tarea_rtc, "rtc_debug", 4096, NULL, 2, NULL);
    xTaskCreate(tarea_pantalla, "pantalla", 4096, NULL, 2, NULL);
}