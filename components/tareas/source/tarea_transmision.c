/*
 * Tarea consumidora de cola_lecturas: envía cada ciclo por HTTP, lo guarda en la SD con el resultado (tx) y publica el estado.
 * Fuentes: SIM7000 Series AT Command Manual V1.06; especificación del servidor (documento de diseño del proyecto);
 * ESP-IDF v6.1, FreeRTOS (colas).
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#include "sensores.h"
#include "tarea_polling.h"
#include "estado_sistema.h"
#include "config_nodo.h"
#include "tarjeta_sd.h"
#include "registro_csv.h"
#include "modem.h"

#define MAX_ARCHIVOS_DIA 9 // archivos del dia: base, _2 .. _9 si cambian los sensores el mismo dia; criterio propio

// SIM de Claro Perú: valores de selectra.com.pe (terceros, no Claro); confirmados en uso (CNACT? devuelve una IP)
#define APN_RED "claro.pe"
#define USUARIO_APN "claro"
#define CLAVE_APN "claro"
// Servidor de pruebas del compañero: HTTP sin 's' (decidido: primero solo se comprueba la conexión)
#define DOMINIO_SERVIDOR "http://yago.alwaysdata.net"
#define RUTA_MEDIDAS "/plataforma/api/v1/medidas"

// #define TIMEOUT_GNSS_MS 90000 // Prueba para GNSS

// #define SERVIDOR_NTP "south-america.pool.ntp.org" // prueba para NTP en america del sur
// #define TIMEOUT_CNTP_MS 120000                    // prueba para NTP no da maximo lo ponemos nosotros

static const char *TAG = "transmision";

/**
 * @brief Traduccion del estado del driver de la SD al codigo de estado_sistema.
 */
static estado_codigo_t sd_a_estado(sd_estado_t est)
{
    switch (est)
    {
    case SD_EST_OK:
        return ESTADO_OK;

    case SD_EST_SIN_FORMATO:
        return ESTADO_SD_SIN_FORMATO;

    case SD_EST_LLENA:
        return ESTADO_SD_LLENA;

    case SD_EST_ERROR_ESCRITURA:
        return ESTADO_SD_ERROR_ESCRITURA;

    case SD_EST_AUSENTE:
    case SD_EST_ERROR_BUS: // solo con pines mal configurados; no hay codigo mejor
    default:
        return ESTADO_SD_AUSENTE;
    }
}

/**
 * @brief Imprime el ciclo en la consola con los nombres de columna.
 */
static void imprimir_ciclo(const ciclo *lectura)
{
    if (lectura->ts == 0)
    {
        printf("--- %s seq %lu @ SIN HORA (RTC no disponible) ---\n",
               config_nodo_id(), (unsigned long)lectura->seq);
    }
    else
    {
        time_t t = (time_t)lectura->ts;
        struct tm local;
        char fecha[20];

        // ts ya lleva sumado OFFSET_PAIS_S, asi que gmtime_r da la hora del pais
        gmtime_r(&t, &local);
        strftime(fecha, sizeof(fecha), "%Y-%m-%d %H:%M:%S", &local);
        printf("--- %s seq %lu @ %s ---\n", config_nodo_id(), (unsigned long)lectura->seq, fecha);
    }

    for (uint8_t i = 0; i < lectura->num_sensores; i++)
    {
        const sensor_descrip *d = &TABLA_SENSORES[i];
        const ciclo_sensor *s = &lectura->sensor[i];

        if (s->codigo == ESTADO_OK)
        {
            for (uint8_t j = 0; j < s->num_valores; j++)
            {
                printf("%s_%s = %.3f\n", d->nombre_fuente, d->nombres_datos[j], s->valores[j]);
            }
        }
        else
        {
            printf("%s: %s\n", d->nombre_fuente, estado_sistema_texto(s->codigo));
        }
    }
}

/**
 * @brief Escribe el ciclo en el archivo del dia. Si el archivo del dia ya
 *        existe con otra cabecera (han cambiado los sensores), usa _2, _3...
 *
 * Devuelve SD_EST_OK también si la línea CSV no cabe (el ciclo se pierde con un log) y
 * SD_EST_ERROR_ESCRITURA si ninguno de los MAX_ARCHIVOS_DIA archivos tiene la cabecera actual.
 */
static sd_estado_t guardar_en_sd(const ciclo *lectura, const char *cabecera, int tx)
{
    // static: fuera de la pila de la tarea
    static char linea[TAM_LINEA_CSV];
    static char base[TAM_RUTA_CSV];
    static char ruta[TAM_RUTA_CSV + 16]; // base + "_<n>.csv" (+16 cubre el peor caso)
    static char primera[TAM_CABECERA_CSV];

    if (registro_csv_linea(lectura, tx, linea, sizeof(linea)) == 0)
    {
        // No es un fallo de la tarjeta: queda en el log y como hueco en seq
        ESP_LOGE(TAG, "Linea CSV demasiado larga: ciclo %lu no guardado", (unsigned long)lectura->seq);
        return SD_EST_OK;
    }

    registro_csv_ruta(lectura->ts, base, sizeof(base));

    bool elegida = false;

    for (unsigned k = 1; k <= MAX_ARCHIVOS_DIA; k++)
    {
        if (k == 1)
        {
            snprintf(ruta, sizeof(ruta), "%s.csv", base);
        }
        else
        {
            snprintf(ruta, sizeof(ruta), "%s_%u.csv", base, k);
        }

        bool existe = false;
        sd_estado_t est = tarjeta_sd_primera_linea(ruta, primera, sizeof(primera), &existe);

        if (est != SD_EST_OK)
        {
            return est;
        }

        if ((existe == false) || (strcmp(primera, cabecera) == 0))
        {
            elegida = true;
            break;
        }
    }

    if (elegida == false)
    {
        ESP_LOGE(TAG, "Ninguno de los %d archivos del dia tiene la cabecera actual", MAX_ARCHIVOS_DIA);
        return SD_EST_ERROR_ESCRITURA;
    }

    sd_estado_t est = tarjeta_sd_escribir_linea(ruta, cabecera, linea);

    if (est == SD_EST_OK)
    {
        ESP_LOGI(TAG, "Ciclo %lu guardado en %s", (unsigned long)lectura->seq, ruta);
    }

    return est;
}

/**
 * @brief Publica el estado de la SD en estado_sistema. Con OK, valores[0] es el % libre.
 */
static void publicar_sd(sd_estado_t est, uint32_t ts)
{
    if (est != SD_EST_OK)
    {
        estado_sistema_publicar(FUENTE_SD, NULL, 0, 0, sd_a_estado(est));
        return;
    }

    uint8_t pct = 0;

    if (tarjeta_sd_espacio(&pct) == SD_EST_OK)
    {
        float valor = (float)pct;
        estado_sistema_publicar(FUENTE_SD, &valor, 1, ts, ESTADO_OK);
    }
    else
    {
        estado_sistema_publicar(FUENTE_SD, NULL, 0, ts, ESTADO_OK);
    }
}
/**
 * @brief Traduce un fallo de modem_http_post (estado + paso en que fallo) al tx propio.
 *
 * Timeout: en CONEXION es que no llega al servidor (5); en RESPUESTA, que el servidor
 * no contesta (7); en CONFIG y PETICION, que el modem no responde a un comando (4).
 * ERROR: en CONEXION (5); en el resto, error del modem (6).
 */
static int tx_desde_http(modem_estado_t est, modem_paso_t paso)
{
    if (est == MODEM_TIMEOUT)
    {
        switch (paso)
        {
        case MODEM_PASO_CONEXION:
            return TX_SIN_SERVIDOR;

        case MODEM_PASO_RESPUESTA:
            return TX_SIN_RESPUESTA;

        case MODEM_PASO_CONFIG:
        case MODEM_PASO_PETICION:
        default:
            return TX_MODEM_NO_CONTESTA;
        }
    }

    if (paso == MODEM_PASO_CONEXION)
    {
        return TX_SIN_SERVIDOR;
    }
    return TX_ERROR_MODEM;
}

/**
 * @brief Envia el ciclo al servidor por HTTP (un solo intento).
 *
 * @return Codigo HTTP del servidor (200, 400, 403, 409, 500...) o un TX_* (< 100).
 */
static int enviar_ciclo(const ciclo *lectura)
{
    static char json[TAM_TRAMA_JSON]; // static: fuera de la pila de la tarea
    size_t len;
    int codigo;
    modem_estado_t est;
    modem_paso_t paso;

    len = registro_csv_json(lectura, json, sizeof(json));
    if (len == 0)
    {
        ESP_LOGE(TAG, "trama JSON no cabe: ciclo %lu", (unsigned long)lectura->seq);
        return TX_TRAMA_NO_CABE;
    }

    // Se pregunta en cada ciclo: separa "sin SIM" (revisar SIM) de "sin red" (revisar antena)
    est = modem_sim_lista();
    if (est == MODEM_TIMEOUT)
    {
        return TX_MODEM_NO_CONTESTA;
    }
    if (est == MODEM_ERROR)
    {
        return TX_SIN_SIM;
    }

    // ¿Hay red? Se comprueba antes de activar datos: sin red, CNACT tardaria 85 s
    est = modem_esperar_registro(0);
    if (est == MODEM_TIMEOUT)
    {
        return TX_MODEM_NO_CONTESTA;
    }
    if (est == MODEM_ERROR)
    {
        return TX_SIN_RED;
    }

    if (modem_abrir_datos(APN_RED, USUARIO_APN, CLAVE_APN) != MODEM_OK)
    {
        return TX_SIN_CONEXION;
    }

    est = modem_http_post(DOMINIO_SERVIDOR, RUTA_MEDIDAS, json, len, &codigo, &paso);
    if (est == MODEM_OK)
    {
        ESP_LOGI(TAG, "seq %lu -> HTTP %d", (unsigned long)lectura->seq, codigo);
        return codigo;
    }
    return tx_desde_http(est, paso);
}

/**
 * @brief Enciende el modem, espera el registro y activa los datos. El detalle de cada paso
 *        lo deja en el log el propio componente del modem.
 */
static void bringup_modem(void)
{
    if (modem_encender() != MODEM_OK)
    {
        ESP_LOGE(TAG, "El modem no responde: se sigue sin transmision");
        return;
    }
    modem_gnss_apagar();
    // un reset a mitad de la espera del fix lo deja encendido: el módem no se reinicia con el ESP32. El retorno se ignora

    // PRUEBA PARA GNSS
    /**
    uint32_t utc = 0;
    uint32_t t0 = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    modem_gnss_encender();
    modem_estado_t est = modem_gnss_esperar_fix(TIMEOUT_GNSS_MS, &utc);
    ESP_LOGI(TAG, "GNSS: est %d en %lu ms, utc %lu", est, ((uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS)) - t0, utc);
    if (est == MODEM_OK)
    {
        time_t tt = (time_t)utc;
        struct tm f;
        char txt[20];
        gmtime_r(&tt, &f);
        strftime(txt, sizeof(txt), "%Y-%m-%d %H:%M:%S", &f);
        ESP_LOGI(TAG, "GNSS: UTC %s", txt);
    }
    modem_gnss_apagar();
    */

    if (modem_sim_lista() == MODEM_OK)
    {
        if (modem_esperar_registro(MODEM_TIMEOUT_REGISTRO_MS) == MODEM_OK)
        {
            if (modem_abrir_datos(APN_RED, USUARIO_APN, CLAVE_APN) == MODEM_OK) // la IP queda en el log
            {
                /**uint32_t utc;
                int codigo;
                uint32_t t0;
                modem_estado_t est;
                struct tm fecha;
                char texto[20];

                est = modem_portador_abrir(APN_RED, USUARIO_APN, CLAVE_APN);
                ESP_LOGI(TAG, "portador: est %d", est);
                if (est == MODEM_OK)
                {
                    t0 = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
                    est = modem_cntp(SERVIDOR_NTP, TIMEOUT_CNTP_MS, &utc, &codigo);
                    ESP_LOGI(TAG, "CNTP: est %d, codigo %d, en %lu ms, utc %lu", est, codigo,
                             (unsigned long)((uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) - t0),
                             (unsigned long)utc);
                    if (est == MODEM_OK)
                    {
                        time_t tt = (time_t)utc;
                        gmtime_r(&tt, &fecha);
                        strftime(texto, sizeof(texto), "%Y-%m-%d %H:%M:%S", &fecha);
                        ESP_LOGI(TAG, "CNTP: UTC %s", texto);
                    }
                }
                modem_portador_cerrar(); // siempre, aunque falle lo anterior*/
            }
        }
    }
    else
    {
        ESP_LOGW(TAG, "SIM no lista: no se intenta el registro");
    }

    // El modem se queda encendido entre ciclos (decision del 01/10/2026, para las pruebas)
}

void tarea_transmision(void *parametros)
{
    // static: fuera de la pila de la tarea
    static char cabecera[TAM_CABECERA_CSV];
    ciclo lectura;
    modem_red_t red;
    red_movil_t red_pub;

    // TABLA_SENSORES es const: la cabecera se genera una sola vez
    if (registro_csv_cabecera(cabecera, sizeof(cabecera)) == 0)
    {
        ESP_LOGE(TAG, "Cabecera CSV demasiado larga: no se guardara nada en la SD");
    }

    bringup_modem();
    while (1)
    {
        xQueueReceive(get_cola_lecturas(), &lectura, portMAX_DELAY);

        imprimir_ciclo(&lectura);

        int tx = enviar_ciclo(&lectura);

        // Antes de la SD (~2 s): la pantalla no tiene por que esperarla.
        // Si modem_leer_red da TIMEOUT, red queda parcial y se publica igual
        modem_leer_red(&red);
        snprintf(red_pub.operador, sizeof(red_pub.operador), "%s", red.operador);
        snprintf(red_pub.tecnologia, sizeof(red_pub.tecnologia), "%s", red.tecnologia);
        red_pub.dbm = red.dbm;
        snprintf(red_pub.ip, sizeof(red_pub.ip), "%s", red.ip);
        estado_sistema_publicar_red(&red_pub, tx, (tx == TX_HTTP_OK), lectura.ts);

        if (cabecera[0] != '\0')
        {
            sd_estado_t est = guardar_en_sd(&lectura, cabecera, tx);
            publicar_sd(est, lectura.ts);
        }
        // Andamiaje: pila libre mínima en bytes (ESP-IDF); criterio: subir la pila si baja de 1 KB
        ESP_LOGI(TAG, "pila libre minima: %u B", (unsigned)uxTaskGetStackHighWaterMark(NULL));
    }
}