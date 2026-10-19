/*
 * Tarea de sondeo: lee cada sensor de TABLA_SENSORES por Modbus RTU y encola un ciclo por vuelta.
 * Fuentes: Modbus Application Protocol V1.1b3, 6.3; Modbus over Serial Line V1.02, 2.5.1.1 (silencio entre tramas);
 * ESP-IDF v6.1, "UART".
 */
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_err.h"

#include "modbus.h"
#include "sensores.h"
#include "tarea_polling.h"
#include "tarea_rtc.h"
#include "estado_sistema.h"
#include "config_nodo.h"
#include "registro_csv.h"

#define TIMEOUT_RESPUESTA_MS 1000   // ms hasta que un sensor empieza a contestar; criterio propio
#define TIMEOUT_LECTURA_UART_MS 100 // ms al leer los bytes ya recibidos; criterio propio

#define RS485_UART UART_NUM_2 // UART del bus Modbus

// Pines de UART2 hacia el modulo RS485-TTL, vistos desde el ESP32.
// Con TX=18/RX=19 los tres sensores daban timeout (30/09/2026): el cableado real cruza al revés.
#define RS485_PIN_TX 19 // GPIO de TX; sale del ESP32 hacia el modulo
#define RS485_PIN_RX 18 // GPIO de RX; entra al ESP32 desde el modulo
#define RS485_BAUDIOS 9600        // baudios (8N1); tiene que coincidir con todos los sensores del bus (datasheets)
#define RS485_TAM_COLA_EVENTOS 10 // eventos de la UART pendientes (uart_queue); criterio propio
#define RS485_TAM_BUFFER 256      // bytes de los buffers RX y TX de la UART2; criterio propio (una respuesta Modbus cabe de sobra)

static QueueHandle_t uart_queue;     // eventos de la UART2
static QueueHandle_t cola_lecturas;  // ciclos de tarea_polling hacia tarea_transmision

static const char *TAG = "polling";

esp_err_t tarea_polling_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = RS485_BAUDIOS,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };

    esp_err_t err = uart_param_config(RS485_UART, &uart_config);
    if (err != ESP_OK)
    {
        return err;
    }
    err = uart_set_pin(RS485_UART, RS485_PIN_TX, RS485_PIN_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK)
    {
        return err;
    }
    err = uart_driver_install(RS485_UART, RS485_TAM_BUFFER, RS485_TAM_BUFFER, RS485_TAM_COLA_EVENTOS, &uart_queue, 0);
    if (err != ESP_OK)
    {
        return err;
    }

    cola_lecturas = xQueueCreate(LONGITUD_COLA_CICLOS, sizeof(ciclo));
    if (cola_lecturas == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void tarea_polling(void *parametros)
{
    uint8_t peticion[MODBUS_TAM_PETICION];
    uint8_t datos_rx[256];
    uart_event_t evento;
    ciclo lectura;

    TickType_t proximo_ciclo = xTaskGetTickCount();

    while (1)
    {
        memset(&lectura, 0, sizeof(lectura)); // todos los códigos quedan en SIN_DATOS (= 0)

        lectura.seq = config_nodo_siguiente_seq();

        // Una sola lectura del RTC por ciclo: todos los sensores comparten el ts del inicio del ciclo
        // (una medida puede salir sellada hasta unos segundos antes de tomarse).
        uint32_t ts = rtc_timestamp_pais();
        if (ts == 0)
        {
            lectura.estado_rtc = ESTADO_SIN_HORA;
            ESP_LOGW(TAG, "Timestamp no fiable, el RTC no responde");
            estado_sistema_publicar(FUENTE_RTC, NULL, 0, 0, ESTADO_SIN_HORA);
        }
        else
        {
            estado_sistema_publicar(FUENTE_RTC, NULL, 0, ts, ESTADO_OK);
            lectura.ts = ts;
            lectura.estado_rtc = ESTADO_OK;
        }

        for (size_t i = 0; i < NUM_SENSORES; i++)
        {
            sensor_descrip entrada = TABLA_SENSORES[i];
            ciclo_sensor *sensor = &lectura.sensor[i];

            construir_peticion(peticion, entrada.slave_id, entrada.reg_H, entrada.reg_L, entrada.num_registros);

            // Cada intercambio empieza limpio: se descartan bytes sobrantes de la
            // peticion anterior y los eventos que ya no corresponden a nada.
            // uart_flush_input no vacia la cola de eventos, hay que hacerlo aparte.
            uart_flush_input(RS485_UART);
            xQueueReset(uart_queue);

            // Para ver por pantalla la trama que estás enviando
            /*for (int j = 0; j < MODBUS_TAM_PETICION; j++)
             {
                 printf("%02X ", peticion[j]);
             }
             printf("\n");*/
             

            uart_write_bytes(RS485_UART, peticion, MODBUS_TAM_PETICION);

            bool trama_ok = false;

            // Espera el primer evento de la UART; sin evento en el plazo = el sensor no contesta
            if (xQueueReceive(uart_queue, (void *)&evento, pdMS_TO_TICKS(TIMEOUT_RESPUESTA_MS)))
            {
                if (evento.type == UART_DATA)
                {
                    size_t longitud = 0;
                    uart_get_buffered_data_len(RS485_UART, &longitud);

                    if (longitud > 0)
                    {
                        int leidos = uart_read_bytes(RS485_UART, datos_rx, longitud, pdMS_TO_TICKS(TIMEOUT_LECTURA_UART_MS));
                        // <= 0 (error o nada leído): longitud 0, que no coincide con la esperada y cuenta como trama inválida
                        longitud = (leidos > 0) ? (size_t)leidos : 0;
                        // Respuesta 0x03: esclavo + función + nº de bytes + datos (2 por registro) + CRC (2)
                        size_t longitud_esperada = 5 + entrada.num_registros * 2;

                        if (longitud == longitud_esperada)
                        {
                            // En RTU el CRC viaja con el byte bajo primero
                            uint16_t crc_recibido = (datos_rx[longitud - 1] << 8) | datos_rx[longitud - 2];
                            uint16_t crc_calculado = calcular_crc16(datos_rx, longitud - 2);

                            if (crc_calculado == crc_recibido)
                            {
                                trama_ok = true;

                               // Para ver por pantalla la trama que recibes
                               /* for (int k = 0; k < longitud; k++)
                                    printf("%02X ", datos_rx[k]);
                                 */   
                            }
                        }
                    }
                }
                if (trama_ok == true)
                {
                    decodificar_datos(datos_rx, entrada.tipo, entrada.num_valores, sensor->valores);

                    sensor->codigo = ESTADO_OK;
                    sensor->num_valores = entrada.num_valores; // Sirve para indicar el numero de valores válidos

                    estado_sistema_publicar(FUENTE_SENSOR_BASE + i, sensor->valores, sensor->num_valores, ts, ESTADO_OK);
                }
                else // llegó algo pero no es válido: longitud o CRC erróneos, o un evento de UART que no es de datos
                {
                    sensor->codigo = ESTADO_TRAMA_INVALIDA;
                    ESP_LOGW(TAG, "%s: trama invalida (longitud o CRC)", entrada.nombre_fuente);
                    estado_sistema_publicar(FUENTE_SENSOR_BASE + i, NULL, 0, 0, ESTADO_TRAMA_INVALIDA);
                }
            }
            else
            {
                sensor->codigo = ESTADO_SIN_RESPUESTA;
                ESP_LOGW(TAG, "%s: sin respuesta en sensor", entrada.nombre_fuente);
                // Timeout: cable o alimentación
                estado_sistema_publicar(FUENTE_SENSOR_BASE + i, NULL, 0, 0, ESTADO_SIN_RESPUESTA);
            }
            // Silencio entre tramas: t3.5 = 3,5 caracteres de 11 bits a 9600 baudios ≈ 4 ms; 5 ms lo cubre
            // (Modbus over Serial Line V1.02, 2.5.1.1). Se aplica también tras un fallo.
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        lectura.num_sensores = NUM_SENSORES;

        if (xQueueSend(cola_lecturas, &lectura, 0) != pdPASS)
        {
            ESP_LOGE(TAG, "cola llena: ciclo perdido");
        }

        // El periodo se cuenta desde el arranque de la tarea, no desde una rejilla UTC
        vTaskDelayUntil(&proximo_ciclo, pdMS_TO_TICKS(PERIODO_MUESTREO_MS));
        ESP_LOGI(TAG, "pila libre minima: %u B", (unsigned)uxTaskGetStackHighWaterMark(NULL));
    }
}

QueueHandle_t get_cola_lecturas(void)
{
    return cola_lecturas;
}
