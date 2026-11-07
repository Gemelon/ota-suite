/* SPDX-FileCopyrightText: 2026 Thomas Stoll
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    bool ota_override;
} status_led_pixel_t;

/* Called by the LED task. Supply normal colors; OTA overrides only opted-in pixels. */
typedef esp_err_t (*status_led_renderer_t)(status_led_pixel_t *pixels, size_t count);

esp_err_t status_led_set_renderer(size_t max_leds, status_led_renderer_t renderer);
esp_err_t status_led_set_gpio(int gpio);
esp_err_t status_led_start(void);

#ifdef __cplusplus
}
#endif
