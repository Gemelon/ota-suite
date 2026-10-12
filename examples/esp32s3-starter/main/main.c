/* SPDX-FileCopyrightText: 2026 Thomas Stoll
 * SPDX-License-Identifier: Apache-2.0
 */

#include "device_app.h"
#include "esp_err.h"

void app_main(void)
{
    ESP_ERROR_CHECK(device_app_start());
}
