    #ifndef TARJETA_SD_H
#define TARJETA_SD_H

#include <stdbool.h>

typedef enum
{
    SD_EST_OK = 0,
    SD_EST_AUSENTE,         // la tarjeta no responde (no esta o mal contacto)
    SD_EST_SIN_FORMATO,     // responde, pero no se puede montar (sin formato, exFAT o dañada)
    SD_EST_LLENA,           // sin espacio (se usara en el paso 5)
    SD_EST_ERROR_ESCRITURA, // estaba montada y fallo al escribir o al releer
    SD_EST_ERROR_BUS        // no se pudo inicializar el bus SPI (pines o configuracion)
} sd_estado_t;

/**
 * @brief Inicializa el bus SPI e intenta montar la tarjeta.
 *        Llamar una vez en app_main, antes de crear las tareas.
 * @return Estado del montaje, o SD_EST_ERROR_BUS si fallo el bus
 */
sd_estado_t tarjeta_sd_init(void);

/**
 * @brief Monta la tarjeta si no lo esta. Nunca formatea.
 * @return SD_EST_OK, SD_EST_AUSENTE, SD_EST_SIN_FORMATO o SD_EST_ERROR_BUS
 */
sd_estado_t tarjeta_sd_montar(void);

/**
 * @brief Desmonta la tarjeta si estaba montada. El bus SPI se queda iniciado.
 */
void tarjeta_sd_desmontar(void);

/**
 * @brief true si la tarjeta esta montada.
 */
bool tarjeta_sd_montada(void);

/**
 * @brief Bring-up: imprime los datos de la tarjeta, capacidad, espacio libre
 *        y el contenido de la raiz.
 */
void tarjeta_sd_info(void);

/**
 * @brief PROVISIONAL (paso 4): monta si hace falta, añade una linea a
 *        PRUEBA.TXT, la relee y la compara. Si falla, desmonta.
 * @return SD_EST_OK o el fallo encontrado
 */
sd_estado_t tarjeta_sd_prueba_escritura(void);

#endif