/*
 * Pinta en la OLED (25 columnas x 8 lineas) dos paginas: sensores, y red + SD; el estado de cada fuente sale en lenguaje llano.
 * Fuentes: u8g2_font_5x8_tf (5x8 px por caracter); textos y acciones de la pagina 2 definidos en el proyecto (paso 5.5).
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sensores.h"
#include "estado_sistema.h"
#include "config_nodo.h"
#include "pantalla_oled.h"
#include "tarea_rtc.h"
#include "tarea_pantalla.h"
#include "registro_csv.h"

#define REFRESCO_MS 1000        // ms entre repintados; el reloj con segundos es el latido del firmware
#define SEGUNDOS_POR_PAGINA 5   // repintados (= segundos con REFRESCO_MS 1000) antes de cambiar de página; criterio propio
#define NUM_PAGINAS 2           // página 0: sensores; página 1: red + SD
#define TAM_TEXTO_ENVIO 18 // 17 caracteres como maximo + '\0': con " !hh:mm" caben las 25 columnas

/**
 * @brief Añade texto al final de una linea sin desbordarla; devuelve la nueva longitud usada (nunca mayor que tam-1).
 *
 * vsnprintf devuelve los caracteres que HABRIA escrito, que puede ser mas
 * de los que caben; por eso se recorta el resultado a tam-1.
 */
static size_t anadir(char *linea, size_t tam, size_t usado, const char *formato, ...)
{
    if (usado + 1 >= tam)
    {
        return usado;
    }

    va_list args;
    va_start(args, formato);
    int escrito = vsnprintf(linea + usado, tam - usado, formato, args);
    va_end(args);

    if (escrito < 0)
    {
        return usado;
    }

    usado += (size_t)escrito;

    if (usado >= tam)
    {
        usado = tam - 1;
    }

    return usado;
}

/**
 * @brief Linea 0: reloj + ID del nodo; linea 1 solo sin hora. Comun a las dos paginas.
 *
 * "29/09 14:38:05 ID:125010" (24 columnas). Sin año: con el no cabe el ID.
 * Los segundos avanzando son el latido del sistema: si se quedan parados,
 * el firmware esta colgado.
 */
static void pintar_reloj(void)
{
    char linea[PANTALLA_COLUMNAS + 1];
    char hora[15]; // "29/09 14:38:05" + '\0'

    uint32_t ts = rtc_timestamp_pais();

    if (ts == 0)
    {
        snprintf(linea, sizeof(linea), "-- SIN HORA -- ID:%s", config_nodo_id());
        pantalla_oled_texto(0, linea);
        pantalla_oled_texto(1, "  RTC no contesta");
    }
    else
    {
        time_t t = (time_t)ts;
        struct tm local;

        // ts ya lleva sumado OFFSET_PAIS_S, asi que gmtime_r da la hora del pais
        gmtime_r(&t, &local);

        strftime(hora, sizeof(hora), "%d/%m %H:%M:%S", &local);
        snprintf(linea, sizeof(linea), "%s ID:%s", hora, config_nodo_id());

        pantalla_oled_texto(0, linea);
    }
}

/**
 * @brief Pagina 1: reloj, valores de cada sensor y sus avisos.
 *
 * Un sensor averiado conserva sus ultimos valores con la marca "!hh:mm" (hora del ultimo dato bueno),
 * para que nadie los confunda con datos frescos.
 */
static void pintar_pagina_datos(void)
{
    char linea[PANTALLA_COLUMNAS + 1];
    char hora[6];
    estado_fuente_t e;

    pintar_reloj();
    // De la linea 2 en adelante: un sensor por linea, dos si esta averiado.
    uint8_t l = 2;

    for (size_t i = 0; i < NUM_SENSORES; i++)
    {
        if (l >= PANTALLA_LINEAS)
        {
            break;
        }

        if (estado_sistema_leer(FUENTE_SENSOR_BASE + i, &e) == false)
        {
            continue;
        }

        bool fallo = (e.codigo != ESTADO_OK) && (e.codigo != ESTADO_SIN_DATOS);

        size_t n = 0;
        n = anadir(linea, sizeof(linea), n, "%-4s", e.nombre);

        if (e.num_valores == 0)
        {
            n = anadir(linea, sizeof(linea), n, "---");
        }
        else
        {
            for (uint8_t v = 0; v < e.num_valores; v++)
            {
                n = anadir(linea, sizeof(linea), n, " %.1f", e.valores[v]);
            }
        }

        if (fallo == true)
        {
            estado_sistema_hora_legible(e.ultimo_ts_ok, hora, sizeof(hora));
            n = anadir(linea, sizeof(linea), n, " !%s", hora);
        }

        pantalla_oled_texto(l, linea);
        l++;

        // Linea con la accion concreta, solo si falla y queda sitio
        if ((fallo == true) && (l < PANTALLA_LINEAS))
        {
            const char *accion = estado_sistema_accion(e.codigo);

            if (accion[0] != '\0')
            {
                snprintf(linea, sizeof(linea), "  %s: %s", e.nombre, accion);
                pantalla_oled_texto(l, linea);
                l++;
            }
        }
    }
}

/**
 * @brief Lineas 6 y 7 de la pagina 2: estado de la SD.
 *
 * OK:    "SD OK, guardado 14:30" (hora del ultimo guardado correcto).
 * Fallo: "SD ERROR ESCRITURA !14:30" (25 columnas justas) y debajo la
 *        accion. Sin ningun guardado desde el arranque, "!--:--".
 * El porcentaje libre no se muestra: marcaria 99 % durante años.
 */
static void pintar_sd(void)
{
    char linea[PANTALLA_COLUMNAS + 1];
    char hora[6];
    estado_fuente_t e;

    if (estado_sistema_leer(FUENTE_SD, &e) == false)
    {
        return;
    }

    estado_sistema_hora_legible(e.ultimo_ts_ok, hora, sizeof(hora));

    if (e.codigo == ESTADO_OK)
    {
        snprintf(linea, sizeof(linea), "SD OK, guardado %s", hora);
        pantalla_oled_texto(6, linea);
        return;
    }

    // Antes del primer ciclo no hay estado: como los sensores, sin marca ni accion
    if (e.codigo == ESTADO_SIN_DATOS)
    {
        snprintf(linea, sizeof(linea), "SD %s", estado_sistema_texto(e.codigo));
        pantalla_oled_texto(6, linea);
        return;
    }

    snprintf(linea, sizeof(linea), "SD %s !%s", estado_sistema_texto(e.codigo), hora);
    pantalla_oled_texto(6, linea);

    const char *accion = estado_sistema_accion(e.codigo);

    if (accion[0] != '\0')
    {
        snprintf(linea, sizeof(linea), "  %s", accion);
        pantalla_oled_texto(7, linea);
    }
}

/**
 * @brief Texto de la linea 4 de la pagina 2 segun el tx del ultimo envio (max. 17 caracteres).
 */
static void texto_envio(int tx, char *buf, size_t n)
{
    const char *t;

    switch (tx)
    {
    case TX_HTTP_OK:
        t = "ENVÍO OK";
        break;
    case TX_SIN_SIM:
        t = "SIN SIM";
        break;
    case TX_SIN_RED:
        t = "SIN RED";
        break;
    case TX_SIN_CONEXION:
        t = "SIN DATOS";
        break;
    case TX_MODEM_NO_CONTESTA:
        t = "MODEM NO CONTESTA";
        break;
    case TX_SIN_SERVIDOR:
        t = "SIN SERVIDOR";
        break;
    case TX_ERROR_MODEM:
        t = "ERROR MODEM";
        break;
    case TX_SIN_RESPUESTA:
        t = "SIN RESPUESTA";
        break;
    case TX_TRAMA_NO_CABE:
        t = "TRAMA NO CABE";
        break;
    case TX_HTTP_MAL_FORMADA:
        t = "TRAMA RECHAZADA";
        break;
    case TX_HTTP_DESACTIVADO:
        t = "NODO DESACTIVADO";
        break;
    case TX_HTTP_DUPLICADO:
        t = "SEQ DUPLICADO";
        break;
    case TX_HTTP_ERROR_SERVIDOR:
        t = "ERROR SERVIDOR";
        break;
    default:
        snprintf(buf, n, "HTTP %d", tx);
        return;
    }
    snprintf(buf, n, "%s", t);
}

/**
 * @brief Accion de la linea 5. Vacia si nadie puede hacer nada en el sitio.
 */
static const char *accion_envio(int tx)
{
    switch (tx)
    {
    case TX_HTTP_OK:
    case TX_SIN_SERVIDOR:
    case TX_SIN_RESPUESTA:
    case TX_HTTP_ERROR_SERVIDOR:
        return "";
    case TX_SIN_SIM:
        return "revisar SIM";
    case TX_SIN_RED:
        return "revisar antena";
    case TX_SIN_CONEXION:
        return "revisar saldo de SIM";
    default: // 4, 6, 8, 400, 403, 409 y cualquier otro codigo HTTP
        return "avisar al tecnico";
    }
}

/**
 * @brief Lineas 2 a 5 de la pagina 2: operador, tecnologia, señal, IP y ultimo envio.
 */
static void pintar_red(void)
{
    char linea[PANTALLA_COLUMNAS + 1];
    char texto[TAM_TEXTO_ENVIO];
    char hora[6];
    registro_red_t r;
    size_t n;

    if (estado_sistema_leer_red(&r) == false)
    {
        return;
    }

    // Antes del primer ciclo: como "SD SIN DATOS", sin hora ni accion
    if (r.hay_datos == false)
    {
        pantalla_oled_texto(4, "RED SIN DATOS");
        return;
    }

    // Linea 2: operador tecnologia señal (peor caso: 25 columnas). anadir() recorta, snprintf directo no compila
    n = 0;
    n = anadir(linea, sizeof(linea), n, "%s %s ",
               (r.red.operador[0] != '\0') ? r.red.operador : "--",
               (r.red.tecnologia[0] != '\0') ? r.red.tecnologia : "--");
    if (r.red.dbm == RED_DBM_DESCONOCIDA)
    {
        n = anadir(linea, sizeof(linea), n, "--");
    }
    else
    {
        n = anadir(linea, sizeof(linea), n, "%ddBm", (int)r.red.dbm);
    }
    pantalla_oled_texto(2, linea);

    // Linea 3
    snprintf(linea, sizeof(linea), "IP %s", (r.red.ip[0] != '\0') ? r.red.ip : "--");
    pantalla_oled_texto(3, linea);

    // Linea 4: OK con la hora del envio; fallo con "!hh:mm" del ultimo 200
    texto_envio(r.tx, texto, sizeof(texto));
    estado_sistema_hora_legible(r.ultimo_ts_200, hora, sizeof(hora));
    if (r.tx == TX_HTTP_OK)
    {
        snprintf(linea, sizeof(linea), "%s %s", texto, hora);
    }
    else
    {
        snprintf(linea, sizeof(linea), "%s !%s", texto, hora);
    }
    pantalla_oled_texto(4, linea);

    // Linea 5
    const char *accion = accion_envio(r.tx);

    if (accion[0] != '\0')
    {
        snprintf(linea, sizeof(linea), "  %s", accion);
        pantalla_oled_texto(5, linea);
    }
}

/**
 * @brief Pagina 2: reloj, red (lineas 2-5) y SD (lineas 6-7).
 */
static void pintar_pagina_red(void)
{
    pintar_reloj();
    pintar_red();
    pintar_sd();
}

void tarea_pantalla(void *parametros)
{
    uint8_t pagina = 0;
    uint8_t segundos_en_pagina = 0;

    while (1)
    {
        // Sin pantalla las primitivas no hacen nada, pero la tarea sigue viva por
        // si algun dia se reintenta pantalla_oled_init().
        pantalla_oled_limpiar();
        switch (pagina)
        {
        case 0:
            pintar_pagina_datos();
            break;

        case 1:
            pintar_pagina_red();
            break;

        default:
            pagina = 0;
            break;
        }

        pantalla_oled_mostrar();

        segundos_en_pagina++;

        if (segundos_en_pagina >= SEGUNDOS_POR_PAGINA)
        {
            segundos_en_pagina = 0;
            pagina = (pagina + 1) % NUM_PAGINAS;
        }
        vTaskDelay(pdMS_TO_TICKS(REFRESCO_MS));
    }
}