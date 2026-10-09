#include "status_led.h"

#include <stdint.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "ota_manager.h"
#include "sdkconfig.h"

static const char *TAG = "status_led";
static led_strip_handle_t strip;

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
    case OTA_IDLE:
        err = led_strip_set_pixel_hsv(strip, 0, (elapsed_ms % 30000) * 360 / 30000,
                                      255, 24);
        break;
    case OTA_DOWNLOADING:
        blue = brightness;
        err = led_strip_set_pixel(strip, 0, red, green, blue);
        break;
    case OTA_VERIFYING:
        red = green = brightness;
        err = led_strip_set_pixel(strip, 0, red, green, blue);
        break;
    case OTA_REBOOTING:
        green = 24;
        err = led_strip_set_pixel(strip, 0, red, green, blue);
        break;
    case OTA_FAILED:
        red = elapsed_ms % 1000 < 500 ? 24 : 0;
        err = led_strip_set_pixel(strip, 0, red, green, blue);
        break;
    default:
        ESP_LOGE(TAG, "Unknown OTA state: %d", state);
        return ESP_ERR_INVALID_STATE;
    }
    return err == ESP_OK ? led_strip_refresh(strip) : err;
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
            vTaskDelete(NULL);
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(50));
    }
}

esp_err_t status_led_start(void)
{
    const led_strip_config_t config = {
        .strip_gpio_num = CONFIG_OTA_SUITE_LED_GPIO,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    const led_strip_rmt_config_t rmt = {
        .resolution_hz = 10000000,
        .flags.with_dma = true,
    };
    esp_err_t err = led_strip_new_rmt_device(&config, &rmt, &strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot initialize RGB LED on GPIO%d: %s",
                 CONFIG_OTA_SUITE_LED_GPIO, esp_err_to_name(err));
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
        return err;
    }
    ESP_LOGI(TAG, "RGB status LED started on GPIO%d", CONFIG_OTA_SUITE_LED_GPIO);
    return ESP_OK;
}
