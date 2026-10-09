#include "web_interface.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_random.h"
#include "mbedtls/base64.h"
#include "network_manager.h"
#include "ota_manager.h"
#include "update_check.h"

extern const unsigned char index_start[] asm("_binary_index_html_start");
extern const unsigned char index_end[] asm("_binary_index_html_end");
extern const unsigned char script_start[] asm("_binary_app_js_start");
extern const unsigned char script_end[] asm("_binary_app_js_end");
extern const unsigned char css_start[] asm("_binary_bootstrap_min_css_start");
extern const unsigned char css_end[] asm("_binary_bootstrap_min_css_end");

static const char *TAG = "web";
static char expected_auth[128];
static char csrf_token[33];

static void response_headers(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
    httpd_resp_set_hdr(req, "Content-Security-Policy",
                       "default-src 'self'; script-src 'self'; style-src 'self'; "
                       "connect-src 'self'; img-src 'self' data:; frame-ancestors 'none'; "
                       "base-uri 'none'; form-action 'self'");
}

static esp_err_t error_response(httpd_req_t *req, const char *status, const char *message)
{
    response_headers(req);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    ESP_LOGW(TAG, "%s %s: %s", status, req->uri, message);
    return httpd_resp_send(req, message, HTTPD_RESP_USE_STRLEN);
}

static bool equal_secret(const char *a, const char *b)
{
    size_t length = strlen(b);
    if (strlen(a) != length) {
        return false;
    }
    unsigned char difference = 0;
    for (size_t i = 0; i < length; ++i) {
        difference |= (unsigned char)a[i] ^ (unsigned char)b[i];
    }
    return difference == 0;
}

static bool authorize(httpd_req_t *req, bool mutation)
{
    char value[128];
    if (httpd_req_get_hdr_value_str(req, "Authorization", value, sizeof(value)) != ESP_OK ||
        !equal_secret(value, expected_auth)) {
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"OTA Suite\", charset=\"UTF-8\"");
        error_response(req, "401 Unauthorized", "Anmeldung erforderlich: admin / Access-Point-Passwort.");
        return false;
    }
    if (mutation &&
        (httpd_req_get_hdr_value_str(req, "X-CSRF-Token", value, sizeof(value)) != ESP_OK ||
         !equal_secret(value, csrf_token))) {
        error_response(req, "403 Forbidden", "Ungueltiges Sitzungstoken. Seite neu laden.");
        return false;
    }
    return true;
}

static esp_err_t index_handler(httpd_req_t *req)
{
    if (!authorize(req, false)) {
        return ESP_OK;
    }
    response_headers(req);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, (const char *)index_start, index_end - index_start);
}

static esp_err_t script_handler(httpd_req_t *req)
{
    if (!authorize(req, false)) {
        return ESP_OK;
    }
    response_headers(req);
    httpd_resp_set_type(req, "application/javascript; charset=utf-8");
    return httpd_resp_send(req, (const char *)script_start, script_end - script_start);
}

static esp_err_t css_handler(httpd_req_t *req)
{
    if (!authorize(req, false)) {
        return ESP_OK;
    }
    response_headers(req);
    httpd_resp_set_type(req, "text/css; charset=utf-8");
    return httpd_resp_send(req, (const char *)css_start, css_end - css_start);
}

static const char *state_name(ota_state_t state)
{
    switch (state) {
    case OTA_IDLE: return "idle";
    case OTA_DOWNLOADING: return "downloading";
    case OTA_VERIFYING: return "verifying";
    case OTA_REBOOTING: return "rebooting";
    case OTA_FAILED: return "failed";
    }
    return "unknown";
}

static esp_err_t status_handler(httpd_req_t *req)
{
    if (!authorize(req, false)) {
        return ESP_OK;
    }
    network_status_t network;
    ota_status_t ota;
    update_check_status_t update;
    update_check_get_status(&update);
    network_manager_get_status(&network);
    ota_manager_get_status(&ota);
    bool busy = ota_manager_is_busy();
    bool ready = ota_manager_is_ready();
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return error_response(req, "500 Internal Server Error", "Nicht genug Speicher fuer Status.");
    }
    bool ok =
        cJSON_AddStringToObject(root, "version", esp_app_get_description()->version) &&
        cJSON_AddStringToObject(root, "project", esp_app_get_description()->project_name) &&
        cJSON_AddStringToObject(root, "csrfToken", csrf_token) &&
        cJSON_AddStringToObject(root, "ssid", network.ssid) &&
        cJSON_AddStringToObject(root, "apSsid", network.ap_ssid) &&
        cJSON_AddStringToObject(root, "ip", network.ip) &&
        cJSON_AddStringToObject(root, "networkMessage", network.message) &&
        cJSON_AddBoolToObject(root, "connected", network.connected) &&
        cJSON_AddBoolToObject(root, "configuring", network.configuring) &&
        cJSON_AddBoolToObject(root, "timeReady", network.time_ready) &&
        cJSON_AddBoolToObject(root, "otaConfigured", ota.configured) &&
        cJSON_AddBoolToObject(root, "otaInsecureHttp", ota.insecure_http) &&
        cJSON_AddBoolToObject(root, "bootConfirmed", ota.boot_confirmed) &&
        cJSON_AddBoolToObject(root, "otaReady", ready) &&
        cJSON_AddBoolToObject(root, "otaBusy", busy) &&
        cJSON_AddStringToObject(root, "otaState", state_name(ota.state)) &&
        cJSON_AddStringToObject(root, "otaMessage", ota.message) &&
        cJSON_AddNumberToObject(root, "bytesRead", ota.bytes_read) &&
        cJSON_AddNumberToObject(root, "totalBytes", ota.total_bytes) &&
        cJSON_AddBoolToObject(root, "updateConfigured", update.configured) &&
        cJSON_AddBoolToObject(root, "updateChecking", update.checking) &&
        cJSON_AddBoolToObject(root, "updateAvailable", update.available) &&
        cJSON_AddBoolToObject(root, "updateStale", update.stale) &&
        cJSON_AddStringToObject(root, "availableVersion", update.version) &&
        cJSON_AddStringToObject(root, "updateNotes", update.notes) &&
        cJSON_AddStringToObject(root, "updateMessage", update.message) &&
        cJSON_AddNumberToObject(root, "updateLastSuccess", update.last_success) &&
        cJSON_AddNumberToObject(root, "updateLastSuccessUptime", update.last_success_uptime);
    char *json = ok ? cJSON_PrintUnformatted(root) : NULL;
    cJSON_Delete(root);
    if (json == NULL) {
        return error_response(req, "500 Internal Server Error", "Status konnte nicht erstellt werden.");
    }
    response_headers(req);
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    cJSON_free(json);
    return err;
}

static esp_err_t network_handler(httpd_req_t *req)
{
    if (!authorize(req, true)) {
        return ESP_OK;
    }
    if (ota_manager_is_busy()) {
        return error_response(req, "409 Conflict", "WLAN kann waehrend OTA nicht geaendert werden.");
    }
    if (req->content_len == 0 || req->content_len >= 512) {
        return error_response(req, "400 Bad Request", "Ungueltige Laenge der WLAN-Konfiguration.");
    }
    char body[512];
    size_t received = 0;
    while (received < req->content_len) {
        int bytes = httpd_req_recv(req, body + received, req->content_len - received);
        if (bytes <= 0) {
            return error_response(req, "408 Request Timeout", "WLAN-Anfrage unvollstaendig.");
        }
        received += bytes;
    }
    body[received] = '\0';
    if (strlen(body) != received || strstr(body, "\\u0000") != NULL) {
        return error_response(req, "400 Bad Request", "Nullzeichen sind nicht erlaubt.");
    }
    cJSON *json = cJSON_ParseWithLengthOpts(body, received + 1, NULL, true);
    memset(body, 0, sizeof(body));
    if (!cJSON_IsObject(json)) {
        cJSON_Delete(json);
        return error_response(req, "400 Bad Request", "Ungueltiges JSON.");
    }
    const cJSON *ssid = cJSON_GetObjectItemCaseSensitive(json, "ssid");
    const cJSON *password = cJSON_GetObjectItemCaseSensitive(json, "password");
    esp_err_t err = ESP_ERR_INVALID_ARG;
    if (cJSON_IsString(ssid) && cJSON_IsString(password)) {
        err = network_manager_configure(ssid->valuestring, password->valuestring);
    }
    if (cJSON_IsString(password)) {
        memset(password->valuestring, 0, strlen(password->valuestring));
    }
    cJSON_Delete(json);
    if (err != ESP_OK) {
        return error_response(req, err == ESP_ERR_INVALID_ARG ? "400 Bad Request" : "409 Conflict",
                              "SSID: 1-32 Bytes; Passwort: leer, 8-63 Bytes oder 64 Hex-Zeichen. "
                              "Es darf keine weitere Konfiguration laufen.");
    }
    response_headers(req);
    httpd_resp_set_status(req, "202 Accepted");
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(req, "WLAN-Konfiguration angenommen; Speicher- und Verbindungsstatus beachten.");
}

static esp_err_t ota_handler(httpd_req_t *req)
{
    if (!authorize(req, true)) {
        return ESP_OK;
    }
    if (req->content_len != 0) {
        return error_response(req, "400 Bad Request", "OTA-Start erwartet keinen Request-Body.");
    }
    char version[32];
    if (httpd_req_get_hdr_value_str(req, "X-Update-Version", version, sizeof(version)) != ESP_OK ||
        ota_manager_start(version) != ESP_OK) {
        return error_response(req, "409 Conflict",
                              "OTA nicht bereit: erlaubte Firmware-Adresse, WLAN, Startpruefung "
                              "und bei HTTPS die Uhrzeit pruefen. Kandidat veraltet/geaendert? Updates erneut pruefen.");
    }
    response_headers(req);
    httpd_resp_set_status(req, "202 Accepted");
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(req, "Manuelles OTA-Update gestartet.");
}

static esp_err_t check_handler(httpd_req_t *req)
{
    if (!authorize(req, true)) return ESP_OK;
    if (req->content_len != 0) {
        return error_response(req, "400 Bad Request", "Update-Pruefung erwartet keinen Request-Body.");
    }
    if (ota_manager_is_busy() || update_check_start() != ESP_OK) {
        return error_response(req, "409 Conflict", "Pruefung nicht bereit: WLAN/Uhrzeit/Manifest oder aktive OTA pruefen.");
    }
    response_headers(req);
    httpd_resp_set_status(req, "202 Accepted");
    return httpd_resp_sendstr(req, "Update-Pruefung gestartet. Keine automatische Installation.");
}

esp_err_t web_interface_start(void)
{
    char login[80];
    snprintf(login, sizeof(login), "admin:%s", network_manager_admin_password());
    size_t length = 0;
    memcpy(expected_auth, "Basic ", 6);
    int result = mbedtls_base64_encode((unsigned char *)expected_auth + 6,
                                      sizeof(expected_auth) - 7, &length,
                                      (const unsigned char *)login, strlen(login));
    memset(login, 0, sizeof(login));
    if (result != 0) {
        ESP_LOGE(TAG, "Cannot encode admin authentication");
        return ESP_FAIL;
    }
    expected_auth[6 + length] = '\0';
    uint8_t random[16];
    esp_fill_random(random, sizeof(random));
    for (size_t i = 0; i < sizeof(random); ++i) {
        snprintf(csrf_token + 2 * i, 3, "%02x", random[i]);
    }
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 5;
    config.send_wait_timeout = 5;
    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server failed: %s", esp_err_to_name(err));
        return err;
    }
    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = index_handler},
        {.uri = "/app.js", .method = HTTP_GET, .handler = script_handler},
        {.uri = "/bootstrap.min.css", .method = HTTP_GET, .handler = css_handler},
        {.uri = "/api/status", .method = HTTP_GET, .handler = status_handler},
        {.uri = "/api/network", .method = HTTP_POST, .handler = network_handler},
        {.uri = "/api/ota", .method = HTTP_POST, .handler = ota_handler},
        {.uri = "/api/updates/check", .method = HTTP_POST, .handler = check_handler},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        err = httpd_register_uri_handler(server, &routes[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Cannot register %s: %s", routes[i].uri, esp_err_to_name(err));
            esp_err_t stopped = httpd_stop(server);
            if (stopped != ESP_OK) {
                ESP_LOGE(TAG, "HTTP cleanup failed: %s", esp_err_to_name(stopped));
            }
            return err;
        }
    }
    ESP_LOGI(TAG, "Local web interface started; OTA remains idle");
    return ESP_OK;
}
