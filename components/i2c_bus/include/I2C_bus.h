#ifndef i2c_bus
#define i2c_bus

#include "driver/i2c_master.h"

esp_err_t i2c_bus_init(void);

i2c_master_bus_handle_t get_i2c_bus(void); 

void i2c_bus_scan(void);  

#endif