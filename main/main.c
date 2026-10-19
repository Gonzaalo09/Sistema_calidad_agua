/*
 * Arranque del nodo: inicializa UART2 (RS-485), estado, NVS, I2C, RTC, OLED, SD y módem, y crea las tareas.
 * Fuentes: ESP-IDF v6.1, "UART"; DS3231 datasheet (Analog Devices), registro de estado 0Fh (OSF).
 */
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
#include "pantalla_oled.h"
#include "estado_sistema.h"
#include "tarea_pantalla.h"
#include "config_nodo.h"
#include "tarjeta_sd.h"
#include "modem.h"

static const char *TAG = "main";

const int uart_buffer_size = 256; // bytes de los buffers RX y TX de la UART2; criterio propio (una respuesta Modbus cabe de sobra)
QueueHandle_t uart_queue;         // eventos de la UART2; la consume tarea_polling

// Pines de UART2 hacia el modulo RS485-TTL, vistos desde el ESP32.
// Con TX=18/RX=19 los tres sensores daban timeout (30/09/2026): el cableado real cruza al revés.
#define RS485_PIN_TX 19 // GPIO de TX; sale del ESP32 hacia el modulo
#define RS485_PIN_RX 18 // GPIO de RX; entra al ESP32 desde el modulo
#define RS485_BAUDIOS 9600        // baudios (8N1); tiene que coincidir con todos los sensores del bus (datasheets)
#define RS485_TAM_COLA_EVENTOS 10 // eventos de la UART pendientes (uart_queue); criterio propio

// 1 = en cada arranque el RTC se pone a la hora de compilación (andamiaje). Desplegar siempre con 0.
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
    uart_config_t uart_config = {
        .baud_rate = RS485_BAUDIOS,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };

    ESP_ERROR_CHECK(uart_param_config(RS485_UART, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(RS485_UART, RS485_PIN_TX, RS485_PIN_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(RS485_UART, uart_buffer_size, uart_buffer_size, RS485_TAM_COLA_EVENTOS, &uart_queue, 0));

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

        // OSF=1: el oscilador se paró y la hora no es fiable. Solo se comprueba aquí, al arrancar.
        rtc_estado_t est_osc = rtc_comprobar_oscilador();
        if (FORZAR_HORA || est_osc == RTC_OSCILADOR_PARADO)
        {
            struct tm utc = hora_compilacion_a_utc();
            ESP_LOGW(TAG, "Fijando hora PROVISIONAL (compilación)");
            rtc_fijar_hora(&utc);
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