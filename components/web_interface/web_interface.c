/* SPDX-FileCopyrightText: 2026 Thomas Stoll
 * SPDX-License-Identifier: Apache-2.0
 */

#include "web_interface.h"
#include "web_auth.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "network_manager.h"
#include "ota_manager.h"
#include "update_check.h"

extern const unsigned char index_start[] asm("_binary_index_html_start");
extern const unsigned char index_end[] asm("_binary_index_html_end");
extern const unsigned char script_start[] asm("_binary_app_js_start");
extern const unsigned char script_end[] asm("_binary_app_js_end");
extern const unsigned char css_start[] asm("_binary_bootstrap_min_css_start");
extern const unsigned char css_end[] asm("_binary_bootstrap_min_css_end");
extern const unsigned char theme_start[] asm("_binary_app_css_start");
extern const unsigned char theme_end[] asm("_binary_app_css_end");
extern const unsigned char i18n_start[] asm("_binary_i18n_js_start");
extern const unsigned char i18n_end[] asm("_binary_i18n_js_end");
extern const unsigned char en_start[] asm("_binary_en_json_start");
extern const unsigned char en_end[] asm("_binary_en_json_end");
extern const unsigned char de_start[] asm("_binary_de_json_start");
extern const unsigned char de_end[] asm("_binary_de_json_end");

static const char *TAG = "web";
#define MAX_EXTENSIONS 16
static const web_interface_extension_t *extensions[MAX_EXTENSIONS];
static size_t extension_count;
static httpd_uri_t extension_asset_routes[MAX_EXTENSIONS][2];
static char extension_asset_paths[MAX_EXTENSIONS][2][40];

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

static esp_err_t index_handler(httpd_req_t *req)
{
    if (!web_interface_authorize_request(req, false)) {
        return ESP_OK;
    }
    response_headers(req);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    const char *start = (const char *)index_start;
    const char *title = strstr(start, "<title>Firmware</title>");
    if (title == NULL) {
        return ESP_FAIL;
    }
    esp_err_t err = httpd_resp_send_chunk(req, start, title - start);
    if (err == ESP_OK) {
        err = httpd_resp_sendstr_chunk(req, "<title>");
    }
    for (const char *p = esp_app_get_description()->project_name; err == ESP_OK && *p; ++p) {
        switch (*p) {
        case '&': err = httpd_resp_sendstr_chunk(req, "&amp;"); break;
        case '<': err = httpd_resp_sendstr_chunk(req, "&lt;"); break;
        case '>': err = httpd_resp_sendstr_chunk(req, "&gt;"); break;
        default: err = httpd_resp_send_chunk(req, p, 1); break;
        }
    }
    if (err == ESP_OK) {
        const char *rest = title + strlen("<title>Firmware");
        err = httpd_resp_send_chunk(req, rest, (const char *)index_end - rest);
    }
    return err == ESP_OK ? httpd_resp_send_chunk(req, NULL, 0) : err;
}

static esp_err_t script_handler(httpd_req_t *req)
{
    if (!web_interface_authorize_request(req, false)) {
        return ESP_OK;
    }
    response_headers(req);
    httpd_resp_set_type(req, "application/javascript; charset=utf-8");
    return httpd_resp_send(req, (const char *)script_start, script_end - script_start);
}

static esp_err_t css_handler(httpd_req_t *req)
{
    response_headers(req);
    httpd_resp_set_type(req, "text/css; charset=utf-8");
    return httpd_resp_send(req, (const char *)css_start, css_end - css_start);
}

static esp_err_t language_handler(httpd_req_t *req)
{
    response_headers(req);
    httpd_resp_set_type(req, "application/javascript; charset=utf-8");
    esp_err_t err = httpd_resp_sendstr_chunk(req, "window.EBCLanguages={en:");
    if (err == ESP_OK) err = httpd_resp_send_chunk(req, (const char *)en_start, en_end - en_start);
    if (err == ESP_OK) err = httpd_resp_sendstr_chunk(req, ",de:");
    if (err == ESP_OK) err = httpd_resp_send_chunk(req, (const char *)de_start, de_end - de_start);
#ifdef CONFIG_OTA_SUITE_WEB_SD_LANGUAGES
    if (err == ESP_OK) err = httpd_resp_sendstr_chunk(req,
        strstr(req->uri, "?login=1") != NULL ? ",loginPage:true,sdEnabled:false};\n" : ",sdEnabled:true};\n");
#else
    if (err == ESP_OK) err = httpd_resp_sendstr_chunk(req, ",sdEnabled:false};\n");
#endif
    if (err == ESP_OK) err = httpd_resp_send_chunk(req, (const char *)i18n_start, i18n_end - i18n_start);
    return err == ESP_OK ? httpd_resp_send_chunk(req, NULL, 0) : err;
}

static esp_err_t theme_handler(httpd_req_t *req)
{
    response_headers(req);
    httpd_resp_set_type(req, "text/css; charset=utf-8");
    return httpd_resp_send(req, (const char *)theme_start, theme_end - theme_start);
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
    if (!web_interface_authorize_request(req, false)) {
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
        return error_response(req, "500 Internal Server Error", "Not enough memory for status.");
    }
    bool ok =
        cJSON_AddStringToObject(root, "version", esp_app_get_description()->version) &&
        cJSON_AddStringToObject(root, "project", esp_app_get_description()->project_name) &&
        cJSON_AddStringToObject(root, "csrfToken", web_auth_csrf_token()) &&
        cJSON_AddNumberToObject(root, "sessionDays", web_auth_session_days()) &&
        cJSON_AddBoolToObject(root, "settingsPasswordRequired", web_auth_settings_password_required()) &&
        cJSON_AddStringToObject(root, "tabOrder", CONFIG_OTA_SUITE_WEB_TAB_ORDER) &&
        cJSON_AddStringToObject(root, "primaryTabs", CONFIG_OTA_SUITE_WEB_PRIMARY_TABS) &&
        cJSON_AddStringToObject(root, "activityUrl", CONFIG_OTA_SUITE_WEB_ACTIVITY_URL) &&
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
    cJSON *tabs = cJSON_CreateArray();
    if (tabs == NULL) {
        cJSON_Delete(root);
        return error_response(req, "500 Internal Server Error", "Could not create UI tabs.");
    }
    for (size_t i = 0; ok && i < extension_count; ++i) {
        if (extensions[i]->visible != NULL && !extensions[i]->visible()) {
            continue;
        }
        cJSON *tab = cJSON_CreateObject();
        if (tab == NULL ||
            !cJSON_AddStringToObject(tab, "id", extensions[i]->id) ||
            !cJSON_AddStringToObject(tab, "label", extensions[i]->label) ||
            !cJSON_AddStringToObject(tab, "contentUrl", extension_asset_routes[i][0].uri) ||
            !cJSON_AddStringToObject(tab, "scriptUrl", extension_asset_routes[i][1].uri) ||
            !cJSON_AddStringToObject(tab, "placement",
                                     extensions[i]->placement != NULL ? extensions[i]->placement : "tab") ||
            !cJSON_AddItemToArray(tabs, tab)) {
            cJSON_Delete(tab);
            ok = false;
            break;
        }
    }
    if (ok && !cJSON_AddItemToObject(root, "tabs", tabs)) {
        ok = false;
    }
    if (!ok) {
        cJSON_Delete(tabs);
    }
    char *json = ok ? cJSON_PrintUnformatted(root) : NULL;
    cJSON_Delete(root);
    if (json == NULL) {
        return error_response(req, "500 Internal Server Error", "Could not create status.");
    }
    response_headers(req);
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    cJSON_free(json);
    return err;
}

static esp_err_t network_handler(httpd_req_t *req)
{
    if (!web_interface_authorize_settings_request(req)) {
        return ESP_OK;
    }
    if (ota_manager_is_busy()) {
        return error_response(req, "409 Conflict", "Wi-Fi cannot be changed during OTA.");
    }
    if (req->content_len == 0 || req->content_len >= 512) {
        return error_response(req, "400 Bad Request", "Invalid Wi-Fi configuration length.");
    }
    char body[512];
    size_t received = 0;
    while (received < req->content_len) {
        int bytes = httpd_req_recv(req, body + received, req->content_len - received);
        if (bytes <= 0) {
            return error_response(req, "408 Request Timeout", "Incomplete Wi-Fi request.");
        }
        received += bytes;
    }
    body[received] = '\0';
    if (strlen(body) != received || strstr(body, "\\u0000") != NULL) {
        return error_response(req, "400 Bad Request", "Null characters are not allowed.");
    }
    cJSON *json = cJSON_ParseWithLengthOpts(body, received + 1, NULL, true);
    memset(body, 0, sizeof(body));
    if (!cJSON_IsObject(json)) {
        cJSON_Delete(json);
        return error_response(req, "400 Bad Request", "Invalid JSON.");
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
                              "SSID: 1–32 bytes; password: empty, 8–63 bytes or 64 hexadecimal characters. "
                              "No other configuration may be running.");
    }
    response_headers(req);
    httpd_resp_set_status(req, "202 Accepted");
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(req, "Wi-Fi configuration accepted; check save and connection status.");
}

static esp_err_t ota_handler(httpd_req_t *req)
{
    if (!web_interface_authorize_request(req, true)) {
        return ESP_OK;
    }
    if (req->content_len != 0) {
        return error_response(req, "400 Bad Request", "OTA start must have an empty request body.");
    }
    char version[32];
    if (httpd_req_get_hdr_value_str(req, "X-Update-Version", version, sizeof(version)) != ESP_OK ||
        ota_manager_start(version) != ESP_OK) {
        return error_response(req, "409 Conflict",
                              "OTA not ready: check allowed firmware URL, Wi-Fi, startup checks "
                              "and the clock for HTTPS. Update candidate stale/changed? Check for updates again.");
    }
    response_headers(req);
    httpd_resp_set_status(req, "202 Accepted");
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(req, "Manual OTA update started.");
}

static esp_err_t check_handler(httpd_req_t *req)
{
    if (!web_interface_authorize_request(req, true)) return ESP_OK;
    if (req->content_len != 0) {
        return error_response(req, "400 Bad Request", "Update check must have an empty request body.");
    }
    if (ota_manager_is_busy() || update_check_start() != ESP_OK) {
        return error_response(req, "409 Conflict", "Check not ready: check Wi-Fi/clock/manifest or active OTA.");
    }
    response_headers(req);
    httpd_resp_set_status(req, "202 Accepted");
    return httpd_resp_sendstr(req, "Update check started. No automatic installation.");
}

static esp_err_t extension_asset_handler(httpd_req_t *req)
{
    if (!web_interface_authorize_request(req, false)) {
        return ESP_OK;
    }
    const web_interface_extension_t *extension = req->user_ctx;
    bool script = strstr(req->uri, "/script") != NULL;
    const unsigned char *content = script ? extension->script : extension->content;
    size_t size = script ? extension->script_size : extension->content_size;
    if (content == NULL || size == 0) {
        return error_response(req, "404 Not Found", "UI extension unavailable.");
    }
    response_headers(req);
    httpd_resp_set_type(req, script ? "application/javascript; charset=utf-8" :
                                      "text/html; charset=utf-8");
    return httpd_resp_send(req, (const char *)content, size);
}

esp_err_t web_interface_register_extension(const web_interface_extension_t *extension)
{
    if (extension == NULL || extension->id == NULL || extension->label == NULL ||
        extension->id[0] == '\0' || strlen(extension->id) > 16 ||
        extension->label[0] == '\0' || strlen(extension->label) > 48 ||
        extension->content == NULL || extension->content_size == 0 ||
        extension->script == NULL || extension->script_size == 0 ||
        (extension->route_count != 0 && extension->routes == NULL) ||
        (extension->placement != NULL && strcmp(extension->placement, "tab") != 0 &&
         strcmp(extension->placement, "settings") != 0) ||
        extension_count >= sizeof(extensions) / sizeof(extensions[0])) {
        ESP_LOGE(TAG, "Invalid or excessive UI extension registration");
        return ESP_ERR_INVALID_ARG;
    }
    for (const unsigned char *p = (const unsigned char *)extension->id; *p; ++p) {
        if (!( (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-')) {
            ESP_LOGE(TAG, "UI extension ID must be lowercase alphanumeric/hyphen");
            return ESP_ERR_INVALID_ARG;
        }
    }
    for (size_t i = 0; i < extension_count; ++i) {
        if (strcmp(extensions[i]->id, extension->id) == 0) {
            ESP_LOGE(TAG, "Duplicate UI extension: %s", extension->id);
            return ESP_ERR_INVALID_STATE;
        }
    }
    extensions[extension_count++] = extension;
    return ESP_OK;
}

esp_err_t web_interface_start(void)
{
    esp_err_t err = web_auth_init();
    if (err != ESP_OK) return err;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 12288;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 5;
    config.send_wait_timeout = 5;
    /* Built-in routes plus two asset routes and the API routes of each extension. */
    config.max_uri_handlers = 96;
    httpd_handle_t server = NULL;
    err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server failed: %s", esp_err_to_name(err));
        return err;
    }
    err = web_auth_register(server);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot register authentication routes: %s", esp_err_to_name(err));
        httpd_stop(server);
        return err;
    }
    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = index_handler},
        {.uri = "/app.js", .method = HTTP_GET, .handler = script_handler},
        {.uri = "/i18n.js", .method = HTTP_GET, .handler = language_handler},
        {.uri = "/bootstrap.min.css", .method = HTTP_GET, .handler = css_handler},
        {.uri = "/app.css", .method = HTTP_GET, .handler = theme_handler},
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
    for (size_t i = 0; i < extension_count; ++i) {
        snprintf(extension_asset_paths[i][0], sizeof(extension_asset_paths[i][0]),
                 "/ui/%s/content", extensions[i]->id);
        snprintf(extension_asset_paths[i][1], sizeof(extension_asset_paths[i][1]),
                 "/ui/%s/script", extensions[i]->id);
        extension_asset_routes[i][0] = (httpd_uri_t){
            .uri = extension_asset_paths[i][0], .method = HTTP_GET, .handler = extension_asset_handler,
            .user_ctx = (void *)extensions[i]};
        extension_asset_routes[i][1] = (httpd_uri_t){
            .uri = extension_asset_paths[i][1], .method = HTTP_GET, .handler = extension_asset_handler,
            .user_ctx = (void *)extensions[i]};
        for (size_t route = 0; route < 2; ++route) {
            err = httpd_register_uri_handler(server, &extension_asset_routes[i][route]);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Cannot register UI extension route: %s", esp_err_to_name(err));
                httpd_stop(server);
                return err;
            }
        }
        for (size_t route = 0; route < extensions[i]->route_count; ++route) {
            err = httpd_register_uri_handler(server, &extensions[i]->routes[route]);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Cannot register extension API %s: %s",
                         extensions[i]->routes[route].uri, esp_err_to_name(err));
                httpd_stop(server);
                return err;
            }
        }
    }
    ESP_LOGI(TAG, "Local web interface started; OTA remains idle");
    return ESP_OK;
}
