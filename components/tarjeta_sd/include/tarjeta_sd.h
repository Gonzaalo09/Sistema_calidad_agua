#ifndef TARJETA_SD_H
#define TARJETA_SD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Resultado de una operación con la tarjeta. */
typedef enum
{
    SD_EST_OK = 0,
    SD_EST_AUSENTE,         // la tarjeta no responde (no esta o mal contacto)
    SD_EST_SIN_FORMATO,     // responde, pero no se puede montar (sin formato, exFAT o dañada)
    SD_EST_LLENA,           // queda menos de SD_MIN_LIBRE_BYTES
    SD_EST_ERROR_ESCRITURA, // estaba montada y fallo al escribir o al leer
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
 * @brief Imprime datos de la tarjeta, capacidad, espacio libre y la raiz.
 */
void tarjeta_sd_info(void);

/**
 * @brief Lee la primera linea de un archivo (ruta relativa a la tarjeta).
 *        Monta la tarjeta si hace falta.
 * @param tam Tamaño de buf
 * @param existe Sale a false si el archivo no existe (buf queda vacio)
 * @return SD_EST_OK (tambien si el archivo no existe), SD_EST_AUSENTE, SD_EST_SIN_FORMATO o SD_EST_ERROR_BUS
 *         si no se pudo montar, o SD_EST_ERROR_ESCRITURA / SD_EST_AUSENTE si fallo al abrirlo
 */
sd_estado_t tarjeta_sd_primera_linea(const char *ruta, char *buf, size_t tam, bool *existe);

/**
 * @brief Añade una linea al final de un archivo (ruta relativa a la tarjeta).
 *        Si el archivo no existe lo crea, con su carpeta, y escribe antes la
 *        cabecera. Abre, escribe y cierra: el archivo nunca queda abierto.
 *        Si falla por algo distinto de falta de espacio, desmonta para reintentar el montaje en el ciclo siguiente.
 * @return SD_EST_OK; SD_EST_AUSENTE, SD_EST_SIN_FORMATO o SD_EST_ERROR_BUS si no se pudo montar;
 *         SD_EST_LLENA (menos de SD_MIN_LIBRE_BYTES o ENOSPC); SD_EST_AUSENTE (ENODEV) o SD_EST_ERROR_ESCRITURA
 */
sd_estado_t tarjeta_sd_escribir_linea(const char *ruta, const char *cabecera, const char *linea);

/**
 * @brief Porcentaje de espacio libre (0-100); queda en 0 si falla.
 * @return SD_EST_OK, SD_EST_AUSENTE si no esta montada o SD_EST_ERROR_ESCRITURA si no se pudo calcular
 */
sd_estado_t tarjeta_sd_espacio(uint8_t *porcentaje_libre);

#endif