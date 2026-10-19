/*
 * Módem SIM7000G por UART1: encendido/apagado con PWRKEY, comandos AT, registro, datos y HTTP (pila SH*).
 * Fuentes: SIM7000 Series AT Command Manual V1.06 (manual); SIM7000 Hardware Design V1.07 (HD, tablas 8 y 9);
 * ESP-IDF v6.1, "UART".
 */
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

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
#define CSQ_DBM_BASE (-114)  // dBm = CSQ_DBM_BASE + 2 * rssi
#define CSQ_DBM_MIN (-115)   // rssi 0: -115 dBm o menos
#define CSQ_DBM_RSSI_1 (-111) // rssi 1
#define CSQ_DBM_MAX (-52)    // rssi 31: -52 dBm o mas

static const char *TAG = "modem";

// Valores del campo <stat> de las respuestas de registro (AT+CEREG?/CGREG?).
#define STAT_SIN_LEER (-1) // Valor propio, no del módem: aún no se ha leído o no se pudo interpretar
#define STAT_REG_CASA 1    // Registrado en la red de su operador
#define STAT_DENEGADO 3    // Registro denegado por la red
#define STAT_REG_ROAMING 5 // Registrado en otra red (roaming)

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
        .pin_bit_mask = 1ULL << MODEM_PIN_PWRKEY,
        .mode = GPIO_MODE_OUTPUT,
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

    // Nivel bajo antes de configurar la salida, para no dar un pulso espurio en PWRKEY (Q4 invierte: alto = pulsado)
    gpio_set_level(MODEM_PIN_PWRKEY, 0);
    err = gpio_config(&io);
    if (err != ESP_OK)
    {
        return MODEM_ERROR;
    }

    return MODEM_OK;
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

modem_estado_t modem_abrir_datos(const char *apn, const char *usuario, const char *clave)
{
    static char resp[MODEM_TAM_RESPUESTA];
    static char cmd[MODEM_TAM_COMANDO];
    modem_estado_t est;
    const char *d;
    int estado;
    uint32_t t0;

    if (apn == NULL || usuario == NULL || clave == NULL)
    {
        return MODEM_ERROR;
    }

    // ¿ya está activa? (el módem sigue encendido entre ciclos). Respuesta: +CNACT: 1,"10.20.30.40"; <status> = 1 es activa (5.2.30)
    if ((consultar("AT+CNACT?", "+CNACT:", &d, true) == MODEM_OK) && (sscanf(d, "%d", &estado) == 1) && (estado == CNACT_ACTIVA))
    {
        return MODEM_OK;
    }

    // APN, usuario, clave y PAP en un solo comando (manual 5.2.31).
    // Si el comando no cabe en cmd se aborta: truncado configuraría un APN o una clave erróneos.
    if (snprintf(cmd, sizeof(cmd), "AT+CNCFG=1,\"%s\",\"%s\",\"%s\",1", apn, usuario, clave) >= (int)sizeof(cmd))
    {
        return MODEM_ERROR;
    }
    est = ejecutar(cmd, resp, sizeof(resp), MODEM_TIMEOUT_COMANDO_MS, true);
    if (est != MODEM_OK)
    {
        return est;
    }

    // Activar (5.2.30): contesta OK al momento; la IP llega después
    est = ejecutar("AT+CNACT=1", resp, sizeof(resp), MODEM_TIMEOUT_COMANDO_MS, true);
    if (est != MODEM_OK)
    {
        return est;
    }

    // Esperar a que esté activa, preguntando cada segundo
    t0 = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    while (((uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) - t0) < MODEM_TIMEOUT_CNACT_MS)
    {
        if ((consultar("AT+CNACT?", "+CNACT:", &d, true) == MODEM_OK) && (sscanf(d, "%d", &estado) == 1) && (estado == CNACT_ACTIVA))
        {
            // La IP ya sale en el log de ejecutar
            ESP_LOGI(TAG, "datos activos en %lu ms",
                     (unsigned long)((uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) - t0));
            return MODEM_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(MODEM_SONDEO_CNACT_MS));
    }

    ESP_LOGW(TAG, "la conexion de datos no se activo");
    return MODEM_TIMEOUT;
}

modem_estado_t modem_http_post(const char *dominio, const char *ruta, const char *cuerpo, size_t len, int *codigo_out, modem_paso_t *paso_out)
{
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

    // Por si quedó una conexión abierta (reset del ESP32 a mitad de envío con el
    // módem encendido). Silencioso: lo normal es que devuelva ERROR
    ejecutar("AT+SHDISC", resp, sizeof(resp), MODEM_TIMEOUT_COMANDO_MS, false);

    // Configurar (9.2.2): antes de conectar. Solo guarda ajustes en el módem; la URL es el dominio, la ruta va en SHREQ.
    if (snprintf(cmd, sizeof(cmd), "AT+SHCONF=\"URL\",\"%s\"", dominio) >= (int)sizeof(cmd))
    {
        return MODEM_ERROR;
    }

    est = ejecutar(cmd, resp, sizeof(resp), MODEM_TIMEOUT_COMANDO_MS, true);
    if (est == MODEM_OK)
    {
        // Tamaño máximo del cuerpo: 1024 B según el manual V1.06 (la nota de versión dice 4096, sin verificar)
        est = ejecutar("AT+SHCONF=\"BODYLEN\",1024", resp, sizeof(resp), MODEM_TIMEOUT_COMANDO_MS, true);
    }

    if (est == MODEM_OK)
    {
        // Tamaño máximo de las cabeceras: 350 B
        est = ejecutar("AT+SHCONF=\"HEADERLEN\",350", resp, sizeof(resp), MODEM_TIMEOUT_COMANDO_MS, true);
    }

    // Abre la conexión con el servidor (9.2.3)
    if (est == MODEM_OK)
    {
        *paso_out = MODEM_PASO_CONEXION;
        est = ejecutar("AT+SHCONN", resp, sizeof(resp), MODEM_TIMEOUT_SHCONN_MS, true);
    }

    // Cabecera y cuerpo (9.2.6 y 9.2.5): después de conectar
    if (est == MODEM_OK)
    {
        *paso_out = MODEM_PASO_PETICION;
        // Guarda la cabecera Content-Type en el módem; se envía con SHREQ
        est =ejecutar("AT+SHAHEAD=\"Content-Type\",\"application/json\"", resp, sizeof(resp),
                       MODEM_TIMEOUT_COMANDO_MS, true);
    }
    if (est == MODEM_OK)
    {
        // Avisa al módem de los <len> bytes de cuerpo que vienen (9.2.5)
        if (snprintf(cmd, sizeof(cmd), "AT+SHBODEXT=%u,%u", (unsigned)len, (unsigned)MODEM_SHBODEXT_MS) >= (int)sizeof(cmd))
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
        // El manual no dice si sale un aviso (">") antes del texto. Con la pausa
        // da igual: si sale, queda en resp y no estorba
        vTaskDelay(pdMS_TO_TICKS(MODEM_PAUSA_CUERPO_MS));
        // El cuerpo va crudo por la UART; el módem lo guarda y no lo envía hasta SHREQ
        if (uart_write_bytes(UART_NUM_1, cuerpo, len) < 0)
        {
            est = MODEM_ERROR;
        }
    }
    if (est == MODEM_OK)
    {
        // Espera a que el módem confirme que ha recibido el cuerpo completo
        // Sin Ctrl-Z: el módem cierra el cuerpo solo, por longitud o al pasar
        // MODEM_SHBODEXT_MS (9.2.5)
        est = esperar("\r\nOK", resp, sizeof(resp), MODEM_SHBODEXT_MS + MODEM_TIMEOUT_COMANDO_MS, "SHBODEXT");
    }

    // POST (9.2.11): OK al momento y, más tarde, +SHREQ: "POST",<codigo>,<len>
    if (est == MODEM_OK)
    {
        // Antes de lanzar SHREQ: un ERROR de SHREQ llega en la espera de +SHREQ:, no aquí
        *paso_out = MODEM_PASO_RESPUESTA;
        // 3 = POST. Si la ruta no cabe se aborta: truncada mandaria el POST a otra URL
        if (snprintf(cmd, sizeof(cmd), "AT+SHREQ=\"%s\",3", ruta) >= (int)sizeof(cmd))
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
        // Espera la respuesta del servidor: +SHREQ: "POST",<codigo>,<len>
        // El OK de antes queda en resp y no estorba
        est = esperar("+SHREQ:", resp, sizeof(resp), MODEM_TIMEOUT_SHREQ_MS, "SHREQ");
    }
    if (est == MODEM_OK)
    {
        // Extrae el código HTTP (200, 4xx...) de la línea +SHREQ
        p = strstr(resp, "+SHREQ:");
        p = (p != NULL) ? strchr(p, ',') : NULL; // primera coma, después de "POST"
        if (p == NULL || sscanf(p + 1, "%d", codigo_out) != 1)
        {
            est = MODEM_ERROR;
        }
    }

    // Desconectar siempre (9.2.13), haya ido bien o mal
    ejecutar("AT+SHDISC", resp, sizeof(resp), MODEM_TIMEOUT_COMANDO_MS, true);

    return est;
}

modem_estado_t modem_sim_lista(void)
{
    modem_estado_t est;

    // TIMEOUT = el modem no contesta; ERROR = +CME ERROR: 10, sin SIM
    est = ejecutar("AT+CPIN?", resp_lectura, sizeof(resp_lectura), MODEM_TIMEOUT_CPIN_MS, true);
    if (est != MODEM_OK)
    {
        return est;
    }

    // SIM PIN, SIM PUK...: el módem contesta OK pero la SIM no sirve, así que no basta el OK
    if (strstr(resp_lectura, "+CPIN: READY") == NULL)
    {
        return MODEM_ERROR;
    }

    return MODEM_OK;
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
    int estado;

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
        else if (strncmp(d, "LTE CAT-M1", 10) == 0)
        {
            snprintf(red->tecnologia, sizeof(red->tecnologia), "CAT-M");
        }
        else if (strncmp(d, "LTE NB-IOT", 10) == 0)
        {
            snprintf(red->tecnologia, sizeof(red->tecnologia), "NB-IOT");
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

    // IP (5.2.30): +CNACT: 1,"100.81.197.154"
    est = consultar("AT+CNACT?", "+CNACT:", &d, true);
    if (est == MODEM_TIMEOUT)
    {
        return MODEM_TIMEOUT;
    }
    if ((est == MODEM_OK) && (sscanf(d, "%d", &estado) == 1) && (estado == CNACT_ACTIVA))
    {
        texto_entre_comillas(d, red->ip, sizeof(red->ip));
    }

    return MODEM_OK; // un dato que falta (sin operador, 99...) no es un fallo
}

modem_estado_t modem_apagar(void)
{
    char resp[MODEM_TAM_RESPUESTA];
    uint32_t t0 = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    uint32_t transcurrido;
    uint32_t resto_ms;

    if (enviar_linea("AT+CPOWD=1") != MODEM_OK)
    {
        return MODEM_ERROR;
    }

    // AT+CPOWD no devuelve OK: su unica respuesta es el URC NORMAL POWER DOWN
    // (manual 5.2.1 y tabla 20.3). Por eso no se usa ejecutar().
    if (esperar("NORMAL POWER DOWN", resp, sizeof resp, MODEM_TIMEOUT_POWERDOWN_MS, NULL) == MODEM_OK)
    {
        ESP_LOGI(TAG, "apagado normal en %lu ms",
                 (unsigned long)((uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) - t0));
    }
    else
    {
        ESP_LOGW(TAG, "sin NORMAL POWER DOWN, recibido: %s", resp);
    }

    // Toff(uart) llega a 6,9 s (HD tabla 9): se espera solo lo que falte hasta MODEM_ESPERA_APAGADO_MS
    transcurrido = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) - t0;
    if (transcurrido < MODEM_ESPERA_APAGADO_MS)
    {
        resto_ms = MODEM_ESPERA_APAGADO_MS - transcurrido;
        vTaskDelay(pdMS_TO_TICKS(resto_ms));
    }

    if (modem_sondear_at(MODEM_SONDEOS_APAGADO) == MODEM_OK)
    {
        ESP_LOGE(TAG, "sigue contestando despues de CPOWD");
        return MODEM_ERROR;
    }

    return MODEM_OK;
}
