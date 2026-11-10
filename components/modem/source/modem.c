/*
 * Módem SIM7000G por UART1: encendido/apagado con PWRKEY, comandos AT, registro, datos y HTTP (pila SH*).
 * Fuentes: SIM7000 Series AT Command Manual V1.06 (manual); SIM7000 Hardware Design V1.07 (HD, tablas 8 y 9);
 * ESP-IDF v6.1, "UART".
 */
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "modem.h"

#define MODEM_TROZO_LECTURA_MS 20 // ms por lectura del UART; el plazo total sigue siendo timeout_ms. Criterio propio (los comandos contestan en <= 20 ms)

// Conversion de AT+CSQ a dBm (manual V1.06, 3.2.16): rssi 2..30 -> -110..-54 dBm
#define CSQ_RSSI_MIN 2
#define CSQ_RSSI_MAX 30
#define CSQ_DBM_BASE (-114)   // dBm = CSQ_DBM_BASE + 2 * rssi
#define CSQ_DBM_MIN (-115)    // rssi 0: -115 dBm o menos
#define CSQ_DBM_RSSI_1 (-111) // rssi 1
#define CSQ_DBM_MAX (-52)     // rssi 31: -52 dBm o mas

// Valores del campo <stat> de las respuestas de registro (AT+CEREG?/CGREG?).
#define STAT_SIN_LEER (-1) // Valor propio, no del módem: aún no se ha leído o no se pudo interpretar
#define STAT_REG_CASA 1    // Registrado en la red de su operador
#define STAT_DENEGADO 3    // Registro denegado por la red
#define STAT_REG_ROAMING 5 // Registrado en otra red (roaming)

#define CNTP_OK 0             // +CNTP: 0 = hora actualizada; no trae la hora (manual V3.00, 15.2.2)
#define CCLK_S_POR_CUARTO 900 // la zona de +CCLK va en cuartos de hora (3.2.17)

static const char *TAG = "modem";

// Buffer de consultar(), modem_sim_lista() y la configuración inicial. Un solo usuario: tarea_transmision
static char resp_lectura[MODEM_TAM_RESPUESTA];

/** @brief Envía un comando más "\r" sin esperar respuesta; vacía el UART ANTES de enviar, no después. */
static modem_estado_t enviar_linea(const char *cmd)
{
    uart_flush_input(UART_NUM_1);
    if (uart_write_bytes(UART_NUM_1, cmd, strlen(cmd)) < 0)
    {
        return MODEM_ERROR;
    }
    if (uart_write_bytes(UART_NUM_1, "\r", 1) < 0)
    {
        return MODEM_ERROR;
    }
    return MODEM_OK;
}

/**
 * @brief Espera una línea que contenga `texto` seguido de "\r\n", leyendo en trozos de MODEM_TROZO_LECTURA_MS.
 *
 * Nunca vacía el UART: lo que ya hubiera cuenta (la secuencia SHBODEXT -> cuerpo -> OK -> SHREQ -> +SHREQ lo
 * necesita). El eco queda dentro de la respuesta. `etiqueta` es el nombre con el que se registra el resultado
 * en el log; NULL = silencioso (ni OK, ni ERROR, ni timeout).
 *
 * @return MODEM_OK (llegó `texto` y su "\r\n"), MODEM_ERROR (ERROR, +CME ERROR o +CMS ERROR; no cabe en resp;
 *         fallo del UART) o MODEM_TIMEOUT.
 */
static modem_estado_t esperar(const char *texto, char *resp, size_t tam, uint32_t timeout_ms, const char *etiqueta)
{
    size_t n = 0;
    size_t hueco;
    int leidos;
    const char *p;
    uint32_t t0 = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

    resp[0] = '\0';

    while (((uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) - t0) < timeout_ms)
    {
        hueco = tam - 1 - n;
        if (hueco == 0)
        {
            if (etiqueta != NULL)
            {
                ESP_LOGE(TAG, "respuesta mas larga que el buffer");
            }
            return MODEM_ERROR;
        }

        leidos = uart_read_bytes(UART_NUM_1, resp + n, hueco, pdMS_TO_TICKS(MODEM_TROZO_LECTURA_MS));
        if (leidos < 0)
        {
            return MODEM_ERROR;
        }
        if (leidos > 0)
        {
            n += (size_t)leidos;
            resp[n] = '\0';

            p = strstr(resp, texto);
            if (p != NULL && strstr(p + strlen(texto), "\r\n") != NULL)
            {
                if (etiqueta != NULL)
                {
                    ESP_LOGI(TAG, "%s -> %s", etiqueta, resp);
                }
                return MODEM_OK;
            }
            if (strstr(resp, "\r\nERROR\r\n") ||
                strstr(resp, "\r\n+CME ERROR:") ||
                strstr(resp, "\r\n+CMS ERROR:"))
            {
                if (etiqueta != NULL)
                {
                    ESP_LOGW(TAG, "%s -> %s", etiqueta, resp);
                }
                return MODEM_ERROR;
            }
        }
    }

    if (etiqueta != NULL)
    {
        ESP_LOGW(TAG, "timeout de %s, recibido: %s", etiqueta, resp);
    }
    return MODEM_TIMEOUT;
}

/**
 * @brief Envía un comando AT (vaciando antes el UART: se pierden los avisos espontáneos del módem) y espera su OK final.
 *
 * Con `con_log` a false no escribe nada en el log (ni OK, ni ERROR, ni timeout); a true registra todo.
 *
 * @return MODEM_OK (llegó "\r\nOK\r\n"), MODEM_ERROR (ERROR, +CME ERROR o +CMS ERROR; no cabe en resp_out;
 *         fallo del UART) o MODEM_TIMEOUT.
 */
static modem_estado_t ejecutar(const char *cmd, char *resp_out, size_t tam_resp, uint32_t timeout_ms, bool con_log)
{
    resp_out[0] = '\0';
    if (enviar_linea(cmd) != MODEM_OK)
    {
        return MODEM_ERROR;
    }
    return esperar("\r\nOK", resp_out, tam_resp, timeout_ms, con_log ? cmd : NULL);
}

/**
 * @brief Envia una consulta y localiza el prefijo de su respuesta.
 *
 * *dato apunta dentro de resp_lectura: la siguiente consulta lo pisa, asi que
 * hay que interpretarlo antes de volver a llamar.
 *
 * Con `con_log` a false la consulta es silenciosa (ver ejecutar).
 *
 * @return MODEM_OK con *dato justo detras del prefijo; MODEM_ERROR si el modem
 *         contesto ERROR o sin el prefijo; MODEM_TIMEOUT si no contesto.
 */
static modem_estado_t consultar(const char *cmd, const char *prefijo, const char **dato, bool con_log)
{
    modem_estado_t est;
    const char *p;

    *dato = NULL;

    est = ejecutar(cmd, resp_lectura, sizeof(resp_lectura), MODEM_TIMEOUT_LECTURA_MS, con_log);
    if (est != MODEM_OK)
    {
        return est;
    }

    p = strstr(resp_lectura, prefijo);
    if (p == NULL)
    {
        return MODEM_ERROR;
    }

    *dato = p + strlen(prefijo); // con CSQ apunta a " 22,99\r\n..."
    return MODEM_OK;
}

/**
 * @brief Copia en buf el texto entre las dos primeras comillas. Si no hay comillas, buf no se toca.
 */
static void texto_entre_comillas(const char *desde, char *buf, size_t tam)
{
    const char *ini;
    const char *fin;
    size_t n;

    ini = strchr(desde, '"');
    if (ini == NULL)
    {
        return;
    }

    fin = strchr(ini + 1, '"');
    if (fin == NULL)
    {
        return;
    }

    n = (size_t)(fin - (ini + 1));
    if (n > tam - 1)
    {
        n = tam - 1; // aqui se corta el operador a lo que cabe en pantalla
    }

    memcpy(buf, ini + 1, n);
    buf[n] = '\0';
}

/**
 * @brief Lee el <stat> de una consulta de registro (AT+CEREG? / AT+CGREG?), sin escribir en el log.
 *
 * Respuesta esperada: "\r\n+CEREG: 0,2\r\n\r\nOK\r\n"; <stat> es el segundo número.
 *
 * @return El <stat>, o STAT_SIN_LEER si el módem no contesta o la respuesta no se entiende.
 */
static int leer_stat(const char *cmd, const char *prefijo)
{
    const char *d;
    int n;
    int stat;

    if (consultar(cmd, prefijo, &d, false) != MODEM_OK)
    {
        return STAT_SIN_LEER;
    }
    if (sscanf(d, "%d,%d", &n, &stat) != 2) // %d se salta el espacio de "+CEREG: "
    {
        return STAT_SIN_LEER;
    }
    return stat;
}

/**
 * @brief Lee la IP del contexto 1 con AT+CGPADDR=1 en ip, de MODEM_TAM_IP bytes; sin IP queda "".
 *
 * El manual (8.2.12) la muestra entre comillas; se aceptan también sin ellas.
 *
 * @return MODEM_OK (contestó, con IP o sin ella), MODEM_ERROR o MODEM_TIMEOUT.
 */
static modem_estado_t leer_ip(char *ip)
{
    const char *d;
    const char *p;
    size_t n;
    modem_estado_t est;

    ip[0] = '\0';

    est = consultar("AT+CGPADDR=1", "+CGPADDR:", &d, true);
    if (est != MODEM_OK)
    {
        return est;
    }

    p = strchr(d, ',');
    if (p == NULL)
    {
        return MODEM_OK; // el manual omite la dirección si no hay ninguna
    }
    p++;
    if (*p == '"')
    {
        p++;
    }

    n = strspn(p, "0123456789.");
    if ((n == 0) || (n >= MODEM_TAM_IP))
    {
        return MODEM_OK;
    }

    memcpy(ip, p, n);
    ip[n] = '\0';

    if (strcmp(ip, "0.0.0.0") == 0)
    {
        ip[0] = '\0';
    }
    return MODEM_OK;
}

/** @brief Envía ATE0 y AT+CMEE=1 (NO_SAVE, manual 1.7.1: hay que mandarlos en cada encendido). Si fallan, solo avisa. */
static void configurar_inicial(void)
{
    static const char *const CMD_INICIALES[] = {"ATE0", "AT+CMEE=1"};

    for (size_t i = 0; i < sizeof(CMD_INICIALES) / sizeof(CMD_INICIALES[0]); i++)
    {
        if (ejecutar(CMD_INICIALES[i], resp_lectura, sizeof(resp_lectura), MODEM_TIMEOUT_COMANDO_MS, false) != MODEM_OK)
        {
            ESP_LOGW(TAG, "%s sin OK: el modem queda con su configuracion anterior", CMD_INICIALES[i]);
        }
    }
}

/**
 * @brief Pasa una fecha UTC desglosada a segundos desde 1970 (formato de ts y t_sync); 0 si no es válida.
 *
 * Entrada con el año completo (2026) y el mes de 1 a 12, como los da el módem.
 * Modifica *t: no llamarla dos veces con la misma estructura.
 */
static uint32_t tm_a_utc(struct tm *t)
{
    t->tm_year -= 1900;   // struct tm cuenta los años desde 1900
    t->tm_mon -= 1;       // y los meses de 0 a 11
    t->tm_isdst = 0;      // con un valor negativo mktime decidiría si hay horario de verano y podría mover una hora
    time_t s = mktime(t); // UTC solo si TZ no está fijado
    if (s == (time_t)-1)
    {
        return 0;
    }
    return (uint32_t)s;
}

// ----------------------------------------------------API_PUBLICA---------------------------------------------------------

modem_estado_t modem_sondear_at(int intentos)
{
    char resp[MODEM_TAM_RESPUESTA];

    for (int i = 0; i < intentos; i++)
    {
        if (ejecutar("AT", resp, sizeof resp, MODEM_TIMEOUT_SONDEO_MS, false) == MODEM_OK)
        {
            return MODEM_OK;
        }
    }
    return MODEM_TIMEOUT;
}

modem_estado_t modem_init(void)
{
    uart_config_t cfg = {
        .baud_rate = MODEM_BAUDIOS,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << MODEM_PIN_PWRKEY) | (1ULL << MODEM_PIN_FLIGHT),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    gpio_config_t entrada = {
        .pin_bit_mask = 1ULL << MODEM_PIN_STATUS,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t err;

    err = uart_param_config(UART_NUM_1, &cfg);
    if (err != ESP_OK)
    {
        return MODEM_ERROR;
    }

    err = uart_set_pin(UART_NUM_1, MODEM_PIN_TX, MODEM_PIN_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK)
    {
        return MODEM_ERROR;
    }

    err = uart_driver_install(UART_NUM_1, MODEM_UART_RX_BUF_SIZE, 0, 0, NULL, 0);
    if (err != ESP_OK)
    {
        return MODEM_ERROR;
    }

    // Niveles antes de configurar las salidas: sin pulso espurio en PWRKEY (Q13 invierte: alto = pulsado) y sin modo avión
    gpio_set_level(MODEM_PIN_PWRKEY, 0);
    gpio_set_level(MODEM_PIN_FLIGHT, 1);
    err = gpio_config(&io);
    if (err != ESP_OK)
    {
        return MODEM_ERROR;
    }

    err = gpio_config(&entrada);
    if (err != ESP_OK)
    {
        return MODEM_ERROR;
    }

    return MODEM_OK;
}

int modem_leer_status(void)
{
    return gpio_get_level(MODEM_PIN_STATUS);
}

modem_estado_t modem_encender(void)
{
    uint32_t t0;

    // PWRKEY conmuta: el mismo pulso enciende y apaga (HD tablas 8 y 9).
    // El módem sobrevive a un reset del ESP32: primero se pregunta.
    if (modem_sondear_at(MODEM_SONDEOS_ENCENDIDO) == MODEM_OK)
    {
        ESP_LOGI(TAG, "el modem ya estaba encendido: no se pulsa PWRKEY");
        configurar_inicial();
        return MODEM_OK;
    }

    ESP_LOGI(TAG, "modem sin respuesta: pulso de encendido");
    t0 = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    gpio_set_level(MODEM_PIN_PWRKEY, 1);
    vTaskDelay(pdMS_TO_TICKS(MODEM_PULSO_ENCENDIDO_MS));
    gpio_set_level(MODEM_PIN_PWRKEY, 0);

    vTaskDelay(pdMS_TO_TICKS(MODEM_ESPERA_UART_MS));

    if (modem_sondear_at(MODEM_SONDEOS_AT) == MODEM_OK)
    {
        ESP_LOGI(TAG, "contesta a los %lu ms del pulso",
                 (unsigned long)((uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) - t0));
        configurar_inicial();
        return MODEM_OK;
    }

    ESP_LOGE(TAG, "el modem no contesta tras el pulso");
    return MODEM_TIMEOUT;
}

modem_estado_t modem_sim_lista(void)
{
    uint32_t t0 = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    modem_estado_t est;

    while (1)
    {
        // TIMEOUT = el modem no contesta; ERROR = +CME ERROR: 10 (sin SIM) o 14 (SIM ocupada)
        est = ejecutar("AT+CPIN?", resp_lectura, sizeof(resp_lectura), MODEM_TIMEOUT_CPIN_MS, true);
        if (est == MODEM_TIMEOUT)
        {
            return MODEM_TIMEOUT;
        }

        if (est == MODEM_OK)
        {
            // SIM PIN, SIM PUK...: el módem contesta OK pero la SIM no sirve, así que no basta el OK
            return (strstr(resp_lectura, "+CPIN: READY") != NULL) ? MODEM_OK : MODEM_ERROR;
        }

        // El módem contesta a AT antes de que la SIM esté lista: da 10 (aún no la detecta) o 14 (ocupada).
        // Cualquier otro error no se arregla esperando
        if ((strstr(resp_lectura, "+CME ERROR: 10") == NULL) && (strstr(resp_lectura, "+CME ERROR: 14") == NULL))
        {
            return MODEM_ERROR;
        }

        if (((uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) - t0) >= MODEM_TIMEOUT_SIM_OCUPADA_MS)
        {
            return MODEM_ERROR;
        }

        vTaskDelay(pdMS_TO_TICKS(MODEM_SONDEO_REGISTRO_MS));
    }
}

modem_estado_t modem_abrir_datos(const char *apn, const char *usuario, const char *clave)
{
    static char resp[MODEM_TAM_RESPUESTA];
    static char cmd[MODEM_TAM_COMANDO];
    char ip[MODEM_TAM_IP];
    modem_estado_t est;
    const char *d;
    uint32_t t0;

    if (apn == NULL || usuario == NULL || clave == NULL)
    {
        return MODEM_ERROR;
    }

    // Si el APN ya está en el contexto 1 no se reescribe: el contexto puede estar activo
    est = consultar("AT+CGDCONT?", "+CGDCONT:", &d, true);
    if (est == MODEM_TIMEOUT)
    {
        return est;
    }
    if ((est != MODEM_OK) || (strstr(d, apn) == NULL))
    {
        if (snprintf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\"", apn) >= (int)sizeof(cmd))
        {
            return MODEM_ERROR;
        }
        est = ejecutar(cmd, resp, sizeof(resp), MODEM_TIMEOUT_COMANDO_MS, true);
        if (est != MODEM_OK)
        {
            return est;
        }

        // PAP = 1. Orden del manual (8.2.15): primero la contraseña y después el usuario
        if (snprintf(cmd, sizeof(cmd), "AT+CGAUTH=1,1,\"%s\",\"%s\"", clave, usuario) >= (int)sizeof(cmd))
        {
            return MODEM_ERROR;
        }
        est = ejecutar(cmd, resp, sizeof(resp), MODEM_TIMEOUT_COMANDO_MS, true);
        if (est != MODEM_OK)
        {
            return est;
        }
    }

    est = leer_ip(ip);
    if (est == MODEM_TIMEOUT)
    {
        return est;
    }
    if (ip[0] != '\0')
    {
        return MODEM_OK;
    }

    t0 = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    est = ejecutar("AT+CGACT=1,1", resp, sizeof(resp), MODEM_TIMEOUT_CGACT_MS, true);
    if (est != MODEM_OK)
    {
        return est;
    }

    est = leer_ip(ip);
    if (est != MODEM_OK)
    {
        return est;
    }
    if (ip[0] == '\0')
    {
        ESP_LOGW(TAG, "contexto activado, pero sin IP");
        return MODEM_ERROR;
    }

    ESP_LOGI(TAG, "datos activos en %lu ms", (unsigned long)((uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) - t0));
    return MODEM_OK;
}
modem_estado_t modem_http_post(const char *dominio, const char *ruta, const char *cuerpo, size_t len, int *codigo_out, modem_paso_t *paso_out)
{
    static const char *const CMD_PARAM[] = {
        "AT+HTTPPARA=\"CONTENT\",\"application/json\"",
        "AT+HTTPPARA=\"CONNECTTO\"," MODEM_HTTP_CONNECTTO_S,
        "AT+HTTPPARA=\"RESPTO\"," MODEM_HTTP_RESPTO_S,
    };
    static char resp[MODEM_TAM_RESPUESTA];
    static char cmd[MODEM_TAM_COMANDO];
    const char *p;
    modem_estado_t est;

    if (codigo_out == NULL)
    {
        return MODEM_ERROR;
    }
    *codigo_out = 0;

    if (paso_out == NULL)
    {
        return MODEM_ERROR;
    }
    *paso_out = MODEM_PASO_CONFIG;

    if (dominio == NULL || ruta == NULL || cuerpo == NULL)
    {
        return MODEM_ERROR;
    }

    // Por si quedó abierto (reset del ESP32 a mitad de envío con el módem encendido). Lo normal es que devuelva ERROR
    ejecutar("AT+HTTPTERM", resp, sizeof(resp), MODEM_TIMEOUT_COMANDO_MS, false);

    est = ejecutar("AT+HTTPINIT", resp, sizeof(resp), MODEM_TIMEOUT_HTTPINIT_MS, true);

    if (est == MODEM_OK)
    {
        // Si la URL no cabe se aborta: truncada mandaría el POST a otra dirección
        if (snprintf(cmd, sizeof(cmd), "AT+HTTPPARA=\"URL\",\"%s%s\"", dominio, ruta) >= (int)sizeof(cmd))
        {
            est = MODEM_ERROR;
        }
        else
        {
            est = ejecutar(cmd, resp, sizeof(resp), MODEM_TIMEOUT_COMANDO_MS, true);
        }
    }

    for (size_t i = 0; (est == MODEM_OK) && (i < sizeof(CMD_PARAM) / sizeof(CMD_PARAM[0])); i++)
    {
        est = ejecutar(CMD_PARAM[i], resp, sizeof(resp), MODEM_TIMEOUT_COMANDO_MS, true);
    }

    if (est == MODEM_OK)
    {
        *paso_out = MODEM_PASO_PETICION;
        if (snprintf(cmd, sizeof(cmd), "AT+HTTPDATA=%u,%d", (unsigned)len, MODEM_HTTPDATA_TIEMPO) >= (int)sizeof(cmd))
        {
            est = MODEM_ERROR;
        }
        else
        {
            est = enviar_linea(cmd);
        }
    }
    if (est == MODEM_OK)
    {
        est = esperar("DOWNLOAD", resp, sizeof(resp), MODEM_TIMEOUT_COMANDO_MS, "HTTPDATA");
    }
    if ((est == MODEM_OK) && (uart_write_bytes(UART_NUM_1, cuerpo, len) < 0))
    {
        est = MODEM_ERROR;
    }
    if (est == MODEM_OK)
    {
        est = esperar("\r\nOK", resp, sizeof(resp), MODEM_TIMEOUT_COMANDO_MS, "cuerpo HTTP");
    }

    if (est == MODEM_OK)
    {
        *paso_out = MODEM_PASO_RESPUESTA;
        est = enviar_linea("AT+HTTPACTION=1");
    }
    if (est == MODEM_OK)
    {
        // El OK llega antes y queda en resp; el resultado viene después: +HTTPACTION: 1,<código>,<longitud>
        est = esperar("+HTTPACTION:", resp, sizeof(resp), MODEM_TIMEOUT_HTTPACTION_MS, "HTTPACTION");
    }
    if (est == MODEM_OK)
    {
        p = strstr(resp, "+HTTPACTION:");
        p = (p != NULL) ? strchr(p, ',') : NULL;
        if ((p == NULL) || (sscanf(p + 1, "%d", codigo_out) != 1))
        {
            est = MODEM_ERROR;
        }
    }

    // Cerrar siempre, haya ido bien o mal
    ejecutar("AT+HTTPTERM", resp, sizeof(resp), MODEM_TIMEOUT_COMANDO_MS, true);

    return est;
}
modem_estado_t modem_esperar_registro(uint32_t timeout_ms)
{
    uint32_t t0 = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    uint32_t transcurrido;
    int lte;
    int gsm;
    int ultimo_lte = STAT_SIN_LEER;
    int ultimo_gsm = STAT_SIN_LEER;

    while (1)
    {
        lte = leer_stat("AT+CEREG?", "+CEREG:"); // registro LTE: Cat-M o NB-IoT
        gsm = leer_stat("AT+CGREG?", "+CGREG:"); // registro de paquetes en GSM (CNMP=2)
        transcurrido = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) - t0;

        if ((lte != ultimo_lte) || (gsm != ultimo_gsm))
        {
            ESP_LOGI(TAG, "%lu ms: CEREG %d, CGREG %d", (unsigned long)transcurrido, lte, gsm);
            ultimo_lte = lte;
            ultimo_gsm = gsm;
        }

        if ((lte == STAT_REG_CASA) || (lte == STAT_REG_ROAMING) || (gsm == STAT_REG_CASA) || (gsm == STAT_REG_ROAMING))
        {
            ESP_LOGI(TAG, "registrado a los %lu ms (CEREG %d, CGREG %d)", (unsigned long)transcurrido, lte, gsm);
            return MODEM_OK;
        }

        // Si se niegan las dos no hay nada que esperar
        if ((lte == STAT_DENEGADO) && (gsm == STAT_DENEGADO))
        {
            break;
        }

        if (transcurrido >= timeout_ms)
        {
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(MODEM_SONDEO_REGISTRO_MS));
    }

    ESP_LOGW(TAG, "sin red tras %lu ms (CEREG %d, CGREG %d)", (unsigned long)transcurrido, lte, gsm);
    return ((lte == STAT_SIN_LEER) && (gsm == STAT_SIN_LEER)) ? MODEM_TIMEOUT : MODEM_ERROR;
}

modem_estado_t modem_leer_red(modem_red_t *red)
{
    modem_estado_t est;
    const char *d;
    int rssi;
    int ber;

    if (red == NULL)
    {
        return MODEM_ERROR;
    }

    memset(red, 0, sizeof(*red)); // todo "sin dato"; dbm = MODEM_DBM_DESCONOCIDA

    // Operador (3.2.9): +COPS: 0,0,"Claro",3
    est = consultar("AT+COPS?", "+COPS:", &d, true);
    if (est == MODEM_TIMEOUT)
    {
        return MODEM_TIMEOUT;
    }
    if (est == MODEM_OK)
    {
        texto_entre_comillas(d, red->operador, sizeof(red->operador));
    }

    // Tecnologia (5.2.21): +CPSI:( *AQUÍ )GSM,Online,...
    est = consultar("AT+CPSI?", "+CPSI:", &d, true);
    if (est == MODEM_TIMEOUT)
    {
        return MODEM_TIMEOUT;
    }
    if (est == MODEM_OK)
    {
        // consultar() deja d justo tras los dos puntos: se saltan los espacios hasta la tecnología
        while (*d == ' ')
        {
            d++;
        }

        if (strncmp(d, "GSM", 3) == 0)
        {
            snprintf(red->tecnologia, sizeof(red->tecnologia), "GSM");
        }
        else if (strncmp(d, "WCDMA", 5) == 0)
        {
            snprintf(red->tecnologia, sizeof(red->tecnologia), "WCDMA");
        }
        else if (strncmp(d, "LTE", 3) == 0)
        {
            snprintf(red->tecnologia, sizeof(red->tecnologia), "LTE");
        }
        // "NO SERVICE" u otro: se queda ""
    }

    // Senal (3.2.16): +CSQ: 22,99
    est = consultar("AT+CSQ", "+CSQ:", &d, true);
    if (est == MODEM_TIMEOUT)
    {
        return MODEM_TIMEOUT;
    }
    // Se leen <rssi>,<ber> de la respuesta; solo rssi se usa
    if ((est == MODEM_OK) && (sscanf(d, "%d,%d", &rssi, &ber) == 2))
    {
        if (rssi == 0)
        {
            red->dbm = CSQ_DBM_MIN;
        }
        else if (rssi == 1)
        {
            red->dbm = CSQ_DBM_RSSI_1;
        }
        else if ((rssi >= CSQ_RSSI_MIN) && (rssi <= CSQ_RSSI_MAX))
        {
            red->dbm = (int16_t)(CSQ_DBM_BASE + 2 * rssi);
        }
        else if (rssi == 31)
        {
            red->dbm = CSQ_DBM_MAX;
        }
        // 99 = desconocida: se queda MODEM_DBM_DESCONOCIDA
    }

    est = leer_ip(red->ip);
    if (est == MODEM_TIMEOUT)
    {
        return MODEM_TIMEOUT;
    }

    return MODEM_OK; // un dato que falta (sin operador, 99...) no es un fallo
}

modem_estado_t modem_gnss_encender(void)
{
    // CVAUXS=1: alimenta la antena activa por VDD_3V (pin 44 VDD_AUX, R43, FB3); CGPS=1,1: sesión autónoma;
    // CGPSINFO=1: un +CGPSINFO por segundo (manual V3.00, 19.2.2, 17.2.1 y 17.2.2)
    static const char *const CMD_GNSS_ON[] = {"AT+CVAUXS=1", "AT+CGPS=1,1", "AT+CGPSINFO=1"};
    modem_estado_t est;

    for (size_t i = 0; i < sizeof(CMD_GNSS_ON) / sizeof(CMD_GNSS_ON[0]); i++)
    {
        est = ejecutar(CMD_GNSS_ON[i], resp_lectura, sizeof(resp_lectura), MODEM_TIMEOUT_COMANDO_MS, true);
        if (est != MODEM_OK)
        {
            return est;
        }
    }
    return MODEM_OK;
}

modem_estado_t modem_gnss_esperar_fix(uint32_t timeout_ms, uint32_t *utc_out)
{
    static char resp[MODEM_TAM_RESPUESTA];
    struct tm t;
    int n;
    int avisos = 0;
    const char *p;
    modem_estado_t est;
    uint32_t t0 = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    uint32_t transcurrido;

    if (utc_out == NULL)
    {
        return MODEM_ERROR;
    }
    *utc_out = 0;

    while ((transcurrido = ((uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS)) - t0) < timeout_ms)
    {
        est = esperar("+CGPSINFO:", resp, sizeof(resp), timeout_ms - transcurrido, NULL);
        if (est == MODEM_TIMEOUT)
        {
            break;
        }
        if (est == MODEM_ERROR)
        {
            ESP_LOGW(TAG, "lectura del GNSS: %s", resp);
            return MODEM_ERROR;
        }
        avisos++;

        p = strstr(resp, "+CGPSINFO:") + strlen("+CGPSINFO:");
        memset(&t, 0, sizeof(t));
        // +CGPSINFO: <lat>,<N/S>,<log>,<E/W>,ddmmyy,hhmmss.s,... (17.2.2). Sin fix los campos van vacíos:
        // %*[^,] no acepta un campo vacío y sscanf se para antes de leer la fecha
        n = sscanf(p, "%*[^,],%*[^,],%*[^,],%*[^,],%2d%2d%2d,%2d%2d%2d", &t.tm_mday, &t.tm_mon,
                   &t.tm_year, &t.tm_hour, &t.tm_min, &t.tm_sec);
        // n va a ser el número de campos leidos que en este caso tiene que ser 8 si ha avido fix
        if (n != 6)
        {
            continue;
        }
        t.tm_year += 2000; // el año llega con dos cifras; tm_a_utc espera el año completo
        *utc_out = tm_a_utc(&t);
        if (*utc_out == 0)
        {
            continue;
        }
        return MODEM_OK;
    }
    ESP_LOGW(TAG, "sin fix tras %lu ms (%d avisos)", transcurrido, avisos);
    return MODEM_TIMEOUT;
}

modem_estado_t modem_gnss_apagar(void)
{
    // CGPSINFO=0 para el aviso, CGPS=0 la sesión y CVAUXS=0 corta VDD_3V de la antena (manual V3.00, 17.2.2, 17.2.1 y 19.2.2)
    static const char *const CMD_GNSS_OFF[] = {"AT+CGPSINFO=0", "AT+CGPS=0", "AT+CVAUXS=0"};
    modem_estado_t est;
    modem_estado_t res = MODEM_OK;

    for (size_t i = 0; i < sizeof(CMD_GNSS_OFF) / sizeof(CMD_GNSS_OFF[0]); i++)
    {
        est = ejecutar(CMD_GNSS_OFF[i], resp_lectura, sizeof(resp_lectura), MODEM_TIMEOUT_COMANDO_MS, true);
        if (est != MODEM_OK && res == MODEM_OK)
        {
            res = est;
        }
    }
    return res;
}

modem_estado_t modem_cntp(const char *servidor, uint32_t timeout_ms, uint32_t *utc_out, int *codigo_out)
{
    static char resp[MODEM_TAM_RESPUESTA];
    static char cmd[MODEM_TAM_COMANDO];
    struct tm t = {0};
    const char *d;
    int zona = 0; // sin zona válida, CCLK no la devuelve (3.2.17, nota 1)
    int n;
    uint32_t local;
    modem_estado_t est;

    if ((servidor == NULL) || (utc_out == NULL) || (codigo_out == NULL))
    {
        return MODEM_ERROR;
    }

    *utc_out = 0;
    *codigo_out = MODEM_CNTP_SIN_CODIGO;

    // Zona 0 = UTC. En la SIM7600 solo hay servidor y zona: ni cid ni modo (15.2.1)
    if (snprintf(cmd, sizeof(cmd), "AT+CNTP=\"%s\",0", servidor) >= (int)sizeof(cmd))
    {
        return MODEM_ERROR;
    }
    est = ejecutar(cmd, resp, sizeof(resp), MODEM_TIMEOUT_COMANDO_MS, true);
    if (est != MODEM_OK)
    {
        return est;
    }

    // enviar_linea + esperar, sin ejecutar(): el OK y un +CNTP rápido llegan al mismo buffer
    est = enviar_linea("AT+CNTP");
    if (est != MODEM_OK)
    {
        return est;
    }
    est = esperar("+CNTP:", resp, sizeof(resp), timeout_ms, NULL);
    if (est == MODEM_TIMEOUT)
    {
        ESP_LOGW(TAG, "sin +CNTP en %lu ms", timeout_ms);
        return MODEM_TIMEOUT;
    }
    if (est == MODEM_ERROR)
    {
        ESP_LOGW(TAG, "error: %s ", resp);
        return MODEM_ERROR;
    }

    sscanf(strstr(resp, "+CNTP:") + strlen("+CNTP:"), "%d", codigo_out);
    if (*codigo_out != CNTP_OK)
    {
        ESP_LOGW(TAG, "+CNTP: %d", *codigo_out);
        return MODEM_ERROR;
    }

    // +CNTP: 0 no trae la hora: se lee del reloj del módem. +CCLK: "yy/MM/dd,hh:mm:ss±zz" (3.2.17)
    est = consultar("AT+CCLK?", "+CCLK:", &d, true);
    if (est != MODEM_OK)
    {
        return est;
    }
    n = sscanf(d, " \"%2d/%2d/%2d,%2d:%2d:%2d%3d", &t.tm_year, &t.tm_mon, &t.tm_mday,
               &t.tm_hour, &t.tm_min, &t.tm_sec, &zona);
    if (n < 6)
    {
        ESP_LOGW(TAG, "hora ilegible: %s", d);
        return MODEM_ERROR;
    }
    t.tm_year += 2000; // dos cifras, igual que en +CGPSINFO

    local = tm_a_utc(&t);
    if (local == 0)
    {
        return MODEM_ERROR;
    }
    // AT+CTZU vale 1 de fábrica: la red puede cambiar la zona después de CNTP, así que se resta siempre
    *utc_out = local - zona * CCLK_S_POR_CUARTO;
    return MODEM_OK;
}

modem_estado_t modem_apagar(void)
{
    char resp[MODEM_TAM_RESPUESTA];
    uint32_t t0 = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    uint32_t transcurrido;

    if (ejecutar("AT+CPOF", resp, sizeof resp, MODEM_TIMEOUT_COMANDO_MS, true) != MODEM_OK)
    {
        ESP_LOGW(TAG, "sin OK de CPOF, recibido: %s", resp);
    }

    // Se espera solo lo que falte hasta MODEM_ESPERA_APAGADO_MS
    transcurrido = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) - t0;
    if (transcurrido < MODEM_ESPERA_APAGADO_MS)
    {
        vTaskDelay(pdMS_TO_TICKS(MODEM_ESPERA_APAGADO_MS - transcurrido));
    }

    if (modem_sondear_at(MODEM_SONDEOS_APAGADO) == MODEM_OK)
    {
        ESP_LOGE(TAG, "sigue contestando despues de CPOF");
        return MODEM_ERROR;
    }

    return MODEM_OK;
}
