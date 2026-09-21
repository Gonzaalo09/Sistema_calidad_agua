#include <stdint.h>
#include <stdbool.h>

#include "driver/i2c_master.h"

#include "RTC.h"

static const uint16_t RTC_DIR_I2C = 0x68;   // dirección de 7 bits, datasheet pág. 17
static const uint32_t RTC_VELOCIDAD_HZ = 100000; 
static const int RTC_TIMEOUT_MS = 100; 

static const uint8_t REG_SEGUNDOS = 0x00; 
static const uint8_t REG_CONTROL = 0x0E;
static const uint8_t REG_ESTADO = 0x0F;
static const uint8_t REG_TEMP_MSB = 0x11; 

static const uint8_t  BIT_OSF = (1U << 7);   // REG_ESTADO del oscilador te dice si está parado o no
static const uint8_t  BIT_MODO_12H = (1U << 6);   // Registro de horas, modo 12 o 24h 

static const uint8_t MASK_SEG_MIN = 0x7F; //Mascara para leer los segundos y los minutos del bit 0 al 6
static const uint8_t MASK_HORA_24H = 0x3F; //Mascara para leer las horas del bit 0 al 5
static const uint8_t MASK_DIA = 0x3F; //Mascara para leer el día del bit 0 al 5
static const uint8_t MASK_MES = 0x1F; //Mascara para leer el mes del bit 0 al 4

static i2c_master_dev_handle_t dev_handle = NULL;

/*=========================================Funciones privadas============================================*/

static uint8_t bcd_a_dec(uint8_t val) {
    return (val >> 4) * 10 + (val & 0x0F);
}

static uint8_t dec_a_bcd(uint8_t val) {
    return ((val/10) << 4) | (val % 10); 
}

static uint8_t es_bisiesto(uint16_t anio) {
    return (anio % 4 == 0) && ((anio % 100 != 0) || (anio % 400 == 0));
}

static uint8_t dias_del_mes( uint8_t mes,uint16_t anio){
    static const uint8_t tabla[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (mes < 1 || mes > 12) {
        return 0;
    }

    if ((mes == 2) && es_bisiesto(anio)) {
        return 29; 
    }
    return tabla[mes - 1]; 
} 

// Devuelve 1..7 (1 = domingo ... 7 = sábado)
// Precondiciones: anio = año completo (2026, no tm_year), mes = 1..12 (ya validado)
static uint8_t dia_semana(int anio, int mes, int dia)
{
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};

    if (mes < 3) {
        anio--;                      // enero y febrero cuentan como del año anterior
    }
    int dow = (anio + anio / 4 - anio / 100 + anio / 400 + t[mes - 1] + dia) % 7;   // 0 = domingo
    return (uint8_t)(dow + 1);
}


/*=========================================Funciones publicas============================================*/






