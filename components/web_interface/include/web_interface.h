/* SPDX-FileCopyrightText: 2026 Thomas Stoll
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_http_server.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *id;
    const char *label;
    const unsigned char *content;
    size_t content_size;
    const unsigned char *script;
    size_t script_size;
    const httpd_uri_t *routes;
    size_t route_count;
    /* NULL or "tab": own tab; "settings": card embedded in the settings tab. */
    const char *placement;
    /* Optional. When set and returning false, the extension is hidden in the UI.
     * The callback must not block; its routes stay registered and must perform
     * their own authorization and availability checks. */
    bool (*visible)(void);
} web_interface_extension_t;

typedef struct {
    const char *password;
    const char *tab_order;
    const char *primary_tabs;
    bool settings_require_password;
    bool sd_languages;
    bool peripherals_active;
    bool maker_mode;
    unsigned session_days;
    bool login_enabled;
    esp_err_t (*save_login)(const char *password, unsigned session_days, bool login_enabled);
    /* Nonblocking. Return whether to show the clock; UTC seconds of zero mean unavailable. */
    bool (*get_clock_utc)(int64_t *utc);
} web_interface_runtime_config_t;

esp_err_t web_interface_set_runtime_config(const web_interface_runtime_config_t *config);
const web_interface_runtime_config_t *web_interface_get_runtime_config(void);
/* The descriptor and referenced assets/routes must outlive the web server. */
esp_err_t web_interface_register_extension(const web_interface_extension_t *extension);
bool web_interface_authorize_request(httpd_req_t *req, bool mutation);
bool web_interface_authorize_settings_request(httpd_req_t *req);
esp_err_t web_interface_start(void);

#ifdef __cplusplus
}
#endif
