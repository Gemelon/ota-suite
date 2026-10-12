/* SPDX-FileCopyrightText: 2026 Thomas Stoll
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ota_manager.h"

#include <stdio.h>
#include <string.h>
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "http_parser.h"
#include "network_manager.h"
#include "sdkconfig.h"
#include "psa/crypto.h"
#include "update_check.h"
#include "update_policy.h"

#if !CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
#error "OTA Suite requires CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE"
#endif
#if !CONFIG_MBEDTLS_CERTIFICATE_BUNDLE || !CONFIG_MBEDTLS_HAVE_TIME_DATE
#error "OTA Suite requires the CA bundle and certificate date validation"
#endif

static const char *TAG = "ota";
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static ota_status_t status;
static TaskHandle_t worker;
static update_candidate_t selected;

static bool valid_url(const char *url)
{
    bool scheme_allowed = strncmp(url, "https://", 8) == 0;
#ifdef CONFIG_OTA_SUITE_OTA_ALLOW_HTTP
    scheme_allowed = scheme_allowed || strncmp(url, "http://", 7) == 0;
#endif
    if (!scheme_allowed || strlen(url) > 1024) {
        return false;
    }
    struct http_parser_url parsed;
    if (http_parser_parse_url(url, strlen(url), 0, &parsed) != 0 ||
        !(parsed.field_set & (1 << UF_HOST)) ||
        parsed.field_data[UF_HOST].len == 0 ||
        (parsed.field_set & ((1 << UF_USERINFO) | (1 << UF_FRAGMENT)))) {
        return false;
    }
    for (const unsigned char *p = (const unsigned char *)url; *p; ++p) {
        if (*p <= 32 || *p == 127 || *p == '@' || *p == '#' || *p == '\\') {
            return false;
        }
    }
    return true;
}

static bool active(ota_state_t state)
{
    return state == OTA_DOWNLOADING || state == OTA_VERIFYING || state == OTA_REBOOTING;
}

static bool ready(const ota_status_t *ota, const network_status_t *network)
{
    return ota->configured && ota->boot_confirmed && !active(ota->state) &&
           network->connected && (ota->insecure_http || network->time_ready) &&
           !network->configuring;
}

static void set_state(ota_state_t state, const char *message)
{
    portENTER_CRITICAL(&lock);
    status.state = state;
    strlcpy(status.message, message, sizeof(status.message));
    portEXIT_CRITICAL(&lock);
}

static void fail_update(const char *stage, esp_err_t err)
{
    update_check_release();
    char message[160];
    snprintf(message, sizeof(message), "%s: %s. Bisherige Firmware bleibt aktiv.",
             stage, esp_err_to_name(err));
    ESP_LOGE(TAG, "%s", message);
    set_state(OTA_FAILED, message);
}

static esp_err_t validate_image(const esp_app_desc_t *image)
{
    const esp_app_desc_t *running = esp_app_get_description();
    if (memchr(image->project_name, '\0', sizeof(image->project_name)) == NULL ||
        memchr(image->version, '\0', sizeof(image->version)) == NULL ||
        strcmp(image->project_name, running->project_name) != 0 ||
        strcmp(image->version, selected.version) != 0 ||
        !update_version_newer(image->version, running->version)) {
        ESP_LOGE(TAG, "Image belongs to a different project or has an invalid descriptor");
        return ESP_ERR_OTA_VALIDATE_FAILED;
    }
    if (memcmp(image->app_elf_sha256, running->app_elf_sha256,
               sizeof(image->app_elf_sha256)) == 0) {
        ESP_LOGW(TAG, "Identical firmware is already running");
        return ESP_ERR_INVALID_VERSION;
    }
    const esp_partition_t *invalid = esp_ota_get_last_invalid_partition();
    if (invalid != NULL) {
        esp_app_desc_t previous;
        esp_err_t err = esp_ota_get_partition_description(invalid, &previous);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Cannot inspect rejected firmware: %s", esp_err_to_name(err));
            return err;
        }
        if (memcmp(image->app_elf_sha256, previous.app_elf_sha256,
                   sizeof(image->app_elf_sha256)) == 0) {
            ESP_LOGE(TAG, "This firmware was previously rolled back");
            return ESP_ERR_OTA_VALIDATE_FAILED;
        }
    }
    ESP_LOGI(TAG, "Candidate version: %s", image->version);
    return ESP_OK;
}

static void perform_update(void)
{
    bool insecure_http = strncmp(selected.url, "http://", 7) == 0;
    const esp_http_client_config_t http = {
        .url = selected.url,
        .crt_bundle_attach = insecure_http ? NULL : esp_crt_bundle_attach,
        .timeout_ms = 15000,
        .keep_alive_enable = true,
        .disable_auto_redirect = true,
        .transport_type = insecure_http ? HTTP_TRANSPORT_OVER_TCP : HTTP_TRANSPORT_OVER_SSL,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http);
    esp_ota_handle_t handle = 0;
    bool begun = false;
    psa_hash_operation_t hash = PSA_HASH_OPERATION_INIT;
    const char *stage = "Firmware download failed";
    int64_t started = esp_timer_get_time();
    esp_err_t err = client ? esp_http_client_open(client, 0) : ESP_ERR_NO_MEM;
    if (err != ESP_OK) goto cleanup;
    int64_t total = esp_http_client_fetch_headers(client);
    if (total < 0 || (total > 0 && total != selected.size) ||
        esp_http_client_get_status_code(client) != 200) {
        err = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    if (next == NULL || selected.size > next->size) {
        err = ESP_ERR_INVALID_SIZE;
        goto cleanup;
    }
    err = esp_ota_begin(next, selected.size, &handle);
    if (err != ESP_OK) goto cleanup;
    begun = true;
    if (psa_crypto_init() != PSA_SUCCESS ||
        psa_hash_setup(&hash, PSA_ALG_SHA_256) != PSA_SUCCESS) { err = ESP_FAIL; goto cleanup; }
    portENTER_CRITICAL(&lock);
    status.total_bytes = selected.size;
    portEXIT_CRITICAL(&lock);
    unsigned char buffer[2048];
    uint32_t received = 0;
    for (;;) {
        network_status_t network;
        network_manager_get_status(&network);
        if (!network.connected || esp_timer_get_time() - started > 300000000LL) {
            err = ESP_ERR_TIMEOUT;
            goto cleanup;
        }
        int bytes = esp_http_client_read(client, (char *)buffer, sizeof(buffer));
        if (bytes < 0) { err = ESP_FAIL; goto cleanup; }
        if (bytes == 0) break;
        if ((uint32_t)bytes > selected.size - received) {
            err = ESP_ERR_INVALID_SIZE;
            goto cleanup;
        }
        if (psa_hash_update(&hash, buffer, bytes) != PSA_SUCCESS) { err = ESP_FAIL; goto cleanup; }
        err = esp_ota_write(handle, buffer, bytes);
        if (err != ESP_OK) goto cleanup;
        received += bytes;
        portENTER_CRITICAL(&lock);
        status.bytes_read = received;
        portEXIT_CRITICAL(&lock);
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    stage = "Firmware size or SHA256 does not match the manifest";
    unsigned char digest[32];
    size_t digest_length = 0;
    if (received != selected.size || !esp_http_client_is_complete_data_received(client) ||
        psa_hash_finish(&hash, digest, sizeof(digest), &digest_length) != PSA_SUCCESS ||
        digest_length != sizeof(digest) || memcmp(digest, selected.sha256, 32)) {
        err = ESP_ERR_OTA_VALIDATE_FAILED;
        goto cleanup;
    }
    set_state(OTA_VERIFYING, "Verifying firmware. Do not disconnect power.");
    stage = "Firmware verification failed";
    esp_app_desc_t image;
    err = esp_ota_get_partition_description(next, &image);
    if (err == ESP_OK) err = validate_image(&image);
    if (err != ESP_OK) goto cleanup;
    /* Only after exact downloaded binary size/hash checks may IDF finalize and select boot. */
    err = esp_ota_end(handle);
    begun = false; /* esp_ota_end consumes the handle, including on error. */
    if (err == ESP_OK) err = esp_ota_set_boot_partition(next);
cleanup:
    if (begun) esp_ota_abort(handle);
    psa_hash_abort(&hash);
    if (client) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
    }
    if (err != ESP_OK) { fail_update(stage, err); return; }
    set_state(OTA_REBOOTING, "Update successful. Restarting, then performing startup checks.");
    ESP_LOGI(TAG, "Firmware verified; restarting");
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
}

static void ota_worker(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        perform_update();
    }
}

esp_err_t ota_manager_init(void)
{
    status.configured = valid_url(CONFIG_OTA_SUITE_UPDATE_MANIFEST_URL);
    status.insecure_http = status.configured &&
                           strncmp(CONFIG_OTA_SUITE_UPDATE_MANIFEST_URL, "http://", 7) == 0;
    strlcpy(status.message, status.configured ?
            "OTA starts only via the button. Startup checks in progress." :
            "OTA disabled: configure an HTTPS URL or explicitly allowed HTTP URL.",
            sizeof(status.message));
    if (!status.configured) {
        ESP_LOGE(TAG, "Configured OTA URL is invalid or HTTP is not explicitly allowed");
    }
    if (status.insecure_http) {
        ESP_LOGW(TAG, "HTTP OTA test mode: firmware server and transport are NOT authenticated");
    }
    if (esp_ota_get_next_update_partition(NULL) == NULL ||
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL) == NULL) {
        ESP_LOGE(TAG, "Missing OTA partitions");
        return ESP_ERR_NOT_FOUND;
    }
    if (xTaskCreate(ota_worker, "ota", 8192, NULL, 5, &worker) != pdPASS) {
        ESP_LOGE(TAG, "Cannot create OTA task");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t ota_manager_confirm_boot(void)
{
    esp_ota_img_states_t state;
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_err_t err = esp_ota_get_state_partition(running, &state);
    if (err == ESP_ERR_NOT_FOUND) {
        /* Initial serial installation has no OTA state entry yet. */
        err = ESP_OK;
    } else if (err == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        err = esp_ota_mark_app_valid_cancel_rollback();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Boot confirmation failed: %s", esp_err_to_name(err));
        return err;
    }
    portENTER_CRITICAL(&lock);
    status.boot_confirmed = true;
    if (status.configured) {
        strlcpy(status.message, "Ready. OTA starts only after clicking Start update.",
                sizeof(status.message));
    }
    portEXIT_CRITICAL(&lock);
    return ESP_OK;
}

esp_err_t ota_manager_start(const char *expected_version)
{
    network_status_t network;
    network_manager_get_status(&network);
    portENTER_CRITICAL(&lock);
    bool allowed = ready(&status, &network);
    if (allowed) {
        status.state = OTA_DOWNLOADING;
        status.bytes_read = 0;
        status.total_bytes = 0;
        strlcpy(status.message, status.insecure_http ?
                "HTTP test download started (unencrypted). Do not disconnect power." :
                "HTTPS download started. Do not disconnect power.",
                sizeof(status.message));
    }
    portEXIT_CRITICAL(&lock);
    if (!allowed) {
        ESP_LOGW(TAG, "Manual OTA request rejected: not ready or already active");
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = update_check_acquire(expected_version, &selected);
    if (err != ESP_OK) {
        set_state(OTA_FAILED, "Update candidate missing, stale or changed. Check for updates again.");
        return err;
    }
    xTaskNotifyGive(worker);
    return ESP_OK;
}

void ota_manager_get_status(ota_status_t *out)
{
    portENTER_CRITICAL(&lock);
    *out = status;
    portEXIT_CRITICAL(&lock);
}

bool ota_manager_is_busy(void)
{
    portENTER_CRITICAL(&lock);
    bool busy = active(status.state);
    portEXIT_CRITICAL(&lock);
    return busy;
}

bool ota_manager_is_ready(void)
{
    network_status_t network;
    ota_status_t ota;
    network_manager_get_status(&network);
    ota_manager_get_status(&ota);
    update_check_status_t update;
    update_check_get_status(&update);
    return ready(&ota, &network) && update.available && !update.checking && !update.stale;
}
