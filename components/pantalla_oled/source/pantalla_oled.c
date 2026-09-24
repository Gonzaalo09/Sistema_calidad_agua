/*
 * pantalla_oled.c
 *
 * Driver de la pantalla OLED SSD1306 (128x64, I2C) sobre la librería u8g2.
 *
 * Fuentes:
 * SSD1306 datasheet Rev 1.1, Solomon Systech. https://cdn-shop.adafruit.com/datasheets/SSD1306.pdf
 * https://github.com/olikraus/u8g2/wiki/Porting-to-new-MCU-platform#communication-callback-eg-u8x8_byte_hw_i2c
 * https://github.com/espressif/esp-idf/blob/v6.0.1/examples/peripherals/i2c/i2c_u8g2/main/i2c_u8g2_main.c
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/i2c_master.h"

#include "esp_log.h"

#include "u8g2.h"

#include "I2C_bus.h"
#include "pantalla_oled.h"

#define OLED_BUF_TX 132 ///< Una transacción de u8g2 no pasa de ~33 bytes (0x00 + comando + argumentos, o 0x40 + 24 datos)

static const uint16_t OLED_DIR_I2C = 0x3C; ///< dirección de 7 bits 0111100 (SA0 = 0), datasheet pág. 19
static const uint32_t OLED_VELOCIDAD_HZ = 100000;
static const int OLED_TIMEOUT_MS = 100;

static const char *TAG = "OLED_PANTALLA";

static u8g2_t u8g2;
static i2c_master_dev_handle_t oled_dev = NULL;
static uint8_t buf_tx[OLED_BUF_TX];
static size_t idx_tx = 0;
static oled_estado_t oled_est = OLED_OK;

/*=========================================CALLBACKS DE U8G2 (COMUNICACIÓN CON EL ESP32)============================================*/

/**
 * @brief Callback de bytes de u8g2: toda la comunicación I2C entre la librería y el SSD1306 pasa por aquí
 *
 * @param u8x8 Estructura u8x8 (no se usa)
 * @param msg Tipo de mensaje que envía u8g2
 * @param arg_int Argumento entero, depende del mensaje (en U8X8_MSG_BYTE_SEND, número de bytes)
 * @param arg_ptr Argumento puntero, depende del mensaje (en U8X8_MSG_BYTE_SEND, bytes a enviar)
 * @return uint8_t 1 si el mensaje se atendió correctamente, 0 si falló o no se reconoce
 */
static uint8_t u8x8_callback_bytes_i2c(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    switch (msg)
    {
    // Se envía una vez al inicializar la pantalla. El dispositivo ya está
    // registrado en el bus cuando se llama a esta función
    case U8X8_MSG_BYTE_INIT:
        break;

    // Inicio de transferencia (en SPI activaría CS; en I2C no hay CS).
    // Se resetea el buffer
    case U8X8_MSG_BYTE_START_TRANSFER:
        idx_tx = 0;
        break;

    // Nivel del pin data/command: solo existe en SPI, en I2C no hace falta
    case U8X8_MSG_BYTE_SET_DC:
        break;

    // Copia al buffer los arg_int bytes que hay en arg_ptr
    case U8X8_MSG_BYTE_SEND:
        if (idx_tx + arg_int > OLED_BUF_TX)
        {
            oled_est = OLED_ERROR_INTERNO;
            return 0;
        }
        for (size_t i = 0; i < arg_int; i++)
        {
            buf_tx[idx_tx++] = *((uint8_t *)arg_ptr + i);
        }
        break;

    // Fin de transferencia (en SPI desactivaría CS). Se transmite el buffer por I2C
    case U8X8_MSG_BYTE_END_TRANSFER:
        if (idx_tx > 0)
        {
            esp_err_t err = i2c_master_transmit(oled_dev, buf_tx, idx_tx, OLED_TIMEOUT_MS);
            if (err != ESP_OK)
            {
                oled_est = OLED_NO_RESPONDE;
                ESP_LOGE(TAG, "OLED no responde: %s (0x%x)", esp_err_to_name(err), err);
                return 0;
            }
        }
        break;

    default:
        return 0;
    }
    return 1;
}

/**
 * @brief Callback de GPIO y esperas de u8g2: atiende los retardos y el pin de reset que pide la librería
 *
 * @param u8x8 Estructura u8x8 (no se usa)
 * @param msg Tipo de mensaje que envía u8g2
 * @param arg_int Argumento entero, depende del mensaje (en U8X8_MSG_DELAY_MILLI, milisegundos)
 * @param arg_ptr Argumento puntero (no se usa)
 * @return uint8_t 1 si el mensaje se atendió correctamente, 0 si no se reconoce
 */
static uint8_t u8x8_gpio_callback_delay(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    // Prueba temporal para ver los mensajes de delay que se envían
    ESP_LOGW(TAG, "delay_cb: msg=%d arg=%d", msg, arg_int);
    switch (msg)
    {
    // Aviso de que u8g2 va a empezar a pedir GPIO y delays. No hay pines que
    // configurar en este montaje (I2C con el driver del ESP32), no hace falta acción
    case U8X8_MSG_GPIO_AND_DELAY_INIT:
        ESP_LOGI(TAG, "GPIO and delay initialization completed");
        break;

    // Espera bloqueante en milisegundos. La usa la secuencia de reset del SSD1306
    // al inicializar (tres esperas de 100 ms según el datasheet)
    case U8X8_MSG_DELAY_MILLI:
        vTaskDelay(pdMS_TO_TICKS(arg_int));
        break;

    // u8g2 pide mover el pin de reset del panel; este módulo no lo tiene
    // cableado (solo GND, VDD, SCK, SDA), se ignora
    case U8X8_MSG_GPIO_RESET:
        break; // No tiene pin de reset

    default:
        return 0;
    }
    return 1;
}

/*=========================================FUNCIONES PUBLICAS============================================*/

/**
 * @brief Inicializa la pantalla OLED: la registra en el bus I2C, comprueba que responde
 * y arranca el SSD1306 con u8g2
 *
 * @return oled_estado_t OLED_OK si la pantalla quedó encendida y lista para dibujar
 */
oled_estado_t pantalla_oled_init(void)
{
    oled_est = OLED_OK;
    i2c_master_bus_handle_t bus = get_i2c_bus();

    // El bus I2C tiene que existir ya (I2C_bus lo crea en app_main
    // antes de llamar aquí). Sin bus no hay nada que hacer
    if (bus == NULL)
    {
        oled_est = OLED_BUS_NO_DISPONIBLE;
        return oled_est;
    }

    // Rregistra el dispositivo solo si no lo está ya. Evita
    // registrarlo dos veces si pantalla_oled_init() se llegara a llamar más de una vez
    if (oled_dev == NULL)
    {
        i2c_device_config_t dev_config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = OLED_DIR_I2C,
            .scl_speed_hz = OLED_VELOCIDAD_HZ,
        };
        esp_err_t err = i2c_master_bus_add_device(bus, &dev_config, &oled_dev);

        // El registro en el bus puede fallar (memoria, dirección
        // ya usada por otro dispositivo...). Si falla, no hay handle válido
        if (err != ESP_OK)
        {
            oled_dev = NULL;
            oled_est = OLED_NO_RESPONDE;
            return oled_est;
        }
    }

    esp_err_t err = i2c_master_probe(bus, OLED_DIR_I2C, OLED_TIMEOUT_MS);

    // Comprobamos con e probe si el dispositivo está realmente ahí
    if (err != ESP_OK)
    {
        // Sin respuesta: se quita el dispositivo del bus para no dejar un
        // handle "registrado" pero inservible, y así un reintento futuro
        // vuelve a entrar en la comprobación 2 y lo registra de cero
        i2c_master_bus_rm_device(oled_dev);
        oled_dev = NULL;
        oled_est = OLED_NO_RESPONDE;
        return oled_est;
    }

    // Crea un buffer dentro del ESP32
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(&u8g2, U8G2_R0, u8x8_callback_bytes_i2c, u8x8_gpio_callback_delay);

    // ~300 ms: aquí es donde U8g2 llama a las dos callbacks y salen los
    // mensajes de prueba (delay_cb) en el log
    u8g2_InitDisplay(&u8g2);

    // Si algo falló durante la secuencia (por ejemplo, i2c_master_transmit dentro de
    // u8x8_callback_bytes_i2c), lo único que queda es lo que esa callback
    // haya dejado escrito en oled_est
    if (oled_est != OLED_OK)
    {
        return oled_est;
    }

    u8g2_SetPowerSave(&u8g2, 0);
    return oled_est;
}

/**
 * @brief Devuelve el último estado de la pantalla
 *
 * @return oled_estado_t Estado que dejó la última operación (init o transmisión I2C)
 */
oled_estado_t pantalla_oled_estado(void)
{
    return oled_est;
}

/**
 * @brief Pantalla de prueba: regla de 25 caracteres, acentos/ñ y un mensaje de error de ejemplo.
 * No hace nada si oled_est != OLED_OK
 */
void pantalla_oled_texto_prueba(void)
{
    if (oled_est != OLED_OK)
    {
        return;
    }
    // Se borra el buffer del esp32
    u8g2_ClearBuffer(&u8g2);
    u8g2_SetFont(&u8g2, u8g2_font_5x8_tf);
    // x e y son el punto donde empieza la línea base, aquí (0, 8)
    u8g2_DrawUTF8(&u8g2, 0, 8, "1234567890123456789012345");
    u8g2_DrawUTF8(&u8g2, 0, 16, "Fósforo ñ á é í ó ú ¿?");
    u8g2_DrawUTF8(&u8g2, 0, 24, "PH: SIN RESPUESTA");
    u8g2_SendBuffer(&u8g2);
}
