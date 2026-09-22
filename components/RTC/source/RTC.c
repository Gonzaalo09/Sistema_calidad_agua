/*
 * rtc_ds3231.c
 *
 * Driver del RTC DS3231 (registros de tiempo, oscilador y temperatura).
 *
 * Fuente: DS3231 datasheet, Analog Devices / Maxim.
 * https://www.analog.com/media/en/technical-documentation/data-sheets/ds3231.pdf
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

#include "driver/i2c_master.h"
#include "esp_log.h"

#include "RTC.h"
#include "I2C_bus.h"

static const char *TAG = "RTC";

static const uint16_t RTC_DIR_I2C = 0x68; ///< dirección de 7 bits, datasheet pág. 17
static const uint32_t RTC_VELOCIDAD_HZ = 100000;
static const int RTC_TIMEOUT_MS = 100;

static const uint8_t REG_SEGUNDOS = 0x00; ///< Registro donde comienzan los datos, empiezan con los segundos
static const uint8_t REG_CONTROL = 0x0E;  ///< Registro de control  
static const uint8_t REG_ESTADO = 0x0F;  ///< REG_ESTADO del oscilador (OSF)
static const uint8_t REG_TEMP_MSB = 0x11;

static const uint8_t BIT_OSF = (1U << 7);      ///< Bit donde esta el que te dice si está parado o no
static const uint8_t BIT_MODO_12H = (1U << 6); ///< Registro de horas, modo 12 o 24h

static const uint8_t MASK_SEG_MIN = 0x7F;  ///< Mascara para leer los segundos y los minutos del bit 0 al 6
static const uint8_t MASK_HORA_24H = 0x3F; ///< Mascara para leer las horas del bit 0 al 5
static const uint8_t MASK_DIA = 0x3F;      ///< Mascara para leer el día del bit 0 al 5
static const uint8_t MASK_MES = 0x1F;      ///< Mascara para leer el mes del bit 0 al 4

static i2c_master_dev_handle_t dev_handle = NULL;

/*=========================================Funciones privadas============================================*/

/**
 * @brief Convierte un byte en BCD a decimal
 * @param val Byte en BCD, p. ej. 0x25
 * @return uint8_t Valor en decimal, p. ej. 25
 */
static uint8_t bcd_a_dec(uint8_t val)
{
    return (val >> 4) * 10 + (val & 0x0F);
}

/**
 * @brief Convierte un byte decimal a BCD, para escribirlo en los registros del RTC
 * @param val Valor en decimal (0-99)
 * @return uint8_t Byte en BCD
 */
static uint8_t dec_a_bcd(uint8_t val)
{
    return ((val / 10) << 4) | (val % 10);
}

/**
 * @brief Comprueba si un año es bisiesto
 * @param anio Año completo
 * @return uint8_t 1 si es bisiesto, 0 si no
 */
static uint8_t es_bisiesto(uint16_t anio)
{
    return (anio % 4 == 0) && ((anio % 100 != 0) || (anio % 400 == 0));
}

/**
 * @brief Devuelve cuántos días tiene un mes, teniendo en cuenta si el año es bisiesto
 * @param anio Año completo
 * @param mes Mes 1..12
 * @return uint8_t Días del mes (0 si el mes no es válido)
 */
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
 * @brief Devuelve 1..7 (1 = domingo ... 7 = sábado)
 *
 * Precondiciones: anio = año completo (2026, no tm_year), mes = 1..12 (ya validado)
 *
 * @param anio Año completo (2026, no tm_year)
 * @param mes Mes 1..12 (ya validado)
 * @param dia Día del mes
 * @return uint8_t Día de la semana, 1..7 (1 = domingo ... 7 = sábado)
 */
static uint8_t dia_semana(int anio, int mes, int dia)
{
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};

    if (mes < 3)
    {
        anio--; // enero y febrero cuentan como del año anterior
    }
    int dow = (anio + anio / 4 - anio / 100 + anio / 400 + t[mes - 1] + dia) % 7; // 0 = domingo
    return (uint8_t)(dow + 1);
}

/**
 * @brief Sirve para después saber los segundos, utiliza en fijar hora, para sincronización
 *
 * @param anio Año completo
 * @param mes Mes 1..12
 * @param dia Día del mes
 * @return int64_t Días completos transcurridos desde el 1970-01-01
 */
static int64_t dias_desde_1970(uint16_t anio, uint8_t mes, uint8_t dia)
{
    int64_t dias = 0;

    for (uint16_t a = 1970; a < anio; a++)
    {
        dias += es_bisiesto(a) ? 366 : 365;
    }
    for (uint8_t m = 1; m < mes; m++)
    {
        dias += dias_del_mes(anio, m);
    }
    dias += dia - 1;

    return dias;
}

/**
 * @brief Sirve para luego poder restar tiempos de toma de muestras, devueve
 * en unix ya que en UTC no se puede restar bien.
 *
 * @param t Hora en UTC (struct tm)
 * @return int64_t Segundos transcurridos desde el 1970-01-01 00:00:00 UTC (timestamp Unix)
 */
static int64_t tm_a_segundos_utc(const struct tm *t)
{
    uint16_t anio = t->tm_year + 1900;
    uint8_t mes = t->tm_mon + 1;
    int64_t dias = dias_desde_1970(anio, mes, t->tm_mday);
    return dias * 86400 + t->tm_hour * 3600 + t->tm_min * 60 + t->tm_sec;
}

/**
 * @brief Decodificas los registros de BCD a decimal
 *
 * @param bcd Registros crudos leídos del RTC (7 bytes: seg, min, hora, día semana, día, mes, año)
 * @param utc Struct tm donde se escribe la hora ya decodificada
 * @return rtc_estado_t RTC_OK si los datos eran coherentes, RTC_HORA_INCOHERENTE si no
 */
static rtc_estado_t decodificar_hora(const uint8_t bcd[7], struct tm *utc)
{

    if (bcd[2] & BIT_MODO_12H)
    {
        return RTC_HORA_INCOHERENTE;
    }

    uint8_t seg = bcd_a_dec(bcd[0] & MASK_SEG_MIN);
    uint8_t min = bcd_a_dec(bcd[1] & MASK_SEG_MIN);
    // bit 3 es el dia de la semana no se usa al leer(ya tenemos la función que nos lo da)
    uint8_t hora = bcd_a_dec(bcd[2] & MASK_HORA_24H);
    uint8_t dia = bcd_a_dec(bcd[4] & MASK_DIA);
    uint8_t mes = bcd_a_dec(bcd[5] & MASK_MES); // 1..12, tal cual lo entrega el chip
    uint16_t anio = 2000 + bcd_a_dec(bcd[6]);   // año completo, p.ej. 2026
    // bit 7 siglo se ignora

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

    return RTC_OK;
}

/**
 * @brief Convierte una hora en struct tm a los registros BCD del RTC (el inverso de decodificar_hora)
 * @param utc Hora en UTC a codificar
 * @param bcd Buffer de 7 bytes donde se escriben los registros en BCD
 * @return rtc_estado_t RTC_OK si la fecha era válida, RTC_HORA_INCOHERENTE si no
 */
static rtc_estado_t codificar_hora(const struct tm *utc, uint8_t bcd[7])
{
    uint8_t seg = utc->tm_sec;
    uint8_t min = utc->tm_min;
    uint8_t hora = utc->tm_hour;
    uint8_t dia = utc->tm_mday;
    uint8_t mes = utc->tm_mon + 1;
    uint16_t anio = utc->tm_year + 1900;

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
    bcd[3] = dec_a_bcd(dia_semana(anio, mes, dia));
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
 *
 * @param msb Byte alto (registro 11h), con el signo en el bit 7
 * @param lsb Byte bajo (registro 12h), solo se usan sus 2 bits altos
 * @return float Temperatura en grados Celsius
 */
static float decodificar_temperatura(int8_t msb, uint8_t lsb)
{
    return (float)msb + (lsb >> 6) * 0.25f;
}
/*=========================================Capa 2: ACCESO AL CHIP============================================*/

/**
 * @brief Lee n bytes desde el registro reg, en una sola transacción con repeated
 * start: START, dirección+W, [reg], REPEATED START, dirección+R, n bytes, STOP.
 *
 * El primer paso ("word address") posiciona el puntero interno del chip
 * Al ser una sola transacción, los buffers secundarios del chip quedan
 * sincronizados desde el START inicial, así que no hay riesgo de leer una
 * mezcla de valores antiguos y nuevos (rollover a mitad de lectura).
 *
 * @param dev Handle I2C del dispositivo
 * @param reg Registro desde el que empezar a leer
 * @param buffer Buffer donde se guardan los n bytes leídos
 * @param n Número de bytes a leer
 * @return esp_err_t ESP_OK si la transacción I2C fue bien
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
 * @brief Escribe n bytes a partir del registro reg, en una sola transacción:
 * START, dirección+W, [reg][datos[0]]...[datos[n-1]], STOP.
 *
 * @verbatim
 *   buf[0]      = reg          (posiciona el puntero, igual que al leer)
 *   buf[1..n]   = datos[0..n-1]
 * @endverbatim
 *
 * Se escribe en una sola transacción porque en cuanto se escribe el registro
 * de segundos, el chip reinicia su cuenta
 * interna, y el resto de registros de hora/fecha deben llegar en menos de
 * 1 segundo o hay riesgo de una fecha inconsistente a medio escribir.
 * Una única transacción I2C tarda del orden de 1 ms a 100 kHz, muy por
 * debajo del límite de 1 s.
 *
 * @param dev Handle I2C del dispositivo
 * @param reg Registro a partir del cual se empieza a escribir
 * @param datos Datos a escribir
 * @param n Número de bytes a escribir (máximo 7)
 * @return esp_err_t ESP_OK si la transacción I2C fue bien
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

/**
 * @brief Traduce un esp_err_t del driver I2C a nuestro rtc_estado_t, dejando un log si algo falló
 * @param err Código de error devuelto por el driver I2C
 * @return rtc_estado_t RTC_OK si err es ESP_OK, RTC_NO_RESPONDE si no
 */
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
/**
 * @brief Inicializamos el RTC, mirando paso por paso si está todo bien creado, esto es el bus I2C, si responde el RTC etc
 *
 * @return esp_err_t ESP_OK si el RTC quedó listo para usarse
 */
esp_err_t rtc_init(void)
{
    if (dev_handle != NULL)
    {
        return ESP_OK;
    }

    i2c_master_bus_handle_t bus = get_i2c_bus();

    // Miramos si se ha creado el bus de i2c
    if (bus == NULL)
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
        i2c_master_bus_rm_device(dev_handle);
        dev_handle = NULL;
        return err;
    }
    
    return ESP_OK; 
}

/**
 * @brief En esta función leemos los registros y lo metemos en utc
 *
 * @param utc Struct tm donde se escribe la hora leída del RTC
 * @return rtc_estado_t RTC_OK si se leyó correctamente
 */
rtc_estado_t rtc_leer_hora(struct tm *utc)
{
    uint8_t buffer[7];
    //No hace falta que sea *buffer porque es un array, que por C se "degrada" y se convierte en un pointer
    esp_err_t err = leer_registros(dev_handle, REG_SEGUNDOS, buffer, 7);

    if (err != ESP_OK)
    {
        return mapear_error(err);
    }

    return decodificar_hora(buffer, utc);
}

/**
 * @brief Nos sirve por si se para el RTC, o para cuando implementemos la sincronización
 *
 * @param utc Hora en UTC que se quiere fijar en el RTC
 * @return rtc_estado_t RTC_OK si se fijó y verificó correctamente
 */
rtc_estado_t rtc_fijar_hora(const struct tm *utc)
{
    uint8_t datos[7];
    
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
    //Verificamos lo escrito 
    est = rtc_leer_hora(&leido);
    
    if (est != RTC_OK)
    {
        return est; 
    }

    if (llabs(tm_a_segundos_utc(&leido) - tm_a_segundos_utc(utc)) > 2)
    {
        return RTC_HORA_INCOHERENTE;
    }

    //Ahora borramos OSF(registro del oscilador), nunca antes de fijar la hora
    uint8_t estado; 
    err = leer_registros(dev_handle, REG_ESTADO, &estado, 1);

    if (err != ESP_OK) {
        return mapear_error(err);
    }

    estado = estado & ~BIT_OSF; 

    err = escribir_registros(dev_handle, REG_ESTADO, &estado, 1);

    return mapear_error(err);
}

/**
 * @brief Comprobamos que el oscilador no se ha parado para que nos podamos fiar de la hora que leemos
 *
 * @return rtc_estado_t RTC_OK si el oscilador funciona, RTC_OSCILADOR_PARADO si el flag OSF está activo
 */
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

/**
 * @brief Leemos la temperatura
 *
 * @param grados_c Donde se escribe la temperatura leída, en grados Celsius
 * @return rtc_estado_t RTC_OK si se leyó correctamente
 */
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

