/* SPDX-FileCopyrightText: 2026 Thomas Stoll
 * SPDX-License-Identifier: Apache-2.0
 */

#include "web_auth.h"
#include "web_session.h"

#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "network_manager.h"
#include "nvs.h"
#include "sdkconfig.h"

extern const unsigned char login_start[] asm("_binary_login_html_start");
extern const unsigned char login_end[] asm("_binary_login_html_end");
extern const unsigned char login_script_start[] asm("_binary_login_js_start");
extern const unsigned char login_script_end[] asm("_binary_login_js_end");

static const char *TAG = "web_auth";
static web_session_t sessions[WEB_SESSION_COUNT];
static char csrf_token[33];
static uint8_t session_days = 1;
static int64_t last_failed_password = -1000000;

static void random_hex(char *destination, size_t bytes)
{
    uint8_t random[32];
    esp_fill_random(random, bytes);
    for (size_t i = 0; i < bytes; ++i) {
        snprintf(destination + 2 * i, 3, "%02x", random[i]);
    }
}

static esp_err_t respond(httpd_req_t *req, const char *status, const char *message)
{
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(req, message);
}

static web_session_t *request_session(httpd_req_t *req)
{
    char cookies[768];
    if (httpd_req_get_hdr_value_str(req, "Cookie", cookies, sizeof(cookies)) != ESP_OK) return NULL;
    return web_session_find(sessions, cookies, (uint64_t)(esp_timer_get_time() / 1000000));
}

static bool password_matches(const char *password)
{
    const int64_t now = esp_timer_get_time();
    if (now - last_failed_password < 1000000) return false;
    if (web_session_equal(password, network_manager_admin_password())) return true;
    last_failed_password = now;
    ESP_LOGW(TAG, "Admin password verification failed");
    return false;
}

bool web_interface_authorize_request(httpd_req_t *req, bool mutation)
{
    if (request_session(req) == NULL) {
        if (req->method == HTTP_GET && strcmp(req->uri, "/") == 0) {
            httpd_resp_set_hdr(req, "Location", "/login");
            respond(req, "303 See Other", "Login required.");
        } else {
            respond(req, "401 Unauthorized", "Login required.");
        }
        return false;
    }
    char token[sizeof(csrf_token)];
    if (mutation &&
        (httpd_req_get_hdr_value_str(req, "X-CSRF-Token", token, sizeof(token)) != ESP_OK ||
         !web_session_equal(token, csrf_token))) {
        respond(req, "403 Forbidden", "Invalid session token. Reload the page.");
        return false;
    }
    return true;
}

bool web_interface_authorize_settings_request(httpd_req_t *req)
{
    if (!web_interface_authorize_request(req, true)) return false;
    if (web_auth_settings_password_required()) {
        char password[64] = {0};
        const bool valid = httpd_req_get_hdr_value_str(req, "X-Admin-Password", password, sizeof(password)) == ESP_OK &&
                           password_matches(password);
        memset(password, 0, sizeof(password));
        if (!valid) {
            respond(req, "403 Forbidden", "Incorrect admin password. Settings were not saved.");
            return false;
        }
    }
    return true;
}

static cJSON *read_json(httpd_req_t *req)
{
    char body[256];
    if (req->content_len == 0 || req->content_len >= sizeof(body)) return NULL;
    size_t received = 0;
    while (received < req->content_len) {
        const int bytes = httpd_req_recv(req, body + received, req->content_len - received);
        if (bytes <= 0) return NULL;
        received += (size_t)bytes;
    }
    body[received] = '\0';
    cJSON *json = NULL;
    if (strlen(body) == received && strstr(body, "\\u0000") == NULL) {
        json = cJSON_ParseWithLengthOpts(body, received + 1, NULL, true);
    }
    memset(body, 0, sizeof(body));
    return json;
}

static esp_err_t login_handler(httpd_req_t *req)
{
    /* A custom header prevents cross-origin form submissions without opening CORS. */
    char marker[8];
    if (httpd_req_get_hdr_value_str(req, "X-Web-Login", marker, sizeof(marker)) != ESP_OK ||
        strcmp(marker, "1") != 0) {
        return respond(req, "403 Forbidden", "Invalid login request.");
    }
    cJSON *json = read_json(req);
    const cJSON *username = cJSON_GetObjectItemCaseSensitive(json, "username");
    cJSON *password = cJSON_GetObjectItemCaseSensitive(json, "password");
    const bool valid = cJSON_IsObject(json) && cJSON_IsString(username) && cJSON_IsString(password) &&
                       strcmp(username->valuestring, "admin") == 0 &&
                       strlen(password->valuestring) < 64 && password_matches(password->valuestring);
    if (cJSON_IsString(password)) memset(password->valuestring, 0, strlen(password->valuestring));
    cJSON_Delete(json);
    if (!valid) return respond(req, "403 Forbidden", "Incorrect username or password. Wait a moment and try again.");

    web_session_t *existing = request_session(req);
    if (existing != NULL) memset(existing, 0, sizeof(*existing));
    char token[WEB_SESSION_TOKEN_LENGTH + 1];
    random_hex(token, 32);
    web_session_t *session = web_session_create(sessions, token,
                                              (uint64_t)(esp_timer_get_time() / 1000000), session_days);
    if (session == NULL) {
        return respond(req, "429 Too Many Requests", "Too many active logins. Log out on another browser or try again later.");
    }
    char cookie[160];
    /* Lax preserves login on links from dashboards; mutations still require CSRF. */
    snprintf(cookie, sizeof(cookie), WEB_SESSION_COOKIE "=%s; Path=/; Max-Age=%u; HttpOnly; SameSite=Lax",
             session->token, (unsigned)session_days * 86400);
    httpd_resp_set_hdr(req, "Set-Cookie", cookie);
    return respond(req, "200 OK", "Logged in.");
}

static esp_err_t logout_handler(httpd_req_t *req)
{
    if (!web_interface_authorize_request(req, true)) return ESP_OK;
    web_session_t *session = request_session(req);
    if (session != NULL) memset(session, 0, sizeof(*session));
    httpd_resp_set_hdr(req, "Set-Cookie", WEB_SESSION_COOKIE "=; Path=/; Max-Age=0; HttpOnly; SameSite=Lax");
    return respond(req, "200 OK", "Logged out.");
}

static esp_err_t settings_handler(httpd_req_t *req)
{
    if (!web_interface_authorize_settings_request(req)) return ESP_OK;
    cJSON *json = read_json(req);
    const cJSON *days = cJSON_GetObjectItemCaseSensitive(json, "sessionDays");
    if (!cJSON_IsObject(json) || !cJSON_IsNumber(days) ||
        (days->valuedouble != 1 && days->valuedouble != 7)) {
        cJSON_Delete(json);
        return respond(req, "400 Bad Request", "Select a daily or weekly login.");
    }
    const uint8_t value = (uint8_t)days->valuedouble;
    cJSON_Delete(json);
    nvs_handle_t handle;
    esp_err_t err = nvs_open("web_auth", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, "session_days", value);
        if (err == ESP_OK) err = nvs_commit(handle);
        nvs_close(handle);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot save login duration: %s", esp_err_to_name(err));
        return respond(req, "500 Internal Server Error", "Could not save login duration.");
    }
    session_days = value;
    return respond(req, "200 OK", "Login duration saved. It applies to new logins.");
}

static esp_err_t asset_handler(httpd_req_t *req)
{
    const bool script = strcmp(req->uri, "/login.js") == 0;
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
    httpd_resp_set_hdr(req, "Content-Security-Policy",
                       "default-src 'self'; script-src 'self'; style-src 'self'; "
                       "frame-ancestors 'none'; base-uri 'none'; form-action 'self'");
    httpd_resp_set_type(req, script ? "application/javascript; charset=utf-8" : "text/html; charset=utf-8");
    return script ? httpd_resp_send(req, (const char *)login_script_start, login_script_end - login_script_start)
                  : httpd_resp_send(req, (const char *)login_start, login_end - login_start);
}

esp_err_t web_auth_init(void)
{
    session_days = 1;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("web_auth", NVS_READONLY, &handle);
    if (err == ESP_OK) {
        err = nvs_get_u8(handle, "session_days", &session_days);
        nvs_close(handle);
    }
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Cannot load login duration: %s", esp_err_to_name(err));
        return err;
    }
    if (session_days != 1 && session_days != 7) {
        ESP_LOGE(TAG, "Invalid stored login duration");
        return ESP_ERR_INVALID_STATE;
    }
    memset(sessions, 0, sizeof(sessions));
    last_failed_password = -1000000;
    random_hex(csrf_token, 16);
    return ESP_OK;
}

esp_err_t web_auth_register(httpd_handle_t server)
{
    const httpd_uri_t routes[] = {
        {.uri = "/login", .method = HTTP_GET, .handler = asset_handler},
        {.uri = "/login.js", .method = HTTP_GET, .handler = asset_handler},
        {.uri = "/api/auth/login", .method = HTTP_POST, .handler = login_handler},
        {.uri = "/api/auth/logout", .method = HTTP_POST, .handler = logout_handler},
        {.uri = "/api/auth/settings", .method = HTTP_POST, .handler = settings_handler},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        const esp_err_t err = httpd_register_uri_handler(server, &routes[i]);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

const char *web_auth_csrf_token(void) { return csrf_token; }
unsigned web_auth_session_days(void) { return session_days; }
bool web_auth_settings_password_required(void)
{
#ifdef CONFIG_OTA_SUITE_WEB_SETTINGS_REQUIRE_PASSWORD
    return true;
#else
    return false;
#endif
}
