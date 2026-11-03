#ifndef TAREA_TRANSMISION_H
#define TAREA_TRANSMISION_H

/**
 * @brief Tarea consumidora de cola_lecturas; única usuaria de la SD y del módem.
 *
 * Por cada ciclo: lo envía por HTTP, lee la red, lo guarda en la SD con el resultado del envío y publica el estado.
 * Con MODO_DEPURACION = 0 atiende un solo ciclo y duerme; con 1 sigue indefinidamente.
 *
 * @param parametros No se usa
 */
void tarea_transmision(void* parametros);

#endif