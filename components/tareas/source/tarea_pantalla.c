/*
 * tarea_pantalla.c
 *
 * Pinta en la OLED (128x64, fuente 5x8: 25 columnas x 8 lineas) el reloj,
 * los ultimos valores de cada sensor y su estado en lenguaje llano.
 *
 * Los valores de un sensor averiado NO se borran: se muestran con la marca
 * "!hh:mm", que es la hora del ultimo dato bueno, para que nadie los
 * confunda con datos frescos.
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
#include "pantalla_oled.h"
#include "tarea_rtc.h"
#include "tarea_pantalla.h"

#define REFRESCO_MS 1000
#define SEGUNDOS_POR_PAGINA 5
#define NUM_PAGINAS 2

/**
 * @brief Añade texto al final de una linea sin desbordarla.
 *
 * vsnprintf devuelve los caracteres que HABRIA escrito, que puede ser mas
 * de los que caben; por eso se recorta el resultado a tam-1.
 *
 * @param linea Buffer de la linea
 * @param tam   Tamaño total del buffer
 * @param usado Caracteres ya escritos
 * @param formato Cadena de formato, como en printf
 * @return Nueva longitud usada, nunca mayor que tam-1
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
 * @brief Lineas 0 y 1: el reloj. Comun a todas las paginas.
 *
 * Los segundos avanzando son el latido del sistema: si se quedan parados,
 * el firmware esta colgado.
 */
static void pintar_reloj(void)
{
    char linea[PANTALLA_COLUMNAS + 1];

    uint32_t ts = rtc_timestamp_pais();

    if (ts == 0)
    {
        pantalla_oled_texto(0, "-- SIN HORA --");
        pantalla_oled_texto(1, "  RTC no contesta");
    }
    else
    {
        time_t t = (time_t)ts;
        struct tm local;

        // ts ya lleva sumado OFFSET_PAIS_S, asi que gmtime_r da hora de Peru
        gmtime_r(&t, &local);

        strftime(linea, sizeof(linea), "%Y-%m-%d %H:%M:%S", &local);

        pantalla_oled_texto(0, linea);
    }
}

/**
 * @brief Pagina 1: reloj, valores de cada sensor y sus avisos.
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
 * @brief Pagina 2: estado de la red movil. Vacia hasta la fase del SIM7600.
 */
static void pintar_pagina_red(void)
{
    char linea[PANTALLA_COLUMNAS + 1];
    estado_fuente_t e;

    pintar_reloj();

    if (estado_sistema_leer(FUENTE_RED, &e) == true)
    {
        snprintf(linea, sizeof(linea), "%s: %s",
                 (e.nombre != NULL) ? e.nombre : "Red",
                 estado_sistema_texto(e.codigo));

        pantalla_oled_texto(2, linea);

        const char *accion = estado_sistema_accion(e.codigo);

        if (accion[0] != '\0')
        {
            snprintf(linea, sizeof(linea), "  %s", accion);
            pantalla_oled_texto(3, linea);
        }
    }
}

void tarea_pantalla(void *parametros)
{
    uint8_t pagina = 0;
    uint8_t segundos_en_pagina = 0;

    while (1)
    {
        pantalla_oled_limpiar();
        // Sin pantalla no hay nada que pintar, pero la tarea sigue viva por
        // si algun dia se reintenta pantalla_oled_init().
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