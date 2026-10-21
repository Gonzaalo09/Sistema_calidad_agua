#ifndef CONFIG_NODO_H
#define CONFIG_NODO_H

#include <stdint.h>

/** @brief Resultado de config_nodo_init. */
typedef enum
{
    CFG_NODO_OK = 0,    // ID y NVS correctos
    CFG_NODO_SIN_ID,    // no se pudo leer la MAC: el ID queda como "SINID"
    CFG_NODO_ERROR_NVS  // la NVS no se pudo usar: seq sera siempre 0
} cfg_nodo_estado_t;

/**
 * @brief Saca el ID del nodo de la MAC de fabrica y lee el ultimo seq de la NVS.
 *        Llamar una vez en app_main, antes de crear las tareas. Nunca borra la NVS:
 *        seq solo se reinicia con `idf.py erase-flash`.
 * @return CFG_NODO_OK, CFG_NODO_SIN_ID o CFG_NODO_ERROR_NVS (si fallan las dos, ERROR_NVS)
 */
cfg_nodo_estado_t config_nodo_init(void);

/**
 * @brief ID del nodo (p. ej. "A1B2C3"), o "SINID" si no se pudo leer la MAC.
 */
const char *config_nodo_id(void);

/**
 * @brief Siguiente numero de ciclo. Se guarda en la NVS antes de devolverlo,
 *        asi que un numero devuelto nunca se repite.
 * @return El nuevo seq, o 0 ("sin numero") si la NVS no esta usable, no se pudo guardar o seq esta agotado (UINT32_MAX)
 */
uint32_t config_nodo_siguiente_seq(void);

uint32_t config_nodo_t_sync(void); // Devuelve la úlima sincronización correcta (GNSS o NTP)

uint32_t config_nodo_t_gnss(void); // Devuelve el último intento de GNSS con o sin fix

void config_nodo_guardar_t_sync(uint32_t t); // Actualiza la copia de t_sync en la RAM y la guarda en la NVS 

void config_nodo_guardar_t_gnss(uint32_t t); // Actualiza la copia de t_gnss en la RAM y la guarda en la NVS

#endif