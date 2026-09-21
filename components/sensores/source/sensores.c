#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "sensores.h"


//Aquí es donde tienes que meter los nuevos sensore si quieres medir uno nuevo
const sensor_descrip TABLA_SENSORES[] = {
    //NPK sensor JXBS-3001-NPK-RS
   { .slave_id = 1, .reg_H = 0x00, .reg_L = 0x1E, .num_registros = 3, .tipo = entero16, 
    .num_valores =3,  .nombres_datos  ={"Nitrogeno", "Fosforo", "Potasio"} },
    
    //OD sensor
    { .slave_id = 3, .reg_H = 0x00, .reg_L = 0x01, .num_registros = 4, .tipo = float32_CDAB, 
    .num_valores = 2,  .nombres_datos = {"OD_medida", "temperatura"}}, 

    //Ph Sensor
    { .slave_id = 5, .reg_H = 0x00, .reg_L = 0x01, .num_registros = 4, .tipo = float32_CDAB,
    .num_valores = 2, .nombres_datos = {"Ph", "temperatura"}},
};

//Hay qye incrementar cuando metas uno más 
const size_t NUM_SENSORES = 3; 