# SPDX-FileCopyrightText: 2026 Thomas Stoll
# SPDX-License-Identifier: Apache-2.0

idf_component_register(SRCS "network_manager.c" INCLUDE_DIRS "include"
                       REQUIRES esp_wifi esp_netif esp_event nvs_flash bootloader_support)
