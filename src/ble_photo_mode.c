#include "ble_photo_mode.h"
#include <stddef.h>
#include <string.h>
#include "nvs.h"

enum { CONFIG_SIZE = 34, CONFIG_VERSION = 1 };

bool ble_photo_mode_pending_allows(const char *operation)
{
    return operation &&
        (strcmp(operation, "diagnostics") == 0 ||
         strcmp(operation, "set_screen_mode") == 0 ||
         strcmp(operation, "reboot") == 0);
}

esp_err_t ble_photo_mode_get(bool *manual, uint8_t key[32])
{
    if (!manual || !key) return ESP_ERR_INVALID_ARG;
    *manual = false;
    memset(key, 0, 32);
    nvs_handle_t handle;
    esp_err_t err = nvs_open("ble_photo", NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    uint8_t config[CONFIG_SIZE] = {0};
    size_t length = sizeof config;
    err = nvs_get_blob(handle, "config", config, &length);
    nvs_close(handle);
    if (err != ESP_OK) {
        memset(config, 0, sizeof config);
        return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
    }
    unsigned nonzero = 0;
    for (size_t i = 2; i < sizeof config; i++) nonzero |= config[i];
    if (length != sizeof config || config[0] != CONFIG_VERSION || config[1] > 1 ||
        (config[1] == 1 && nonzero == 0) || (config[1] == 0 && nonzero != 0)) {
        memset(config, 0, sizeof config);
        return ESP_ERR_INVALID_STATE;
    }
    *manual = config[1] == 1;
    if (*manual) memcpy(key, config + 2, 32);
    memset(config, 0, sizeof config);
    return ESP_OK;
}

esp_err_t ble_photo_mode_set(bool manual, const uint8_t candidate[32], uint8_t authorized[32])
{
    if (!authorized || (manual && !candidate)) return ESP_ERR_INVALID_ARG;
    memset(authorized, 0, 32);
    bool existing = false;
    uint8_t current[32] = {0};
    esp_err_t err = ble_photo_mode_get(&existing, current);
    if (err != ESP_OK) {
        memset(current, 0, sizeof current);
        return err; /* never silently replace an unreadable key */
    }
    if (manual && !existing) memcpy(current, candidate, sizeof current);
    unsigned nonzero = 0;
    for (size_t i = 0; i < sizeof current; i++) nonzero |= current[i];
    if (manual && nonzero == 0) {
        memset(current, 0, sizeof current);
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t config[CONFIG_SIZE] = {CONFIG_VERSION, manual ? 1 : 0};
    if (manual) memcpy(config + 2, current, sizeof current);
    memset(current, 0, sizeof current);
    nvs_handle_t handle;
    err = nvs_open("ble_photo", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, "config", config, sizeof config);
        if (err == ESP_OK) err = nvs_commit(handle);
        nvs_close(handle);
    }
    if (err == ESP_OK && manual) memcpy(authorized, config + 2, 32);
    memset(config, 0, sizeof config);
    return err;
}
