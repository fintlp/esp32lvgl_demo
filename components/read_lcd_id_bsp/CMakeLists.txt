idf_component_register(
  SRCS "read_lcd_id_bsp.c"
  PRIV_REQUIRES  driver esp_driver_gpio
  INCLUDE_DIRS "./")
