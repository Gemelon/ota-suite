/* SPDX-FileCopyrightText: 2026 Thomas Stoll
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OTA_IDLE,
    OTA_DOWNLOADING,
    OTA_VERIFYING,
    OTA_REBOOTING,
    OTA_FAILED,
} ota_state_t;

typedef struct {
    ota_state_t state;
    bool configured;
    bool insecure_http;
    bool boot_confirmed;
    int bytes_read;
    int total_bytes;
    char message[160];
} ota_status_t;

typedef struct {
    const char *manifest_url;
    bool allow_http;
    bool services_enabled;
} ota_runtime_config_t;

esp_err_t ota_manager_set_runtime_config(const ota_runtime_config_t *config);
esp_err_t ota_manager_init(void);
esp_err_t ota_manager_confirm_boot(void);
esp_err_t ota_manager_start(const char *expected_version);
void ota_manager_get_status(ota_status_t *status);
bool ota_manager_is_busy(void);
bool ota_manager_is_ready(void);

#ifdef __cplusplus
}
#endif
