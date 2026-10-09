#include "update_check.h"
#include "update_policy.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network_manager.h"
#include "sdkconfig.h"

#define CHECK_INTERVAL_US (6LL * 60 * 60 * 1000000)
#define RETRY_GUARD_US (60LL * 1000000)
#define MANIFEST_MAX 2048

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static update_check_status_t status;
static update_candidate_t candidate;
static TaskHandle_t worker;
static int64_t checked_at;
static int64_t attempted_at;
static bool attempted;
static bool installing;

static bool http_allowed(void)
{
#ifdef CONFIG_OTA_SUITE_OTA_ALLOW_HTTP
    return true;
#else
    return false;
#endif
}

static bool network_ready(void)
{
    network_status_t network;
    network_manager_get_status(&network);
    return network.connected && !network.configuring &&
           (strncmp(CONFIG_OTA_SUITE_UPDATE_MANIFEST_URL, "http://", 7) == 0 || network.time_ready);
}

static bool parse_manifest(const char *body, size_t length, update_candidate_t *out)
{
    const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
    return partition && update_manifest_parse(body, length, CONFIG_OTA_SUITE_UPDATE_MANIFEST_URL,
           esp_app_get_description()->project_name, CONFIG_IDF_TARGET, partition->size, out);
}

static esp_err_t fetch_manifest(update_candidate_t *out)
{
    bool http = strncmp(CONFIG_OTA_SUITE_UPDATE_MANIFEST_URL, "http://", 7) == 0;
    esp_http_client_config_t config = {
        .url = CONFIG_OTA_SUITE_UPDATE_MANIFEST_URL,
        .crt_bundle_attach = http ? NULL : esp_crt_bundle_attach,
        .timeout_ms = 5000,
        .disable_auto_redirect = true,
        .transport_type = http ? HTTP_TRANSPORT_OVER_TCP : HTTP_TRANSPORT_OVER_SSL,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_ERR_NO_MEM;
    int64_t started = esp_timer_get_time();
    esp_err_t err = esp_http_client_open(client, 0);
    char body[MANIFEST_MAX + 1];
    size_t length = 0;
    if (err == ESP_OK) {
        int64_t total = esp_http_client_fetch_headers(client);
        if (total < 0 || total > MANIFEST_MAX || esp_http_client_get_status_code(client) != 200) {
            err = ESP_ERR_INVALID_RESPONSE;
        }
    }
    while (err == ESP_OK) {
        if (esp_timer_get_time() - started > 20000000 || !network_ready()) {
            err = ESP_ERR_TIMEOUT;
            break;
        }
        int bytes = esp_http_client_read(client, body + length, sizeof(body) - 1 - length);
        if (bytes < 0) { err = ESP_FAIL; break; }
        if (bytes == 0) {
            if (!esp_http_client_is_complete_data_received(client)) err = ESP_ERR_INVALID_SIZE;
            break;
        }
        length += bytes;
        if (length >= MANIFEST_MAX) { err = ESP_ERR_INVALID_SIZE; break; }
    }
    body[length] = '\0';
    if (err == ESP_OK && !parse_manifest(body, length, out)) err = ESP_ERR_INVALID_RESPONSE;
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return err;
}

static void check_worker(void *arg)
{
    (void)arg;
    bool was_ready = false;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
        bool connected = network_ready();
        int64_t now = esp_timer_get_time();
        portENTER_CRITICAL(&lock);
        bool due = !attempted || now - attempted_at >= CHECK_INTERVAL_US ||
                   (!was_ready && now - attempted_at >= RETRY_GUARD_US);
        if (!status.checking && status.configured && !installing && connected && due) {
            status.checking = true;
            status.available = false;
            strlcpy(status.message, "Update-Pruefung laeuft.", sizeof(status.message));
        }
        bool run = status.checking;
        if (run) {
            attempted = true;
            attempted_at = now;
        }
        portEXIT_CRITICAL(&lock);
        was_ready = connected;
        if (!run) continue;
        update_candidate_t next = {0};
        esp_err_t err = connected ? fetch_manifest(&next) : ESP_ERR_INVALID_STATE;
        portENTER_CRITICAL(&lock);
        status.checking = false;
        status.available = false;
        if (err == ESP_OK) {
            candidate = next;
            checked_at = esp_timer_get_time();
            time_t wall = time(NULL);
            status.last_success = wall >= 1704067200 ? wall : 0;
            status.last_success_uptime = checked_at / 1000000;
            strlcpy(status.version, next.version, sizeof(status.version));
            strlcpy(status.notes, next.notes, sizeof(status.notes));
            status.stale = false;
            status.available = update_version_newer(next.version, esp_app_get_description()->version);
            strlcpy(status.message, status.available ? "Neue Firmware verfuegbar. Installation nur manuell." :
                    "Keine neuere Firmware verfuegbar.", sizeof(status.message));
        } else {
            status.stale = checked_at != 0;
            snprintf(status.message, sizeof(status.message), "Update-Pruefung fehlgeschlagen: %s. Erneut pruefen.",
                     esp_err_to_name(err));
        }
        portEXIT_CRITICAL(&lock);
    }
}

esp_err_t update_check_init(void)
{
    status.configured = update_manifest_url_valid(CONFIG_OTA_SUITE_UPDATE_MANIFEST_URL, http_allowed());
    strlcpy(status.message, status.configured ? "Noch keine Update-Pruefung." :
            "Manifest-Adresse ungueltig oder HTTP nicht erlaubt.", sizeof(status.message));
    return xTaskCreate(check_worker, "update_check", 8192, NULL, 3, &worker) == pdPASS ?
           ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t update_check_start(void)
{
    if (!network_ready()) return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&lock);
    bool allowed = status.configured && !status.checking && !installing;
    if (allowed) {
        status.checking = true;
        status.available = false;
        strlcpy(status.message, "Update-Pruefung angefordert.", sizeof(status.message));
    }
    portEXIT_CRITICAL(&lock);
    if (!allowed) return ESP_ERR_INVALID_STATE;
    xTaskNotifyGive(worker);
    return ESP_OK;
}

void update_check_get_status(update_check_status_t *out)
{
    portENTER_CRITICAL(&lock);
    *out = status;
    if (checked_at && esp_timer_get_time() - checked_at >= CHECK_INTERVAL_US) {
        out->stale = true;
        out->available = false;
        strlcpy(out->message, "Kandidat veraltet. Updates erneut pruefen.", sizeof(out->message));
    }
    portEXIT_CRITICAL(&lock);
}

esp_err_t update_check_acquire(const char *expected_version, update_candidate_t *out)
{
    portENTER_CRITICAL(&lock);
    bool allowed = expected_version && status.available && !status.checking && !installing &&
                   !status.stale && checked_at && esp_timer_get_time() - checked_at < CHECK_INTERVAL_US &&
                   !strcmp(expected_version, candidate.version);
    if (allowed) {
        installing = true;
        *out = candidate;
    }
    portEXIT_CRITICAL(&lock);
    return allowed ? ESP_OK : ESP_ERR_INVALID_STATE;
}

void update_check_release(void)
{
    portENTER_CRITICAL(&lock);
    installing = false;
    status.available = false;
    status.stale = true;
    strlcpy(status.message, "Nach OTA-Versuch Updates erneut pruefen.", sizeof(status.message));
    portEXIT_CRITICAL(&lock);
}
