/*
 * Driver del DS3231 (hora, oscilador y temperatura) en tres capas: conversión pura, acceso I2C y API.
 * Fuente: DS3231 datasheet, Analog Devices (https://www.analog.com/media/en/technical-documentation/data-sheets/ds3231.pdf).
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

#include "driver/i2c_master.h"
#include "esp_log.h"

#include "RTC.h"
#include "i2c_bus.h"

static const char *TAG = "RTC";

static const uint16_t RTC_DIR_I2C = 0x68; ///< dirección de 7 bits, datasheet pág. 17
static const uint32_t RTC_VELOCIDAD_HZ = 100000; ///< Hz; el DS3231 admite hasta 400 kHz (datasheet), 100 kHz como la OLED que comparte bus
static const int RTC_TIMEOUT_MS = 100;            ///< ms por transacción I2C; criterio propio

static const uint8_t REG_SEGUNDOS = 0x00; ///< Primer registro de hora (segundos); los 7 de hora y fecha son contiguos
static const uint8_t REG_CONTROL = 0x0E;  ///< Registro de control: bit 7 = EOSC; el de estado (0Fh) va justo detrás
static const uint8_t REG_ESTADO = 0x0F;   ///< Registro de estado: contiene OSF
static const uint8_t REG_TEMP_MSB = 0x11; ///< Byte alto de la temperatura; el bajo es 0x12

static const uint8_t BIT_OSF = (1U << 7);      ///< Bit 7 del registro de estado: 1 = el oscilador se ha parado
static const uint8_t BIT_MODO_12H = (1U << 6); ///< Bit 6 del registro de horas: 1 = modo 12 h

static const uint8_t MASK_SEG_MIN = 0x7F;  ///< Segundos y minutos: bits 0–6
static const uint8_t MASK_HORA_24H = 0x3F; ///< Horas en modo 24 h: bits 0–5
static const uint8_t MASK_DIA = 0x3F;      ///< Día del mes: bits 0–5
static const uint8_t MASK_MES = 0x1F;      ///< Mes: bits 0–4 (el bit 7 es el siglo)

static i2c_master_dev_handle_t dev_handle = NULL;

/*=========================================Funciones privadas============================================*/

/** @brief Convierte un byte BCD a decimal (0x25 -> 25). */
static uint8_t bcd_a_dec(uint8_t val)
{
    return (val >> 4) * 10 + (val & 0x0F);
}

/** @brief Convierte un valor decimal (0-99) a BCD (25 -> 0x25). */
static uint8_t dec_a_bcd(uint8_t val)
{
    return ((val / 10) << 4) | (val % 10);
}

/** @brief 1 si el año completo es bisiesto, 0 si no. */
static uint8_t es_bisiesto(uint16_t anio)
{
    return (anio % 4 == 0) && ((anio % 100 != 0) || (anio % 400 == 0));
}

/** @brief Días del mes (1..12) en ese año completo; 0 si el mes no es válido. */
static uint8_t dias_del_mes(uint16_t anio, uint8_t mes)
{
    static const uint8_t tabla[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (mes < 1 || mes > 12)
    {
        return 0;
    }

    if ((mes == 2) && es_bisiesto(anio))
    {
        return 29;
    }
    return tabla[mes - 1];
}

/**
 * @brief Decodifica los 7 registros BCD (seg, min, hora, día semana, día, mes, año) a struct tm.
 * @return RTC_OK, o RTC_HORA_INCOHERENTE si está en modo 12 h o algún campo está fuera de rango.
 */
static rtc_estado_t decodificar_hora(const uint8_t bcd[7], struct tm *utc)
{

    if (bcd[2] & BIT_MODO_12H)
    {
        return RTC_HORA_INCOHERENTE;
    }

    uint8_t seg = bcd_a_dec(bcd[0] & MASK_SEG_MIN);
    uint8_t min = bcd_a_dec(bcd[1] & MASK_SEG_MIN);
    // bcd[3] (día de la semana) no se usa al leer: codificar_hora() lo calcula al escribir
    uint8_t hora = bcd_a_dec(bcd[2] & MASK_HORA_24H);
    uint8_t dia = bcd_a_dec(bcd[4] & MASK_DIA);
    uint8_t mes = bcd_a_dec(bcd[5] & MASK_MES); // 1..12, tal cual lo entrega el chip
    uint16_t anio = 2000 + bcd_a_dec(bcd[6]);   // año completo, p.ej. 2026
    // el bit 7 del mes (siglo) se ignora: MASK_MES lo descarta

    if ((seg > 59) || (min > 59) || (hora > 23) || (mes < 1) || (mes > 12))
    {
        return RTC_HORA_INCOHERENTE;
    }
    if ((dia < 1) || (dia > dias_del_mes(anio, mes)))
    {
        return RTC_HORA_INCOHERENTE;
    }

    utc->tm_sec = seg;
    utc->tm_min = min;
    utc->tm_hour = hora;
    utc->tm_mday = dia;
    utc->tm_mon = mes - 1;      // struct tm: 0 = enero
    utc->tm_year = anio - 1900; // struct tm: años desde 1900
    utc->tm_isdst = 0;          // sin horario de verano; evita basura si el llamador no inicializó la struct

    return RTC_OK;
}

/**
 * @brief Inverso de decodificar_hora: struct tm a 7 registros BCD.
 * @return RTC_OK, o RTC_HORA_INCOHERENTE si la fecha no es válida o el año no está en 2000–2099.
 */
static rtc_estado_t codificar_hora(const struct tm *utc, uint8_t bcd[7])
{
    uint8_t seg = utc->tm_sec;
    uint8_t min = utc->tm_min;
    uint8_t hora = utc->tm_hour;
    uint8_t dia = utc->tm_mday;
    uint8_t mes = utc->tm_mon + 1;
    uint16_t anio = utc->tm_year + 1900;

    struct tm copia = *utc; 
    copia.tm_isdst = 0;
    mktime(&copia); 

    if ((seg > 59) || (min > 59) || (hora > 23) || (mes < 1) || (mes > 12) || (anio < 2000) || (anio > 2099))
    {
        return RTC_HORA_INCOHERENTE;
    }
    if ((dia < 1) || (dia > dias_del_mes(anio, mes)))
    {
        return RTC_HORA_INCOHERENTE;
    }

    bcd[0] = dec_a_bcd(seg);
    bcd[1] = dec_a_bcd(min);
    bcd[2] = dec_a_bcd(hora);
    bcd[3] = dec_a_bcd(copia.tm_wday + 1);
    bcd[4] = dec_a_bcd(dia);
    bcd[5] = dec_a_bcd(mes);
    bcd[6] = dec_a_bcd(anio - 2000);

    return RTC_OK;
}

/**
 * @brief Formato de la temperatura (DS3231, registros 11h y 12h).
 *
 * @verbatim
 * Temperature Register - Upper Byte (11h)
 * BIT7  BIT6  BIT5  BIT4  BIT3  BIT2  BIT1  BIT0
 * Sign  Data  Data  Data  Data  Data  Data  Data
 *
 * Temperature Register - Lower Byte (12h)
 * BIT7  BIT6  BIT5  BIT4  BIT3  BIT2  BIT1  BIT0
 * Data  Data   0     0     0     0     0     0
 * @endverbatim
 *
 * 10 bits en total (8 del byte 11h + 2 altos del byte 12h), en complemento
 * a dos, con resolución de 0.25 °C. Al aplicar alimentación, el valor por
 * defecto es 0 °C hasta que termina la primera conversión.
 * msb lleva el signo (bit 7); de lsb solo se usan sus 2 bits altos.
 */
static float decodificar_temperatura(int8_t msb, uint8_t lsb)
{
    return (float)msb + (lsb >> 6) * 0.25f;
}
/*=========================================Capa 2: ACCESO AL CHIP============================================*/

/**
 * @brief Lee n bytes desde el registro reg en una sola transacción con repeated start.
 *
 * Una sola transacción mantiene los buffers secundarios del chip sincronizados desde el START inicial:
 * no se mezclan valores viejos y nuevos si cambia el segundo a mitad de lectura.
 * Devuelve ESP_ERR_INVALID_STATE si dev es NULL.
 */
static esp_err_t leer_registros(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *buffer, size_t n)
{
    if (dev == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_transmit_receive(dev, &reg, 1, buffer, n, RTC_TIMEOUT_MS);
}

/**
 * @brief Escribe n bytes (máximo 7) a partir del registro reg en una sola transacción.
 *
 * Al escribir el registro de segundos el chip reinicia su cadena de división, y el resto de la hora y la fecha
 * deben llegar en menos de 1 s o queda una fecha inconsistente (datasheet). Una transacción a 100 kHz tarda ~1 ms.
 * Devuelve ESP_ERR_INVALID_STATE si dev es NULL y ESP_ERR_INVALID_ARG si n > 7.
 */
static esp_err_t escribir_registros(i2c_master_dev_handle_t dev, uint8_t reg,
                                    const uint8_t *datos, size_t n)
{
    if (dev == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (n > 7)
    { // buf solo reserva 1 (registro) + 7 (datos)
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t buf[8];
    buf[0] = reg;
    memcpy(&buf[1], datos, n);

    return i2c_master_transmit(dev, buf, n + 1, RTC_TIMEOUT_MS);
}

/** @brief Traduce un esp_err_t del driver I2C a rtc_estado_t (OK o NO_RESPONDE); registra un aviso si falló. */
static rtc_estado_t mapear_error(esp_err_t err)
{
    if (err == ESP_OK)
    {
        return RTC_OK;
    }
    ESP_LOGW(TAG, "Error I2C: %s", esp_err_to_name(err));
    return RTC_NO_RESPONDE;
}

/*=========================================Capa 3: FUNCIONES PUBLICAS============================================*/

esp_err_t rtc_ds3231_init(void)
{
    if (dev_handle != NULL)
    {
        return ESP_OK;
    }

    i2c_master_bus_handle_t bus = get_i2c_bus();

    if (bus == NULL) // i2c_bus_init no se ha llamado o falló
    {
        return ESP_ERR_INVALID_STATE;
    }

    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, // dirección de 7 bits (no 10)
        .device_address = RTC_DIR_I2C,         // 0x68
        .scl_speed_hz = RTC_VELOCIDAD_HZ,      // 100000
    };

    esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &dev_handle);

    if (err != ESP_OK)
    {
        dev_handle = NULL;
        ESP_LOGE(TAG, "Error añadiendo dispositivo en i2c: %s", esp_err_to_name(err));
        return err;
    }

    err = i2c_master_probe(bus, RTC_DIR_I2C, RTC_TIMEOUT_MS);

    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "RTC no responde en 0x68, ¿pull-up?: %s", esp_err_to_name(err));
        // Se quita el dispositivo para no dejar un handle que apunte a un chip ausente
        i2c_master_bus_rm_device(dev_handle);
        dev_handle = NULL;
        return err;
    }

    return ESP_OK;
}

rtc_estado_t rtc_leer_hora(struct tm *utc)
{
    uint8_t buffer[7];
    esp_err_t err = leer_registros(dev_handle, REG_SEGUNDOS, buffer, 7);

    if (err != ESP_OK)
    {
        return mapear_error(err);
    }

    return decodificar_hora(buffer, utc);
}

rtc_estado_t rtc_fijar_hora(const struct tm *utc)
{
    uint8_t datos[7];
    struct tm escrita = *utc; 
    escrita.tm_isdst = 0;

    rtc_estado_t est = codificar_hora(utc, datos);

    if (est != RTC_OK)
    {
        return est;
    }

    esp_err_t err = escribir_registros(dev_handle, REG_SEGUNDOS, datos, 7);

    if (err != ESP_OK)
    {
        return mapear_error(err);
    }

    struct tm leido;
    // Se relee para verificar; ±2 s de tolerancia por si cambia el segundo entre la escritura y la lectura
    est = rtc_leer_hora(&leido);

    if (est != RTC_OK)
    {
        return est;
    }

    if (llabs(mktime(&leido) - mktime(&escrita)) > 2)
    {
        return RTC_HORA_INCOHERENTE;
    }

    // OSF se borra solo tras verificar la hora, nunca antes; lectura-modificación-escritura del registro de estado
    uint8_t estado;
    err = leer_registros(dev_handle, REG_ESTADO, &estado, 1);

    if (err != ESP_OK)
    {
        return mapear_error(err);
    }

    estado = estado & ~BIT_OSF;

    err = escribir_registros(dev_handle, REG_ESTADO, &estado, 1);

    return mapear_error(err);
}

rtc_estado_t rtc_comprobar_oscilador(void)
{
    uint8_t estado;
    esp_err_t err = leer_registros(dev_handle, REG_ESTADO, &estado, 1);

    if (err != ESP_OK)
    {
        return mapear_error(err);
    }

    if (estado & BIT_OSF)
    {
        return RTC_OSCILADOR_PARADO;
    }

    return RTC_OK;
}

rtc_estado_t rtc_leer_control_estado(uint8_t *control, uint8_t *estado)
{
    uint8_t reg[2];

    if ((control == NULL) || (estado == NULL))
    {
        return RTC_NO_RESPONDE;
    }

    // 0Eh y 0Fh son contiguos: una sola transacción
    esp_err_t err = leer_registros(dev_handle, REG_CONTROL, reg, 2);

    if (err != ESP_OK)
    {
        return mapear_error(err);
    }

    *control = reg[0];
    *estado = reg[1];

    return RTC_OK;
}

rtc_estado_t rtc_leer_temperatura(float *grados_c)
{
    uint8_t bcd[2];
    esp_err_t err = leer_registros(dev_handle, REG_TEMP_MSB, bcd, 2);

    if (err != ESP_OK)
    {
        return mapear_error(err);
    }

    *grados_c = decodificar_temperatura((int8_t)bcd[0], bcd[1]);

    return RTC_OK;
}
