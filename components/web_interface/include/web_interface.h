/* SPDX-FileCopyrightText: 2026 Thomas Stoll
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
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
} web_interface_extension_t;

esp_err_t web_interface_register_extension(const web_interface_extension_t *extension);
bool web_interface_authorize_request(httpd_req_t *req, bool mutation);
esp_err_t web_interface_start(void);

#ifdef __cplusplus
}
#endif
