#ifndef REGISTRO_CSV_H
#define REGISTRO_CSV_H

#include <stddef.h>
#include <stdint.h>

#include "tarea_polling.h"

#define FORMATO_CSV 1      // subir si cambia el orden o el significado de las columnas
#define TX_NO_INTENTADO 0  // valor de tx mientras no exista la transmision

#define TAM_CABECERA_CSV 512 // Suficiente tamaño para varios sensores
#define TAM_LINEA_CSV 512 // Max tamaño de la linea que se mete en la SD
#define TAM_RUTA_CSV 48 // Max tamaño del nombre del archivo de base

#define TAM_TRAMA_JSON        768 // Max tamaño de la trama 

#define TX_SIN_RED            1   // ni CEREG ni CGREG registrados
#define TX_SIN_CONEXION       2   // no se pudo activar la conexión de datos (CNACT)
#define TX_MODEM_NO_CONTESTA  3   // no se pudo leer el registro de ninguna red
#define TX_ERROR_MODEM        4   // un comando SH* devolvió ERROR
#define TX_SIN_RESPUESTA      5   // no llegó +SHREQ a tiempo   
#define TX_TRAMA_NO_CABE      6   // el JSON no cabe en TAM_TRAMA_JSON

/**
 * @brief Genera la cabecera del CSV a partir de TABLA_SENSORES.
 * @return Longitud escrita, o 0 si no cabia en buf
 */
size_t registro_csv_cabecera(char *buf, size_t tam);

/**
 * @brief Genera la linea CSV de un ciclo. Un sensor que fallo deja sus valores vacios.
 * @return Longitud escrita, o 0 si no cabia en buf
 */
size_t registro_csv_linea(const ciclo *c, int tx, char *buf, size_t tam);

/**
 * @brief Ruta relativa del archivo del dia, sin extension:
 *        "2026/125010_2026-09-29", o "125010_SINHORA" si ts == 0.
 */
void registro_csv_ruta(uint32_t ts, char *buf, size_t tam);

size_t registro_csv_json(const ciclo *c, char *buf, size_t tam);


#endif