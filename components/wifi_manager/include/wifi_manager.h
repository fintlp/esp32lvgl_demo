#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "esp_netif_ip_addr.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_MANAGER_MAX_SSID_LEN     32
#define WIFI_MANAGER_MAX_PASSWORD_LEN 64

typedef struct {
    char ssid[WIFI_MANAGER_MAX_SSID_LEN + 1];
    char password[WIFI_MANAGER_MAX_PASSWORD_LEN + 1];
} wifi_manager_credentials_t;

typedef enum {
    WIFI_MANAGER_EVENT_STARTED = 0,
    WIFI_MANAGER_EVENT_CONNECTED,
    WIFI_MANAGER_EVENT_DISCONNECTED,
    WIFI_MANAGER_EVENT_GOT_IP,
} wifi_manager_event_t;

typedef struct {
    esp_ip4_addr_t ip;
} wifi_manager_ip_info_t;

typedef void (*wifi_manager_event_cb_t)(wifi_manager_event_t event,
                                        void *ctx,
                                        const void *event_data);

/**
 * @brief Initialize Wi-Fi manager and (optionally) load stored credentials.
 *
 * @param initial_creds Optional credentials to use when nothing stored in NVS.
 */
esp_err_t wifi_manager_init(const wifi_manager_credentials_t *initial_creds);

/**
 * @brief Register for Wi-Fi manager state notifications.
 */
esp_err_t wifi_manager_register_event_handler(wifi_manager_event_cb_t cb, void *ctx);

/**
 * @brief Update credentials (optionally persisting them) and reconnect.
 */
esp_err_t wifi_manager_set_credentials(const wifi_manager_credentials_t *creds, bool persist);

/**
 * @brief Trigger a (re)connection attempt with current credentials.
 */
esp_err_t wifi_manager_connect(void);

/**
 * @brief Return pointer to current credentials (NULL if none set).
 */
const wifi_manager_credentials_t *wifi_manager_get_credentials(void);

/**
 * @brief Whether Wi-Fi manager has valid credentials loaded.
 */
bool wifi_manager_has_credentials(void);

#ifdef __cplusplus
}
#endif
