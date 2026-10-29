/* SPDX-FileCopyrightText: 2026 Thomas Stoll
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "web_interface.h"

esp_err_t web_auth_init(void);
esp_err_t web_auth_register(httpd_handle_t server);
const char *web_auth_csrf_token(void);
unsigned web_auth_session_days(void);
bool web_auth_settings_password_required(void);
