set(UI_SOURCES
    ui/ui.c
    ui/ui_Screen2.c
    ui/ui_comp.c
    ui/ui_comp_bubble.c
    ui/ui_comp_hook.c
    ui/ui_helpers.c
    ui/ui_font_FontLarge.c
    ui/ui_font_FontVeryLarge.c
    ui/ui_font_FontVeryVeryLarge.c)

idf_component_register(
    SRCS "example_qspi_with_ram.c" ${UI_SOURCES}
    INCLUDE_DIRS "." "ui"
    REQUIRES wifi_manager mqtt_manager touch_bsp read_lcd_id_bsp
    PRIV_REQUIRES driver)

set_source_files_properties(
    ${LV_DEMOS_SOURCES}
    PROPERTIES COMPILE_OPTIONS
    -DLV_LVGL_H_INCLUDE_SIMPLE)
