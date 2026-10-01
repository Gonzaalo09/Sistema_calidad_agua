#ifndef MODEM_H
#define MODEM_H

#include <stddef.h>
#include <stdint.h>

// Pines de conexión (Hardware)
#define MODEM_PIN_TX                 27 // Ya dado
#define MODEM_PIN_RX                 26 // Ya dado
#define MODEM_PIN_PWRKEY             4  // Ya dado

// Configuración de la UART (115200 8N1, sin control de flujo)
#define MODEM_BAUDIOS                115200 // El autobaud admite 9600-115200, se conecta a 115200 automaticamente

// Dimensionamiento de memoria
#define MODEM_UART_RX_BUF_SIZE       1024   // Exigencia uart_driver_install (>128B). Margen para respuestas largas
#define MODEM_TAM_RESPUESTA          256    // Margen holgado para respuestas extensas como AT+CGMR o AT+CPSI?

// Tiempos de Secuencia de Encendido
#define MODEM_PULSO_ENCENDIDO_MS     1100   // Ton >= 1 s + 10% de margen Ton(time on)
#define MODEM_ESPERA_UART_MS         4600   // Ton(uart) >= 4.5 s + 100 ms (inicio del sondeo)

// Tiempos de Secuencia de Apagado (HD Tabla 9)
#define MODEM_PULSO_APAGADO_MS       1300   // Toff >= 1.2 s (Para apagado físico de emergencia)
#define MODEM_ESPERA_APAGADO_MS      7000   // Toff(uart) máx. 6.9 s (Límite para recibir NORMAL POWER DOWN)
#define MODEM_ESPERA_REENCENDIDO_MS  2000   // Toff-on >= 2 s (Reposo obligatorio antes de un nuevo PWRKEY)

// Parámetros lógicos de la Máquina de Estados AT
#define MODEM_SONDEOS_AT             10     // Número de reintentos para asegurar sincronización de autobaud
#define MODEM_TIMEOUT_SONDEO_MS      500    // Tiempo de bloqueo esperando el OK inicial
#define MODEM_TIMEOUT_COMANDO_MS     2000   // Timeout por defecto para comandos de red sin especificación

#define MODEM_SONDEOS_ENCENDIDO   3      // sondeos antes de decidir si hay que pulsar
#define MODEM_SONDEOS_APAGADO     2      // sondeos tras CPOWD
#define MODEM_TIMEOUT_POWERDOWN_MS 3000  // espera del URC NORMAL POWER DOWN

// Registro en la red (timeouts de criterio propio, sin fuente)
#define MODEM_TIMEOUT_LECTURA_MS   2000    // Timeout de un comando de consulta rápida (señal, operador...)
#define MODEM_TIMEOUT_CPIN_MS      5000    // Espera máxima a que la SIM responda al consultar su estado (AT+CPIN?)
#define MODEM_TIMEOUT_REGISTRO_MS  120000  // Tiempo máximo total para registrarse en la red (2 min); pasado, el ciclo sigue sin red
#define MODEM_SONDEO_REGISTRO_MS   1000    // Intervalo entre consultas del estado de registro mientras se espera

// Valores del campo <stat> de las respuestas de registro (AT+CREG?/CEREG?).
#define STAT_SIN_LEER     (-1)  // Valor propio, no del módem: aún no se ha leído o no se pudo interpretar
#define STAT_REG_CASA     1     // Registrado en la red de su operador
#define STAT_DENEGADO     3     // Registro denegado por la red
#define STAT_REG_ROAMING  5     // Registrado en otra red (roaming)

typedef enum {
    MODEM_OK,
    MODEM_ERROR,
    MODEM_TIMEOUT
} modem_estado_t; 

modem_estado_t modem_init(void);

modem_estado_t modem_encender(void); 

modem_estado_t modem_comando(const char *cmd, char *resp_out, size_t tam_resp, uint32_t timeout_ms);

modem_estado_t modem_apagar(void);

#endif  