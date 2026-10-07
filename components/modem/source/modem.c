#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "modem.h"

#define MODEM_TROZO_LECTURA_MS 20 // latencia por lectura; el plazo total sigue siendo timeout_ms

// Conversion de AT+CSQ a dBm (manual V1.06, 3.2.16): rssi 2..30 -> -110..-54 dBm
#define CSQ_RSSI_MIN 2
#define CSQ_RSSI_MAX 30
#define CSQ_DBM_BASE (-114)  // dBm = CSQ_DBM_BASE + 2 * rssi
#define CSQ_DBM_MIN (-115)   // rssi 0: -115 dBm o menos
#define CSQ_DBM_RSSI_1 (-111) // rssi 1
#define CSQ_DBM_MAX (-52)    // rssi 31: -52 dBm o mas

static const char *TAG = "modem";

// Comando para apagar el modem
static const char CMD_APAGAR[] = "AT+CPOWD=1\r";

// Buffer de consultar() y modem_sim_lista(). Un solo usuario: tarea_transmision
static char resp_lectura[MODEM_TAM_RESPUESTA];

/**
 * @brief Envía un comando AT al módem y espera su respuesta final.
 *
 * Vacía el UART, envía el comando más "\r" y acumula lo recibido hasta
 * encontrar "OK", un error del módem o agotar el tiempo. El eco del
 * comando queda dentro de la respuesta.
 *
 * @param cmd        Comando sin terminador (p. ej. "AT+CGMR").
 * @param resp_out   Buffer donde se guarda la respuesta, siempre terminada en '\0'.
 * @param tam_resp   Tamaño de resp_out en bytes (incluye el '\0').
 * @param timeout_ms Tiempo máximo total de espera, en milisegundos.
 * @param log_fallo  true: registra error y timeout en el log; false: silencioso.
 *
 * @return MODEM_OK       Llegó "\r\nOK\r\n".
 * @return MODEM_ERROR    Llegó ERROR, +CME ERROR o +CMS ERROR; la respuesta
 *                        no cabe en resp_out; o falló la escritura al UART.
 * @return MODEM_TIMEOUT  No llegó ni OK ni error dentro de timeout_ms.
 */
static modem_estado_t ejecutar(const char *cmd, char *resp_out, size_t tam_resp, uint32_t timeout_ms, bool log_fallo)
{
    size_t n = 0;
    size_t hueco;
    int leidos;
    uint32_t t0 = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    uint32_t transcurrido;

    resp_out[0] = '\0';

    uart_flush_input(UART_NUM_1);
    if (uart_write_bytes(UART_NUM_1, cmd, strlen(cmd)) < 0)
    {
        return MODEM_ERROR;
    }
    if (uart_write_bytes(UART_NUM_1, "\r", 1) < 0)
    {
        return MODEM_ERROR;
    }

    while (1)
    {
        transcurrido = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) - t0;
        if (transcurrido >= timeout_ms)
        {
            break;
        }

        hueco = tam_resp - 1 - n;
        if (hueco == 0)
        {
            if (log_fallo)
            {
                ESP_LOGE(TAG, "respuesta mas larga que el buffer");
            }
            return MODEM_ERROR;
        }

        leidos = uart_read_bytes(UART_NUM_1, resp_out + n, hueco, pdMS_TO_TICKS(MODEM_TROZO_LECTURA_MS));
        if (leidos < 0)
        {
            return MODEM_ERROR;
        }
        if (leidos > 0)
        {
            n += (size_t)leidos;
            resp_out[n] = '\0';

            if (strstr(resp_out, "\r\nOK\r\n"))
            {
                ESP_LOGI(TAG, "%s -> %s", cmd, resp_out);
                return MODEM_OK;
            }
            if (strstr(resp_out, "\r\nERROR\r\n") ||
                strstr(resp_out, "\r\n+CME ERROR:") ||
                strstr(resp_out, "\r\n+CMS ERROR:"))
            {
                ESP_LOGW(TAG, "%s -> %s", cmd, resp_out);
                return MODEM_ERROR;
            }
        }
    }

    if (log_fallo)
    {
        ESP_LOGW(TAG, "timeout de %s, recibido: %s", cmd, resp_out);
    }
    return MODEM_TIMEOUT;
}

static modem_estado_t enviar_linea(const char *cmd)
{
    uart_flush_input(UART_NUM_1); // se vacía ANTES de enviar; después ya no
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

static modem_estado_t esperar_linea(const char *texto, char *resp, size_t tam, uint32_t timeout_ms)
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
            ESP_LOGE(TAG, "respuesta mas larga que el buffer");
            return MODEM_ERROR;
        }

        leidos = uart_read_bytes(UART_NUM_1, resp + n, hueco, pdMS_TO_TICKS(MODEM_TROZO_LECTURA_MS));
        if (leidos < 0)
        {
            return MODEM_ERROR;
        }
        else
        {
            n += leidos;
            resp[n] = '\0';

            p = strstr(resp, texto);
            if (p != NULL && strstr(p + strlen(texto), "\r\n") != NULL)
            {
                ESP_LOGI(TAG, "-> %s", p);
                return MODEM_OK;
            }

            if ((strstr(resp, "\r\nERROR\r\n") || strstr(resp, "\r\n+CME ERROR:")))
            {
                ESP_LOGW(TAG, "-> %s", resp);
                return MODEM_ERROR;
            }
        }
    }
    ESP_LOGW(TAG, "timeout esperando %s, recibido: %s", texto, resp);
    return MODEM_TIMEOUT;
}

/**
 * @brief Comprueba si la conexión de datos está activa (AT+CNACT?, manual V1.06, 5.2.30).
 *
 * Uso interno de `modem_abrir_datos`: al principio, para saltarse la activación si el
 * módem ya la tiene de un ciclo anterior, y en el bucle de espera tras AT+CNACT=1,
 * preguntando cada MODEM_SONDEO_CNACT_MS hasta que pase a activa.
 *
 * @param resp     Buffer de trabajo donde se guarda la respuesta del módem (lo pisa).
 * @param tam_resp Tamaño de resp en bytes (incluye el '\0').
 *
 * @return true   La respuesta trae "+CNACT: <status>" con status = CNACT_ACTIVA.
 * @return false  Status distinto, respuesta sin "+CNACT:" o el comando falló
 *                (ERROR o timeout); no distingue entre estas causas.
 */
static bool app_network_activa(char *resp, size_t tam_resp)
{
    const char *p;
    int estado;

    if (ejecutar("AT+CNACT?", resp, tam_resp, MODEM_TIMEOUT_COMANDO_MS, true) != MODEM_OK)
    {
        return false;
    }
    // Localiza el inicio de la línea de interes
    p = strstr(resp, "+CNACT:");
    // la suma p + ... slata ese texto y apunta justo detrás, respuesta supuesta: CNACT: 1,"10.20.30.40"
    if (p == NULL || sscanf(p + strlen("+CNACT:"), "%d", &estado) != 1)
    {
        return false;
    }
    return estado == CNACT_ACTIVA;
}

/**
 * @brief Envia una consulta y localiza el prefijo de su respuesta.
 *
 * *dato apunta dentro de resp_lectura: la siguiente consulta lo pisa, asi que
 * hay que interpretarlo antes de volver a llamar.
 *
 * @return MODEM_OK con *dato justo detras del prefijo; MODEM_ERROR si el modem
 *         contesto ERROR o sin el prefijo; MODEM_TIMEOUT si no contesto.
 */
static modem_estado_t consultar(const char *cmd, const char *prefijo, const char **dato)
{
    modem_estado_t est;
    const char *p;

    *dato = NULL;

    est = ejecutar(cmd, resp_lectura, sizeof(resp_lectura), MODEM_TIMEOUT_LECTURA_MS, true);
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

    // Configura el pin GPIO4 como salida para el PWRKEY del modem
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

    // Se pone a low para que no mande lo que tenga puesto de un princpio
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
        return MODEM_OK;
    }

    ESP_LOGE(TAG, "el modem no contesta tras el pulso");
    return MODEM_TIMEOUT;
}

// Documentación en modem.h
modem_estado_t modem_comando(const char *cmd, char *resp_out, size_t tam_resp, uint32_t timeout_ms)
{
    if (cmd == NULL || resp_out == NULL || tam_resp == 0)
    {
        return MODEM_ERROR;
    }
    return ejecutar(cmd, resp_out, tam_resp, timeout_ms, true);
}

/**
 * Miras si la red está lista para recibir datos
 */
modem_estado_t modem_abrir_datos(const char *apn, const char *usuario, const char *clave)
{
    static char resp[MODEM_TAM_RESPUESTA];
    static char cmd[MODEM_TAM_COMANDO];
    modem_estado_t est;
    uint32_t t0;

    if (apn == NULL || usuario == NULL || clave == NULL)
    {
        return MODEM_ERROR;
    }

    // ¿ya está activa? (el módem sigue encendido entre ciclos)
    if (app_network_activa(resp, sizeof(resp)))
    {
        return MODEM_OK;
    }

    // APN, usuario, clave y PAP en un solo comando (manual V1.06, 5.2.31)
    // Miras si el buffer se pasa de tamaño
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
        if (app_network_activa(resp, sizeof(resp)))
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

    // Configurar (9.2.2): antes de conectar. Solo guarda ajustes en el módem
    // Guarda la URL del servidor. Aún no conecta
    if (snprintf(cmd, sizeof(cmd), "AT+SHCONF=\"URL\",\"%s\"", dominio) >= (int)sizeof(cmd))
    {
        return MODEM_ERROR;
    }

    est = ejecutar(cmd, resp, sizeof(resp), MODEM_TIMEOUT_COMANDO_MS, true);
    if (est == MODEM_OK)
    {
        // Fija el tamaño máximo del cuerpo (1024 B). Solo configura
        est = ejecutar("AT+SHCONF=\"BODYLEN\",1024", resp, sizeof(resp), MODEM_TIMEOUT_COMANDO_MS, true);
    }

    if (est == MODEM_OK)
    {
        // Fija el tamaño máximo de las cabeceras (350 B). Solo configura
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
        // Guarda la cabecera Content-Type en el módem. Aún no se envía
        est =ejecutar("AT+SHAHEAD=\"Content-Type\",\"application/json\"", resp, sizeof(resp),
                       MODEM_TIMEOUT_COMANDO_MS, true);
    }
    if (est == MODEM_OK)
    {
        // Avisa al módem: "te mando <len> bytes de cuerpo". Aún no es el cuerpo
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
        // Manda el JSON por la UART. El módem lo guarda, aún no se envía
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
        est = esperar_linea("\r\nOK", resp, sizeof(resp), MODEM_SHBODEXT_MS + MODEM_TIMEOUT_COMANDO_MS);
    }

    // POST (9.2.11): OK al momento y, más tarde, +SHREQ: "POST",<codigo>,<len>
    if (est == MODEM_OK)
    {
        // Antes de lanzar SHREQ: un ERROR de SHREQ llega en la espera de +SHREQ:, no aquí
        *paso_out = MODEM_PASO_RESPUESTA;
        // Lanza el POST: ahora el módem envía cabecera y cuerpo al servidor
        // 3 = POST. Una ruta truncada mandaria el POST a otra URL
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
        est = esperar_linea("+SHREQ:", resp, sizeof(resp), MODEM_TIMEOUT_SHREQ_MS);
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

// Mira si la SIM está lista 
modem_estado_t modem_sim_lista(void)
{
    modem_estado_t est;

    // TIMEOUT = el modem no contesta; ERROR = +CME ERROR: 10, sin SIM
    est = ejecutar("AT+CPIN?", resp_lectura, sizeof(resp_lectura), MODEM_TIMEOUT_CPIN_MS, true);
    if (est != MODEM_OK)
    {
        return est;
    }

    // SIM PIN, SIM PUK...: contesta OK pero la SIM no sirve, devuelve NULL si no encuentra string2
    if (strstr(resp_lectura, "+CPIN: READY") == NULL)
    {
        return MODEM_ERROR;
    }

    return MODEM_OK;
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
    est = consultar("AT+COPS?", "+COPS:", &d);
    if (est == MODEM_TIMEOUT)
    {
        return MODEM_TIMEOUT;
    }
    if (est == MODEM_OK)
    {
        texto_entre_comillas(d, red->operador, sizeof(red->operador));
    }

    // Tecnologia (5.2.21): +CPSI:( *AQUÍ )GSM,Online,...
    est = consultar("AT+CPSI?", "+CPSI:", &d);
    if (est == MODEM_TIMEOUT)
    {
        return MODEM_TIMEOUT;
    }
    if (est == MODEM_OK)
    {
        // Consultar deja justo despés de lso dos puntos( * ), se los salta hasta que apunta a la respuesta
        while (*d == ' ')
        {
            d++;
        }

        // Devuelve si son iguales o no 
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
    est = consultar("AT+CSQ", "+CSQ:", &d);
    if (est == MODEM_TIMEOUT)
    {
        return MODEM_TIMEOUT;
    }
    // sscanf lee los valores que hay en d que es la respuesta a AT+CSQ, y te da los valores de la respuesta
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
    est = consultar("AT+CNACT?", "+CNACT:", &d);
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
    size_t n = 0;
    size_t hueco;
    int leidos;
    bool visto = false;
    uint32_t t0 = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    uint32_t transcurrido;
    uint32_t resto_ms;

    resp[0] = '\0';
    uart_flush_input(UART_NUM_1);
    // -1 en el sizeof para quitar el \0
    if (uart_write_bytes(UART_NUM_1, CMD_APAGAR, sizeof(CMD_APAGAR) - 1) < 0)
    {
        return MODEM_ERROR;
    }

    // AT+CPOWD no devuelve OK: su unica respuesta es el URC NORMAL POWER DOWN
    // (manual 5.2.1 y tabla 20.3). Por eso no se usa ejecutar().
    while (!visto)
    {
        transcurrido = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) - t0;
        if (transcurrido >= MODEM_TIMEOUT_POWERDOWN_MS)
        {
            break;
        }

        hueco = sizeof resp - 1 - n;
        if (hueco == 0)
        {
            break;
        }

        leidos = uart_read_bytes(UART_NUM_1, resp + n, hueco, pdMS_TO_TICKS(MODEM_TROZO_LECTURA_MS));
        if (leidos < 0)
        {
            break;
        }
        if (leidos > 0)
        {
            n += (size_t)leidos;
            resp[n] = '\0';
            // Compara lo recibido con la cadena esperada, si es distinto de NULL pone true en visto
            visto = (strstr(resp, "NORMAL POWER DOWN") != NULL);
        }
    }

    if (visto)
    {
        ESP_LOGI(TAG, "apagado normal en %lu ms",
                 (unsigned long)((uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) - t0));
    }
    else
    {
        ESP_LOGW(TAG, "sin NORMAL POWER DOWN, recibido: %s", resp);
    }

    // Toff(uart) llega a 6,9 s (HD tabla 9): se espera solo lo que falte. Tarda el modem en apagarse según la tabla MODEM_ESPEA_APAGADO_MS
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
