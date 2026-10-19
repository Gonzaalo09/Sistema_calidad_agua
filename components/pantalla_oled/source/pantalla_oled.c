/*
 * Driver de la OLED SSD1306 (128x64, I2C) sobre u8g2: callbacks de la librería y primitivas limpiar/texto/mostrar.
 * Fuentes: SSD1306 datasheet Rev 1.1 (Solomon Systech, https://cdn-shop.adafruit.com/datasheets/SSD1306.pdf);
 * wiki de u8g2 "Porting to new MCU platform"; ejemplo i2c_u8g2 de ESP-IDF
 * (https://github.com/espressif/esp-idf/blob/v6.0.1/examples/peripherals/i2c/i2c_u8g2/main/i2c_u8g2_main.c).
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
static const uint32_t OLED_VELOCIDAD_HZ = 100000; ///< Hz; como el RTC, que comparte bus
static const int OLED_TIMEOUT_MS = 100;            ///< ms por transacción; evita que una OLED colgada bloquee el bus del RTC

static const char *TAG = "OLED_PANTALLA";

static u8g2_t u8g2;
static i2c_master_dev_handle_t oled_dev = NULL; // si la pantalla llegó a registrarse; solo cambia en init
static uint8_t buf_tx[OLED_BUF_TX];
static size_t idx_tx = 0;
static oled_estado_t oled_est = OLED_OK;        // cómo fue el último intento de dibujar; se reinicia en cada frame (no es lo mismo que oled_dev)

/*=========================================CALLBACKS DE U8G2 (COMUNICACIÓN CON EL ESP32)============================================*/

/**
 * @brief Callback de bytes de u8g2: toda la comunicación I2C con el SSD1306 pasa por aquí.
 *
 * Deja el fallo en oled_est (u8g2 no propaga errores). Devuelve 1 si atendió el mensaje, 0 si falló o no lo reconoce.
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
                if (oled_est == OLED_OK) // solo el primer fallo de cada frame: un frame son 64 transacciones y llenarían la consola
                {
                    ESP_LOGE(TAG, "OLED no responde: %s (0x%x)", esp_err_to_name(err), err);
                }
                oled_est = OLED_NO_RESPONDE;
                return 0;
            }
        }
        break;

    default:
        return 0;
    }
    return 1;
}

/** @brief Callback de GPIO y esperas de u8g2: atiende los retardos y el pin de reset. Devuelve 1 si atendió el mensaje, 0 si no lo reconoce. */
static uint8_t u8x8_gpio_callback_delay(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    // Prueba temporal para ver los mensajes de delay que se envían
    //ESP_LOGW(TAG, "delay_cb: msg=%d arg=%d", msg, arg_int);
    switch (msg)
    {
    // Aviso de que u8g2 va a empezar a pedir GPIO y delays. No hay pines que
    // configurar en este montaje (I2C con el driver del ESP32), no hace falta acción
    case U8X8_MSG_GPIO_AND_DELAY_INIT:
        ESP_LOGI(TAG, "GPIO and delay initialization completed");
        break;

    // Espera bloqueante en milisegundos. La usa la secuencia de reset de u8g2
    // al inicializar (tres esperas de 100 ms, verificado en el log del montaje)
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

oled_estado_t pantalla_oled_init(void)
{
    oled_est = OLED_OK;
    i2c_master_bus_handle_t bus = get_i2c_bus();

    // Sin bus (i2c_bus_init no llamado o fallido) no hay nada que hacer
    if (bus == NULL)
    {
        oled_est = OLED_BUS_NO_DISPONIBLE;
        return oled_est;
    }

    // Se registra solo si no lo está ya: así init se puede llamar más de una vez
    if (oled_dev == NULL)
    {
        i2c_device_config_t dev_config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = OLED_DIR_I2C,
            .scl_speed_hz = OLED_VELOCIDAD_HZ,
        };
        esp_err_t err = i2c_master_bus_add_device(bus, &dev_config, &oled_dev);

        // Puede fallar (memoria, dirección ya usada...); entonces no hay handle válido
        if (err != ESP_OK)
        {
            oled_dev = NULL;
            oled_est = OLED_NO_RESPONDE;
            return oled_est;
        }
    }

    esp_err_t err = i2c_master_probe(bus, OLED_DIR_I2C, OLED_TIMEOUT_MS);

    // El probe comprueba que el dispositivo está realmente ahí
    if (err != ESP_OK)
    {
        // Sin respuesta: se quita del bus para no dejar un handle registrado pero
        // inservible; un reintento futuro lo vuelve a registrar de cero
        i2c_master_bus_rm_device(oled_dev);
        oled_dev = NULL;
        oled_est = OLED_NO_RESPONDE;
        return oled_est;
    }

    // Variante "_f": buffer de frame completo en RAM del ESP32
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(&u8g2, U8G2_R0, u8x8_callback_bytes_i2c, u8x8_gpio_callback_delay);

    // ~300 ms (3 × 100 ms del reset de u8g2); aquí u8g2 llama a las dos callbacks
    u8g2_InitDisplay(&u8g2);

    // u8g2 no propaga errores: el único rastro de un fallo en la secuencia
    // es lo que la callback de bytes haya dejado escrito en oled_est
    if (oled_est != OLED_OK)
    {
        return oled_est;
    }

    u8g2_SetPowerSave(&u8g2, 0);
    return oled_est;
}

oled_estado_t pantalla_oled_estado(void)
{
    return oled_est;
}

void pantalla_oled_texto_prueba(void)
{
    if (oled_est != OLED_OK)
    {
        return;
    }
    u8g2_ClearBuffer(&u8g2);
    u8g2_SetFont(&u8g2, u8g2_font_5x8_tf);
    // x e y son el punto donde empieza la línea base, aquí (0, 8)
    u8g2_DrawUTF8(&u8g2, 0, 8, "1234567890123456789012345");
    u8g2_DrawUTF8(&u8g2, 0, 16, "Fósforo ñ á é í ó ú ¿?");
    u8g2_DrawUTF8(&u8g2, 0, 24, "PH: SIN RESPUESTA");
    u8g2_SendBuffer(&u8g2);
}

void pantalla_oled_limpiar(void)
{
    // Se mira oled_dev y no oled_est: con oled_est la pantalla se congelaba tras un fallo del bus
    // (oled_est se quedaba en error) o, con la condición invertida, no se borraba el buffer
    if (oled_dev == NULL)
    {
        return;
    }
    oled_est = OLED_OK;

    u8g2_ClearBuffer(&u8g2);
}

void pantalla_oled_texto(uint8_t linea, const char *texto)
{
    if (oled_est != OLED_OK)
    {
        return;
    }
    if (linea >= PANTALLA_LINEAS)
    {
        return;
    }
    u8g2_SetFont(&u8g2, u8g2_font_5x8_tf);
    // (linea + 1) * 8: en u8g2 la y de DrawUTF8 es la línea base del texto, no su borde superior. La línea 0 va en y=8, la 7 en y=64.
    // Se usa DrawUTF8 (no DrawStr) para los acentos y la ñ; los archivos fuente deben estar en UTF-8.
    u8g2_DrawUTF8(&u8g2, 0, (linea + 1) * 8, texto);
}

void pantalla_oled_mostrar(void)
{
    if (oled_est != OLED_OK)
    {
        return;
    }
    u8g2_SendBuffer(&u8g2);
}
