/*
 * Identidad del nodo: ID (3 ultimos bytes de la MAC de fabrica, en hexadecimal) y seq (uint32_t en la NVS, nunca se repite; 0 = sin numero).
 * Fuentes: ESP-IDF v6.1, "Non-Volatile Storage Library" y "Miscellaneous System APIs" (MAC).
 */

#include <stdio.h>
#include <stdbool.h>

#include "nvs_flash.h"
#include "nvs.h"
#include "esp_mac.h"
#include "esp_log.h"

#include "config_nodo.h"

#define NVS_NAMESPACE "nodo"  // namespace de la NVS, para acceder a seq y el resto de variables; cambiarlo haria perder el seq guardado
#define CLAVE_SEQ "seq"       // clave del ultimo seq (uint32_t)
#define CLAVE_T_SYNC "t_sync" // última sincornización correcta (uint32_t)
#define CLAVE_T_GNSS "t_gnss" // último intento de GNSS, con o sin fix

static const char *TAG = "config_nodo";

static nvs_handle_t nvs = 0;       // handle del namespace, abierto en lectura/escritura
static bool nvs_ok = false;        // false: no se escribe nada, para no arriesgar repetir seq
static uint32_t seq_actual = 0;    // ultimo seq guardado en la NVS
static char id_nodo[13] = "SINID"; // 13: cabe la MAC entera en hexadecimal
static uint32_t t_sync_actual = 0; // copia en la RAM de t_sync
static uint32_t t_gnss_actual = 0; // copia en la RAM de t_gnss

/** @brief Pone en id_nodo los 3 ultimos bytes de la MAC de fabrica; false si no se pudo leer. */
static bool leer_id_de_mac(void)
{
    uint8_t mac[6];
    esp_err_t err = esp_efuse_mac_get_default(mac);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "No se pudo leer la MAC: %s", esp_err_to_name(err));
        return false;
    }

    snprintf(id_nodo, sizeof(id_nodo), "%02X%02X%02X", mac[3], mac[4], mac[5]);
    return true;
}

// Lee la marca que le pidas, gnss o sync
static void leer_marca(const char *clave, uint32_t *copia)
{
    esp_err_t err = nvs_get_u32(nvs, clave, copia);

    if (err == ESP_OK)
    {
        return;
    }
    *copia = 0; // No nos fiamos si hay un error de lo que hubiese guardado
    if (err != ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGE(TAG, "No se pudo leer %s: %ls", clave, esp_err_to_name(err));
    }
}

// Guarda la marca
static void guardar_marca(const char *clave, uint32_t valor, uint32_t *copia)
{
    *copia = valor; // Primero se guarda en la RAM, la hora seguirá valida aunque falle la NVS
    if (nvs_ok == false)
    {
        return; 
    }

    esp_err_t err = nvs_set_u32(nvs, clave, valor); 

    if (err == ESP_OK)
    {
        err = nvs_commit(nvs);
    }

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "No se pudo guardar %s %lu: %s", clave, valor, esp_err_to_name(err));
    }
}

const char *config_nodo_id(void)
{
    return id_nodo;
}

uint32_t config_nodo_siguiente_seq(void)
{
    if (nvs_ok == false)
    {
        return 0;
    }

    if (seq_actual == UINT32_MAX)
    {
        ESP_LOGE(TAG, "seq agotado");
        return 0;
    }

    uint32_t candidato = seq_actual + 1;

    // Primero se guarda y solo despues se usa: un reinicio deja un hueco, nunca una repeticion
    esp_err_t err = nvs_set_u32(nvs, CLAVE_SEQ, candidato);

    if (err == ESP_OK)
    {
        err = nvs_commit(nvs);
    }

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "No se pudo guardar seq %lu: %s", (unsigned long)candidato, esp_err_to_name(err));
        return 0;
    }

    seq_actual = candidato;

    return candidato;
}

uint32_t config_nodo_t_sync(void)
{
    return t_sync_actual;
}

uint32_t config_nodo_t_gnss(void)
{
    return t_gnss_actual;
}

void config_nodo_guardar_t_sync(uint32_t t)
{
    guardar_marca(CLAVE_T_SYNC, t, &t_sync_actual);
}

void config_nodo_guardar_t_gnss(uint32_t t)
{
    guardar_marca(CLAVE_T_GNSS, t, &t_gnss_actual);
}

cfg_nodo_estado_t config_nodo_init(void)
{
    bool id_ok = leer_id_de_mac();
    ESP_LOGI(TAG, "ID del nodo: %s", id_nodo);

    // Si falla, no se borra la NVS (los ejemplos de ESP-IDF si lo hacen): borrarla reiniciaria seq y repetiria pares (nodo, seq)
    esp_err_t err = nvs_flash_init();

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "nvs_flash_init: %s. seq sera 0: avisar al tecnico", esp_err_to_name(err));
        return CFG_NODO_ERROR_NVS;
    }

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "nvs_open: %s", esp_err_to_name(err));
        return CFG_NODO_ERROR_NVS;
    }

    err = nvs_get_u32(nvs, CLAVE_SEQ, &seq_actual);

    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        seq_actual = 0;
        ESP_LOGI(TAG, "NVS vacia: seq empieza en 1");
    }
    else if (err != ESP_OK)
    {
        // Sin saber el ultimo seq, escribir podria repetir numeros: no se escribe nada
        ESP_LOGE(TAG, "No se pudo leer seq: %s", esp_err_to_name(err));
        return CFG_NODO_ERROR_NVS;
    }
    else
    {
        ESP_LOGI(TAG, "Ultimo seq guardado: %lu", (unsigned long)seq_actual);
    }

    leer_marca(CLAVE_T_SYNC, &t_sync_actual);
    leer_marca(CLAVE_T_GNSS, &t_gnss_actual);
    ESP_LOGI(TAG, "t_sync %lu, t_gnss %lu (0 = nunca)", t_sync_actual, t_gnss_actual);
    nvs_ok = true;

    return (id_ok == true) ? CFG_NODO_OK : CFG_NODO_SIN_ID;
}