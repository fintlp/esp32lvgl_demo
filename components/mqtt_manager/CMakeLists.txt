idf_component_register(
    SRCS "mqtt_manager.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES mqtt esp_event wifi_manager)
