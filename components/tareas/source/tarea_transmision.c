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
#define TAM_RESP_MODEM 256 // bytes; criterio: la respuesta mas larga de 5.1 (AT+CGMR) son decenas de bytes

// SIM de Claro Perú: valores de selectra.com.pe (terceros, no Claro); confirmados en uso (CNACT? devuelve una IP)
#define APN_RED "claro.pe"
#define USUARIO_APN "claro"
#define CLAVE_APN "claro"
// Servidor de pruebas del compañero: HTTP sin 's' (decidido: primero solo se comprueba la conexión)
#define DOMINIO_SERVIDOR "http://yago.alwaysdata.net"
#define RUTA_MEDIDAS "/servidor_3/api/v1/medidas"

static const char *TAG = "transmision";

/**
 * @brief Un comando del bring-up con su timeout.
 */
typedef struct
{
    const char *cmd;
    uint32_t timeout_ms;
} comando_bringup;

// Andamiaje del paso 5.1: todos de lectura salvo ATE0 y CMEE, que son NO_SAVE
// (manual 1.7.1) y no tocan la NVRAM del modem.
// Los timeouts de CFUN y CPIN son el "Max Response Time" del AT Command Manual
// V1.05; los 2000 del resto son provisionales (su campo es "-" en el manual) y
// se ajustan con lo que se mida: decision pendiente 5.
// RESPUESTAS: respuestas_at.txt
static const comando_bringup BRINGUP[] = {
    {"ATE0", 2000},      // Apaga el eco de los comandos
    {"AT+CMEE=1", 2000}, // Cambia el formato de los mensajes de error a errores numericos
    //{"AT+CGMR", 2000},      // version de firmware
    //{"AT+CGSN", 2000},      // IMEI número de serie único global de tu módulo de radio
    //{"AT+IPR?", 2000},      // velocidad de baudios en el que esta: 0 = autobaud
    {"AT+CSCLK?", 2000},    // 1 = el modem puede dormirse por DTR
    {"AT+CFUN?", 10000},    // 1 = RF activa (manual 3.2.19)
    {"AT+CNMP?", 2000},     // tecnologias permitidas (manual 5.2.17)
    {"AT+CMNB?", 2000},     // Cat-M1 / NB-IoT (manual 5.2.18)
    {"AT+CBANDCFG?", 2000}, // bandas Cat-M habilitadas (manual 5.2.29)
    //{"AT+CPIN?", 5000},     // estado de la SIM (manual 3.2.11)
    {"AT+CCID", 2000},
    //{"AT+CPSI?", 2000},
};
#define NUM_BRINGUP (sizeof(BRINGUP) / sizeof(BRINGUP[0]))

// Se leen una vez registrado
static const comando_bringup RED[] = {
    {"AT+CPSI?", MODEM_TIMEOUT_LECTURA_MS}, // tecnologia, MCC-MNC, banda, RSRP/RSRQ/SNR (manual 5.2.21)
    {"AT+COPS?", MODEM_TIMEOUT_LECTURA_MS}, // operador (manual 3.2.9): list of quadruplets, each representing an operator present inthe network. 
    {"AT+CSQ", MODEM_TIMEOUT_LECTURA_MS},   // rssi 0-31, 99 = desconocido (manual 3.2.16)
};
#define NUM_RED (sizeof(RED) / sizeof(RED[0]))

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
static int leer_stat(const char *cmd, const char *prefijo, char *resp, size_t tam)
{
    const char *p;
    int n;
    int stat;

    if (modem_comando(cmd, resp, tam, MODEM_TIMEOUT_LECTURA_MS) != MODEM_OK)
    {
        return STAT_SIN_LEER;
    }

    // Prefijo en el caso de ejemplo: \r\n+CEREG:
    // Busca donde empieza el prefijo dentro de la respuesta del modem, porque es desde donde quieres leer
    p = strstr(resp, prefijo);
    // resp = "\r\n+CEREG: 0,2\r\n\r\nOK\r\n
    if (p == NULL)
    {
        return STAT_SIN_LEER;
    }

    if (sscanf(p + strlen(prefijo), "%d,%d", &n, &stat) != 2) // %d se salta el espacio de "+CEREG: "
    {
        return STAT_SIN_LEER;
    }
    return stat;
}

static bool esperar_registro(char *resp, size_t tam)
{
    uint32_t t0 = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    uint32_t transcurrido = 0;
    int state_lte;
    int state_gsm;
    int ultimo_lte = STAT_SIN_LEER;
    int ultimo_gsm = STAT_SIN_LEER;
    bool registrado = false;

    // Para hacer que no te salgan todos los mensajes
    esp_log_level_set("modem", ESP_LOG_WARN);

    while (transcurrido < MODEM_TIMEOUT_REGISTRO_MS)
    {
        state_lte = leer_stat("AT+CEREG?", "+CEREG:", resp, tam);  // registro LTE: Cat-M o NB-IoT
        state_gsm = leer_stat("AT+CGREG?", "+CGREG:", resp, tam); // registro de paquetes en GSM (CNMP=2)
        transcurrido = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) - t0;

        if ((state_lte != ultimo_lte) || (state_gsm != ultimo_gsm))
        {
            ESP_LOGI(TAG, "%lu ms: CEREG %d, CGREG %d", transcurrido, state_lte, state_gsm);
            ultimo_lte = state_lte;
            ultimo_gsm = state_gsm;
        }

        if ((state_lte == STAT_REG_CASA) || (state_lte == STAT_REG_ROAMING) || (state_gsm == STAT_REG_CASA) || (state_gsm == STAT_REG_ROAMING))
        {
            registrado = true;
            break;
        }

        if ((state_lte == STAT_DENEGADO) && (state_gsm == STAT_DENEGADO))
        {
            break;
            // Si se niegan las dos no hay nad que hacer se sale del bucle
        }

        vTaskDelay(pdMS_TO_TICKS(MODEM_SONDEO_REGISTRO_MS));
    }

    esp_log_level_set("modem", ESP_LOG_INFO); // un unico punto de salida para restaurarlo siempre

    if (registrado)
    {
        ESP_LOGI(TAG, "registrado a los %lu ms (CEREG %d, CGREG %d)", transcurrido, state_lte, state_gsm);
    }
    else
    {
        ESP_LOGW(TAG, "sin red tras %lu ms (CEREG %d, CGREG %d)", transcurrido, state_lte, state_gsm);
    }
    return registrado;
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
    static char resp[TAM_RESP_MODEM];
    size_t len;
    int lte;
    int gsm;
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
    lte = leer_stat("AT+CEREG?", "+CEREG:", resp, sizeof(resp));
    gsm = leer_stat("AT+CGREG?", "+CGREG:", resp, sizeof(resp));
    if ((lte == STAT_SIN_LEER) && (gsm == STAT_SIN_LEER))
    {
        return TX_MODEM_NO_CONTESTA;
    }
    if ((lte != STAT_REG_CASA) && (lte != STAT_REG_ROAMING) &&
        (gsm != STAT_REG_CASA) && (gsm != STAT_REG_ROAMING))
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
 * @brief Andamiaje del paso 5.1: enciende el modem, recorre los comandos de
 *        lectura y lo apaga. El detalle de cada respuesta lo deja en el log el
 *        propio componente del modem; aqui solo queda el veredicto.
 */
static void bringup_modem(void)
{
    static char resp[TAM_RESP_MODEM]; // static: fuera de la pila de la tarea
    unsigned fallos = 0;

    if (modem_encender() != MODEM_OK)
    {
        ESP_LOGE(TAG, "El modem no responde: se sigue sin transmision");
        return;
    }

    for (size_t i = 0; i < NUM_BRINGUP; i++)
    {
        if (modem_comando(BRINGUP[i].cmd, resp, sizeof(resp), BRINGUP[i].timeout_ms) != MODEM_OK)
        {
            fallos++;
        }
    }

    if (fallos > 0)
    {
        ESP_LOGW(TAG, "Bring-up: %u de %u comandos fallaron", fallos, (unsigned)NUM_BRINGUP);
    }

    if ((modem_comando("AT+CPIN?", resp, sizeof(resp), MODEM_TIMEOUT_CPIN_MS) == MODEM_OK) && (strstr(resp, "+CPIN: READY") != NULL))
    {
        if (esperar_registro(resp, sizeof(resp)) == true)
        {
            for (size_t i = 0; i < NUM_RED; i++)
            {
                modem_comando(RED[i].cmd, resp, sizeof(resp), RED[i].timeout_ms);
            }
            modem_abrir_datos(APN_RED, USUARIO_APN, CLAVE_APN); // la IP queda en el log
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
        estado_sistema_publicar_red(&red, tx, (tx == TX_HTTP_OK), lectura.ts);

        if (cabecera[0] != '\0')
        {
            sd_estado_t est = guardar_en_sd(&lectura, cabecera, tx);
            publicar_sd(est, lectura.ts);
        }
        // Andamiaje: pila libre mínima en bytes (ESP-IDF); criterio: subir la pila si baja de 1 KB
        ESP_LOGI(TAG, "pila libre minima: %u B", (unsigned)uxTaskGetStackHighWaterMark(NULL));
    }
}