/* SPDX-FileCopyrightText: 2026 Thomas Stoll
 * SPDX-License-Identifier: Apache-2.0
 */

#include "device_app.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network_manager.h"
#include "ota_manager.h"
#include "status_led.h"
#include "web_interface.h"
#include "update_check.h"

static void confirm_boot(void *arg)
{
    (void)arg;
    /* Confirm only after all local services have survived the startup window. */
    vTaskDelay(pdMS_TO_TICKS(10000));
    ESP_ERROR_CHECK(ota_manager_confirm_boot());
    ESP_LOGI("device_app", "Startup checks passed; firmware confirmed");
    vTaskDelete(NULL);
}

esp_err_t device_app_start(void)
{
    esp_err_t err = ota_manager_init();
    if (err != ESP_OK) {
        return err;
    }
    err = network_manager_init();
    if (err != ESP_OK) {
        return err;
    }
    err = update_check_init();
    if (err != ESP_OK) {
        return err;
    }
    err = web_interface_start();
    if (err != ESP_OK) {
        return err;
    }
#ifdef CONFIG_OTA_SUITE_STATUS_LED_ENABLE
    err = status_led_start();
    if (err != ESP_OK) {
        return err;
    }
#endif
    if (xTaskCreate(confirm_boot, "confirm_boot", 3072, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE("device_app", "Cannot create boot confirmation task");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
