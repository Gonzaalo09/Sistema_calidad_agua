/*
 * Convierte un ciclo en texto: CSV (cabecera, linea, nombre de archivo) y trama JSON. No toca hardware.
 * Separador ';', punto decimal, 3 decimales. Sin fuentes externas: el formato es decision del proyecto.
 */

#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <math.h>

#include "sensores.h"
#include "config_nodo.h"
#include "registro_csv.h"

/**
 * @brief Añade un trozo con formato printf al final de buf, que se construye por trozos.
 *
 * Si un trozo no cabe entero, la linea se marca como rota (*usado = tam) y las llamadas siguientes no hacen
 * nada. Quien llama comprueba usado >= tam al terminar: una linea cortada nunca llega a la SD. Empezar con usado = 0.
 * buf queda siempre terminado en '\0'.
 *
 * El atributo format(printf, 4, 5) es de GCC: el parametro 4 es el formato y los argumentos empiezan en el 5,
 * asi el compilador avisa si un tipo no cuadra (p. ej. %s con un float).
 */
__attribute__((format(printf, 4, 5))) static void anadir(char *buf, size_t tam, size_t *usado, const char *formato, ...)
{
    if (*usado >= tam)
    {
        return;
    }

    va_list args;
    va_start(args, formato);
    int n = vsnprintf(buf + *usado, tam - *usado, formato, args);
    va_end(args);

    if ((n < 0) || ((size_t)n >= tam - *usado))
    {
        *usado = tam;
        return;
    }

    *usado += (size_t)n;
}

size_t registro_csv_cabecera(char *buf, size_t tam)
{
    size_t usado = 0;

    anadir(buf, tam, &usado, "formato;fecha;ts;nodo;seq;rtc");

    for (size_t i = 0; i < NUM_SENSORES; i++)
    {
        const sensor_descrip *d = &TABLA_SENSORES[i];

        anadir(buf, tam, &usado, ";%s_est", d->nombre_fuente);

        for (uint8_t j = 0; j < d->num_valores; j++)
        {
            anadir(buf, tam, &usado, ";%s_%s", d->nombre_fuente, d->nombres_datos[j]);
        }
    }

    anadir(buf, tam, &usado, ";tx");

    if (usado >= tam)
    {
        buf[0] = '\0';
        return 0;
    }

    return usado;
}

size_t registro_csv_linea(const ciclo *c, int tx, char *buf, size_t tam)
{
    size_t usado = 0;
    char fecha[20] = ""; // "2026-09-29 10:30:00" + '\0'; vacia si no hay hora

    if (c->ts != 0)
    {
        time_t t = (time_t)c->ts;
        struct tm local;

        // ts ya lleva sumado OFFSET_PAIS_S, asi que gmtime_r da la hora del pais
        gmtime_r(&t, &local);
        strftime(fecha, sizeof(fecha), "%Y-%m-%d %H:%M:%S", &local);
    }

    anadir(buf, tam, &usado, "%d;%s;%lu;%s;%lu;%d",
           FORMATO_CSV, fecha, (unsigned long)c->ts, config_nodo_id(),
           (unsigned long)c->seq, (int)c->estado_rtc);

    for (size_t i = 0; i < NUM_SENSORES; i++)
    {
        const sensor_descrip *d = &TABLA_SENSORES[i];
        const ciclo_sensor *s = &c->sensor[i];

        anadir(buf, tam, &usado, ";%d", (int)s->codigo);

        // Columnas: las de la tabla (d), no las del ciclo (s): si un sensor falla (s->num_valores = 0)
        // la linea no se desplaza respecto a la cabecera. Los huecos se dejan vacios.
        for (uint8_t j = 0; j < d->num_valores; j++)
        {
            if (j < s->num_valores)
            {
                anadir(buf, tam, &usado, ";%.3f", s->valores[j]);
            }
            else
            {
                anadir(buf, tam, &usado, ";");
            }
        }
    }

    anadir(buf, tam, &usado, ";%d", tx);

    if (usado >= tam)
    {
        buf[0] = '\0';
        return 0;
    }

    return usado;
}

void registro_csv_ruta(uint32_t ts, char *buf, size_t tam)
{
    if (ts == 0)
    {
        snprintf(buf, tam, "%s_SINHORA", config_nodo_id());
        return;
    }

    time_t t = (time_t)ts;
    struct tm local;
    gmtime_r(&t, &local); // ts ya lleva OFFSET_PAIS_S: gmtime_r da la hora del pais

    char anio[5]; // "2026"
    char dia[11]; // "2026-09-29"
    strftime(anio, sizeof(anio), "%Y", &local);
    strftime(dia, sizeof(dia), "%Y-%m-%d", &local);

    snprintf(buf, tam, "%s/%s_%s", anio, config_nodo_id(), dia);
}

size_t registro_csv_json(const ciclo *c, char *buf, size_t tam)
{
    size_t usado = 0;

    anadir(buf, tam, &usado, "{\"formato\":%d,\"nodo\":\"%s\",\"seq\":%lu,\"ts\":%lu,\"rtc\":%d",
           FORMATO_CSV, config_nodo_id(), (unsigned long)c->seq,
           (unsigned long)c->ts, (int)c->estado_rtc);

    for (size_t i = 0; i < NUM_SENSORES; i++)
    {
        // TABLA_SENSORES[i] describe el sensor, c->sensor[i] trae sus valores en este ciclo
        const sensor_descrip *d = &TABLA_SENSORES[i];
        const ciclo_sensor *s = &c->sensor[i];
        anadir(buf, tam, &usado, ",\"%s_est\":%d", d->nombre_fuente, (int)s->codigo);

        for (int j = 0; j < d->num_valores; j++)
        {
            anadir(buf, tam, &usado, ",\"%s_%s\":", d->nombre_fuente, d->nombres_datos[j]);
            if ((j < s->num_valores) && (isfinite(s->valores[j])))
            {
                anadir(buf, tam, &usado, "%.3f", s->valores[j]);
            }
            else
            {
                anadir(buf, tam, &usado, "null"); // Sensor que falla o valor no finito
            }
        }
    }
    anadir(buf, tam, &usado, "}");

    if (usado >= tam)
    {
        buf[0] = '\0';
        return 0;
    }
    return usado;
}