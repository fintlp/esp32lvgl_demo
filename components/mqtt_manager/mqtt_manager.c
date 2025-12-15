#include "mqtt_manager.h"

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "mqtt_client.h"

static const char *TAG = "mqtt_mgr";
static const char HIVEMQ_CLOUD_ROOT_CA[] =
"-----BEGIN CERTIFICATE-----\n"
"MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw\n"
"TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh\n"
"cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4\n"
"WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu\n"
"ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY\n"
"MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc\n"
"h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+\n"
"0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U\n"
"A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW\n"
"T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH\n"
"B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC\n"
"B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv\n"
"KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn\n"
"OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn\n"
"jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw\n"
"qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI\n"
"rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV\n"
"HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq\n"
"hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL\n"
"ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ\n"
"3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK\n"
"NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5\n"
"ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur\n"
"TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC\n"
"jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc\n"
"oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq\n"
"4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA\n"
"mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d\n"
"emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=\n"
"-----END CERTIFICATE-----\n";

typedef struct {
    bool initialized;
    bool mqtt_connected;
    esp_mqtt_client_handle_t client;
    mqtt_manager_config_t config;
    mqtt_manager_message_cb_t message_cb;
    void *message_ctx;
    mqtt_manager_status_cb_t status_cb;
    void *status_ctx;
} mqtt_manager_ctx_t;

static mqtt_manager_ctx_t s_mqtt;

static void mqtt_manager_handle_event(esp_mqtt_event_handle_t event)
{
    switch (event->event_id) {
    case MQTT_EVENT_CONNECTED:
        s_mqtt.mqtt_connected = true;
        ESP_LOGI(TAG, "Connected to broker");
        esp_mqtt_client_subscribe(event->client,
                                  s_mqtt.config.temperature_sub_topic,
                                  1);
        if (s_mqtt.status_cb) {
            s_mqtt.status_cb(true, s_mqtt.status_ctx);
        }
        break;
    case MQTT_EVENT_DISCONNECTED:
        s_mqtt.mqtt_connected = false;
        ESP_LOGW(TAG, "Disconnected from broker");
        if (s_mqtt.status_cb) {
            s_mqtt.status_cb(false, s_mqtt.status_ctx);
        }
        break;
    case MQTT_EVENT_DATA: {
        char topic[MQTT_MANAGER_MAX_TOPIC_LEN] = { 0 };
        size_t topic_len = (size_t)event->topic_len < sizeof(topic) - 1 ?
                           (size_t)event->topic_len : sizeof(topic) - 1;
        memcpy(topic, event->topic, topic_len);

        char payload[MQTT_MANAGER_MAX_PAYLOAD_LEN] = { 0 };
        size_t payload_len = (size_t)event->data_len < sizeof(payload) - 1 ?
                             (size_t)event->data_len : sizeof(payload) - 1;
        memcpy(payload, event->data, payload_len);

        if (strcmp(topic, s_mqtt.config.temperature_sub_topic) == 0 &&
            s_mqtt.message_cb) {
            s_mqtt.message_cb(topic, payload, s_mqtt.message_ctx);
        }
        break;
    }
    default:
        break;
    }
}

static void mqtt_event_handler(void *handler_args,
                               esp_event_base_t base,
                               int32_t event_id,
                               void *event_data)
{
    (void)handler_args;
    (void)base;
    mqtt_manager_handle_event((esp_mqtt_event_handle_t)event_data);
}

static void wifi_event_forwarder(wifi_manager_event_t event, void *ctx, const void *event_data)
{
    (void)ctx;
    (void)event_data;
    if (!s_mqtt.client) {
        return;
    }
    switch (event) {
    case WIFI_MANAGER_EVENT_GOT_IP:
        esp_mqtt_client_start(s_mqtt.client);
        break;
    case WIFI_MANAGER_EVENT_DISCONNECTED:
        esp_mqtt_client_stop(s_mqtt.client);
        break;
    default:
        break;
    }
}

static void mqtt_manager_copy_string(char *dest, size_t dest_size, const char *src)
{
    if (!dest || dest_size == 0 || !src) {
        return;
    }
    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

static void mqtt_manager_apply_default_config(mqtt_manager_config_t *cfg)
{
    if (!cfg->broker_uri[0]) {
        mqtt_manager_copy_string(cfg->broker_uri,
                                 sizeof(cfg->broker_uri),
                                 "mqtts://af241302675248c586ce5c30d689ded4.s1.eu.hivemq.cloud:8883");
    }
    if (!cfg->temperature_sub_topic[0]) {
        mqtt_manager_copy_string(cfg->temperature_sub_topic,
                                 sizeof(cfg->temperature_sub_topic),
                                 "esp32lvgl/temperature");
    }
    if (!cfg->button_pub_topic_prefix[0]) {
        mqtt_manager_copy_string(cfg->button_pub_topic_prefix,
                                 sizeof(cfg->button_pub_topic_prefix),
                                 "esp32lvgl/buttons");
    }
}

esp_err_t mqtt_manager_init(const mqtt_manager_config_t *config,
                            mqtt_manager_message_cb_t message_cb,
                            void *message_ctx,
                            mqtt_manager_status_cb_t status_cb,
                            void *status_ctx)
{
    if (s_mqtt.initialized) {
        return ESP_OK;
    }

    if (config) {
        s_mqtt.config = *config;
    } else {
        memset(&s_mqtt.config, 0, sizeof(s_mqtt.config));
    }
    mqtt_manager_apply_default_config(&s_mqtt.config);

    esp_mqtt_client_config_t client_cfg = {
        .broker.address.uri = s_mqtt.config.broker_uri,
        .broker.verification.certificate = HIVEMQ_CLOUD_ROOT_CA,
        .credentials.username = "audihmi",
        .credentials.authentication.password = "Audi1!!!",
    };
    s_mqtt.client = esp_mqtt_client_init(&client_cfg);
    if (!s_mqtt.client) {
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(s_mqtt.client,
                                   ESP_EVENT_ANY_ID,
                                   mqtt_event_handler,
                                   NULL);

    esp_err_t err = wifi_manager_register_event_handler(wifi_event_forwarder, NULL);
    if (err != ESP_OK) {
        return err;
    }

    s_mqtt.message_cb = message_cb;
    s_mqtt.message_ctx = message_ctx;
    s_mqtt.status_cb = status_cb;
    s_mqtt.status_ctx = status_ctx;
    s_mqtt.initialized = true;

    return ESP_OK;
}

esp_err_t mqtt_manager_publish_button_event(const char *button_id, const char *state)
{
    if (!s_mqtt.client || !s_mqtt.mqtt_connected || !button_id || !state) {
        return ESP_ERR_INVALID_STATE;
    }
    char topic[MQTT_MANAGER_MAX_TOPIC_LEN] = { 0 };
    snprintf(topic, sizeof(topic), "%s/%s", s_mqtt.config.button_pub_topic_prefix, button_id);
    return (esp_mqtt_client_publish(s_mqtt.client, topic, state, 0, 1, 0) >= 0) ?
               ESP_OK :
               ESP_FAIL;
}
