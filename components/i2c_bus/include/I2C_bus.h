#ifndef I2C_bus_H
#define I2C_bus_H

#include "driver/i2c_master.h"

/**
 * @brief Crea el bus I2C maestro (I2C_NUM_0). Llamar desde app_main antes de crear tareas.
 * @return ESP_OK (también si ya estaba creado) o el error de i2c_new_master_bus.
 */
esp_err_t i2c_bus_init(void);

/**
 * @brief Handle del bus para registrar dispositivos con i2c_master_bus_add_device.
 * @return El handle, o NULL si i2c_bus_init no se ha llamado o falló.
 */
i2c_master_bus_handle_t get_i2c_bus(void); 

/**
 * @brief Sondea 0x08–0x77 y registra en el log los dispositivos que responden y los timeouts. Solo bring-up.
 */
void i2c_bus_scan(void);  

#endif