#ifndef TIEMPO_H
#define TIEMPO_H

#include <stdint.h>

// Segundos de desfase respecto a UTC del país de despliegue (hora_local = UTC + OFFSET_PAIS_S).
// Perú (UTC-5, sin horario de verano) = -5 * 3600.
// ÚNICO valor a cambiar si el nodo se despliega en otro país.
#define OFFSET_PAIS_S (-5 * 3600)

typedef enum 
{
    TIEMPO_OK, // Hora fiable, sincronizada hace menos de VALIDEZ_S
    TIEMPO_SIN_RTC, // El RTC no contesta por I2C
    TIEMPO_NO_VALIDA, // Contesta, pero OSF = 1, hora incoherente o t_sync = 0
    TIEMPO_CADUCADA // Hora utilizable, pero sin sincronizar desde hace VALIDEZ_S o más
}tiempo_estado_t; 

tiempo_estado_t tiempo_ts_pais(uint32_t *ts_pais); 

void tiempo_sincronizar(const char *apn, const char *usuario, const char *clave);

/**
 * @brief Bloquea hasta la marca de la rejilla guardada antes de dormir, sondeando el DS3231 cada SONDEO_MARCA_MS.
 *
 * Vuelve sin esperar, para medir ya, si no hay marca (arranque en frío o ciclo anterior sin hora válida), si la
 * hora no es fiable o si la marca ya pasó (despertar tarde). Si falla una lectura durante la espera, deja de esperar.
 */
void tiempo_esperar_marca(void);

/**
 * @brief Calcula la siguiente marca de la rejilla UTC, la guarda para el próximo despertar y dice cuánto dormir.
 *
 * La marca es el primer múltiplo de PERIODO_CICLO_S que, restándole MARGEN_DESPERTAR_S, queda después de ahora; si el
 * ciclo fue más largo que el periodo, se salta las marcas perdidas. Llamar lo último del ciclo, después de cualquier
 * escritura en el DS3231: una corrección posterior desplazaría el despertar.
 *
 * @return Segundos hasta MARGEN_DESPERTAR_S antes de la marca, o PERIODO_CICLO_S sin hora fiable (y queda sin marca).
 */
uint32_t tiempo_segundos_hasta_despertar(void);

#endif