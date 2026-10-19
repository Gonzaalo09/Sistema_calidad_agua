#ifndef REGISTRO_CSV_H
#define REGISTRO_CSV_H

#include <stddef.h>
#include <stdint.h>

#include "tarea_polling.h"

#define FORMATO_CSV 1      // subir si cambia el orden o el significado de las columnas fijas
#define TX_NO_INTENTADO 0  // no se llegó a intentar el envío (definido otra vez más abajo, con el mismo valor)

#define TAM_CABECERA_CSV 512 // bytes; criterio propio (141 B con NPK, OD y pH; con 8 sensores de 8 valores no cabría)
#define TAM_LINEA_CSV 512 // bytes; línea que se escribe en la SD; criterio propio (375 B en el peor caso con NPK, OD y pH)
#define TAM_RUTA_CSV 48 // bytes; ruta base del archivo (el caso más largo son ~30 caracteres); criterio propio

#define TAM_TRAMA_JSON        768 // bytes; trama JSON enviada por HTTP; criterio propio, por debajo de los 1024 B de BODYLEN del módem

// Valores de tx: los 1 a 8 son propios (por debajo de 100); 200 y siguientes son el código HTTP del servidor.
// Viajan en la SD: solo se añaden al final, nunca se reordenan ni se reutilizan.
#define TX_NO_INTENTADO        0 // no se llegó a intentar el envío
#define TX_SIN_SIM             1 // AT+CPIN? no da READY
#define TX_SIN_RED             2 // ni CEREG ni CGREG en stat 1 o 5
#define TX_SIN_CONEXION        3 // modem_abrir_datos falla
#define TX_MODEM_NO_CONTESTA   4 // timeout en un comando de lectura o configuración
#define TX_SIN_SERVIDOR        5 // AT+SHCONN da ERROR o timeout
#define TX_ERROR_MODEM         6 // ERROR del módem en el resto de la secuencia HTTP
#define TX_SIN_RESPUESTA       7 // sin +SHREQ en MODEM_TIMEOUT_SHREQ_MS
#define TX_TRAMA_NO_CABE       8 // registro_csv_json devuelve 0

#define TX_HTTP_OK             200 // guardado, o ya estaba
#define TX_HTTP_MAL_FORMADA    400 // falta nodo, seq, ts o rtc
#define TX_HTTP_DESACTIVADO    403 // dispositivo desactivado en la plataforma
#define TX_HTTP_DUPLICADO      409 // mismo (nodo, seq) con otro contenido
#define TX_HTTP_ERROR_SERVIDOR 500 // error del servidor o de la base de datos

/**
 * @brief Genera la cabecera del CSV a partir de TABLA_SENSORES (nunca se escribe a mano).
 * @return Longitud escrita, o 0 si no cabia en buf (buf queda vacio)
 */
size_t registro_csv_cabecera(char *buf, size_t tam);

/**
 * @brief Genera la linea CSV de un ciclo, con las mismas columnas y orden que la cabecera.
 *
 * Columnas: formato, fecha legible, ts, nodo, seq, codigo del RTC; por cada sensor su codigo y una columna por
 * magnitud; y tx. Valores con 3 decimales; los codigos son estado_codigo_t sin traducir. Un sensor que fallo deja
 * sus valores vacios. Sin hora (ts == 0), fecha queda vacia. No añade el '\n' final.
 *
 * @param tx  Resultado del envio (TX_*).
 * @param tam Tamaño de buf (TAM_LINEA_CSV).
 * @return Longitud escrita, o 0 si no cabia en buf (buf queda vacio)
 */
size_t registro_csv_linea(const ciclo *c, int tx, char *buf, size_t tam);

/**
 * @brief Ruta relativa del archivo del dia, sin extension:
 *        "2026/125010_2026-09-29", o "125010_SINHORA" si ts == 0.
 *
 * El dia sale de ts, que ya lleva la hora del pais: el archivo cambia a las 00:00 locales.
 * Quien llama añade ".csv" o "_2.csv" y el punto de montaje.
 *
 * @param tam Tamaño de buf (TAM_RUTA_CSV)
 */
void registro_csv_ruta(uint32_t ts, char *buf, size_t tam);

/**
 * @brief Genera la trama JSON plana del ciclo, con las claves de las columnas del CSV salvo fecha y tx.
 *
 * Un sensor que falla, o un valor no finito (NaN, infinito), va como null.
 *
 * @param tam Tamaño de buf (TAM_TRAMA_JSON)
 * @return Longitud escrita, o 0 si no cabia en buf (buf queda vacio)
 */
size_t registro_csv_json(const ciclo *c, char *buf, size_t tam);


#endif