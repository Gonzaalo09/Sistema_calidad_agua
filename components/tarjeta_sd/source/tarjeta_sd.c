/*
 * tarjeta_sd.c
 *
 * Tarjeta microSD por SPI con sistema de archivos FAT (FatFs + VFS de ESP-IDF).
 * El bus SPI se inicializa una vez en app_main. Montar y desmontar se puede
 * repetir despues desde la unica tarea que usa la SD (tarea_transmision).
 * El nodo nunca formatea la tarjeta.
 *
 * Fuentes: ESP-IDF v6.1, FAT Filesystem Support, SD SPI Host Driver,
 * SD/SDIO/MMC Driver y SD Pull-up Requirements.
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>

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

#define PUNTO_MONTAJE "/sdcard" // Nombre que se le da a la SD, al montar la tarjeta le dices "todo lo que empiece por /sdcard es la SD
#define ARCHIVO_PRUEBA PUNTO_MONTAJE "/PRUEBA.TXT"
#define TAM_LINEA_PRUEBA 48

static const char *TAG = "tarjeta_sd";

static sdmmc_card_t *card = NULL;
static bool bus_ok = false;
static bool montada = false;

sd_estado_t tarjeta_sd_init(void)
{
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_PIN_MOSI,
        .miso_io_num = SD_PIN_MISO,
        .sclk_io_num = SD_PIN_SCK,
        .quadwp_io_num = -1,
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

    // Traduccion PROVISIONAL: se confirma con los codigos reales del bring-up
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

    if (esp_vfs_fat_info(PUNTO_MONTAJE, &total, &libre) == ESP_OK)
    {
        ESP_LOGI(TAG, "Capacidad %lu MB, libre %lu MB",
                 (unsigned long)(total / (1024 * 1024)),
                 (unsigned long)(libre / (1024 * 1024)));
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

sd_estado_t tarjeta_sd_prueba_escritura(void)
{
    if (montada == false)
    {
        sd_estado_t est = tarjeta_sd_montar();

        if (est != SD_EST_OK)
        {
            return est;
        }
    }

    char escrita[TAM_LINEA_PRUEBA];
    snprintf(escrita, sizeof(escrita), "prueba %lu ms", (unsigned long)esp_log_timestamp());

    // Escritura: abrir en modo añadir, escribir y cerrar (fclose vuelca a la tarjeta)
    FILE *f = fopen(ARCHIVO_PRUEBA, "a");

    if (f == NULL)
    {
        ESP_LOGE(TAG, "No se pudo abrir para escribir: %s", strerror(errno));
        tarjeta_sd_desmontar();
        return SD_EST_ERROR_ESCRITURA;
    }

    int escrito = fprintf(f, "%s\n", escrita);
    int cerrado = fclose(f);

    if ((escrito < 0) || (cerrado != 0))
    {
        ESP_LOGE(TAG, "Escritura fallida: %s", strerror(errno));
        tarjeta_sd_desmontar();
        return SD_EST_ERROR_ESCRITURA;
    }

    // Relectura: la ultima linea del archivo tiene que ser la recien escrita
    f = fopen(ARCHIVO_PRUEBA, "r");

    if (f == NULL)
    {
        ESP_LOGE(TAG, "No se pudo abrir para releer: %s", strerror(errno));
        tarjeta_sd_desmontar();
        return SD_EST_ERROR_ESCRITURA;
    }

    char linea[TAM_LINEA_PRUEBA];
    char ultima[TAM_LINEA_PRUEBA] = "";

    while (fgets(linea, sizeof(linea), f) != NULL)
    {
        strcpy(ultima, linea);
    }

    fclose(f);

    ultima[strcspn(ultima, "\n")] = '\0';

    if (strcmp(ultima, escrita) != 0)
    {
        ESP_LOGE(TAG, "Relectura distinta: escrito '%s', leido '%s'", escrita, ultima);
        return SD_EST_ERROR_ESCRITURA;
    }

    ESP_LOGI(TAG, "Escrito y releido OK: '%s'", escrita);

    return SD_EST_OK;
}