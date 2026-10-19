/*
 * Dueño único del bus I2C del nodo (RTC y OLED cuelgan de él).
 * Fuente: ESP-IDF v6.1, "I2C Driver" (modo maestro, API bus-dispositivo).
 */
#include "driver/i2c_master.h"
#include "esp_log.h"

#include "i2c_bus.h"

#define I2C_SDA_IO  GPIO_NUM_21 // SDA; mismo GPIO en el DevKit y en la T-SIM7000G (esquemático V1.6)
#define I2C_SCL_IO  GPIO_NUM_22 // SCL; mismo GPIO en el DevKit y en la T-SIM7000G (esquemático V1.6)
#define PROBE_TIMEOUT_MS 10     // ms por dirección al sondear; criterio propio

static const char *TAG = "I2C_bus";

static i2c_master_bus_handle_t i2c_bus_handle = NULL;


esp_err_t i2c_bus_init(void){

    if (i2c_bus_handle != NULL) {
        return ESP_OK; 
    }
    i2c_master_bus_config_t i2c_mst_config  = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7, // valor típico según la doc de ESP-IDF
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA_IO, 
        .scl_io_num = I2C_SCL_IO, 
        .flags.enable_internal_pullup = true, // la T-SIM7000G no monta pull-ups (R9/R10 NC); los externos están en los módulos del RTC y la OLED
    };

    // Reserva el puerto y devuelve el handle con el que luego se añaden los dispositivos
    esp_err_t err = i2c_new_master_bus(&i2c_mst_config, &i2c_bus_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error al crear el bus I2C: %s (0x%x)", esp_err_to_name(err), err);
    }

    return err;
}

i2c_master_bus_handle_t get_i2c_bus(void) {
    return i2c_bus_handle; 
}

void i2c_bus_scan(void) {
    if (i2c_bus_handle == NULL) {
        ESP_LOGE(TAG, "Bus I2C no creado correctamente");
        return;
    }
    // 0x08–0x77: direcciones de 7 bits no reservadas
    for (int i = 0x08; i <= 0x77; i ++ ) {
        esp_err_t err = i2c_master_probe(i2c_bus_handle, i, PROBE_TIMEOUT_MS);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Dispositivo en dir 0x%02X", i);
        }else if(err == ESP_ERR_TIMEOUT) { // una dirección vacía daría ESP_ERR_NOT_FOUND: un timeout apunta a un problema eléctrico
            ESP_LOGW(TAG, "Timeout, mirar cables, en dir 0x%02X", i);
        
        }
    }
} 

