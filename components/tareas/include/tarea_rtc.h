#ifndef TAREA_RTC_H
#define TAREA_RTC_H

#include <stdint.h>

// Segundos de desfase respecto a UTC del país de despliegue (hora_local = UTC + OFFSET_PAIS_S).
// Perú (UTC-5, sin horario de verano) = -5 * 3600.
// ÚNICO valor a cambiar si el nodo se despliega en otro país.
#define OFFSET_PAIS_S (-5 * 3600)

/** @brief Tarea de depuración: registra cada 15 s la hora UTC, la temperatura del DS3231 y el timestamp del país. */
void tarea_rtc(void *pvParameters);

/**
 * @brief Timestamp actual en el "UTC del país" (UTC real + OFFSET_PAIS_S), en segundos.
 *
 * Hace una lectura I2C completa en cada llamada: guardar el resultado en vez de llamarla dos veces.
 * PROVISIONAL: se trasladará al componente `tiempo` en la fase 8.
 *
 * @return El timestamp, o 0 si el RTC no responde (tratarlo como inválido).
 */
uint32_t rtc_timestamp_pais(void);

#endif