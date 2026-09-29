#ifndef MODBUS_H
#define MODBUS_H

#include <stdio.h>

// Peticion de lectura 0x03: esclavo(1) + funcion(1) + registro(2) + cantidad(2) + CRC(2)
#define MODBUS_TAM_PETICION 8

/**
 * @brief Formato en el que un sensor codifica sus registros Modbus.
 */
typedef enum TipoDato {
    entero16,      /**< Entero de 16 bits, big-endian, ocupa 1 registro (2 bytes). */
    float32_CDAB   /**< Float IEEE754 de 32 bits en orden de bytes CDAB (word-swapped), ocupa 2 registros (4 bytes). */
} TipoDato;

/**
 * @brief Calcula el CRC16 Modbus (RTU) de un buffer de bytes.
 *
 * @param buffer   Puntero al primer byte sobre el que calcular el CRC.
 * @param longitud Número de bytes a incluir en el cálculo. NO debe incluir
 *                 los 2 bytes de CRC finales de la trama (ni al construir
 *                 una petición, donde aún no existen, ni al verificar una
 *                 respuesta, donde son los que se comparan contra el resultado).
 * @return CRC de 16 bits; el byte bajo va en `crc & 0xFF` y el alto en `crc >> 8`.
 */
uint16_t calcular_crc16(uint8_t *buffer, uint16_t longitud);

/**
 * @brief Construye una trama de petición Modbus RTU (función 0x03, "leer
 *        holding registers") y le añade su CRC16 al final.
 *
 * @param[out] peticion_out  Buffer ya reservado por quien llama, de al
 *                           menos MODBUS_TAM_PETICION bytes, donde se escribe la trama
 *                           completa (incluido el CRC).
 * @param id_esclavo         Dirección del esclavo Modbus (slave ID).
 * @param addres_H           Byte alto de la dirección del primer registro a leer.
 * @param addres_L           Byte bajo de la dirección del primer registro a leer.
 * @param num_registros      Número de registros de 16 bits a leer.
 */
void construir_peticion(uint8_t* peticion_out, uint8_t id_esclavo, uint8_t addres_H, uint8_t addres_L, uint16_t num_registros);

/**
 * @brief Decodifica los valores de datos de una respuesta Modbus ya
 *        validada (longitud y CRC correctos) a un array de floats.
 *
 * @param datos        Trama de respuesta completa recibida. Se asume que
 *                     el campo de datos empieza en el byte 3 (slave_id
 *                     + código de función + número de bytes).
 * @param tipo         Formato de codificación de cada valor (ver TipoDato).
 * @param num_valores  Número de valores a extraer.
 * @param[out] valores_out  Array ya reservado por quien llama, de al
 *                          menos num_valores floats, donde se escribe
 *                          cada valor decodificado.
 */
void decodificar_datos(uint8_t* datos, TipoDato tipo, uint8_t num_valores, float* valores_out);

#endif