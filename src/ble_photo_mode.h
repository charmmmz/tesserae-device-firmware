#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* One atomically committed NVS blob. Read-only absent config is Automatic.
 * The caller supplies entropy for a newly authorized key; this module never
 * emits a key before commit succeeds. Clear Wi-Fi does not touch this namespace. */
esp_err_t ble_photo_mode_get(bool *manual, uint8_t key[32]);
esp_err_t ble_photo_mode_set(bool manual, const uint8_t candidate[32], uint8_t authorized[32]);

/* After a mode commit, disconnect is a pending reboot. Other state-changing
 * commands must not ACK an action that this disconnect could supersede. */
bool ble_photo_mode_pending_allows(const char *operation);
