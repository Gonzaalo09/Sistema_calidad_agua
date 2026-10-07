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

// Son los números de los errores que te pueden salir, para traducir a la pantalla lo que está pasando
#define TX_NO_INTENTADO        0
#define TX_SIN_SIM             1
#define TX_SIN_RED             2
#define TX_SIN_CONEXION        3
#define TX_MODEM_NO_CONTESTA   4
#define TX_SIN_SERVIDOR        5
#define TX_ERROR_MODEM         6
#define TX_SIN_RESPUESTA       7
#define TX_TRAMA_NO_CABE       8

#define TX_HTTP_OK             200
#define TX_HTTP_MAL_FORMADA    400
#define TX_HTTP_DESACTIVADO    403
#define TX_HTTP_DUPLICADO      409
#define TX_HTTP_ERROR_SERVIDOR 500

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