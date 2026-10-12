# SPDX-FileCopyrightText: 2026 Thomas Stoll
# SPDX-License-Identifier: Apache-2.0

idf_component_register(SRCS "status_led.c" INCLUDE_DIRS "include"
                       REQUIRES espressif__led_strip ota_manager esp_timer)
