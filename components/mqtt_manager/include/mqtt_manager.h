#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "wifi_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MQTT_MANAGER_MAX_TOPIC_LEN   128
#define MQTT_MANAGER_MAX_PAYLOAD_LEN 256

typedef void (*mqtt_manager_message_cb_t)(const char *topic,
                                          const char *payload,
                                          void *ctx);

typedef void (*mqtt_manager_status_cb_t)(bool connected, void *ctx);

typedef struct {
    char broker_uri[MQTT_MANAGER_MAX_TOPIC_LEN];
    char temperature_sub_topic[MQTT_MANAGER_MAX_TOPIC_LEN];
    char button_pub_topic_prefix[MQTT_MANAGER_MAX_TOPIC_LEN];
} mqtt_manager_config_t;

esp_err_t mqtt_manager_init(const mqtt_manager_config_t *config,
                            mqtt_manager_message_cb_t message_cb,
                            void *message_ctx,
                            mqtt_manager_status_cb_t status_cb,
                            void *status_ctx);

esp_err_t mqtt_manager_publish_button_event(const char *button_id, const char *state);

#ifdef __cplusplus
}
#endif
