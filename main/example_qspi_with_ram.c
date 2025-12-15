
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_netif_ip_addr.h"
#include "esp_netif.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_err.h"
#include "esp_log.h"

#include "lvgl.h"
#include "lv_demos.h"
#include "esp_lcd_sh8601.h"
#include "touch_bsp.h"
#include "read_lcd_id_bsp.h"
#include "wifi_manager.h"
#include "mqtt_manager.h"
#include "ui/ui.h"
static const char *TAG = "example";
static SemaphoreHandle_t lvgl_mux = NULL;
static lv_obj_t * scr1 = NULL;
static lv_obj_t * scr2 = NULL;
static lv_obj_t * scr_settings = NULL;
static lv_obj_t *temperature_value_label = NULL;
static lv_obj_t *wifi_status_value_label = NULL;
static lv_obj_t *mqtt_status_value_label = NULL;
static lv_obj_t *ssid_input = NULL;
static lv_obj_t *password_input = NULL;
static lv_obj_t *settings_keyboard = NULL;
static QueueHandle_t ui_event_queue = NULL;
static lv_obj_t * scr_squareline = NULL;
static lv_obj_t *registered_screens[4] = {0};
static size_t registered_screen_count = 0;
static bool is_playing = false;
static lv_obj_t *current_screen = NULL;

#define MQTT_BUTTON_MEDIA_PLAY       "media/play"
#define MQTT_BUTTON_MEDIA_VOLUME_UP  "media/volume_up"
#define MQTT_BUTTON_MEDIA_VOLUME_DOWN "media/volume_down"
#define MQTT_BUTTON_MEDIA_PREV       "media/previous"
#define MQTT_BUTTON_MEDIA_NEXT       "media/next"
#define MQTT_BUTTON_HVAC_TEMP_UP     "hvac/temp_up"
#define MQTT_BUTTON_HVAC_TEMP_DOWN   "hvac/temp_down"
#define MQTT_BUTTON_HVAC_POWER       "hvac/power"
#define MQTT_BUTTON_HVAC_INTENSITY   "hvac/intensity"
#define MQTT_BUTTON_HVAC_TEMP_SET    "hvac/temp_set"

typedef enum {
    UI_EVENT_TEMPERATURE,
    UI_EVENT_WIFI_STATUS,
    UI_EVENT_MQTT_STATUS,
} ui_event_type_t;

typedef struct {
    ui_event_type_t type;
    float temperature;
    char message[64];
} ui_event_t;

static void gesture_event_cb(lv_event_t * e);

static void register_screen(lv_obj_t *screen)
{
    if (registered_screen_count < (sizeof(registered_screens) / sizeof(registered_screens[0]))) {
        registered_screens[registered_screen_count++] = screen;
        lv_obj_add_event_cb(screen, gesture_event_cb, LV_EVENT_GESTURE, NULL);
    }
}

static size_t get_screen_index(lv_obj_t *screen)
{
    for (size_t i = 0; i < registered_screen_count; ++i) {
        if (registered_screens[i] == screen) {
            return i;
        }
    }
    return 0;
}

static void enqueue_ui_event(ui_event_type_t type, const char *message, float temperature)
{
    if (!ui_event_queue) {
        return;
    }
    ui_event_t evt = {
        .type = type,
        .temperature = temperature,
    };
    if (message) {
        strlcpy(evt.message, message, sizeof(evt.message));
    } else {
        evt.message[0] = '\0';
    }
    xQueueSend(ui_event_queue, &evt, 0);
}

static void handle_ui_event(const ui_event_t *event)
{
    if (!event) {
        return;
    }
    char buffer[64];
    switch (event->type) {
    case UI_EVENT_TEMPERATURE:
        if (temperature_value_label) {
            snprintf(buffer, sizeof(buffer), "%.1f°C", event->temperature);
            lv_label_set_text(temperature_value_label, buffer);
        }
        break;
    case UI_EVENT_WIFI_STATUS:
        if (wifi_status_value_label) {
            lv_label_set_text(wifi_status_value_label, event->message);
        }
        break;
    case UI_EVENT_MQTT_STATUS:
        if (mqtt_status_value_label) {
            lv_label_set_text(mqtt_status_value_label, event->message);
        }
        break;
    default:
        break;
    }
}

#define LCD_HOST    SPI2_HOST

#define EXAMPLE_Rotate_90
#define SH8601_ID 0x86
#define CO5300_ID 0xff
static uint8_t READ_LCD_ID = 0x00; 


#if CONFIG_LV_COLOR_DEPTH == 32
#define LCD_BIT_PER_PIXEL       (24)
#elif CONFIG_LV_COLOR_DEPTH == 16
#define LCD_BIT_PER_PIXEL       (16)
#endif

#define EXAMPLE_LCD_BK_LIGHT_ON_LEVEL  1
#define EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL !EXAMPLE_LCD_BK_LIGHT_ON_LEVEL
#define EXAMPLE_PIN_NUM_LCD_CS            (GPIO_NUM_9)
#define EXAMPLE_PIN_NUM_LCD_PCLK          (GPIO_NUM_10) 
#define EXAMPLE_PIN_NUM_LCD_DATA0         (GPIO_NUM_11)
#define EXAMPLE_PIN_NUM_LCD_DATA1         (GPIO_NUM_12)
#define EXAMPLE_PIN_NUM_LCD_DATA2         (GPIO_NUM_13)
#define EXAMPLE_PIN_NUM_LCD_DATA3         (GPIO_NUM_14)
#define EXAMPLE_PIN_NUM_LCD_RST           (GPIO_NUM_21)
#define EXAMPLE_PIN_NUM_BK_LIGHT          (-1)

// The pixel number in horizontal and vertical
#define EXAMPLE_LCD_H_RES              466
#define EXAMPLE_LCD_V_RES              466


#define EXAMPLE_LVGL_BUF_HEIGHT        (EXAMPLE_LCD_V_RES / 4)
#define EXAMPLE_LVGL_TICK_PERIOD_MS    2
#define EXAMPLE_LVGL_TASK_MAX_DELAY_MS 500
#define EXAMPLE_LVGL_TASK_MIN_DELAY_MS 1
#define EXAMPLE_LVGL_TASK_STACK_SIZE   (4 * 1024)
#define EXAMPLE_LVGL_TASK_PRIORITY     2

static const sh8601_lcd_init_cmd_t sh8601_lcd_init_cmds[] = 
{
    {0x11, (uint8_t []){0x00}, 0, 120},
    {0x44, (uint8_t []){0x01, 0xD1}, 2, 0},
    {0x35, (uint8_t []){0x00}, 1, 0},
    {0x53, (uint8_t []){0x20}, 1, 10},
    {0x51, (uint8_t []){0x00}, 1, 10},
    {0x29, (uint8_t []){0x00}, 0, 10},
    {0x51, (uint8_t []){0xFF}, 1, 0},
    //{0x36, (uint8_t []){0x80}, 1, 0},
};
static const sh8601_lcd_init_cmd_t co5300_lcd_init_cmds[] = 
{
    {0x11, (uint8_t []){0x00}, 0, 80},   
    {0xC4, (uint8_t []){0x80}, 1, 0},
    //{0x44, (uint8_t []){0x01, 0xD1}, 2, 0},
    //{0x35, (uint8_t []){0x00}, 1, 0},//TE ON
    {0x53, (uint8_t []){0x20}, 1, 1},
    {0x63, (uint8_t []){0xFF}, 1, 1},
    {0x51, (uint8_t []){0x00}, 1, 1},
    {0x29, (uint8_t []){0x00}, 0, 10},
    {0x51, (uint8_t []){0xFF}, 1, 0},
    //{0x36, (uint8_t []){0x60}, 1, 0},
};

static bool example_notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_disp_drv_t *disp_driver = (lv_disp_drv_t *)user_ctx;
    lv_disp_flush_ready(disp_driver);
    return false;
}

static void example_lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t) drv->user_data;
    const int offsetx1 = (READ_LCD_ID == SH8601_ID) ? area->x1 : area->x1 + 0x06;
    const int offsetx2 = (READ_LCD_ID == SH8601_ID) ? area->x2 : area->x2 + 0x06;
    const int offsety1 = area->y1;
    const int offsety2 = area->y2;
#if LCD_BIT_PER_PIXEL == 24
    uint8_t *to = (uint8_t *)color_map;
    uint8_t temp = 0;
    uint16_t pixel_num = (offsetx2 - offsetx1 + 1) * (offsety2 - offsety1 + 1);

    // Special dealing for first pixel
    temp = color_map[0].ch.blue;
    *to++ = color_map[0].ch.red;
    *to++ = color_map[0].ch.green;
    *to++ = temp;
    // Normal dealing for other pixels
    for (int i = 1; i < pixel_num; i++) {
        *to++ = color_map[i].ch.red;
        *to++ = color_map[i].ch.green;
        *to++ = color_map[i].ch.blue;
    }
#endif

    // copy a buffer's content to a specific area of the display
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);
}

void example_lvgl_rounder_cb(struct _lv_disp_drv_t *disp_drv, lv_area_t *area)
{
    uint16_t x1 = area->x1;
    uint16_t x2 = area->x2;

    uint16_t y1 = area->y1;
    uint16_t y2 = area->y2;

    // round the start of coordinate down to the nearest 2M number
    area->x1 = (x1 >> 1) << 1;
    area->y1 = (y1 >> 1) << 1;
    // round the end of coordinate up to the nearest 2N+1 number
    area->x2 = ((x2 >> 1) << 1) + 1;
    area->y2 = ((y2 >> 1) << 1) + 1;
}

static void example_lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    uint16_t tp_x;
    uint16_t tp_y;
    uint8_t win = getTouch(&tp_x,&tp_y);
    if (win)
    {
        data->point.x = tp_x;
        data->point.y = tp_y;
        data->state = LV_INDEV_STATE_PRESSED;
    }
    else 
    {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void example_increase_lvgl_tick(void *arg)
{
    /* Tell LVGL how many milliseconds has elapsed */
    lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}

static bool example_lvgl_lock(int timeout_ms)
{
    assert(lvgl_mux && "bsp_display_start must be called first");

    const TickType_t timeout_ticks = (timeout_ms == -1) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(lvgl_mux, timeout_ticks) == pdTRUE;
}

static void example_lvgl_unlock(void)
{
    assert(lvgl_mux && "bsp_display_start must be called first");
    xSemaphoreGive(lvgl_mux);
}

static void example_lvgl_port_task(void *arg)
{
    ESP_LOGI(TAG, "Starting LVGL task");
    uint32_t task_delay_ms = EXAMPLE_LVGL_TASK_MAX_DELAY_MS;
    while (1) {
        // Lock the mutex due to the LVGL APIs are not thread-safe
        if (example_lvgl_lock(-1)) {
            task_delay_ms = lv_timer_handler();
            if (ui_event_queue) {
                ui_event_t evt;
                while (xQueueReceive(ui_event_queue, &evt, 0) == pdTRUE) {
                    handle_ui_event(&evt);
                }
            }
            // Release the mutex
            example_lvgl_unlock();
        }
        if (task_delay_ms > EXAMPLE_LVGL_TASK_MAX_DELAY_MS) {
            task_delay_ms = EXAMPLE_LVGL_TASK_MAX_DELAY_MS;
        } else if (task_delay_ms < EXAMPLE_LVGL_TASK_MIN_DELAY_MS) {
            task_delay_ms = EXAMPLE_LVGL_TASK_MIN_DELAY_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
    }
}

static void gesture_event_cb(lv_event_t * e) {
    lv_obj_t * scr = lv_event_get_target(e);
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    size_t index = get_screen_index(scr);
    if (dir == LV_DIR_LEFT && index + 1 < registered_screen_count) {
        current_screen = registered_screens[index + 1];
        lv_scr_load(current_screen);
    } else if (dir == LV_DIR_RIGHT && index > 0) {
        current_screen = registered_screens[index - 1];
        lv_scr_load(current_screen);
    }
}

static void play_pause_event_cb(lv_event_t * e) {
    lv_obj_t * btn = lv_event_get_target(e);
    lv_obj_t * label = lv_obj_get_child(btn, 0);
    if (is_playing) {
        lv_label_set_text(label, "Stop");
        is_playing = false;
        mqtt_manager_publish_button_event(MQTT_BUTTON_MEDIA_PLAY, "stop");
    } else {
        lv_label_set_text(label, "Play");
        is_playing = true;
        mqtt_manager_publish_button_event(MQTT_BUTTON_MEDIA_PLAY, "play");
    }
}

static void action_button_event_cb(lv_event_t * e)
{
    const char *button_id = (const char *)lv_event_get_user_data(e);
    if (button_id) {
        mqtt_manager_publish_button_event(button_id, "pressed");
    }
}

static void connect_button_event_cb(lv_event_t * e)
{
    (void)e;
    if (!ssid_input || !password_input) {
        return;
    }
    const char *ssid = lv_textarea_get_text(ssid_input);
    const char *password = lv_textarea_get_text(password_input);
    wifi_manager_credentials_t creds = { 0 };
    if (ssid) {
        strncpy(creds.ssid, ssid, WIFI_MANAGER_MAX_SSID_LEN);
        creds.ssid[WIFI_MANAGER_MAX_SSID_LEN] = '\0';
    }
    if (password) {
        strncpy(creds.password, password, WIFI_MANAGER_MAX_PASSWORD_LEN);
        creds.password[WIFI_MANAGER_MAX_PASSWORD_LEN] = '\0';
    }
    if (creds.ssid[0] == '\0') {
        enqueue_ui_event(UI_EVENT_WIFI_STATUS, "SSID is required", 0);
        return;
    }
    if (wifi_manager_set_credentials(&creds, true) == ESP_OK) {
        enqueue_ui_event(UI_EVENT_WIFI_STATUS, "Connecting...", 0);
        wifi_manager_connect();
    } else {
        enqueue_ui_event(UI_EVENT_WIFI_STATUS, "Invalid credentials", 0);
    }
}

static void text_area_event_cb(lv_event_t * e)
{
    if (!settings_keyboard) {
        return;
    }
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(settings_keyboard, lv_event_get_target(e));
        lv_obj_clear_flag(settings_keyboard, LV_OBJ_FLAG_HIDDEN);
    } else if (code == LV_EVENT_DEFOCUSED) {
        lv_obj_add_flag(settings_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

static void keyboard_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(settings_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

static void app_wifi_event_handler(wifi_manager_event_t event, void *ctx, const void *event_data)
{
    (void)ctx;
    char message[64] = { 0 };
    switch (event) {
    case WIFI_MANAGER_EVENT_STARTED:
        snprintf(message, sizeof(message), "Wi-Fi started");
        break;
    case WIFI_MANAGER_EVENT_CONNECTED:
        snprintf(message, sizeof(message), "Wi-Fi connected");
        break;
    case WIFI_MANAGER_EVENT_DISCONNECTED:
        snprintf(message, sizeof(message), "Wi-Fi disconnected");
        break;
    case WIFI_MANAGER_EVENT_GOT_IP: {
        const wifi_manager_ip_info_t *info = (const wifi_manager_ip_info_t *)event_data;
        char ip_str[32] = { 0 };
        if (info) {
            esp_ip4addr_ntoa(&info->ip, ip_str, sizeof(ip_str));
            snprintf(message, sizeof(message), "IP: %s", ip_str);
        }
        break;
    }
    default:
        break;
    }
    if (message[0] != '\0') {
        enqueue_ui_event(UI_EVENT_WIFI_STATUS, message, 0);
    }
}

static void mqtt_message_handler(const char *topic, const char *payload, void *ctx)
{
    (void)topic;
    (void)ctx;
    float value = strtof(payload ? payload : "0", NULL);
    enqueue_ui_event(UI_EVENT_TEMPERATURE, NULL, value);
}

static void mqtt_status_handler(bool connected, void *ctx)
{
    (void)ctx;
    enqueue_ui_event(UI_EVENT_MQTT_STATUS,
                     connected ? "MQTT connected" : "MQTT disconnected",
                     0);
}

static void publish_squareline_temperature(int position)
{
    char payload[8];
    if (position <= 0) {
        strlcpy(payload, "LO", sizeof(payload));
    } else if (position >= UI_TEMP_ARC_STEPS) {
        strlcpy(payload, "HI", sizeof(payload));
    } else {
        int temp_tenths = UI_TEMP_MIN_TENTHS + position * UI_TEMP_STEP_TENTHS;
        if (temp_tenths % 10 == 0) {
            snprintf(payload, sizeof(payload), "%d", temp_tenths / 10);
        } else {
            snprintf(payload, sizeof(payload), "%d.%d", temp_tenths / 10, temp_tenths % 10);
        }
    }
    mqtt_manager_publish_button_event(MQTT_BUTTON_HVAC_TEMP_SET, payload);
}

static void squareline_temp_event_cb(lv_event_t * e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    lv_obj_t * slider = lv_event_get_target(e);
    publish_squareline_temperature(lv_arc_get_value(slider));
}

void app_main(void)
{
    READ_LCD_ID = read_lcd_id();
    ui_event_queue = xQueueCreate(10, sizeof(ui_event_t));
    assert(ui_event_queue);

    ESP_ERROR_CHECK(wifi_manager_init(NULL));
    ESP_ERROR_CHECK(wifi_manager_register_event_handler(app_wifi_event_handler, NULL));
    mqtt_manager_config_t mqtt_cfg = { 0 };
    ESP_ERROR_CHECK(mqtt_manager_init(&mqtt_cfg,
                                      mqtt_message_handler,
                                      NULL,
                                      mqtt_status_handler,
                                      NULL));
    static lv_disp_draw_buf_t disp_buf; // contains internal graphic buffer(s) called draw buffer(s)
    static lv_disp_drv_t disp_drv;      // contains callback functions

#if EXAMPLE_PIN_NUM_BK_LIGHT >= 0
    ESP_LOGI(TAG, "Turn off LCD backlight");
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << EXAMPLE_PIN_NUM_BK_LIGHT
    };
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
#endif

    ESP_LOGI(TAG, "Initialize SPI bus");
    const spi_bus_config_t buscfg = SH8601_PANEL_BUS_QSPI_CONFIG(EXAMPLE_PIN_NUM_LCD_PCLK,
                                                                 EXAMPLE_PIN_NUM_LCD_DATA0,
                                                                 EXAMPLE_PIN_NUM_LCD_DATA1,
                                                                 EXAMPLE_PIN_NUM_LCD_DATA2,
                                                                 EXAMPLE_PIN_NUM_LCD_DATA3,
                                                                 EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * LCD_BIT_PER_PIXEL / 8);
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Install panel IO");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    const esp_lcd_panel_io_spi_config_t io_config = SH8601_PANEL_IO_QSPI_CONFIG(EXAMPLE_PIN_NUM_LCD_CS,
                                                                                example_notify_lvgl_flush_ready,
                                                                                &disp_drv);
    sh8601_vendor_config_t vendor_config = {
        .flags = {
            .use_qspi_interface = 1,
        },
    };
    // Attach the LCD to the SPI bus
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    esp_lcd_panel_handle_t panel_handle = NULL;
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = EXAMPLE_PIN_NUM_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BIT_PER_PIXEL,
        .vendor_config = &vendor_config,
    };
    vendor_config.init_cmds = (READ_LCD_ID == SH8601_ID) ? sh8601_lcd_init_cmds : co5300_lcd_init_cmds;
    vendor_config.init_cmds_size = (READ_LCD_ID == SH8601_ID) ? sizeof(sh8601_lcd_init_cmds) / sizeof(sh8601_lcd_init_cmds[0]) : sizeof(co5300_lcd_init_cmds) / sizeof(co5300_lcd_init_cmds[0]);
    ESP_LOGI(TAG, "Install SH8601 panel driver");
    ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(io_handle, &panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    // user can flush pre-defined pattern to the screen before we turn on the screen or backlight
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    Touch_Init(); //Touch initialization

#if EXAMPLE_PIN_NUM_BK_LIGHT >= 0
    ESP_LOGI(TAG, "Turn on LCD backlight");
    gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, EXAMPLE_LCD_BK_LIGHT_ON_LEVEL);
#endif

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();
    // alloc draw buffers used by LVGL
    // it's recommended to choose the size of the draw buffer(s) to be at least 1/10 screen sized
    size_t lvgl_buffer_size = EXAMPLE_LCD_H_RES * EXAMPLE_LVGL_BUF_HEIGHT * sizeof(lv_color_t);
    lv_color_t *buf1 = heap_caps_malloc(lvgl_buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    if (!buf1) {
        buf1 = heap_caps_malloc(lvgl_buffer_size, MALLOC_CAP_DMA);
    }
    if (!buf1) {
        ESP_LOGE(TAG, "Failed to allocate LVGL buffer 1 (%zu bytes)", lvgl_buffer_size);
        abort();
    }
    lv_color_t *buf2 = heap_caps_malloc(lvgl_buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    if (!buf2) {
        buf2 = heap_caps_malloc(lvgl_buffer_size, MALLOC_CAP_DMA);
    }
    if (!buf2) {
        ESP_LOGE(TAG, "Failed to allocate LVGL buffer 2 (%zu bytes)", lvgl_buffer_size);
        abort();
    }
    // initialize LVGL draw buffers
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, EXAMPLE_LCD_H_RES * EXAMPLE_LVGL_BUF_HEIGHT);

    ESP_LOGI(TAG, "Register display driver to LVGL");
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = EXAMPLE_LCD_H_RES;
    disp_drv.ver_res = EXAMPLE_LCD_V_RES;
    disp_drv.flush_cb = example_lvgl_flush_cb;
    disp_drv.rounder_cb = example_lvgl_rounder_cb;
    disp_drv.draw_buf = &disp_buf;
    disp_drv.user_data = panel_handle;
#ifdef EXAMPLE_Rotate_90
    disp_drv.sw_rotate = 1;
    disp_drv.rotated = LV_DISP_ROT_270;
#endif
    lv_disp_t *disp = lv_disp_drv_register(&disp_drv);

    ESP_LOGI(TAG, "Install LVGL tick timer");
    // Tick interface for LVGL (using esp_timer to generate 2ms periodic event)
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &example_increase_lvgl_tick,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000));

    static lv_indev_drv_t indev_drv;    // Input device driver (Touch)
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.disp = disp;
    indev_drv.read_cb = example_lvgl_touch_cb;
    lv_indev_drv_register(&indev_drv);


    lvgl_mux = xSemaphoreCreateMutex();
    assert(lvgl_mux);
    xTaskCreate(example_lvgl_port_task, "LVGL", EXAMPLE_LVGL_TASK_STACK_SIZE, NULL, EXAMPLE_LVGL_TASK_PRIORITY, NULL);

    ESP_LOGI(TAG, "Display custom UI");
    // Lock the mutex due to the LVGL APIs are not thread-safe
    if (example_lvgl_lock(-1)) {
        
        // Create screens for swipe navigation
        scr1 = lv_obj_create(NULL); // First screen
        scr2 = lv_obj_create(NULL); // Second screen (steering wheel style)
        scr_settings = lv_obj_create(NULL); // Third screen for settings
        ui_init();
        scr_squareline = ui_Screen2;
        if (ui_TempSlider) {
            lv_obj_add_event_cb(ui_TempSlider, squareline_temp_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
            publish_squareline_temperature(lv_arc_get_value(ui_TempSlider));
        }

        registered_screen_count = 0;
        register_screen(scr1);
        register_screen(scr2);
        register_screen(scr_settings);
        if (scr_squareline) {
            register_screen(scr_squareline);
        }

        lv_obj_set_style_bg_color(scr1, lv_color_hex(0x000000), 0); // Black background
        lv_obj_set_style_bg_color(scr2, lv_color_hex(0x000000), 0); // Black background
        lv_obj_set_style_bg_color(scr_settings, lv_color_hex(0x000000), 0);

        // Add music-themed background to screen 1
        lv_obj_t * music_bg1 = lv_label_create(scr1);
        lv_label_set_text(music_bg1, LV_SYMBOL_AUDIO LV_SYMBOL_AUDIO);
        lv_obj_set_style_text_color(music_bg1, lv_color_white(), 0); // White for visibility
        lv_obj_set_style_text_font(music_bg1, &lv_font_montserrat_16, 0);
        lv_obj_align(music_bg1, LV_ALIGN_TOP_LEFT, 20, 20);

        lv_obj_t * music_bg2 = lv_label_create(scr1);
        lv_label_set_text(music_bg2, LV_SYMBOL_SHUFFLE);
        lv_obj_set_style_text_color(music_bg2, lv_color_white(), 0);
        lv_obj_set_style_text_font(music_bg2, &lv_font_montserrat_16, 0);
        lv_obj_align(music_bg2, LV_ALIGN_LEFT_MID, 20, 0);

        // Add AC-themed background to screen 2
        lv_obj_t * ac_bg1 = lv_label_create(scr2);
        lv_label_set_text(ac_bg1, "\u2744\u2744");
        lv_obj_set_style_text_color(ac_bg1, lv_color_white(), 0); // White for visibility
        lv_obj_set_style_text_font(ac_bg1, &lv_font_montserrat_16, 0);
        lv_obj_align(ac_bg1, LV_ALIGN_TOP_LEFT, 20, 20);

        lv_obj_t * ac_bg2 = lv_label_create(scr2);
        lv_label_set_text(ac_bg2, LV_SYMBOL_HOME);
        lv_obj_set_style_text_color(ac_bg2, lv_color_white(), 0);
        lv_obj_set_style_text_font(ac_bg2, &lv_font_montserrat_16, 0);
        lv_obj_align(ac_bg2, LV_ALIGN_LEFT_MID, 20, 0);

        // First screen: Current layout
        // Center button for play/pause
        lv_obj_t * btn_center1 = lv_btn_create(scr1);
        lv_obj_set_size(btn_center1, 120, 80);
        lv_obj_align(btn_center1, LV_ALIGN_CENTER, 0, 0);
        lv_obj_t * label_center1 = lv_label_create(btn_center1);
        lv_label_set_text(label_center1, "Play");
        lv_obj_center(label_center1);
        lv_obj_set_style_text_font(label_center1, &lv_font_montserrat_16, 0);
        lv_obj_add_event_cb(btn_center1, play_pause_event_cb, LV_EVENT_CLICKED, NULL);

        // Top button: Volume +
        lv_obj_t * btn_top1 = lv_btn_create(scr1);
        lv_obj_set_size(btn_top1, 120, 80);
        lv_obj_align(btn_top1, LV_ALIGN_TOP_MID, 0, 50);
        lv_obj_t * label_top1 = lv_label_create(btn_top1);
        lv_label_set_text(label_top1, "Volume +");
        lv_obj_center(label_top1);
        lv_obj_set_style_text_font(label_top1, &lv_font_montserrat_16, 0);
        lv_obj_add_event_cb(btn_top1, action_button_event_cb, LV_EVENT_CLICKED, (void *)MQTT_BUTTON_MEDIA_VOLUME_UP);

        // Bottom button: Volume -
        lv_obj_t * btn_bottom1 = lv_btn_create(scr1);
        lv_obj_set_size(btn_bottom1, 120, 80);
        lv_obj_align(btn_bottom1, LV_ALIGN_BOTTOM_MID, 0, -50);
        lv_obj_t * label_bottom1 = lv_label_create(btn_bottom1);
        lv_label_set_text(label_bottom1, "Volume -");
        lv_obj_center(label_bottom1);
        lv_obj_set_style_text_font(label_bottom1, &lv_font_montserrat_16, 0);
        lv_obj_add_event_cb(btn_bottom1, action_button_event_cb, LV_EVENT_CLICKED, (void *)MQTT_BUTTON_MEDIA_VOLUME_DOWN);

        // Left button: <<
        lv_obj_t * btn_left1 = lv_btn_create(scr1);
        lv_obj_set_size(btn_left1, 120, 80);
        lv_obj_align(btn_left1, LV_ALIGN_LEFT_MID, 50, 0);
        lv_obj_t * label_left1 = lv_label_create(btn_left1);
        lv_label_set_text(label_left1, "<<");
        lv_obj_center(label_left1);
        lv_obj_set_style_text_font(label_left1, &lv_font_montserrat_16, 0);
        lv_obj_add_event_cb(btn_left1, action_button_event_cb, LV_EVENT_CLICKED, (void *)MQTT_BUTTON_MEDIA_PREV);

        // Right button: >>
        lv_obj_t * btn_right1 = lv_btn_create(scr1);
        lv_obj_set_size(btn_right1, 120, 80);
        lv_obj_align(btn_right1, LV_ALIGN_RIGHT_MID, -50, 0);
        lv_obj_t * label_right1 = lv_label_create(btn_right1);
        lv_label_set_text(label_right1, ">>");
        lv_obj_center(label_right1);
        lv_obj_set_style_text_font(label_right1, &lv_font_montserrat_16, 0);
        lv_obj_add_event_cb(btn_right1, action_button_event_cb, LV_EVENT_CLICKED, (void *)MQTT_BUTTON_MEDIA_NEXT);

        // Second screen: Steering wheel style
        // Center button for temperature
        lv_obj_t * btn_center2 = lv_btn_create(scr2);
        lv_obj_set_size(btn_center2, 120, 80);
        lv_obj_align(btn_center2, LV_ALIGN_CENTER, 0, 0);
        lv_obj_t * label_center2 = lv_label_create(btn_center2);
        lv_label_set_text(label_center2, "--");
        lv_obj_center(label_center2);
        lv_obj_set_style_text_font(label_center2, &lv_font_montserrat_16, 0);
        temperature_value_label = label_center2;

        // Top button: +
        lv_obj_t * btn_top2 = lv_btn_create(scr2);
        lv_obj_set_size(btn_top2, 120, 80);
        lv_obj_align(btn_top2, LV_ALIGN_TOP_MID, 0, 50);
        lv_obj_t * label_top2 = lv_label_create(btn_top2);
        lv_label_set_text(label_top2, "+");
        lv_obj_center(label_top2);
        lv_obj_set_style_text_font(label_top2, &lv_font_montserrat_16, 0);
        lv_obj_add_event_cb(btn_top2, action_button_event_cb, LV_EVENT_CLICKED, (void *)MQTT_BUTTON_HVAC_TEMP_UP);

        // Bottom button: -
        lv_obj_t * btn_bottom2 = lv_btn_create(scr2);
        lv_obj_set_size(btn_bottom2, 120, 80);
        lv_obj_align(btn_bottom2, LV_ALIGN_BOTTOM_MID, 0, -50);
        lv_obj_t * label_bottom2 = lv_label_create(btn_bottom2);
        lv_label_set_text(label_bottom2, "-");
        lv_obj_center(label_bottom2);
        lv_obj_set_style_text_font(label_bottom2, &lv_font_montserrat_16, 0);
        lv_obj_add_event_cb(btn_bottom2, action_button_event_cb, LV_EVENT_CLICKED, (void *)MQTT_BUTTON_HVAC_TEMP_DOWN);

        // Right button: Intensity
        lv_obj_t * btn_right2 = lv_btn_create(scr2);
        lv_obj_set_size(btn_right2, 120, 80);
        lv_obj_align(btn_right2, LV_ALIGN_RIGHT_MID, -50, 0);
        lv_obj_t * label_right2 = lv_label_create(btn_right2);
        lv_label_set_text(label_right2, "Intensity");
        lv_obj_center(label_right2);
        lv_obj_set_style_text_font(label_right2, &lv_font_montserrat_16, 0);
        lv_obj_add_event_cb(btn_right2, action_button_event_cb, LV_EVENT_CLICKED, (void *)MQTT_BUTTON_HVAC_INTENSITY);

        // Left button: (I) power
        lv_obj_t * btn_left2 = lv_btn_create(scr2);
        lv_obj_set_size(btn_left2, 120, 80);
        lv_obj_align(btn_left2, LV_ALIGN_LEFT_MID, 50, 0);
        lv_obj_t * label_left2 = lv_label_create(btn_left2);
        lv_label_set_text(label_left2, "(I)");
        lv_obj_center(label_left2);
        lv_obj_set_style_text_font(label_left2, &lv_font_montserrat_16, 0);
        lv_obj_add_event_cb(btn_left2, action_button_event_cb, LV_EVENT_CLICKED, (void *)MQTT_BUTTON_HVAC_POWER);

        // Third screen: Wi-Fi & MQTT settings
        lv_obj_t * settings_title = lv_label_create(scr_settings);
        lv_label_set_text(settings_title, "Wi-Fi & MQTT");
        lv_obj_set_style_text_color(settings_title, lv_color_white(), 0);
        lv_obj_set_style_text_font(settings_title, &lv_font_montserrat_16, 0);
        lv_obj_align(settings_title, LV_ALIGN_TOP_MID, 0, 20);

        ssid_input = lv_textarea_create(scr_settings);
        lv_obj_set_size(ssid_input, 280, 50);
        lv_obj_align(ssid_input, LV_ALIGN_TOP_MID, 0, 60);
        lv_textarea_set_placeholder_text(ssid_input, "SSID");
        lv_textarea_set_max_length(ssid_input, WIFI_MANAGER_MAX_SSID_LEN);
        lv_textarea_set_one_line(ssid_input, true);
        lv_obj_add_event_cb(ssid_input, text_area_event_cb, LV_EVENT_ALL, NULL);

        password_input = lv_textarea_create(scr_settings);
        lv_obj_set_size(password_input, 280, 50);
        lv_obj_align(password_input, LV_ALIGN_TOP_MID, 0, 130);
        lv_textarea_set_placeholder_text(password_input, "Password");
        lv_textarea_set_max_length(password_input, WIFI_MANAGER_MAX_PASSWORD_LEN);
        lv_textarea_set_one_line(password_input, true);
        lv_textarea_set_password_mode(password_input, true);
        lv_obj_add_event_cb(password_input, text_area_event_cb, LV_EVENT_ALL, NULL);

        lv_obj_t * connect_btn = lv_btn_create(scr_settings);
        lv_obj_set_size(connect_btn, 160, 50);
        lv_obj_align(connect_btn, LV_ALIGN_TOP_MID, 0, 200);
        lv_obj_add_event_cb(connect_btn, connect_button_event_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t * connect_label = lv_label_create(connect_btn);
        lv_label_set_text(connect_label, "Connect");
        lv_obj_center(connect_label);

        const wifi_manager_credentials_t *stored_creds = wifi_manager_get_credentials();
        if (stored_creds) {
            lv_textarea_set_text(ssid_input, stored_creds->ssid);
            lv_textarea_set_text(password_input, stored_creds->password);
        }

        lv_obj_t * wifi_status_title = lv_label_create(scr_settings);
        lv_label_set_text(wifi_status_title, "Wi-Fi Status:");
        lv_obj_set_style_text_color(wifi_status_title, lv_color_white(), 0);
        lv_obj_align(wifi_status_title, LV_ALIGN_BOTTOM_MID, 0, -120);

        wifi_status_value_label = lv_label_create(scr_settings);
        lv_label_set_text(wifi_status_value_label, "Not connected");
        lv_obj_set_style_text_color(wifi_status_value_label, lv_color_white(), 0);
        lv_obj_align_to(wifi_status_value_label, wifi_status_title, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);

        lv_obj_t * mqtt_status_title = lv_label_create(scr_settings);
        lv_label_set_text(mqtt_status_title, "MQTT Status:");
        lv_obj_set_style_text_color(mqtt_status_title, lv_color_white(), 0);
        lv_obj_align(mqtt_status_title, LV_ALIGN_BOTTOM_MID, 0, -60);

        mqtt_status_value_label = lv_label_create(scr_settings);
        lv_label_set_text(mqtt_status_value_label, "Waiting for Wi-Fi");
        lv_obj_set_style_text_color(mqtt_status_value_label, lv_color_white(), 0);
        lv_obj_align_to(mqtt_status_value_label, mqtt_status_title, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);

        settings_keyboard = lv_keyboard_create(scr_settings);
        lv_obj_add_flag(settings_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(settings_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_add_event_cb(settings_keyboard, keyboard_event_cb, LV_EVENT_ALL, NULL);
        lv_keyboard_set_textarea(settings_keyboard, NULL);

        // Load first screen initially
        current_screen = scr1;
        lv_scr_load(scr1);

        // Release the mutex
        example_lvgl_unlock();
    }
}
