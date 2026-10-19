/*
 * Modbus RTU puro (CRC16, petición 0x03, decodificación de la respuesta); no toca hardware.
 * Fuentes: Modbus Application Protocol V1.1b3, 6.3; Modbus over Serial Line V1.02, apéndice B (CRC);
 * datasheets REMOND (OD y pH) para el orden de bytes.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_err.h"

#include "modbus.h"

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


void decodificar_datos(uint8_t* datos, TipoDato tipo, uint8_t num_valores, float* valores_out){
    
    // Bytes por valor: 2 (entero16) o 4 (float32_CDAB)
    int tamano_valor = (tipo == entero16) ? 2 : 4;  

    for (int i = 0; i <= num_valores - 1 ; i++ ) {
    
        int offset = 3 + i * tamano_valor; 
    
         switch (tipo)
        {
        case entero16: {
        // Big-endian: byte alto en offset, bajo en offset + 1
        valores_out[i] = (float)((datos[offset] << 8) | datos[offset + 1]);
            break;
        }
        case float32_CDAB : {  
            // El sensor manda los bytes en orden C,D,A,B (word-swapped) y el ESP32 es little-endian:
            // en memoria hacen falta D,C,B,A. Si se asume otro orden no hay error: salen floats absurdos o NaN.
            // Con un sensor nuevo, verificar el orden contra un ejemplo numérico de su datasheet.
            uint8_t bytes_salida[4] = {datos[offset + 1], datos[offset], datos[offset + 3], datos[offset + 2]};
            memcpy(&valores_out[i], bytes_salida, 4);
           break;
        }
        default:
          break;
        }
    
    }
}

