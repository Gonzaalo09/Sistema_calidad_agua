#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "esp_err.h"

#include "modbus.h"
#include "tarea_polling.h"
#include "tarea_transmision.h"

// Buffer reducido a 256 bytes, suficiente para Modbus RTU
const int uart_buffer_size = 256;
QueueHandle_t uart_queue;

void app_main(void) {
    // 1. Configuración del UART
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

    xTaskCreate(tarea_polling, "polling", 4096, NULL, 5, NULL);
    xTaskCreate(tarea_transmision, "transmision", 4096, NULL, 3, NULL);
}