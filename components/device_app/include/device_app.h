/* SPDX-FileCopyrightText: 2026 Thomas Stoll
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>

void device_app_set_led_enabled(bool enabled);
esp_err_t device_app_start(void);
