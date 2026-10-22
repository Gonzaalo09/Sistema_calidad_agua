#ifndef RTC_H
#define RTC_H

#include <time.h>
#include "esp_err.h"

/**
 * @brief Resultado de una operación con el RTC; separa el fallo del bus del fallo de la hora.
 */
typedef enum {
     RTC_OK,
     RTC_NO_RESPONDE, // NACK o timeout en el bus, o dispositivo no registrado
     RTC_OSCILADOR_PARADO, // flag OSF = 1, hora no fiable
     RTC_HORA_INCOHERENTE // campos fuera de rango, modo 12h inesperado, o la hora releída difiere de la escrita en más de 2 s
}rtc_estado_t;

/**
 * @brief Registra el DS3231 (0x68) en el bus de get_i2c_bus() y comprueba que responde.
 *
 * Si falla, deja el dispositivo sin registrar. Llamar tras i2c_bus_init y antes de crear tareas.
 *
 * @return ESP_OK (también si ya estaba inicializado), ESP_ERR_INVALID_STATE si no hay bus,
 *         o el error de i2c_master_bus_add_device / i2c_master_probe.
 */
esp_err_t rtc_ds3231_init(void);

/**
 * @brief Lee la hora UTC del DS3231.
 * @param[out] utc Hora decodificada; solo se escriben año, mes, día, hora, minuto y segundo.
 * @return RTC_OK, RTC_NO_RESPONDE o RTC_HORA_INCOHERENTE (modo 12h o campos fuera de rango).
 */
rtc_estado_t rtc_leer_hora(struct tm *utc);

/**
 * @brief Escribe la hora UTC, la relee para verificarla (tolerancia ±2 s) y solo entonces borra OSF.
 * @return RTC_OK, RTC_HORA_INCOHERENTE (fecha inválida o verificación fallida) o RTC_NO_RESPONDE.
 */
rtc_estado_t rtc_fijar_hora(const struct tm *utc);

/**
 * @brief Lee el flag OSF (oscilador parado) del registro de estado.
 * @return RTC_OK (oscilador en marcha), RTC_OSCILADOR_PARADO (OSF = 1) o RTC_NO_RESPONDE.
 */
rtc_estado_t rtc_comprobar_oscilador(void);

/**
 * @brief Lee en una sola transacción los registros de control (0Eh) y de estado (0Fh), sin interpretarlos.
 *
 * Diagnóstico: el bit 7 de control es EOSC (1 = el oscilador se para al pasar a la pila) y el bit 7 de estado es OSF.
 *
 * @param[out] control Registro 0Eh.
 * @param[out] estado  Registro 0Fh.
 * @return RTC_OK, o RTC_NO_RESPONDE (bus, dispositivo no registrado o puntero nulo).
 */
rtc_estado_t rtc_leer_control_estado(uint8_t *control, uint8_t *estado);

/**
 * @brief Lee la temperatura interna del DS3231 (resolución 0,25 °C).
 * @param[out] grados_c Temperatura en °C.
 * @return RTC_OK o RTC_NO_RESPONDE.
 */
rtc_estado_t rtc_leer_temperatura(float *grados_c);

#endif