#ifndef TAREA_PANTALLA_H
#define TAREA_PANTALLA_H

#include <stdint.h>

/**
 * @brief Tarea que refresca la pantalla OLED con los datos y el estado del nodo.
 *
 * Lee estado_sistema y el RTC; no consume cola_lecturas. Rota entre las
 * paginas cada SEGUNDOS_POR_PAGINA y repinta cada segundo, de modo que los
 * segundos del reloj avanzando sirven de latido: si se quedan parados, el
 * firmware esta colgado.
 *
 * @param parametros No se usa
 */
void tarea_pantalla(void *parametros);

#endif