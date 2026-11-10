#ifndef MODEM_H
#define MODEM_H

#include <stddef.h>
#include <stdint.h>

// Fuentes: SIM7000 Series AT Command Manual V1.06 (manual); SIM7000 Hardware Design V1.07 (HD); esquemático T-SIM7000G V1.6.
// Pines de conexión (Hardware), vistos desde el ESP32 (esquemático T-SIM7000G V1.6)
#define MODEM_PIN_TX 27    // GPIO de TX del ESP32 hacia el RXD del módem
#define MODEM_PIN_RX 26    // GPIO de RX del ESP32 desde el TXD del módem
#define MODEM_PIN_PWRKEY 4 // GPIO que activa PWRKEY a través de Q4: alto = PWRKEY a GND
#define MODEM_PIN_FLIGHT 25 // GPIO de FLIGHTMODE a través de U5: bajo = modo avión (HD V1.00, "Flight Mode Control")
#define MODEM_PIN_STATUS 34 // GPIO de STATUS a través de U5: alto = módem encendido y listo (HD V1.00, 3.2.2); solo entrada

// Configuración de la UART (115200 8N1, sin control de flujo)
#define MODEM_BAUDIOS 115200 // baudios; el autobaud admite 9600-115200, se conecta a 115200 automaticamente

// Dimensionamiento de memoria
#define MODEM_UART_RX_BUF_SIZE 1024 // bytes; uart_driver_install exige más de 128. Margen para respuestas largas
#define MODEM_TAM_RESPUESTA 256     // bytes; margen holgado para respuestas extensas como AT+CGMR o AT+CPSI?

// Tiempos de Secuencia de Encendido (SIM7600G Hardware Design V1.00, 3.2.1)
#define MODEM_PULSO_ENCENDIDO_MS 500 // ms; Ton típico 500 ms, por debajo del Toff típico de 2,5 s
#define MODEM_ESPERA_UART_MS 13000   // ms; Ton(uart) mínimo 13 s; los sondeos cubren el resto

// Tiempos de Secuencia de Apagado (SIM7600G Hardware Design V1.00, 3.2.2)
#define MODEM_ESPERA_APAGADO_MS 15000 // ms; Toff(uart) típico 15 s, dado para PWRKEY: sin medir con AT+CPOF

// Parámetros lógicos de la Máquina de Estados AT
#define MODEM_SONDEOS_AT 30           // reintentos de "AT" para asegurar la sincronización de autobaud; criterio propio
#define MODEM_TIMEOUT_SONDEO_MS 500   // ms esperando el OK de cada "AT"; criterio propio
#define MODEM_TIMEOUT_COMANDO_MS 2000 // ms; timeout por defecto de los comandos sin tiempo propio; criterio propio

#define MODEM_SONDEOS_ENCENDIDO 3       // sondeos antes de decidir si hay que pulsar; criterio propio
#define MODEM_SONDEOS_APAGADO 2         // sondeos tras CPOWD; criterio propio

// Registro en la red
#define MODEM_TIMEOUT_LECTURA_MS 2000    // ms de un comando de consulta rápida (señal, operador...); criterio propio
#define MODEM_TIMEOUT_CPIN_MS 5000       // ms hasta que la SIM responde a AT+CPIN?; criterio propio
#define MODEM_TIMEOUT_SIM_OCUPADA_MS 15000 // ms esperando a que la SIM deje de estar ocupada (+CME ERROR: 14); criterio propio: 2 x los 7,2 s medidos como máximo
#define MODEM_TIMEOUT_REGISTRO_MS 120000 // ms; tiempo máximo de respuesta de AT+COPS= (manual 3.2.9); pasado, el ciclo sigue sin red
#define MODEM_SONDEO_REGISTRO_MS 1000    // ms entre consultas del estado de registro mientras se espera; criterio propio

#define MODEM_TAM_COMANDO 128         // bytes; el comando más largo es CNCFG o SHREQ, unos 50 B
#define MODEM_TIMEOUT_CGACT_MS 85000 // ms; el manual V3.00 no da máximo para AT+CGACT: se mantiene el valor usado con el SIM7000G#define MODEM_TIMEOUT_SHCONN_MS 70000 // ms; >= el TIMEOUT de SHCONF (60 s, valor por defecto) + 10 s de margen
#define MODEM_HTTP_CONNECTTO_S "40"          // s; límite de conexión de HTTPACTION; decisión propia
#define MODEM_HTTP_RESPTO_S "30"             // s; límite de respuesta del servidor; decisión propia
#define MODEM_TIMEOUT_HTTPACTION_MS 90000    // ms; 40 + 30 + 10 de RECVTO + 10 de margen
#define MODEM_TIMEOUT_HTTPINIT_MS 120000     // ms; máximo del manual V3.00, 13.2.1
#define MODEM_HTTPDATA_TIEMPO 1000           // tiempo máximo para enviar el cuerpo; manual V3.00, 13.2.7 (rango 10-65535)

// Tamaños de los textos de modem_red_t, ajustados a la pantalla
#define MODEM_TAM_OPERADOR 11   // 10 visibles + '\0' (paso 5.5); se trunca aquí
#define MODEM_TAM_TECNOLOGIA 7  // "WCDMA" + '\0', con 1 B de margen
#define MODEM_TAM_IP 16         // "255.255.255.255" + '\0'
#define MODEM_DBM_DESCONOCIDA 0 // ningún valor de CSQ da 0 dBm

#define MODEM_CNTP_SIN_CODIGO (-1) // no llegó +CNTP; 0 es éxito en la SIM7600 (manual V3.00, 15.2.2)

/** @brief Resultado de una operación con el módem. */
typedef enum
{
    MODEM_OK,
    MODEM_ERROR,
    MODEM_TIMEOUT
} modem_estado_t;

/** @brief Paso de modem_http_post en el que falló; permite distinguir "no llega al servidor" de "el módem rechaza un comando". */
typedef enum
{
    MODEM_PASO_CONFIG,   // HTTPINIT y HTTPPARA
    MODEM_PASO_PETICION, // HTTPDATA y cuerpo
    MODEM_PASO_RESPUESTA // HTTPACTION y espera de +HTTPACTION
} modem_paso_t;

/** @brief Datos de red del último ciclo, para la pantalla. */
typedef struct
{
    char operador[MODEM_TAM_OPERADOR];     // "" = sin dato
    char tecnologia[MODEM_TAM_TECNOLOGIA]; // "GSM", "WCDMA", "LTE" o ""
    int16_t dbm;                           // MODEM_DBM_DESCONOCIDA = sin dato
    char ip[MODEM_TAM_IP];                 // "" = sin conexión de datos
} modem_red_t;

/**
 * @brief Configura la UART1 (115200 8N1) y el GPIO del PWRKEY (a nivel bajo). No enciende el módem.
 * @return MODEM_OK o MODEM_ERROR si falla la UART o el GPIO.
 */
modem_estado_t modem_init(void);

/**
 * @brief Enciende el módem con un pulso de PWRKEY, salvo que ya conteste (PWRKEY conmuta: el mismo pulso apaga).
 *
 * Bloquea unos 6 s si hay que pulsar (pulso 1,1 s + espera 4,6 s + sondeos). Cuando contesta envía ATE0 y
 * AT+CMEE=1 (NO_SAVE: hay que mandarlos en cada encendido); si fallan, solo avisa en el log.
 *
 * @return MODEM_OK (ya encendido, o contesta tras el pulso) o MODEM_TIMEOUT si no contesta tras el pulso.
 */
modem_estado_t modem_encender(void);

/**
 * @brief Apaga el módem con AT+CPOWD=1 y comprueba que deja de contestar.
 *
 * Bloquea ~8 s: espera el Toff(uart) máximo (HD tabla 9).
 *
 * @return MODEM_OK (apagado), o MODEM_ERROR si falla la escritura o el módem sigue contestando.
 */
modem_estado_t modem_apagar(void);

/**
 * @brief Activa la conexión de datos (AT+CNCFG + AT+CNACT) y espera a que tenga IP.
 *
 * No hace nada si ya está activa (se llama al encender el módem y otra vez en cada envío).
 *
 * @return MODEM_OK, MODEM_ERROR (puntero nulo, comando truncado o rechazado) o MODEM_TIMEOUT
 *         (sin respuesta, o sin IP en MODEM_TIMEOUT_CNACT_MS).
 */
modem_estado_t modem_abrir_datos(const char *apn, const char *usuario, const char *clave);

/**
 * @brief Envía un POST con cuerpo JSON por la pila HTTP SH* del SIM7000 (manual cap. 9) y cierra la conexión.
 *
 * Bloquea hasta ~140 s en el peor caso (SHCONN y +SHREQ, 70 s cada uno).
 *
 * @param dominio    URL base con el formato "http://dominio" (SHCONF "URL", máx. 64 B).
 * @param ruta       Ruta del recurso (p. ej. "/plataforma/api/v1/medidas").
 * @param cuerpo     Cuerpo; el módem admite 1024 B (BODYLEN).
 * @param[out] codigo_out Código HTTP de +SHREQ; 0 si no llegó.
 * @param[out] paso_out   Paso en el que se está o falló (modem_paso_t); permite elegir el código de tx.
 * @return MODEM_OK (hay código HTTP en codigo_out), MODEM_ERROR (punteros nulos, comando truncado,
 *         ERROR del módem o +SHREQ ilegible) o MODEM_TIMEOUT.
 */
modem_estado_t modem_http_post(const char *dominio, const char *ruta, const char *cuerpo, size_t len, int *codigo_out, modem_paso_t *paso_out);

/**
 * @brief Envía "AT" hasta que el módem contesta OK.
 * @param intentos Número máximo de intentos (MODEM_TIMEOUT_SONDEO_MS cada uno).
 * @return MODEM_OK o MODEM_TIMEOUT.
 */
modem_estado_t modem_sondear_at(int intentos);

/**
 * @brief Espera a que el módem se registre en la red (AT+CEREG? para LTE y AT+CGREG? para GSM).
 *
 * Sondea los dos cada MODEM_SONDEO_REGISTRO_MS hasta que cualquiera dé stat 1 (casa) o 5 (roaming), los dos den
 * 3 (denegado) o pase timeout_ms. Con timeout_ms = 0 hace una sola lectura. Los comandos del sondeo no se
 * escriben en el log; sí los cambios de estado y el resultado.
 *
 * @param timeout_ms Plazo máximo de espera, en milisegundos.
 * @return MODEM_OK (registrado), MODEM_TIMEOUT (en la última lectura no contestó ninguno de los dos) o
 *         MODEM_ERROR (sin red al agotar el plazo, o registro denegado en los dos).
 */
modem_estado_t modem_esperar_registro(uint32_t timeout_ms);

/**
 * @brief Pregunta el estado de la SIM (AT+CPIN?).
 * @return MODEM_OK (READY), MODEM_ERROR (sin SIM, o SIM que pide PIN/PUK) o MODEM_TIMEOUT (el módem no contesta).
 */
modem_estado_t modem_sim_lista(void);

/**
 * @brief Lee operador, tecnología, señal e IP (COPS, CPSI, CSQ, CNACT).
 *
 * Un dato que falta (sin operador, CSQ 99...) no es un fallo: su campo queda vacío o en MODEM_DBM_DESCONOCIDA.
 *
 * @return MODEM_OK, MODEM_TIMEOUT si algún comando no contesta (los campos ya leídos se conservan) o MODEM_ERROR si red es NULL.
 */
modem_estado_t modem_leer_red(modem_red_t *red);

modem_estado_t modem_gnss_encender(void);

modem_estado_t modem_gnss_esperar_fix(uint32_t timeout_ms, uint32_t *utc_out);

modem_estado_t modem_gnss_apagar(void);

modem_estado_t modem_cntp(const char *servidor, uint32_t timeout_ms, uint32_t *utc_out, int *codigo_out);

/**
 * @brief Lee el pin STATUS del módem.
 *
 * @return 1 (módem encendido y listo) o 0.
 */
int modem_leer_status(void);

#endif