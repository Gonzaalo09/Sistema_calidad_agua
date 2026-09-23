#include "driver/i2c_master.h"
#include "esp_log.h"

#include "I2C_bus.h"

#define I2C_SDA_IO  GPIO_NUM_21
#define I2C_SCL_IO  GPIO_NUM_22
#define PROBE_TIMEOUT_MS 10

static const char *TAG = "I2C_bus";

static i2c_master_bus_handle_t i2c_bus_handle = NULL;


esp_err_t i2c_bus_init(void){

    if (i2c_bus_handle != NULL) {
        return ESP_OK; 
    }
    i2c_master_bus_config_t i2c_mst_config  = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA_IO, 
        .scl_io_num = I2C_SCL_IO, 
        .flags.enable_internal_pullup = true,
    };

    /**
     * Creas un bus, reservas el puerto y pones las configuraciones para crear el bus de I2C, devuelve el bus_handle que luego
     * es el que se usa para añadir nuevos dispositivos
     */
    esp_err_t err = i2c_new_master_bus(&i2c_mst_config, &i2c_bus_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error al crear el bus I2C: %s (0x%x)", esp_err_to_name(err), err);
    }

    return err;
}

/**
 * Para dar el bus_handle en otros archivos
 */
i2c_master_bus_handle_t get_i2c_bus(void) {
    return i2c_bus_handle; 
}

/**
 * Escaneas el bus que acabas de crear para detectar perifericos    
 */
void i2c_bus_scan(void) {
    if (i2c_bus_handle == NULL) {
        ESP_LOGE(TAG, "Bus I2C no creado correctamente");
        return;
    }
    for (int i = 0x08; i <= 0x77; i ++ ) {
        esp_err_t err = i2c_master_probe(i2c_bus_handle, i, PROBE_TIMEOUT_MS);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Dispositivo en dir 0x%02X", i);
        }else if(err == ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "Timeout, mirar cables, en dir 0x%02X", i);
        
        }
    }
} 

