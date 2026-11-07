/* SPDX-FileCopyrightText: 2026 Thomas Stoll
 * SPDX-License-Identifier: Apache-2.0
 */

#include "status_led.h"

#include <stdint.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "ota_manager.h"

static const char *TAG = "status_led";
static led_strip_handle_t strip;
static int led_gpio = -1;
static size_t pixel_count = 1;
static status_led_renderer_t normal_renderer;
static status_led_pixel_t *pixels;

esp_err_t status_led_set_renderer(size_t max_leds, status_led_renderer_t renderer)
{
    if (strip != NULL || max_leds < 1 || max_leds > 16 || renderer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    pixel_count = max_leds;
    normal_renderer = renderer;
    return ESP_OK;
}

esp_err_t status_led_set_gpio(int gpio)
{
    if (strip != NULL || gpio < 0) return ESP_ERR_INVALID_ARG;
    led_gpio = gpio;
    return ESP_OK;
}

static uint32_t pulse(uint64_t milliseconds)
{
    uint32_t phase = milliseconds % 2000;
    uint32_t ramp = phase < 1000 ? phase : 2000 - phase;
    return 2 + 22 * ramp / 1000;
}

static esp_err_t render(ota_state_t state, uint64_t elapsed_ms)
{
    uint32_t red = 0, green = 0, blue = 0;
    uint32_t brightness = pulse(elapsed_ms);
    esp_err_t err;
    switch (state) {
    case OTA_IDLE: break;
    case OTA_DOWNLOADING:
        blue = brightness;
        break;
    case OTA_VERIFYING:
        red = green = brightness;
        break;
    case OTA_REBOOTING:
        green = 24;
        break;
    case OTA_FAILED:
        red = elapsed_ms % 1000 < 500 ? 24 : 0;
        break;
    default:
        ESP_LOGE(TAG, "Unknown OTA state: %d", state);
        return ESP_ERR_INVALID_STATE;
    }
    if (normal_renderer != NULL) {
        err = normal_renderer(pixels, pixel_count);
        if (err != ESP_OK) return err;
    }
    for (size_t i = 0; i < pixel_count; ++i) {
        if (normal_renderer != NULL && (state == OTA_IDLE || !pixels[i].ota_override)) {
            err = led_strip_set_pixel(strip, i, pixels[i].red, pixels[i].green, pixels[i].blue);
        } else if (state == OTA_IDLE) {
            err = led_strip_set_pixel_hsv(strip, i, (elapsed_ms % 30000) * 360 / 30000, 255, 24);
        } else {
            err = led_strip_set_pixel(strip, i, red, green, blue);
        }
        if (err != ESP_OK) return err;
    }
    return led_strip_refresh(strip);
}

static void led_worker(void *arg)
{
    (void)arg;
    ota_state_t previous = OTA_IDLE;
    int64_t state_started = esp_timer_get_time();
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        ota_status_t ota;
        ota_manager_get_status(&ota);
        int64_t now = esp_timer_get_time();
        if (ota.state != previous) {
            previous = ota.state;
            state_started = now;
        }
        esp_err_t err = render(ota.state, (now - state_started) / 1000);
        if (err != ESP_OK) {
            /* LED failure must not interrupt a firmware write or trigger a reboot. */
            ESP_LOGE(TAG, "LED output stopped: %s", esp_err_to_name(err));
            esp_err_t cleanup = led_strip_del(strip);
            if (cleanup != ESP_OK) {
                ESP_LOGE(TAG, "LED cleanup failed: %s", esp_err_to_name(cleanup));
            }
            strip = NULL;
            free(pixels);
            pixels = NULL;
            vTaskDelete(NULL);
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(50));
    }
}

esp_err_t status_led_start(void)
{
    if (led_gpio < 0 || strip != NULL) return ESP_ERR_INVALID_STATE;
    const led_strip_config_t config = {
        .strip_gpio_num = led_gpio,
        .max_leds = pixel_count,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    const led_strip_rmt_config_t rmt = {
        .resolution_hz = 10000000,
        .flags.with_dma = true,
    };
    pixels = calloc(pixel_count, sizeof(*pixels));
    if (pixels == NULL) {
        ESP_LOGE(TAG, "Cannot allocate RGB LED output buffer");
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = led_strip_new_rmt_device(&config, &rmt, &strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot initialize RGB LED on GPIO%d: %s",
                 led_gpio, esp_err_to_name(err));
        free(pixels);
        pixels = NULL;
        return err;
    }
    err = render(OTA_IDLE, 0);
    if (err == ESP_OK &&
        xTaskCreate(led_worker, "status_led", 3072, NULL, 2, NULL) != pdPASS) {
        err = ESP_ERR_NO_MEM;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot start RGB LED: %s", esp_err_to_name(err));
        esp_err_t cleanup = led_strip_del(strip);
        if (cleanup != ESP_OK) {
            ESP_LOGE(TAG, "LED cleanup failed: %s", esp_err_to_name(cleanup));
        }
        strip = NULL;
        free(pixels);
        pixels = NULL;
        return err;
    }
    ESP_LOGI(TAG, "%u RGB status LEDs started on GPIO%d", (unsigned)pixel_count, led_gpio);
    return ESP_OK;
}
