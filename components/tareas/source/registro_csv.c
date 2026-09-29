/*
 * registro_csv.c
 *
 * Convierte un ciclo de medida en texto CSV (cabecera, linea y nombre de
 * archivo). No toca hardware: la misma linea servira para la trama.
 * Separador ';', punto decimal, 3 decimales. Un sensor que falla deja sus
 * valores vacios, nunca el ultimo valor bueno.
 */

#include <stdio.h>
#include <stdarg.h>
#include <time.h>

#include "sensores.h"
#include "config_nodo.h"
#include "registro_csv.h"

/**
 * @brief Añade un trozo de texto con formato printf al final de buf.
 *
 * Se llama varias veces seguidas con el mismo buf y el mismo usado para
 * construir una linea por trozos. Si un trozo no cabe entero, la linea se
 * marca como rota (*usado = tam) y las llamadas siguientes no hacen nada.
 * Al terminar, el llamador comprueba usado >= tam para descartarla: una
 * linea cortada nunca llega a la SD.
 *
 * Ejemplo con tam = 16:
 *   usado = 0  + "1790000000;" (11) -> cabe,    usado = 11
 *   usado = 11 + "125010;"     (7)  -> no cabe, usado = 16 (rota)
 *   usado = 16 + ";1"               -> se ignora
 *
 * @param buf     Buffer donde se construye la linea. Siempre queda terminado
 *                en '\0', aunque este rota, guarda todo lo que se va a escribir.
 * @param tam     Tamaño total de buf en bytes (sizeof del array).
 * @param usado   Entrada: caracteres ya escritos, que es donde empieza el
 *                trozo nuevo. Salida: avanza lo que ocupa el trozo, o vale
 *                tam si no cabia. Empezar la linea con usado = 0.
 * @param formato Cadena de formato de printf ("%d", "%s", "%.3f"...).
 * @param ...     Valores para formato, como en printf.
 *
 * @note __attribute__((format(printf, 4, 5))) es una extension de GCC: el
 *       parametro 4 (formato) es un formato printf y los argumentos a
 *       comprobar empiezan en el 5 (...). Asi el compilador avisa, igual
 *       que con printf, si un tipo no cuadra (p. ej. %s con un float).
 */
__attribute__((format(printf, 4, 5)))
static void anadir(char *buf, size_t tam, size_t *usado, const char *formato, ...)
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

// Se crea la cabecera a traves de la tabla sensores por lo tanto no hay posibilidad de error al meter nuevos sensores 
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

/**
 * @brief Convierte un ciclo en una linea CSV, con las mismas columnas y en
 *        el mismo orden que registro_csv_cabecera.
 *
 * Orden: formato, fecha legible, ts, nodo, seq, codigo del RTC; por cada
 * sensor de TABLA_SENSORES su codigo y una columna por magnitud; y tx.
 * Valores con 3 decimales; los codigos son estado_codigo_t sin traducir.
 *
 * Un sensor que fallo deja sus valores vacios: el numero de columnas sale
 * de la tabla (d->num_valores), no del ciclo (s->num_valores), para que la
 * linea no se desplace respecto a la cabecera. Sin hora (ts == 0), la
 * fecha queda vacia y ts vale 0.
 *
 * Ejemplo: NPK y OD bien, pH sin respuesta (codigo 2), tx no intentado (0):
 *   1;2026-09-29 10:00:00;1790676000;125010;42;1;1;85.000;119.000;239.000;1;7.012;21.500;2;;;0
 *
 * No añade el '\n' final: lo pone quien escribe la linea en el archivo.
 *
 * @param c   Ciclo recibido de cola_lecturas.
 * @param tx  Resultado del envio (TX_NO_INTENTADO mientras no haya transmision).
 * @param buf Buffer donde se escribe la linea.
 * @param tam Tamaño de buf en bytes (TAM_LINEA_CSV).
 * @return Longitud de la linea, o 0 si no cabia en buf (buf queda vacio).
 */
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

        // Columnas: las de la tabla (d). Valores rellenos: los del ciclo (s). 
        for (uint8_t j = 0; j < d->num_valores; j++)
        {
            // Se chequea prque s tiene el num_valores de los que se ha leido correctamente, y si no 
            // se han podidio leer bien tienes que poner huecos para que tenga la misma longitud que la trama
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

/**
 * @brief Nombre del archivo del dia al que pertenece un ciclo, sin extension.
 *
 * Un archivo por dia y una carpeta por año, con el ID del nodo delante para
 * que no se mezclen tarjetas de nodos distintos en el PC. El dia sale de ts,
 * que ya lleva la hora del pais: el archivo cambia a las 00:00 locales.
 *
 *   ts = 1790676000 (2026-09-29 10:00:00) -> "2026/125010_2026-09-29"
 *   ts = 0 (RTC sin hora)                 -> "125010_SINHORA"
 *
 * Solo genera el texto. Lo demas lo añade quien llama: ".csv" o "_2.csv"
 * (guardar_en_sd), "/sdcard/" delante y la carpeta del año si no existe
 * (tarjeta_sd_escribir_linea).
 *
 * @param ts  Timestamp del ciclo (UTC + OFFSET_PAIS_S), o 0 si no hay hora.
 * @param buf Buffer donde se escribe el nombre.
 * @param tam Tamaño de buf en bytes; con TAM_RUTA_CSV (48) sobra: el caso
 *            mas largo son unos 30 caracteres.
 */
void registro_csv_ruta(uint32_t ts, char *buf, size_t tam)
{
    if (ts == 0)
    {
        snprintf(buf, tam, "%s_SINHORA", config_nodo_id());
        return;
    }

    time_t t = (time_t)ts;
    struct tm local;
    gmtime_r(&t, &local);

    char anio[5];  // "2026"
    char dia[11];  // "2026-09-29"
    strftime(anio, sizeof(anio), "%Y", &local);
    strftime(dia, sizeof(dia), "%Y-%m-%d", &local);


    snprintf(buf, tam, "%s/%s_%s", anio, config_nodo_id(), dia);
}