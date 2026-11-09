/* SPDX-FileCopyrightText: 2026 Thomas Stoll
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    bool connected;
    bool configuring;
    bool time_ready;
    char ssid[33];
    char ap_ssid[33];
    char ip[16];
    char message[128];
} network_status_t;

typedef struct {
    const char *ap_password;
    const char *sntp_server;
    bool ap_only;
    /* Zero keeps the default interval. Callback runs in the SNTP task and must not block. */
    uint32_t sntp_sync_interval_ms;
    void (*time_synced)(int64_t utc);
} network_runtime_config_t;

esp_err_t network_manager_set_runtime_config(const network_runtime_config_t *config);
esp_err_t network_manager_init(void);
esp_err_t network_manager_configure(const char *ssid, const char *password);
void network_manager_get_status(network_status_t *status);
const char *network_manager_admin_password(void);
