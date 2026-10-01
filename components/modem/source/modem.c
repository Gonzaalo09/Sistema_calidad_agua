#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "modem.h"

#define MODEM_TROZO_LECTURA_MS 20 // latencia por lectura; el plazo total sigue siendo timeout_ms

static const char *TAG = "modem";

// Comando para apagar el modem
static const char CMD_APAGAR[] = "AT+CPOWD=1\r";

/**
 * @brief Envía un comando AT al módem y espera su respuesta final.
 *
 * Vacía el UART, envía el comando más "\r" y acumula lo recibido hasta
 * encontrar "OK", un error del módem o agotar el tiempo. El eco del
 * comando queda dentro de la respuesta.
 *
 * @param cmd        Comando sin terminador (p. ej. "AT+CGMR").
 * @param resp_out   Buffer donde se guarda la respuesta, siempre terminada en '\0'.
 * @param tam_resp   Tamaño de resp_out en bytes (incluye el '\0').
 * @param timeout_ms Tiempo máximo total de espera, en milisegundos.
 * @param log_fallo  true: registra error y timeout en el log; false: silencioso.
 *
 * @return MODEM_OK       Llegó "\r\nOK\r\n".
 * @return MODEM_ERROR    Llegó ERROR, +CME ERROR o +CMS ERROR; la respuesta
 *                        no cabe en resp_out; o falló la escritura al UART.
 * @return MODEM_TIMEOUT  No llegó ni OK ni error dentro de timeout_ms.
 */
static modem_estado_t ejecutar(const char *cmd, char *resp_out, size_t tam_resp,
                               uint32_t timeout_ms, bool log_fallo)
{
    size_t n = 0;
    size_t hueco;
    int leidos;
    uint32_t t0 = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    uint32_t transcurrido;
    
    resp_out[0] = '\0';

    uart_flush_input(UART_NUM_1);
    if (uart_write_bytes(UART_NUM_1, cmd, strlen(cmd)) < 0)
    {
        return MODEM_ERROR;
    }
    if (uart_write_bytes(UART_NUM_1, "\r", 1) < 0)
    {
        return MODEM_ERROR;
    }

    while (1)
    {
        transcurrido = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) - t0;
        if (transcurrido >= timeout_ms)
        {
            break;
        }

        hueco = tam_resp - 1 - n;
        if (hueco == 0)
        {
            if (log_fallo)
            {
                ESP_LOGE(TAG, "respuesta mas larga que el buffer");
            }
            return MODEM_ERROR;
        }

        leidos = uart_read_bytes(UART_NUM_1, resp_out + n, hueco, pdMS_TO_TICKS(MODEM_TROZO_LECTURA_MS));
        if (leidos < 0)
        {
            return MODEM_ERROR;
        }
        if (leidos > 0)
        {
            n += (size_t)leidos;
            resp_out[n] = '\0';

            if (strstr(resp_out, "\r\nOK\r\n"))
            {
                ESP_LOGI(TAG, "%s -> %s", cmd, resp_out);
                return MODEM_OK;
            }
            if (strstr(resp_out, "\r\nERROR\r\n") ||
                strstr(resp_out, "\r\n+CME ERROR:") ||
                strstr(resp_out, "\r\n+CMS ERROR:"))
            {
                ESP_LOGW(TAG, "%s -> %s", cmd, resp_out);
                return MODEM_ERROR;
            }
        }
    }

    if (log_fallo)
    {
        ESP_LOGW(TAG, "timeout de %s, recibido: %s", cmd, resp_out);
    }
    return MODEM_TIMEOUT;
}


modem_estado_t modem_sondear_at(int intentos)
{
    char resp[MODEM_TAM_RESPUESTA];

    for (int i = 0; i < intentos; i++)
    {
        if (ejecutar("AT", resp, sizeof resp, MODEM_TIMEOUT_SONDEO_MS, false) == MODEM_OK)
        {
            return MODEM_OK;
        }
    }
    return MODEM_TIMEOUT;
}

modem_estado_t modem_init(void)
{
    uart_config_t cfg = {
        .baud_rate = MODEM_BAUDIOS,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    // Configura el pin GPIO4 como salida para el PWRKEY del modem
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << MODEM_PIN_PWRKEY,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t err;

    err = uart_param_config(UART_NUM_1, &cfg);
    if (err != ESP_OK)
    {
        return MODEM_ERROR;
    }

    err = uart_set_pin(UART_NUM_1, MODEM_PIN_TX, MODEM_PIN_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK)
    {
        return MODEM_ERROR;
    }

    err = uart_driver_install(UART_NUM_1, MODEM_UART_RX_BUF_SIZE, 0, 0, NULL, 0);
    if (err != ESP_OK)
    {
        return MODEM_ERROR;
    }

    // Se pone a low para que no mande lo que tenga puesto de un princpio
    gpio_set_level(MODEM_PIN_PWRKEY, 0);
    err = gpio_config(&io);
    if (err != ESP_OK)
    {
        return MODEM_ERROR;
    }

    return MODEM_OK;
}


modem_estado_t modem_encender(void)
{
    uint32_t t0;

    // PWRKEY conmuta: el mismo pulso enciende y apaga (HD tablas 8 y 9).
    // El módem sobrevive a un reset del ESP32: primero se pregunta.
    if (modem_sondear_at(MODEM_SONDEOS_ENCENDIDO) == MODEM_OK)
    {
        ESP_LOGI(TAG, "el modem ya estaba encendido: no se pulsa PWRKEY");
        return MODEM_OK;
    }

    ESP_LOGI(TAG, "modem sin respuesta: pulso de encendido");
    t0 = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    gpio_set_level(MODEM_PIN_PWRKEY, 1);
    vTaskDelay(pdMS_TO_TICKS(MODEM_PULSO_ENCENDIDO_MS));
    gpio_set_level(MODEM_PIN_PWRKEY, 0);

    vTaskDelay(pdMS_TO_TICKS(MODEM_ESPERA_UART_MS));

    if (modem_sondear_at(MODEM_SONDEOS_AT) == MODEM_OK)
    {
        ESP_LOGI(TAG, "contesta a los %lu ms del pulso",
                 (unsigned long)((uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) - t0));
        return MODEM_OK;
    }

    ESP_LOGE(TAG, "el modem no contesta tras el pulso");
    return MODEM_TIMEOUT;
}

modem_estado_t modem_comando(const char *cmd, char *resp_out, size_t tam_resp, uint32_t timeout_ms)
{
    if (cmd == NULL || resp_out == NULL || tam_resp == 0 )
    {
        return MODEM_ERROR; 
    }
    return ejecutar(cmd, resp_out, tam_resp, timeout_ms, true);
}

modem_estado_t modem_apagar(void)
{
    char resp[MODEM_TAM_RESPUESTA];
    size_t n = 0;
    size_t hueco;
    int leidos;
    bool visto = false;
    uint32_t t0 = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    uint32_t transcurrido;
    uint32_t resto_ms;

    resp[0] = '\0';
    uart_flush_input(UART_NUM_1);
    // -1 en el sizeof para quitar el \0
    if (uart_write_bytes(UART_NUM_1, CMD_APAGAR, sizeof(CMD_APAGAR) - 1) < 0)
    {
        return MODEM_ERROR;
    }

    // AT+CPOWD no devuelve OK: su unica respuesta es el URC NORMAL POWER DOWN
    // (manual 5.2.1 y tabla 20.3). Por eso no se usa ejecutar().
    while (!visto)
    {
        transcurrido = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) - t0;
        if (transcurrido >= MODEM_TIMEOUT_POWERDOWN_MS)
        {
            break;
        }

        hueco = sizeof resp - 1 - n;
        if (hueco == 0)
        {
            break;
        }

        leidos = uart_read_bytes(UART_NUM_1, resp + n, hueco, pdMS_TO_TICKS(MODEM_TROZO_LECTURA_MS));
        if (leidos < 0)
        {
            break;
        }
        if (leidos > 0)
        {
            n += (size_t)leidos;
            resp[n] = '\0';
            // Compara lo recibido con la cadena esperada, si es distinto de NULL pone true en visto
            visto = (strstr(resp, "NORMAL POWER DOWN") != NULL);
        }
    }

    if (visto)
    {
        ESP_LOGI(TAG, "apagado normal en %lu ms",
                 (unsigned long)((uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) - t0));
    }
    else
    {
        ESP_LOGW(TAG, "sin NORMAL POWER DOWN, recibido: %s", resp);
    }

    // Toff(uart) llega a 6,9 s (HD tabla 9): se espera solo lo que falte. Tarda el modem en apagarse según la tabla MODEM_ESPEA_APAGADO_MS
    transcurrido = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) - t0;
    if (transcurrido < MODEM_ESPERA_APAGADO_MS)
    {
        resto_ms = MODEM_ESPERA_APAGADO_MS - transcurrido;
        vTaskDelay(pdMS_TO_TICKS(resto_ms));
    }

    if (modem_sondear_at(MODEM_SONDEOS_APAGADO) == MODEM_OK)
    {
        ESP_LOGE(TAG, "sigue contestando despues de CPOWD");
        return MODEM_ERROR;
    }

    return MODEM_OK;
}
