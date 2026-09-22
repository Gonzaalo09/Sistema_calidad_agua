#ifndef TAREA_RTC_H
#define TAREA_RTC_H

#include <stdint.h>

// Desfase respecto a UTC del país de despliegue (hora_local = UTC + OFFSET_PAIS_S).
// Perú (UTC-5, sin horario de verano) = -5 * 3600.
// ÚNICO valor a cambiar si el nodo se despliega en otro país.
#define OFFSET_PAIS_S (-5 * 3600)

void tarea_rtc(void *pvParameters);

// Timestamp actual en el "UTC del país" (UTC real + OFFSET_PAIS_S), en segundos.
// Devuelve 0 si el RTC no responde; quien llama debe tratarlo como timestamp inválido.
// PROVISIONAL: se trasladará al módulo `tiempo` cuando exista (fase 3).
uint32_t rtc_timestamp_pais(void);

#endif