#ifndef TIEMPO_H
#define TIEMPO_H

#include <stdint.h>

// Segundos de desfase respecto a UTC del país de despliegue (hora_local = UTC + OFFSET_PAIS_S).
// Perú (UTC-5, sin horario de verano) = -5 * 3600.
// ÚNICO valor a cambiar si el nodo se despliega en otro país.
#define OFFSET_PAIS_S (-5 * 3600)

typedef enum 
{
    TIEMPO_OK, 
    TIEMPO_SIN_RTC, // El RTC no contesta por I2C
    TIEMPO_NO_VALIDA, // Contesta, pero OSF = 1, hora incoherente o t_sync demasiado antigua 
    TIEMPO_CADUCADA
}tiempo_estado_t; 

tiempo_estado_t tiempo_ts_pais(uint32_t *ts_pais); 

void tiempo_sincronizar(const char *apn, const char *usuario, const char *clave);

#endif