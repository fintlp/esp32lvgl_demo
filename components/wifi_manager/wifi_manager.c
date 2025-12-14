#include "wifi_manager.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#define WIFI_MANAGER_MAX_EVENT_HANDLERS 4
#define WIFI_MANAGER_NVS_NAMESPACE      "wifi_mgr"
#define WIFI_MANAGER_NVS_KEY_SSID       "ssid"
#define WIFI_MANAGER_NVS_KEY_PASS       "pass"

static const char *TAG = "wifi_mgr";

typedef struct {
    wifi_manager_event_cb_t cb;
    void *ctx;
} wifi_manager_handler_entry_t;

static struct {
    bool initialized;
    bool wifi_started;
    bool creds_valid;
    wifi_manager_credentials_t creds;
    nvs_handle_t nvs;
    wifi_manager_handler_entry_t handlers[WIFI_MANAGER_MAX_EVENT_HANDLERS];
} s_wifi_mgr;

static void wifi_manager_notify_handlers(wifi_manager_event_t event, const void *event_data)
{
    for (size_t i = 0; i < WIFI_MANAGER_MAX_EVENT_HANDLERS; ++i) {
        if (s_wifi_mgr.handlers[i].cb) {
            s_wifi_mgr.handlers[i].cb(event, s_wifi_mgr.handlers[i].ctx, event_data);
        }
    }
}

static void wifi_manager_load_credentials_from_nvs(void)
{
    size_t ssid_len = sizeof(s_wifi_mgr.creds.ssid);
    esp_err_t err = nvs_get_str(s_wifi_mgr.nvs, WIFI_MANAGER_NVS_KEY_SSID,
                                s_wifi_mgr.creds.ssid, &ssid_len);
    if (err == ESP_OK && ssid_len > 0) {
        size_t pass_len = sizeof(s_wifi_mgr.creds.password);
        err = nvs_get_str(s_wifi_mgr.nvs, WIFI_MANAGER_NVS_KEY_PASS,
                          s_wifi_mgr.creds.password, &pass_len);
        if (err != ESP_OK) {
            s_wifi_mgr.creds.password[0] = '\0';
        }
        s_wifi_mgr.creds_valid = true;
    }
}

static void wifi_manager_save_credentials_to_nvs(void)
{
    if (!s_wifi_mgr.nvs) {
        return;
    }
    nvs_set_str(s_wifi_mgr.nvs, WIFI_MANAGER_NVS_KEY_SSID, s_wifi_mgr.creds.ssid);
    nvs_set_str(s_wifi_mgr.nvs, WIFI_MANAGER_NVS_KEY_PASS, s_wifi_mgr.creds.password);
    nvs_commit(s_wifi_mgr.nvs);
}

static void wifi_manager_config_apply(void)
{
    if (!s_wifi_mgr.creds_valid) {
        return;
    }
    wifi_config_t cfg = { 0 };
    strlcpy((char *)cfg.sta.ssid, s_wifi_mgr.creds.ssid, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, s_wifi_mgr.creds.password, sizeof(cfg.sta.password));
    cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    cfg.sta.pmf_cfg.capable = true;
    cfg.sta.pmf_cfg.required = false;
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
}

static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            wifi_manager_notify_handlers(WIFI_MANAGER_EVENT_STARTED, NULL);
            if (s_wifi_mgr.creds_valid) {
                esp_wifi_connect();
            }
            break;
        case WIFI_EVENT_STA_CONNECTED:
            wifi_manager_notify_handlers(WIFI_MANAGER_EVENT_CONNECTED, NULL);
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            wifi_manager_notify_handlers(WIFI_MANAGER_EVENT_DISCONNECTED, NULL);
            if (s_wifi_mgr.creds_valid) {
                esp_wifi_connect();
            }
            break;
        default:
            break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;
        wifi_manager_ip_info_t info = {
            .ip = event->ip_info.ip,
        };
        wifi_manager_notify_handlers(WIFI_MANAGER_EVENT_GOT_IP, &info);
    }
}

static esp_err_t wifi_manager_ensure_system_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(esp_netif_init());
    err = esp_event_loop_create_default();
    if (err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(err);
    }
    esp_netif_create_default_wifi_sta();
    return ESP_OK;
}

esp_err_t wifi_manager_init(const wifi_manager_credentials_t *initial_creds)
{
    if (s_wifi_mgr.initialized) {
        return ESP_OK;
    }

    ESP_ERROR_CHECK(wifi_manager_ensure_system_init());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,
                                               ESP_EVENT_ANY_ID,
                                               &wifi_event_handler,
                                               NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,
                                               IP_EVENT_STA_GOT_IP,
                                               &wifi_event_handler,
                                               NULL));

    esp_err_t err = nvs_open(WIFI_MANAGER_NVS_NAMESPACE, NVS_READWRITE, &s_wifi_mgr.nvs);
    ESP_ERROR_CHECK(err);

    if (initial_creds) {
        s_wifi_mgr.creds = *initial_creds;
        s_wifi_mgr.creds_valid = (initial_creds->ssid[0] != '\0');
        if (s_wifi_mgr.creds_valid) {
            wifi_manager_save_credentials_to_nvs();
        }
    } else {
        wifi_manager_load_credentials_from_nvs();
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    wifi_manager_config_apply();
    ESP_ERROR_CHECK(esp_wifi_start());
    s_wifi_mgr.wifi_started = true;
    s_wifi_mgr.initialized = true;

    return ESP_OK;
}

esp_err_t wifi_manager_register_event_handler(wifi_manager_event_cb_t cb, void *ctx)
{
    if (!cb) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < WIFI_MANAGER_MAX_EVENT_HANDLERS; ++i) {
        if (s_wifi_mgr.handlers[i].cb == NULL) {
            s_wifi_mgr.handlers[i].cb = cb;
            s_wifi_mgr.handlers[i].ctx = ctx;
            return ESP_OK;
        }
    }
    return ESP_ERR_NO_MEM;
}

esp_err_t wifi_manager_set_credentials(const wifi_manager_credentials_t *creds, bool persist)
{
    if (!creds) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(&s_wifi_mgr.creds, 0, sizeof(s_wifi_mgr.creds));
    strlcpy(s_wifi_mgr.creds.ssid, creds->ssid, sizeof(s_wifi_mgr.creds.ssid));
    strlcpy(s_wifi_mgr.creds.password, creds->password, sizeof(s_wifi_mgr.creds.password));
    s_wifi_mgr.creds_valid = (s_wifi_mgr.creds.ssid[0] != '\0');
    if (persist && s_wifi_mgr.creds_valid) {
        wifi_manager_save_credentials_to_nvs();
    }
    if (s_wifi_mgr.wifi_started) {
        wifi_manager_config_apply();
    }
    return s_wifi_mgr.creds_valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t wifi_manager_connect(void)
{
    if (!s_wifi_mgr.creds_valid) {
        return ESP_ERR_INVALID_STATE;
    }
    wifi_manager_config_apply();
    return esp_wifi_connect();
}

const wifi_manager_credentials_t *wifi_manager_get_credentials(void)
{
    if (!s_wifi_mgr.creds_valid) {
        return NULL;
    }
    return &s_wifi_mgr.creds;
}

bool wifi_manager_has_credentials(void)
{
    return s_wifi_mgr.creds_valid;
}
