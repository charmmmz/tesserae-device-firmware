#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "ble_photo_mode.h"
#include "nvs.h"
static uint8_t durable[64], pending[64];
static size_t durable_len, pending_len;
static bool namespace_present, fail_commit;
static esp_err_t get_error;
esp_err_t nvs_open(const char *space, int mode, nvs_handle_t *handle)
{
    assert(strcmp(space, "ble_photo") == 0);
    if (!namespace_present && mode == NVS_READONLY) return ESP_ERR_NVS_NOT_FOUND;
    *handle = 1; return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *length)
{
    (void)h; assert(strcmp(key, "config") == 0);
    if (get_error) return get_error;
    if (!durable_len) return ESP_ERR_NVS_NOT_FOUND;
    if (*length < durable_len) return ESP_ERR_NVS_INVALID_LENGTH;
    *length = durable_len; memcpy(out, durable, durable_len); return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *value, size_t length)
{
    (void)h; assert(strcmp(key, "config") == 0 && length <= sizeof pending);
    memcpy(pending, value, length); pending_len = length; return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h)
{
    (void)h;
    if (fail_commit) return ESP_FAIL;
    memcpy(durable, pending, pending_len); durable_len = pending_len;
    namespace_present = true; return ESP_OK;
}
void nvs_close(nvs_handle_t h) { (void)h; memset(pending, 0, sizeof pending); pending_len = 0; }
static bool zeros(const uint8_t key[32])
{
    unsigned v = 0; for (size_t i = 0; i < 32; i++) v |= key[i]; return v == 0;
}
int main(void)
{
    uint8_t key[32], first[32], second[32];
    memset(first, 1, sizeof first); memset(second, 2, sizeof second);
    bool manual = true;
    assert(ble_photo_mode_get(&manual, key) == ESP_OK && !manual && zeros(key));
    fail_commit = true;
    assert(ble_photo_mode_set(true, first, key) == ESP_FAIL && zeros(key));
    assert(ble_photo_mode_get(&manual, key) == ESP_OK && !manual && zeros(key));
    fail_commit = false;
    assert(ble_photo_mode_set(true, first, key) == ESP_OK && memcmp(key, first, 32) == 0);
    assert(ble_photo_mode_get(&manual, key) == ESP_OK && manual && memcmp(key, first, 32) == 0);
    /* Once mode commit makes disconnect a reboot, only key-save retries,
     * another mode choice, diagnostics and explicit reboot remain safe.
     * In particular a clear/reset must be refused BEFORE its acceptance ACK. */
    assert(ble_photo_mode_pending_allows("diagnostics"));
    assert(ble_photo_mode_pending_allows("set_screen_mode"));
    assert(ble_photo_mode_pending_allows("reboot"));
    const char *blocked[] = {"clear_wifi", "factory_reset", "stage", "apply", "scan",
        "photo_begin", "photo_capabilities", "diagnostics_extra", "reboot_now", "", NULL};
    for (size_t i = 0; i < sizeof blocked / sizeof blocked[0]; i++)
        assert(!ble_photo_mode_pending_allows(blocked[i]));
    assert(ble_photo_mode_get(&manual, key) == ESP_OK && manual && memcmp(key, first, 32) == 0);
    assert(ble_photo_mode_set(true, second, key) == ESP_OK && memcmp(key, first, 32) == 0);
    fail_commit = true;
    assert(ble_photo_mode_set(false, NULL, key) == ESP_FAIL && zeros(key));
    assert(ble_photo_mode_get(&manual, key) == ESP_OK && manual && memcmp(key, first, 32) == 0);
    fail_commit = false;
    assert(ble_photo_mode_set(false, NULL, key) == ESP_OK && zeros(key));
    assert(ble_photo_mode_get(&manual, key) == ESP_OK && !manual && zeros(key));
    assert(ble_photo_mode_set(true, second, key) == ESP_OK && memcmp(key, second, 32) == 0);
    /* Corrupt/version-mismatched blobs fail closed rather than exposing a key
     * or replacing unknown persisted authorization. */
    durable[0] = 99;
    assert(ble_photo_mode_get(&manual, key) == ESP_ERR_INVALID_STATE && !manual && zeros(key));
    assert(ble_photo_mode_set(true, first, key) == ESP_ERR_INVALID_STATE && zeros(key));
    durable_len = 33;
    assert(ble_photo_mode_get(&manual, key) == ESP_ERR_INVALID_STATE && zeros(key));
    get_error = ESP_FAIL;
    assert(ble_photo_mode_get(&manual, key) == ESP_FAIL && zeros(key));
    puts("BLE photo mode: atomic commit/key reuse/revocation/failure/corruption/pending-command gate PASS");
    return 0;
}
