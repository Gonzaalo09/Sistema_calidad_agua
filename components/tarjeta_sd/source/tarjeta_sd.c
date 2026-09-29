/*
 * tarjeta_sd.c
 *
 * Tarjeta microSD por SPI con sistema de archivos FAT (FatFs + VFS de ESP-IDF).
 * El bus SPI se inicializa una vez en app_main. Montar, desmontar y escribir
 * se hace despues desde la unica tarea que usa la SD (tarea_transmision).
 * El nodo nunca formatea la tarjeta. Las rutas que recibe son relativas a la
 * tarjeta; el punto de montaje es privado de este archivo.
 *
 * Fuentes: ESP-IDF v6.1, FAT Filesystem Support, SD SPI Host Driver,
 * SD/SDIO/MMC Driver y SD Pull-up Requirements.
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>

#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_log.h"

#include "tarjeta_sd.h"

// Unico sitio con el hardware de la SD: al migrar al LilyGo se cambia aqui
#define SD_SPI_HOST SPI3_HOST // VSPI
#define SD_PIN_MOSI 23
#define SD_PIN_MISO 19
#define SD_PIN_SCK 18
#define SD_PIN_CS 5
#define SD_FREQ_KHZ 5000 // 40 MHz / 8: de sobra para una linea por ciclo

#define PUNTO_MONTAJE "/sdcard"
#define TAM_RUTA_COMPLETA 96
#define SD_MIN_LIBRE_BYTES (1024ULL * 1024ULL) // por debajo se considera llena

static const char *TAG = "tarjeta_sd";

static sdmmc_card_t *card = NULL;
static bool bus_ok = false;
static bool montada = false;

/**
 * @brief Traduce el errno de un fallo de archivo a sd_estado_t.
 *        Si la tarjeta no esta o fallo al escribir, desmonta para reintentar el
 *        montaje en el ciclo siguiente.
 */
static sd_estado_t fallo_errno(const char *donde)
{
    int e = errno;

    ESP_LOGE(TAG, "%s: %s (errno %d)", donde, strerror(e), e);

    if (e == ENOSPC)
    {
        return SD_EST_LLENA;
    }

    tarjeta_sd_desmontar();

    if (e == ENODEV) // visto en el bring-up al sacar la tarjeta en marcha
    {
        return SD_EST_AUSENTE;
    }

    return SD_EST_ERROR_ESCRITURA;
}

/**
 * @brief Monta la tarjeta si no lo esta.
 */
static sd_estado_t asegurar_montada(void)
{
    if (montada == true)
    {
        return SD_EST_OK;
    }

    return tarjeta_sd_montar();
}

/**
 * @brief Crea la carpeta que contiene a 'completa' si no existe.
 *        Un archivo en la raiz no necesita carpeta.
 */
static bool crear_carpeta_padre(const char *completa)
{
    char carpeta[TAM_RUTA_COMPLETA];
    snprintf(carpeta, sizeof(carpeta), "%s", completa);

    char *barra = strrchr(carpeta, '/');

    if (barra == NULL)
    {
        return true;
    }

    *barra = '\0';

    if (strcmp(carpeta, PUNTO_MONTAJE) == 0)
    {
        return true;
    }

    struct stat st;

    if (stat(carpeta, &st) == 0)
    {
        return true;
    }

    if (mkdir(carpeta, 0775) == 0)
    {
        ESP_LOGI(TAG, "Carpeta creada: %s", carpeta);
        return true;
    }

    return (errno == EEXIST);
}

sd_estado_t tarjeta_sd_init(void)
{
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_PIN_MOSI,
        .miso_io_num = SD_PIN_MISO,
        .sclk_io_num = SD_PIN_SCK,
        .quadwp_io_num = -1, // los nativos de SPI3 (22 y 21) son el bus I2C
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    esp_err_t err = spi_bus_initialize(SD_SPI_HOST, &bus_cfg, SDSPI_DEFAULT_DMA);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "spi_bus_initialize: %s", esp_err_to_name(err));
        return SD_EST_ERROR_BUS;
    }

    bus_ok = true;

    return tarjeta_sd_montar();
}

sd_estado_t tarjeta_sd_montar(void)
{
    if (bus_ok == false)
    {
        return SD_EST_ERROR_BUS;
    }

    if (montada == true)
    {
        return SD_EST_OK;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_SPI_HOST;
    host.max_freq_khz = SD_FREQ_KHZ;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SD_PIN_CS;
    slot_config.host_id = SD_SPI_HOST;

    esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,  // el nodo nunca formatea
        .max_files = 2,
        .disk_status_check_enable = true, // detecta tarjetas sacadas sin desmontar
    };

    uint32_t t0 = esp_log_timestamp();
    esp_err_t err = esp_vfs_fat_sdspi_mount(PUNTO_MONTAJE, &host, &slot_config, &mount_config, &card);
    uint32_t ms = esp_log_timestamp() - t0;

    if (err == ESP_OK)
    {
        montada = true;
        ESP_LOGI(TAG, "Montada en %lu ms", (unsigned long)ms);
        return SD_EST_OK;
    }

    card = NULL;

    // Confirmado en el bring-up: sin tarjeta 0x108, tarjeta sin FAT ESP_FAIL
    ESP_LOGW(TAG, "Montaje fallido en %lu ms: %s (0x%x)",
             (unsigned long)ms, esp_err_to_name(err), (unsigned)err);

    if (err == ESP_FAIL)
    {
        return SD_EST_SIN_FORMATO;
    }

    return SD_EST_AUSENTE;
}

void tarjeta_sd_desmontar(void)
{
    if (montada == false)
    {
        return;
    }

    esp_err_t err = esp_vfs_fat_sdcard_unmount(PUNTO_MONTAJE, card);

    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Desmontaje: %s", esp_err_to_name(err));
    }

    card = NULL;
    montada = false;
}

bool tarjeta_sd_montada(void)
{
    return montada;
}

void tarjeta_sd_info(void)
{
    if (montada == false)
    {
        ESP_LOGW(TAG, "Info: la SD no esta montada");
        return;
    }

    sdmmc_card_print_info(stdout, card);

    uint64_t total = 0;
    uint64_t libre = 0;

    uint32_t t0 = esp_log_timestamp();
    esp_err_t err = esp_vfs_fat_info(PUNTO_MONTAJE, &total, &libre);
    uint32_t ms = esp_log_timestamp() - t0;

    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "Capacidad %lu MB, libre %lu MB (calculado en %lu ms)",
                 (unsigned long)(total / (1024 * 1024)),
                 (unsigned long)(libre / (1024 * 1024)),
                 (unsigned long)ms);
    }

    DIR *dir = opendir(PUNTO_MONTAJE);

    if (dir == NULL)
    {
        ESP_LOGE(TAG, "No se pudo abrir la raiz: %s", strerror(errno));
        return;
    }

    ESP_LOGI(TAG, "Contenido de la raiz:");

    struct dirent *entrada;

    while ((entrada = readdir(dir)) != NULL)
    {
        ESP_LOGI(TAG, "  %s", entrada->d_name);
    }

    closedir(dir);
}

sd_estado_t tarjeta_sd_primera_linea(const char *ruta, char *buf, size_t tam, bool *existe)
{
    *existe = false;
    buf[0] = '\0';

    sd_estado_t est = asegurar_montada();

    if (est != SD_EST_OK)
    {
        return est;
    }

    char completa[TAM_RUTA_COMPLETA];
    snprintf(completa, sizeof(completa), "%s/%s", PUNTO_MONTAJE, ruta);

    // Si stat falla se trata como "no existe": si el fallo era de la tarjeta,
    // la escritura que viene despues lo detecta y lo traduce.
    struct stat st;

    if (stat(completa, &st) != 0)
    {
        return SD_EST_OK;
    }

    *existe = true;

    // Escritura: abrir en modo añadir, escribir y cerrar (fclose vuelca a la tarjeta)
    // a cadena es el modo y lo que vas a hacer con el archivo: 
    // "r" : lectura si escritura no: Si lo puede abrir lo lee desde el principio
    // "w" : lectura no escritura si: Si no existe lo crea, si ya existe lo vacía
    // "a" : lectura no escritura si, siempre al final: Si no existe lo crea, si existe lo conserva y añade detras
    // "r+" : lectura si escritura si: si no existe falla, si existe escribe encima sin vaciar
    // "w+" : lectura si escritura si: si no existe lo crea, si existe lo vacía
    // "a+" : lectura si, esctirua si, siempre al final: si no existe lo crea, si existe, lee desde donde quieras y escribe al final
    FILE *f = fopen(completa, "r");

    if (f == NULL)
    {
        return fallo_errno("Abrir para leer");
    }

    if (fgets(buf, tam, f) == NULL)
    {
        buf[0] = '\0'; // archivo vacio
    }

    fclose(f);

    buf[strcspn(buf, "\r\n")] = '\0';

    return SD_EST_OK;
}

sd_estado_t tarjeta_sd_escribir_linea(const char *ruta, const char *cabecera, const char *linea)
{
    sd_estado_t est = asegurar_montada();

    if (est != SD_EST_OK)
    {
        return est;
    }

    uint64_t total = 0;
    uint64_t libre = 0;

    if ((esp_vfs_fat_info(PUNTO_MONTAJE, &total, &libre) == ESP_OK) && (libre < SD_MIN_LIBRE_BYTES))
    {
        ESP_LOGW(TAG, "Tarjeta llena: quedan %lu KB", (unsigned long)(libre / 1024));
        return SD_EST_LLENA;
    }

    char completa[TAM_RUTA_COMPLETA];
    snprintf(completa, sizeof(completa), "%s/%s", PUNTO_MONTAJE, ruta);

    if (crear_carpeta_padre(completa) == false)
    {
        return fallo_errno("Crear carpeta");
    }

    struct stat st;
    bool nuevo = (stat(completa, &st) != 0);

    FILE *f = fopen(completa, "a");

    if (f == NULL)
    {
        return fallo_errno("Abrir para escribir");
    }

    bool fallo = false;

    if ((nuevo == true) && (cabecera != NULL))
    {
        if (fprintf(f, "%s\n", cabecera) < 0)
        {
            fallo = true;
        }
    }

    if (fprintf(f, "%s\n", linea) < 0)
    {
        fallo = true;
    }

    // fclose vuelca a la tarjeta (f_sync): aqui es cuando de verdad se escribe
    if (fclose(f) != 0)
    {
        fallo = true;
    }

    if (fallo == true)
    {
        return fallo_errno("Escribir");
    }

    return SD_EST_OK;
}

sd_estado_t tarjeta_sd_espacio(uint8_t *porcentaje_libre)
{
    *porcentaje_libre = 0;

    if (montada == false)
    {
        return SD_EST_AUSENTE;
    }

    uint64_t total = 0;
    uint64_t libre = 0;

    if ((esp_vfs_fat_info(PUNTO_MONTAJE, &total, &libre) != ESP_OK) || (total == 0))
    {
        return SD_EST_ERROR_ESCRITURA;
    }

    *porcentaje_libre = (uint8_t)((libre * 100) / total);

    return SD_EST_OK;
}