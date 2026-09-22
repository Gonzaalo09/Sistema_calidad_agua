#include <stdio.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"

#include "modbus.h"
#include "tarea_polling.h"
#include "tarea_transmision.h"
#include "i2c_bus.h"
#include "RTC.h"
#include "tarea_rtc.h"

static const char *TAG = "main";

const int uart_buffer_size = 256;
QueueHandle_t uart_queue;

// Poner a 0 tras la primera escritura correcta del RTC.
#define FORZAR_HORA 1

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
    //mktime pasa de structm a numero 
    time_t epoch_local = mktime(&t_local);
    time_t epoch_utc = epoch_local - OFFSET_PAIS_S;

    struct tm utc;
    //gmtime_r toma el número ts(segundos desde 1970) y lo descompone en año,mes,día...
    gmtime_r(&epoch_utc, &utc);
    return utc;
}
// ============ FIN ANDAMIAJE TEMPORAL ============

void app_main(void)
{
    uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };

    ESP_ERROR_CHECK(uart_param_config(UART_NUM_2, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_2, 17, 16, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_2, uart_buffer_size, uart_buffer_size, 10, &uart_queue, 0));

    esp_err_t err_bus = i2c_bus_init();
    if (err_bus != ESP_OK)
    {
        ESP_LOGE(TAG, "Fallo creando el bus I2C, RTC y pantalla no disponibles");
    }
    else
    {
        i2c_bus_scan();

        esp_err_t err_rtc = rtc_init();
        if (err_rtc != ESP_OK)
        {
            ESP_LOGW(TAG, "RTC no disponible al arrancar");
        }

        rtc_estado_t est_osc = rtc_comprobar_oscilador();
        if (FORZAR_HORA || est_osc == RTC_OSCILADOR_PARADO)
        {
            struct tm utc = hora_compilacion_a_utc();
            ESP_LOGW(TAG, "Fijando hora PROVISIONAL (compilación)");
            rtc_fijar_hora(&utc);
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

    xTaskCreate(tarea_polling, "polling", 4096, NULL, 5, NULL);
    xTaskCreate(tarea_transmision, "transmision", 4096, NULL, 3, NULL);
    xTaskCreate(tarea_rtc, "rtc_debug", 4096, NULL, 2, NULL);
}