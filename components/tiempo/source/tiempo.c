#include <stdint.h>
#include <time.h>
#include <stdlib.h>
#include <stdbool.h>

#include "esp_log.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "tiempo.h"
#include "RTC.h"
#include "config_nodo.h"
#include "modem.h"
#include "esp_timer.h"

#define VALIDEZ_S 28571                           // s; 0,1 s / 3,5 ppm (ecuación 6.2 de la tesis): plazo desde la última sincronización
#define PERIODO_GNSS_S 172800                     // s; 2 días entre intentos de GNSS
#define PERIODO_NTP_S 14400                       // s; 4 h desde la última sincronización correcta
#define TIMEOUT_GNSS_MS 90000                     // ms; espera del fix (medido: 70,55 s en interior)
#define TIMEOUT_CNTP_MS 120000                    // ms; andamiaje: el tiempo de un CNTP fallido está sin medir
#define UMBRAL_NTP_MS 1700                        // ms; con CNTP solo se escribe el DS3231 por encima de esto
#define SERVIDOR_NTP "south-america.pool.ntp.org" // depende de la región: README, "Desplegar en otro país"

#define PERIODO_CICLO_S 600 // Periodo en el que se despertará 
#define MARGEN_DESPERTAR_S 15  // Margen que le damos por la deriva del reloj del esp
#define SONDEO_MARCA_MS 10 // ms entre lecturas del DS3231 esperando la marca; provisional: basta con <= 1 s de exactitud y cada lectura cuesta ~1 ms

// Para prueba de casos distintos: 0 = normal; 1 = ciclo largo; 2 = despertar tarde; 3 = hora no valida. La usa la copia de prueba comentada de tiempo_segundos_hasta_despertar
// #define PRUEBA_CASO 2

static RTC_DATA_ATTR uint32_t marca_objetivo; // UTC en s; 0 = sin rejilla, en RTC slow porque tiene que sobrevivir el deep_sleep. Guarda siempre la marca sin el margen 

static const char *TAG = "Tiempo";

/**
 * @brief Lee el DS3231 y dice si su hora es fiable, sin mirar la antigüedad de la sincronización.
 *
 * TIEMPO_OK = hora fiable (*seg en segundos UTC); TIEMPO_NO_VALIDA = incoherente, OSF = 1 o sin sincronizar
 * nunca; TIEMPO_SIN_RTC = no contesta. *seg vale 0 salvo con TIEMPO_OK.
 */
static tiempo_estado_t leer_rtc(uint32_t *seg)
{
    struct tm utc = {0}; // mktime lee campos que el driver no rellena
    rtc_estado_t est;

    *seg = 0;

    est = rtc_leer_hora(&utc);
    if (est == RTC_NO_RESPONDE)
    {
        return TIEMPO_SIN_RTC;
    }
    if (est != RTC_OK)
    {
        return TIEMPO_NO_VALIDA;
    }

    est = rtc_comprobar_oscilador();
    if (est == RTC_NO_RESPONDE)
    {
        return TIEMPO_SIN_RTC;
    }
    if (est == RTC_OSCILADOR_PARADO)
    {
        return TIEMPO_NO_VALIDA;
    }

    uint32_t s = (uint32_t)mktime(&utc);
    uint32_t t_sync = config_nodo_t_sync();

    // Una hora anterior a la última sincronización es imposible si el RTC contase bien
    if ((t_sync == 0) || (s < t_sync))
    {
        return TIEMPO_NO_VALIDA;
    }

    *seg = s;
    return TIEMPO_OK;
}

tiempo_estado_t tiempo_ts_pais(uint32_t *ts_pais)
{
    uint32_t seg;
    tiempo_estado_t est = leer_rtc(&seg);

    *ts_pais = 0;

    if (est != TIEMPO_OK)
    {
        return est;
    }

    *ts_pais = (uint32_t)(seg + OFFSET_PAIS_S);

    if (seg - config_nodo_t_sync() >= VALIDEZ_S)
    {
        return TIEMPO_CADUCADA;
    }

    return TIEMPO_OK;
}

// Protocolo de sincronización, primero se intenta GNSS, si no se puede o no se hace pasa a mirar si hacer NTP, si entra, cambia el t_sync a ese intento aunque no se cambie la hora dentro del
// DS3231, tiene que ser una diferencia mayor de 1.7s
void tiempo_sincronizar(const char *apn, const char *usuario, const char *clave)
{

    uint32_t ahora, t_gnss, t_sync;
    uint32_t utc_fuente = 0;
    tiempo_estado_t est;
    bool fiable;
    bool sincronizado = false;
    bool intento_gnss = false;

    // Hora local, sirve para decidir si toca
    est = leer_rtc(&ahora);
    if (est == TIEMPO_SIN_RTC)
    {
        ESP_LOGW(TAG, "No hay RTC");
        return;
    }

    fiable = (est == TIEMPO_OK); // Pone a TRUE si es TIEMPO_OK

    t_sync = config_nodo_t_sync();
    t_gnss = config_nodo_t_gnss();

    // GNSS
    if ((t_gnss == 0) || (ahora - t_gnss >= PERIODO_GNSS_S))
    {
        intento_gnss = true;
        int64_t t0 = esp_timer_get_time();
        modem_estado_t est_gnss = modem_gnss_encender();

        if (est_gnss == MODEM_OK)
        {
            est_gnss = modem_gnss_esperar_fix(TIMEOUT_GNSS_MS, &utc_fuente);
            if (est_gnss == MODEM_OK)
            {
                // Lectura previa solo para el log de la diferencia; ~1 ms, no retrasa la escritura de forma apreciable
                struct tm tm_rtc = {0};
                bool rtc_leido = (rtc_leer_hora(&tm_rtc) == RTC_OK);
                uint32_t rtc_s = (uint32_t)mktime(&tm_rtc);

                time_t t = (time_t)utc_fuente;
                struct tm tm_fuente;
                gmtime_r(&t, &tm_fuente);

                if (rtc_fijar_hora(&tm_fuente) == RTC_OK)
                {
                    sincronizado = true;
                }
                else
                {
                    ESP_LOGE(TAG, "GNSS: no se pudo escribir el DS3231");
                }

                if (rtc_leido)
                {
                    ESP_LOGI(TAG, "GNSS: DS3231 - GNSS = %ld s", (long)(int32_t)(rtc_s - utc_fuente));
                }
            }
        }
        modem_gnss_apagar();
        ESP_LOGI(TAG, "GNSS: est  %d, %lld ms ", est_gnss, (long long)((esp_timer_get_time() - t0) / 1000));
    }

    if (!sincronizado && (t_sync == 0 || ahora - t_sync >= PERIODO_NTP_S))
    {
        int codigo = 0;
        modem_estado_t est_cntp = modem_portador_abrir(apn, usuario, clave);
        if (est_cntp == MODEM_OK)
        {
            est_cntp = modem_cntp(SERVIDOR_NTP, TIMEOUT_CNTP_MS, &utc_fuente, &codigo);
            if (est_cntp == MODEM_OK)
            {
                struct tm tm_rtc = {0};
                if (rtc_leer_hora(&tm_rtc) != RTC_OK)
                {
                    fiable = false;
                }
                time_t rtc_s = mktime(&tm_rtc);
                int32_t diff = (int32_t)(rtc_s - utc_fuente);

                if (fiable == false || (abs(diff) * 1000 > UMBRAL_NTP_MS))
                {
                    time_t t = utc_fuente;
                    struct tm tm_fuente;
                    gmtime_r(&t, &tm_fuente);
                    if (rtc_fijar_hora(&tm_fuente) == RTC_OK)
                    {
                        sincronizado = true;
                        ESP_LOGI(TAG, "CNTP: DS3231 - CNTP = %li", diff);
                    }
                    else
                    {
                        ESP_LOGE(TAG, "CNTP: No se pudo escribir el DS3231");
                    }
                }
                else
                {
                    sincronizado = true; // CNTP confirma el DS3231 sin escribirlo, y t_sync pasa a ser la actual
                    ESP_LOGI(TAG, "CNTP: DS3231 - CNTP = %li s, no se escribe", diff);
                }
            }
        }
        if (est_cntp != MODEM_OK)
        {
            ESP_LOGW(TAG, "CNTP fallido: %i", codigo);
        }
        modem_portador_cerrar();
    }

    if (sincronizado)
    {
        config_nodo_guardar_t_sync(utc_fuente);
    }
    if (intento_gnss)
    {
        if (sincronizado)
        {
            config_nodo_guardar_t_gnss(utc_fuente);
        }
        else if (est == TIEMPO_OK)
        {
            config_nodo_guardar_t_gnss(ahora);
        }
        else
        {
            ESP_LOGW(TAG, "sin hora con la que anotar t_gnss");
        }
    }
}

void tiempo_esperar_marca(void)
{
    uint32_t ahora;

    if (marca_objetivo == 0)
    {
        ESP_LOGI(TAG, "Sin marca: ciclo inmediato");
        return;
    }

    if (leer_rtc(&ahora) != TIEMPO_OK)
    {
        ESP_LOGW(TAG, "hora no válida al despertar: se mide ya");
        return;
    }

    // Despertar tarde: la deriva superó el margen; se mide ya, sin saltar a la siguiente marca
    if (ahora >= marca_objetivo)
    {
        ESP_LOGW(TAG, "despertar tarde: %lu s despues de la marca", ahora - marca_objetivo);
        return;
    }

    ESP_LOGI(TAG, "marca %lu, faltan %lu s", marca_objetivo, marca_objetivo - ahora);
    while (ahora < marca_objetivo)
    {
        vTaskDelay(pdMS_TO_TICKS(SONDEO_MARCA_MS));
        if (leer_rtc(&ahora) != TIEMPO_OK)
        {
            ESP_LOGW(TAG, "error leyendo la hora durante la espera: se mide ya");
            break;
        }
    }
    ESP_LOGI(TAG, "marca alcanzada a los %lld ms del arranque", esp_timer_get_time() / 1000);
}

// ESTO ES EL AUTENTICO EL QUE FUNCIONA 
uint32_t tiempo_segundos_hasta_despertar(void)
{
    uint32_t ahora;
    uint32_t marca;

    if (leer_rtc(&ahora) != TIEMPO_OK)
    {
        // Solo vale para el gateway: un nodo sin SIM despertaría siempre fuera de la ventana de su padre
        marca_objetivo = 0;
        ESP_LOGW(TAG, "hora no valida: sueño de un periodo, sin rejilla");
        return PERIODO_CICLO_S;
    }

    // Se suma el margen antes de dividir: una marca a menos de MARGEN_DESPERTAR_S de ahora ya no da tiempo a despertar antes de ella
    marca = ((ahora + MARGEN_DESPERTAR_S) / PERIODO_CICLO_S + 1) * PERIODO_CICLO_S;

    if (marca_objetivo != 0 && (marca > (marca_objetivo + PERIODO_CICLO_S)))
    {
        ESP_LOGW(TAG, "ciclo largo: se saltan %lu marcas", (marca - marca_objetivo) / PERIODO_CICLO_S - 1);       
    }

    marca_objetivo = marca; 
    ESP_LOGI(TAG, "proxima marca %lu, duermo %lu s", marca, marca - MARGEN_DESPERTAR_S - ahora); 
    return marca - MARGEN_DESPERTAR_S - ahora; 
}


// ESTE ES DE PRUEBA 
/** uint32_t tiempo_segundos_hasta_despertar(void)
{
    uint32_t ahora;
    uint32_t marca;

    // ANDAMIAJE paso 6, caso 1: alarga un periodo el ciclo, solo tras un despertar (con marca_objetivo = 0 el log del salto no sale)
    if ((PRUEBA_CASO == 1) && (marca_objetivo != 0))
    {
        ESP_LOGW(TAG, "PRUEBA: ciclo alargado %d s", PERIODO_CICLO_S);
        vTaskDelay(pdMS_TO_TICKS(PERIODO_CICLO_S * 1000));
    }

    // ANDAMIAJE paso 6, caso 3: (PRUEBA_CASO == 3) simula que leer_rtc falla
    if ((PRUEBA_CASO == 3) || (leer_rtc(&ahora) != TIEMPO_OK))
    {
        marca_objetivo = 0;
        ESP_LOGW(TAG, "hora no valida: sueño de un periodo, sin rejilla");
        return PERIODO_CICLO_S;
    }

    marca = ((ahora + MARGEN_DESPERTAR_S) / PERIODO_CICLO_S + 1) * PERIODO_CICLO_S;

    if (marca_objetivo != 0 && (marca > (marca_objetivo + PERIODO_CICLO_S)))
    {
        ESP_LOGW(TAG, "ciclo largo: se saltan %lu marcas", (marca - marca_objetivo) / PERIODO_CICLO_S - 1);
    }

    marca_objetivo = marca;
    ESP_LOGI(TAG, "proxima marca %lu, duermo %lu s", marca, marca - MARGEN_DESPERTAR_S - ahora);

    // ANDAMIAJE paso 6, caso 2: duerme dos margenes de mas -> despierta ~15 s despues de la marca
    if (PRUEBA_CASO == 2)
    {
        ESP_LOGW(TAG, "PRUEBA: se duermen %d s de mas", 2 * MARGEN_DESPERTAR_S);
        return marca + MARGEN_DESPERTAR_S - ahora;
    }

    return marca - MARGEN_DESPERTAR_S - ahora;
} 
*/