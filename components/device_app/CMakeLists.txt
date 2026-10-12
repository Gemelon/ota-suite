# SPDX-FileCopyrightText: 2026 Thomas Stoll
# SPDX-License-Identifier: Apache-2.0

idf_component_register(SRCS "device_app.c" INCLUDE_DIRS "include"
                       REQUIRES network_manager ota_manager web_interface status_led update_check)
