#ifndef CONFIG_NODO_H
#define CONFIG_NODO_H

#include <stdint.h>

typedef enum
{
    CFG_NODO_OK = 0,
    CFG_NODO_SIN_ID,    // no se pudo leer la MAC: el ID queda como "SINID"
    CFG_NODO_ERROR_NVS  // la NVS no se pudo usar: seq sera siempre 0
} cfg_nodo_estado_t;

/**
 * @brief Saca el ID del nodo de la MAC de fabrica y lee el ultimo seq de la NVS.
 *        Llamar una vez en app_main, antes de crear las tareas. Nunca borra la NVS.
 * @return CFG_NODO_OK, CFG_NODO_SIN_ID o CFG_NODO_ERROR_NVS
 */
cfg_nodo_estado_t config_nodo_init(void);

/**
 * @brief ID del nodo (p. ej. "A1B2C3"), o "SINID" si no se pudo leer la MAC.
 */
const char *config_nodo_id(void);

/**
 * @brief Siguiente numero de ciclo. Se guarda en la NVS antes de devolverlo,
 *        asi que un numero devuelto nunca se repite.
 * @return El nuevo seq, o 0 si no se pudo guardar (0 = "sin numero")
 */
uint32_t config_nodo_siguiente_seq(void);

#endif