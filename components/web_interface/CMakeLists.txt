# SPDX-FileCopyrightText: 2026 Thomas Stoll
# SPDX-License-Identifier: Apache-2.0

idf_component_register(SRCS "web_interface.c" "web_auth.c" "web_session.c" INCLUDE_DIRS "include"
                       REQUIRES esp_http_server app_update mbedtls
                                network_manager ota_manager update_check espressif__cjson nvs_flash esp_timer
                       EMBED_FILES "assets/index.html" "assets/app.js"
                                   "assets/bootstrap.min.css" "assets/app.css"
                                   "assets/i18n.js" "assets/en.json" "assets/de.json"
                                   "assets/login.html" "assets/login.js")
