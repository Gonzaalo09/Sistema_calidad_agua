#ifndef RTC
#define RTC

#include "time.h"
#include "esp_err.h"

/**
 * Esto nos sirve para diferenciar entre errores de comunicación y del RTC, para cuando queramos tener la hora
 * necesitamos saber si es fiable o no. también para la sincronización(más adelante).
 */
typedef enum {
     RTC_OK,
     RTC_NO_RESPONDE, //NACK o timeout en el bus
     RTC_OSCILADOR_PARADO, // flag OSF = 1, hora no fiable
     RTC_HORA_INCOHERENTE, // Campos fuera de rango, modo 12h inesperado
     RTC_OCUPADO // Otra tarea está escribiendo la hora
}rtc_estado_t;

esp_err_t rtc_ds3231_init(void);

rtc_estado_t rtc_leer_hora(struct tm *utc);

rtc_estado_t rtc_fijar_hora(const struct tm *utc);

rtc_estado_t rtc_comprobar_oscilador(void);

rtc_estado_t rtc_leer_temperatura(float *grados_c);

char rtc_nombre_estado(rtc_estado_t);

#endif