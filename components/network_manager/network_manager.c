/* SPDX-FileCopyrightText: 2026 Thomas Stoll
 * SPDX-License-Identifier: Apache-2.0
 */

#include "network_manager.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "bootloader_random.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

typedef struct {
    char ssid[33];
    char password[65];
} credentials_t;

static const char *TAG = "network";
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static network_status_t status;
static QueueHandle_t config_queue;
static char admin_password[64];
static bool has_credentials;
static bool connecting;

static void set_message(const char *message)
{
    portENTER_CRITICAL(&lock);
    strlcpy(status.message, message, sizeof(status.message));
    portEXIT_CRITICAL(&lock);
}

static void connection_error(esp_err_t err)
{
    char message[128];
    snprintf(message, sizeof(message), "Wi-Fi error: %s. Access point remains accessible.",
             esp_err_to_name(err));
    set_message(message);
    ESP_LOGE(TAG, "%s", message);
}

static void connect_station(void)
{
    portENTER_CRITICAL(&lock);
    bool should_connect = !status.connected && !connecting;
    if (should_connect) {
        connecting = true;
    }
    portEXIT_CRITICAL(&lock);
    if (should_connect) {
        esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK) {
            portENTER_CRITICAL(&lock);
            connecting = false;
            portEXIT_CRITICAL(&lock);
            connection_error(err);
        }
    }
}

static void time_sync(struct timeval *tv)
{
    portENTER_CRITICAL(&lock);
    status.time_ready = tv->tv_sec >= 1704067200;
    portEXIT_CRITICAL(&lock);
    ESP_LOGI(TAG, "SNTP time synchronized");
}

static void network_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (has_credentials) {
            connect_station();
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        portENTER_CRITICAL(&lock);
        status.connected = false;
        connecting = false;
        status.ip[0] = '\0';
        portEXIT_CRITICAL(&lock);
        set_message("Wi-Fi disconnected. Reconnecting; access point remains accessible.");
        ESP_LOGW(TAG, "Station disconnected");
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = data;
        char ip[16];
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&event->ip_info.ip));
        portENTER_CRITICAL(&lock);
        status.connected = true;
        connecting = false;
        strlcpy(status.ip, ip, sizeof(status.ip));
        portEXIT_CRITICAL(&lock);
        set_message("Wi-Fi connected.");
        ESP_LOGI(TAG, "Web interface: http://%s", ip);
        esp_err_t err = esp_netif_sntp_start();
        if (err != ESP_OK) {
            connection_error(err);
        }
    }
}

static esp_err_t save_credentials(const credentials_t *credentials)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("network", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    /* One blob keeps SSID and password atomic across interrupted writes. */
    err = nvs_set_blob(handle, "station", credentials, sizeof(*credentials));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

static void network_worker(void *arg)
{
    (void)arg;
    credentials_t credentials;
    for (;;) {
        if (xQueueReceive(config_queue, &credentials, pdMS_TO_TICKS(5000)) == pdTRUE) {
            esp_err_t err = save_credentials(&credentials);
            if (err == ESP_OK) {
                wifi_config_t wifi = {0};
                memcpy(wifi.sta.ssid, credentials.ssid, strlen(credentials.ssid));
                memcpy(wifi.sta.password, credentials.password, strlen(credentials.password));
                wifi.sta.pmf_cfg.capable = true;
                err = esp_wifi_disconnect();
                if (err == ESP_ERR_WIFI_NOT_CONNECT) {
                    err = ESP_OK;
                }
                if (err == ESP_OK) {
                    err = esp_wifi_set_config(WIFI_IF_STA, &wifi);
                }
                if (err == ESP_OK) {
                    has_credentials = true;
                    portENTER_CRITICAL(&lock);
                    strlcpy(status.ssid, credentials.ssid, sizeof(status.ssid));
                    status.connected = false;
                    connecting = false;
                    status.ip[0] = '\0';
                    portEXIT_CRITICAL(&lock);
                    set_message("Wi-Fi saved. Connecting.");
                    connect_station();
                }
            }
            memset(&credentials, 0, sizeof(credentials));
            if (err != ESP_OK) {
                connection_error(err);
            }
            portENTER_CRITICAL(&lock);
            status.configuring = false;
            portEXIT_CRITICAL(&lock);
        } else {
            network_status_t current;
            network_manager_get_status(&current);
            if (has_credentials && !current.connected) {
                connect_station();
            }
        }
    }
}

static esp_err_t load_settings(credentials_t *credentials)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("network", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    size_t size = sizeof(*credentials);
    err = nvs_get_blob(handle, "station", credentials, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    } else if (err == ESP_OK) {
        if (size != sizeof(*credentials) || credentials->ssid[32] != '\0' ||
            credentials->password[64] != '\0') {
            err = ESP_ERR_INVALID_STATE;
        } else {
            has_credentials = credentials->ssid[0] != '\0';
        }
    }
    if (err == ESP_OK && CONFIG_OTA_SUITE_AP_PASSWORD[0] != '\0') {
        strlcpy(admin_password, CONFIG_OTA_SUITE_AP_PASSWORD, sizeof(admin_password));
        if (strlen(CONFIG_OTA_SUITE_AP_PASSWORD) < 8 ||
            strlen(CONFIG_OTA_SUITE_AP_PASSWORD) >= sizeof(admin_password)) {
            err = ESP_ERR_INVALID_ARG;
        }
    } else if (err == ESP_OK) {
        size = sizeof(admin_password);
        err = nvs_get_str(handle, "ap_password", admin_password, &size);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            /* RF is not started yet; temporarily enable a hardware entropy source. */
            bootloader_random_enable();
            snprintf(admin_password, sizeof(admin_password), "%08lx%08lx",
                     (unsigned long)esp_random(), (unsigned long)esp_random());
            bootloader_random_disable();
            err = nvs_set_str(handle, "ap_password", admin_password);
            if (err == ESP_OK) {
                err = nvs_commit(handle);
            }
        }
        if (err == ESP_OK && strlen(admin_password) < 8) {
            err = ESP_ERR_INVALID_STATE;
        }
    }
    nvs_close(handle);
    return err;
}

esp_err_t network_manager_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS initialization failed: %s; refusing to erase stored settings",
                 esp_err_to_name(err));
        return err;
    }
    credentials_t credentials = {0};
    err = load_settings(&credentials);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot load network settings: %s", esp_err_to_name(err));
        return err;
    }
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    if (!esp_netif_create_default_wifi_ap() || !esp_netif_create_default_wifi_sta()) {
        return ESP_ERR_NO_MEM;
    }
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, network_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, network_event, NULL));

    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP));
    snprintf(status.ap_ssid, sizeof(status.ap_ssid), "%s-%02X%02X%02X",
             CONFIG_OTA_SUITE_AP_SSID_PREFIX,
             mac[3], mac[4], mac[5]);
    strlcpy(status.ssid, credentials.ssid, sizeof(status.ssid));
    wifi_config_t ap = {0};
    strlcpy((char *)ap.ap.ssid, status.ap_ssid, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen(status.ap_ssid);
    strlcpy((char *)ap.ap.password, admin_password, sizeof(ap.ap.password));
    ap.ap.channel = 1;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap.ap.pmf_cfg.capable = true;
    wifi_config_t station = {0};
    memcpy(station.sta.ssid, credentials.ssid, strlen(credentials.ssid));
    memcpy(station.sta.password, credentials.password, strlen(credentials.password));
    station.sta.pmf_cfg.capable = true;
    memset(&credentials, 0, sizeof(credentials));

    config_queue = xQueueCreate(1, sizeof(credentials_t));
    if (config_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &station));
    esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_OTA_SUITE_SNTP_SERVER);
    sntp.start = false;
    sntp.sync_cb = time_sync;
    ESP_ERROR_CHECK(esp_netif_sntp_init(&sntp));
    ESP_ERROR_CHECK(esp_wifi_start());
    if (xTaskCreate(network_worker, "network", 4096, NULL, 4, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Setup AP: %s / password: %s", status.ap_ssid, admin_password);
    ESP_LOGI(TAG, "Open http://192.168.4.1; web login: admin / AP password");
    set_message(has_credentials ? "Connecting to saved Wi-Fi." :
                "No Wi-Fi configured yet. Please save credentials.");
    return ESP_OK;
}

esp_err_t network_manager_configure(const char *ssid, const char *password)
{
    if (ssid == NULL || password == NULL || strlen(ssid) == 0 || strlen(ssid) > 32 ||
        strlen(password) > 64 || (strlen(password) > 0 && strlen(password) < 8)) {
        ESP_LOGW(TAG, "Invalid station credentials");
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(password) == 64) {
        for (size_t i = 0; i < 64; ++i) {
            if (!((password[i] >= '0' && password[i] <= '9') ||
                  (password[i] >= 'a' && password[i] <= 'f') ||
                  (password[i] >= 'A' && password[i] <= 'F'))) {
                ESP_LOGW(TAG, "64-character WPA key must be hexadecimal");
                return ESP_ERR_INVALID_ARG;
            }
        }
    }
    portENTER_CRITICAL(&lock);
    bool busy = status.configuring;
    if (!busy) {
        status.configuring = true;
    }
    portEXIT_CRITICAL(&lock);
    if (busy) {
        return ESP_ERR_INVALID_STATE;
    }
    credentials_t credentials = {0};
    strlcpy(credentials.ssid, ssid, sizeof(credentials.ssid));
    strlcpy(credentials.password, password, sizeof(credentials.password));
    BaseType_t sent = xQueueSend(config_queue, &credentials, 0);
    memset(&credentials, 0, sizeof(credentials));
    if (sent != pdTRUE) {
        portENTER_CRITICAL(&lock);
        status.configuring = false;
        portEXIT_CRITICAL(&lock);
        ESP_LOGE(TAG, "Network configuration queue is full");
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

void network_manager_get_status(network_status_t *out)
{
    portENTER_CRITICAL(&lock);
    *out = status;
    portEXIT_CRITICAL(&lock);
    out->time_ready = out->time_ready && time(NULL) >= 1704067200;
}

const char *network_manager_admin_password(void)
{
    return admin_password;
}
