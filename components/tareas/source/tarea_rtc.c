#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "RTC.h"
#include "tarea_rtc.h"

static const char *TAG = "tarea_rtc";

uint32_t rtc_timestamp_pais(void)
{
    struct tm utc;
    rtc_estado_t est = rtc_leer_hora(&utc);

    if (est != RTC_OK)
    {
        return 0;
    }

    time_t epoch_utc = mktime(&utc); // TZ no fijada -> se trata como UTC
    return (uint32_t)(epoch_utc + OFFSET_PAIS_S);
}

void tarea_rtc(void *pvParameters)
{
    while (1)
    {
        struct tm utc;
        rtc_estado_t est = rtc_leer_hora(&utc);

        if (est == RTC_OK)
        {
            float temperatura;
            rtc_estado_t est_temp = rtc_leer_temperatura(&temperatura);

            if (est_temp == RTC_OK)
            {
                ESP_LOGI(TAG, "%04d-%02d-%02d %02d:%02d:%02d UTC  %.2f°C  ts_pais=%lu",
                         utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
                         utc.tm_hour, utc.tm_min, utc.tm_sec, temperatura,
                         (unsigned long)rtc_timestamp_pais());
            }
            else
            {
                ESP_LOGW(TAG, "Error leyendo temperatura: %d", est_temp);
            }
        }
        else
        {
            ESP_LOGW(TAG, "Error leyendo hora: %d", est);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}