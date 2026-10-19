#ifndef MODEM_H
#define MODEM_H

#include <stddef.h>
#include <stdint.h>

// Fuentes: SIM7000 Series AT Command Manual V1.06 (manual); SIM7000 Hardware Design V1.07 (HD); esquemático T-SIM7000G V1.6.
// Pines de conexión (Hardware), vistos desde el ESP32 (esquemático T-SIM7000G V1.6)
#define MODEM_PIN_TX 27    // GPIO de TX del ESP32 hacia el RXD del módem
#define MODEM_PIN_RX 26    // GPIO de RX del ESP32 desde el TXD del módem
#define MODEM_PIN_PWRKEY 4 // GPIO que activa PWRKEY a través de Q4: alto = PWRKEY a GND

// Configuración de la UART (115200 8N1, sin control de flujo)
#define MODEM_BAUDIOS 115200 // baudios; el autobaud admite 9600-115200, se conecta a 115200 automaticamente

// Dimensionamiento de memoria
#define MODEM_UART_RX_BUF_SIZE 1024 // bytes; uart_driver_install exige más de 128. Margen para respuestas largas
#define MODEM_TAM_RESPUESTA 256     // bytes; margen holgado para respuestas extensas como AT+CGMR o AT+CPSI?

// Tiempos de Secuencia de Encendido (HD tabla 8)
#define MODEM_PULSO_ENCENDIDO_MS 1100 // ms; Ton >= 1 s + 10 % de margen
#define MODEM_ESPERA_UART_MS 4600     // ms; Ton(uart) >= 4,5 s + 100 ms (inicio del sondeo)

// Tiempos de Secuencia de Apagado (HD tabla 9)
#define MODEM_ESPERA_APAGADO_MS 7000     // ms; Toff(uart) máx. 6,9 s (límite para recibir NORMAL POWER DOWN)

// Parámetros lógicos de la Máquina de Estados AT
#define MODEM_SONDEOS_AT 10           // reintentos de "AT" para asegurar la sincronización de autobaud; criterio propio
#define MODEM_TIMEOUT_SONDEO_MS 500   // ms esperando el OK de cada "AT"; criterio propio
#define MODEM_TIMEOUT_COMANDO_MS 2000 // ms; timeout por defecto de los comandos sin tiempo propio; criterio propio

#define MODEM_SONDEOS_ENCENDIDO 3       // sondeos antes de decidir si hay que pulsar; criterio propio
#define MODEM_SONDEOS_APAGADO 2         // sondeos tras CPOWD; criterio propio
#define MODEM_TIMEOUT_POWERDOWN_MS 3000 // ms de espera del URC NORMAL POWER DOWN; criterio propio (medido: llega a los 30 ms)

// Registro en la red
#define MODEM_TIMEOUT_LECTURA_MS 2000    // ms de un comando de consulta rápida (señal, operador...); criterio propio
#define MODEM_TIMEOUT_CPIN_MS 5000       // ms hasta que la SIM responde a AT+CPIN?; criterio propio
#define MODEM_TIMEOUT_REGISTRO_MS 120000 // ms; tiempo máximo de respuesta de AT+COPS= (manual 3.2.9); pasado, el ciclo sigue sin red
#define MODEM_SONDEO_REGISTRO_MS 1000    // ms entre consultas del estado de registro mientras se espera; criterio propio

#define MODEM_TAM_COMANDO 128        // bytes; el comando más largo es CNCFG o SHREQ, unos 50 B
#define CNACT_ACTIVA 1               // <status> de AT+CNACT? cuando la conexión de datos está activa (manual 5.2.30)
#define MODEM_TIMEOUT_CNACT_MS 85000 // ms; el manual V1.06 no da máximo ("-"): se toma el de la operación equivalente de la pila clásica (SAPBR=1, 7.2.1). Medido: 1,05–3,1 s
#define MODEM_SONDEO_CNACT_MS 1000    // ms entre consultas de CNACT? mientras se espera la IP; criterio propio
#define MODEM_TIMEOUT_SHCONN_MS 70000 // ms; >= el TIMEOUT de SHCONF (60 s, valor por defecto) + 10 s de margen
#define MODEM_SHBODEXT_MS 1000        // ms que el módem espera el cuerpo; rango 100–10000 (manual 9.2.5). 525 B a 115200 baudios tardan 46 ms
#define MODEM_PAUSA_CUERPO_MS 100     // ms entre SHBODEXT y el cuerpo; los comandos contestan en <= 20 ms (medido en el 5.1), más margen
#define MODEM_TIMEOUT_SHREQ_MS 70000  // ms; 60 s de SHCONF "TIMEOUT" más 10 s de margen para que llegue el +SHREQ del módem


// Valores del campo <stat> de las respuestas de registro (AT+CREG?/CEREG?/CGREG?).
#define STAT_SIN_LEER (-1) // Valor propio, no del módem: aún no se ha leído o no se pudo interpretar
#define STAT_REG_CASA 1    // Registrado en la red de su operador
#define STAT_DENEGADO 3    // Registro denegado por la red
#define STAT_REG_ROAMING 5 // Registrado en otra red (roaming)

// Tamaños de los textos de modem_red_t, ajustados a la pantalla
#define MODEM_TAM_OPERADOR   11   // 10 visibles + '\0' (paso 5.5); se trunca aquí
#define MODEM_TAM_TECNOLOGIA 7    // "NB-IOT" + '\0'
#define MODEM_TAM_IP         16   // "255.255.255.255" + '\0'
#define MODEM_DBM_DESCONOCIDA 0   // ningún valor de CSQ da 0 dBm

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
    MODEM_PASO_CONFIG,     // SHCONF (URL, BODYLEN, HEADERLEN)
    MODEM_PASO_CONEXION,   // SHCONN: abre la conexión HTTP con el servidor (manual 9.2.3)
    MODEM_PASO_PETICION,   // SHAHEAD, SHBODEXT + cuerpo, envío de SHREQ(manda el POST)
    MODEM_PASO_RESPUESTA   // espera de +SHREQ y lectura del código
} modem_paso_t;

/** @brief Datos de red del último ciclo, para la pantalla. */
typedef struct
{
    char operador[MODEM_TAM_OPERADOR];     // "" = sin dato
    char tecnologia[MODEM_TAM_TECNOLOGIA]; // "GSM", "CAT-M", "NB-IOT" o ""
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
 * Bloquea unos 6 s si hay que pulsar (pulso 1,1 s + espera 4,6 s + sondeos).
 *
 * @return MODEM_OK (ya encendido, o contesta tras el pulso) o MODEM_TIMEOUT si no contesta tras el pulso.
 */
modem_estado_t modem_encender(void);

/**
 * @brief Envía un comando AT al módem desde fuera del componente.
 *
 * Puerta pública de ejecutar() (static): la usan los componentes externos
 * (tarea_transmision). Valida los punteros y delega con log_fallo a true.
 * Vacía el UART antes de enviar: se pierden los avisos que el módem mande por su cuenta.
 *
 * @param cmd        Comando sin terminador (p. ej. "AT+CPIN?").
 * @param resp_out   Buffer donde se guarda la respuesta, siempre terminada en '\0'.
 * @param tam_resp   Tamaño de resp_out en bytes (incluye el '\0').
 * @param timeout_ms Tiempo máximo total de espera, en milisegundos.
 *
 * @return MODEM_OK       Llegó "\r\nOK\r\n".
 * @return MODEM_ERROR    Puntero nulo o tam_resp 0; o lo que devuelve ejecutar(),
 *                        (ERROR/+CME ERROR, respuesta que no cabe, fallo de UART).
 * @return MODEM_TIMEOUT  No llegó ni OK ni error dentro de timeout_ms.
 */
modem_estado_t modem_comando(const char *cmd, char *resp_out, size_t tam_resp, uint32_t timeout_ms);

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
 * No hace nada si ya está activa (el módem sigue encendido entre ciclos).
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
 * @param ruta       Ruta del recurso (p. ej. "/servidor_3/api/v1/medidas").
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

#endif