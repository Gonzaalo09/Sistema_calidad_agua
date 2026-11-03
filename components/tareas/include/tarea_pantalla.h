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

/**
 * @brief Pide a tarea_pantalla que apague la OLED y se borre, y espera a que lo haga, como mucho PANTALLA_ESPERA_PARO_MS.
 *
 * Lo apaga la propia tarea, para no usar u8g2 desde dos tareas a la vez; mira la petición una vez por repintado.
 * Si no para a tiempo solo queda un log y el llamante sigue.
 */
void tarea_pantalla_detener(void);


#endif