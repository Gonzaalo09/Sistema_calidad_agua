#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"

#include "modbus.h"
#include "sensores.h"
#include "tarea_polling.h"
#include "tarea_rtc.h"

QueueHandle_t cola_lecturas;

void tarea_polling(void *parametros)
{
    cola_lecturas = xQueueCreate(10, sizeof(lectura_sensor));

    uint8_t peticion[8];
    uint8_t datos_rx[256];
    uart_event_t evento;
    float valores[8];
    lectura_sensor lectura;

    TickType_t proximo_ciclo = xTaskGetTickCount();

    while (1)
    {
        for (int i = 0; i <= NUM_SENSORES - 1; i++)
        {
            sensor_descrip entrada = TABLA_SENSORES[i];

            construir_peticion(peticion, entrada.slave_id, entrada.reg_H, entrada.reg_L, entrada.num_registros);

            // Para ver por pantalla la trama que estás enviando
            /* for (int j = 0; j < 8; j++)
             {
                 printf("%02X ", peticion[j]);
             }
             printf("\n");
             */

            uart_write_bytes(UART_NUM_2, peticion, 8);

            lectura.valido = false;

            if (xQueueReceive(uart_queue, (void *)&evento, pdMS_TO_TICKS(1000)))
            {
                if (evento.type == UART_DATA)
                {
                    size_t longitud = 0;
                    uart_get_buffered_data_len(UART_NUM_2, &longitud);

                    if (longitud > 0)
                    {
                        longitud = uart_read_bytes(UART_NUM_2, datos_rx, longitud, 100 / portTICK_PERIOD_MS);
                        // Depende del número de registros (2bytes por cada registro)
                        size_t longitud_esperada = 5 + entrada.num_registros * 2;

                        if (longitud == longitud_esperada)
                        {
                            uint16_t crc_recibido = (datos_rx[longitud - 1] << 8) | datos_rx[longitud - 2];
                            uint16_t crc_calculado = calcular_crc16(datos_rx, longitud - 2);

                            if (crc_calculado == crc_recibido)
                            {
                                lectura.valido = true;

                                /*Para ver por pantalla la trama que recibes
                                for (int k = 0; k < longitud; k++)
                                    printf("%02X ", datos_rx[k]);
                                    */
                            }
                        }
                    }
                }
                if (lectura.valido == true)
                {
                    decodificar_datos(datos_rx, entrada.tipo, entrada.num_valores, valores);

                    uint32_t ts = rtc_timestamp_pais();
                    if (ts == 0)
                    {
                        printf("%s: timestamp no fiable (RTC no responde)\n", entrada.nombres_datos[0]);
                    }

                    for (int j = 0; j <= entrada.num_valores - 1; j++)
                    {
                        strcpy(lectura.nombre, entrada.nombres_datos[j]);
                        lectura.timestamp = ts;
                        lectura.valor = valores[j];
                        xQueueSend(cola_lecturas, &lectura, 0);
                    }
                }
            }
            // Aunque la trama vaya mal tiene que haber un tiempo entre medidas para que se sepa cuando es el final
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        vTaskDelayUntil(&proximo_ciclo, pdMS_TO_TICKS(PERIODO_MUESTREO_MS));
    }
}

QueueHandle_t get_cola_lecturas(void)
{
    return cola_lecturas;
}
