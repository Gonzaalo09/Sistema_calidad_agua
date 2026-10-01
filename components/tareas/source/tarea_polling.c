#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "esp_log.h"

#include "modbus.h"
#include "sensores.h"
#include "tarea_polling.h"
#include "tarea_rtc.h"
#include "estado_sistema.h"
#include "config_nodo.h"

#define TIMEOUT_RESPUESTA_MS 1000   // espera maxima a que un sensor empiece a contestar
#define TIMEOUT_LECTURA_UART_MS 100 // espera maxima al leer los bytes ya recibidos

QueueHandle_t cola_lecturas;

static const char *TAG = "polling";

void tarea_polling(void *parametros)
{
    cola_lecturas = xQueueCreate(LONGITUD_COLA_CICLOS, sizeof(ciclo));

    uint8_t peticion[MODBUS_TAM_PETICION];
    uint8_t datos_rx[256];
    uart_event_t evento;
    ciclo lectura;

    TickType_t proximo_ciclo = xTaskGetTickCount();

    while (1)
    {
        memset(&lectura, 0, sizeof(lectura)); // Se pone al inicio todo el ciclo a 0

        lectura.seq = config_nodo_siguiente_seq(); // Configuramos el numero de seq al siguiente

        // Se toma el valor del RTC al principio del ciclo, todos los sensores vana atener esta hora aunque hay adiferencias de tiempo entre sus tomas serán de segundos
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

        for (int i = 0; i <= NUM_SENSORES - 1; i++)
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

            // Llega algo
            if (xQueueReceive(uart_queue, (void *)&evento, pdMS_TO_TICKS(TIMEOUT_RESPUESTA_MS)))
            {
                if (evento.type == UART_DATA)
                {
                    size_t longitud = 0;
                    uart_get_buffered_data_len(RS485_UART, &longitud);

                    if (longitud > 0)
                    {
                        longitud = uart_read_bytes(RS485_UART, datos_rx, longitud, pdMS_TO_TICKS(TIMEOUT_LECTURA_UART_MS));
                        // Depende del número de registros (2bytes por cada registro)
                        size_t longitud_esperada = 5 + entrada.num_registros * 2;

                        if (longitud == longitud_esperada)
                        {
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
                else // Trama corrupta llega pero hay algo mal
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
                // Hay un Timeout y no llega nada y la uart no recibe nada
                estado_sistema_publicar(FUENTE_SENSOR_BASE + i, NULL, 0, 0, ESTADO_SIN_RESPUESTA);
            }
            // Aunque la trama vaya mal tiene que haber un tiempo entre medidas para que se sepa cuando es el final
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        lectura.num_sensores = NUM_SENSORES;

        if (xQueueSend(cola_lecturas, &lectura, 0) != pdPASS)
        {
            ESP_LOGE(TAG, "cola llena: ciclo perdido");
        }

        vTaskDelayUntil(&proximo_ciclo, pdMS_TO_TICKS(PERIODO_MUESTREO_MS));
        ESP_LOGI(TAG, "pila libre minima: %u B", (unsigned)uxTaskGetStackHighWaterMark(NULL));
    }
}

QueueHandle_t get_cola_lecturas(void)
{
    return cola_lecturas;
}
