/*
 * modbus.c
 *
 * Protocolo Modbus RTU: CRC16, construcción de peticiones (función 0x03,
 * leer holding registers) y decodificación de la respuesta. Genérico: no
 * sabe nada de sensores concretos ni toca hardware.
 *
 * ORDEN DE BYTES EN LOS FLOAT (float32_CDAB)
 *
 * Los sensores de OD y pH (REMOND) codifican cada valor como float32
 * IEEE-754 en orden CDAB (word-swapped): mandan primero la palabra
 * "Data_L" y luego la "Data_H", al revés del orden estándar.
 *
 * Al reorden CDAB del sensor hay que sumarle un SEGUNDO reordenamiento,
 * porque el ESP32 es little-endian y memcpy reconstruye el float según
 * el orden en que estén los bytes en memoria. La combinación de ambos es
 * lo que hace decodificar_datos para bytes recibidos como C,D,A,B.
 *
 * Al integrar un sensor nuevo con floats Modbus: verificar SIEMPRE su
 * orden de bytes contra un ejemplo numérico real de su datasheet, nunca
 * asumirlo. Hay cuatro variantes posibles (ABCD, BADC, CDAB, DCBA) y
 * ninguna es "la estándar" de Modbus: cada fabricante elige la suya.
 * Un orden equivocado no da error, da floats astronómicos o NaN.
 *
 * Fuentes:
 *  - Modbus Application Protocol Specification V1.1b3, apartado 6.3
 *    (Read Holding Registers, 0x03). https://modbus.org/specs.php
 *  - Datasheets de los sensores REMOND (OD y pH) para el orden de bytes.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_err.h"

#include "modbus.h"

/**
 * @brief Calcula el CRC16 Modbus (RTU) de un buffer de bytes.
 *
 * @param buffer   Puntero al primer byte sobre el que calcular el CRC.
 * @param longitud Número de bytes a incluir en el cálculo. Tiene que ser
 *                 la longitud total de la trama menos 2: no se cuentan
 *                 los 2 bytes de CRC finales, ya que son el resultado
 *                 de este mismo cálculo.
 * @return CRC de 16 bits; el byte bajo va en `crc & 0xFF` y el alto en `crc >> 8`.
 */
uint16_t calcular_crc16(uint8_t *buffer, uint16_t longitud) {
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < longitud; i++) {
        crc ^= buffer[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}


/**
 * @brief Construye una trama de petición Modbus RTU (función 0x03, "leer
 *        holding registers") y le añade su CRC16 al final.
 *
 * @param[out] peticion_out  Buffer ya reservado por quien llama, de al
 *                           menos 8 bytes, donde se escribe la trama
 *                           completa (incluido el CRC).
 * @param id_esclavo         Dirección del esclavo Modbus (slave ID).
 * @param addres_H           Byte alto de la dirección del primer registro a leer.
 * @param addres_L           Byte bajo de la dirección del primer registro a leer.
 * @param num_registros      Número de registros de 16 bits a leer.
 */
void construir_peticion(uint8_t* peticion_out, uint8_t id_esclavo, uint8_t addres_H, uint8_t addres_L, uint16_t num_registros) {
    peticion_out[0] = id_esclavo;
    peticion_out[1] = 0x03;                        // función Modbus: leer registros
    peticion_out[2] = addres_H;
    peticion_out[3] = addres_L;
    peticion_out[4] = (num_registros >> 8) & 0xFF;  // num_registros byte alto
    peticion_out[5] = num_registros & 0xFF;         // num_registros byte bajo

    uint16_t crc_peticion = calcular_crc16(peticion_out, 6);
    peticion_out[6] = crc_peticion & 0xFF;          // CRC byte bajo
    peticion_out[7] = (crc_peticion >> 8) & 0xFF;   // CRC byte alto
}


/**
 * @brief Decodifica los valores de datos de una respuesta Modbus ya
 *        validada (longitud y CRC correctos) a un array de floats.
 *
 * @param datos        Trama de respuesta completa recibida. Se asume que
 *                     el campo de datos empieza en el byte 3 (tras
 *                     address + código de función + número de bytes).
 * @param tipo         Formato de codificación de cada valor (ver TipoDato).
 * @param num_valores  Número de valores a extraer.
 * @param[out] valores_out  Array ya reservado por quien llama, de al
 *                          menos num_valores floats, donde se escribe
 *                          cada valor decodificado.
 */
void decodificar_datos(uint8_t* datos, TipoDato tipo, uint8_t num_valores, float* valores_out){
    
    // Si el valor es entero  cada dato está cada 2 bytes si es float cada 4 
    int tamano_valor = (tipo == entero16) ? 2 : 4;  

    for (int i = 0; i <= num_valores - 1 ; i++ ) {
    
        int offset = 3 + i * tamano_valor; 
    
         switch (tipo)
        {
        case entero16: {
        //El H está en offset pero hay que moverlo 8 posiciones para meter el L(offseet + 1)            
        valores_out[i] = (float)((datos[offset] << 8) | datos[offset + 1]);
            break;
        }
        case float32_CDAB : {  
            //Están en CDBA hay que pasarlos a DCBA para leerlos, ya que el esp32 es little endian (se guarda primero el menos significativo)
            uint8_t bytes_salida[4] = {datos[offset + 1], datos[offset], datos[offset + 3], datos[offset + 2]};
            //Escribes en la dirección a la que está apuntando valores_out
            memcpy(&valores_out[i], bytes_salida, 4);
           break;
        }
        default:
          break;
        }
    
    }
}

