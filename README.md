Vehicle like UX for the ESP32 AMOLED, based on the ESP LVGL Demo Code
added Wifi, MQTT functionality

> **Need the big-picture write-up?** See `docs/system_overview.md` for the formal project brief (concept, hardware stack, architecture diagrams, and workflow notes).

## Architecture Overview

- **LVGL UI** lives in `main/example_qspi_with_ram.c`. The file builds three gesture‑navigable screens (media controls, HVAC controls, and Wi‑Fi/MQTT settings). A FreeRTOS mutex and queue guard all LVGL interactions so background tasks can post updates safely.
- **Wi-Fi management** is encapsulated in `components/wifi_manager`, which initializes `esp_netif`, stores credentials in NVS, and raises connection/IP events to registered listeners. The settings screen writes new credentials that are persisted and automatically retried on reconnect.
- **MQTT integration** is handled by `components/mqtt_manager`. It depends on the Wi-Fi manager, auto-connects to a configurable broker, publishes button actions (media/HVAC) and subscribes to temperature telemetry that is rendered on screen 2.
- **Touch and LCD bring-up** remain modular (`touch_bsp`, `read_lcd_id_bsp`, `esp_lcd_sh8601`). The display driver is selected dynamically by probing the panel ID, so the rest of the UI code stays agnostic.
- **Configuration** is driven by `sdkconfig.defaults` plus `main/idf_component.yml`. Wi-Fi and MQTT components are added via the ESP-IDF component manager, while LVGL stays vendored under `components/lvgl`.

### High-Level Diagram

```
 ┌──────────────────────────────────────────────────────────────────────┐
 │                          app_main (main/)                            │
 │                                                                      │
 │  ┌──────────────┐      ┌──────────────────────┐      ┌─────────────┐  │
 │  │ LVGL Screens │◄────►│  UI Event Queue      │◄────►│ MQTT Manager│  │
 │  │ (media/hvac/ │      │  (status + telemetry │      │ (button pub/│  │
 │  │  settings)   │      │   updates)           │      │  temp sub)  │  │
 │  └─────▲────────┘      └─────────▲────────────┘      └─────▲───────┘  │
 │        │                         │                             │       │
 │        │                ┌────────┴────────┐                    │       │
 │        │                │ Wi-Fi Manager   │◄──────────────┐    │       │
 │        │                │ (NVS creds,     │               │    │       │
 │        │                │  connect events)│               │    │       │
 │        │                └────────▲────────┘               │    │       │
 │        │                         │                        │    │       │
 │        │                         │                        │    │       │
 │   ┌────┴─────┐       ┌───────────┴──────────┐        ┌────┴────┐ │
 │   │ touch_bsp│       │ read_lcd_id_bsp      │        │ esp_lcd │ │
 │   │ (FT3168) │       │ (panel detect)       │        │ driver   │ │
 │   └────▲─────┘       └──────────▲───────────┘        └────▲────┘ │
 │        │                        │                           │      │
 │        └────────────┬───────────┴───────────┬───────────────┘      │
 │                     │        ESP-IDF        │                      │
 └──────────────────────────────────────────────────────────────────────┘
```




## Hardware Snapshot

- **Target MCU:** ESP32‑S3 with 8 MB PSRAM / 4 MB QSPI flash
- **Display:** 1.78″ circular AMOLED (SH8601) driven over QSPI, 466×466 px
- **Touch:** FT3168 multi‑touch controller on I²C (interrupt driven)
- **Sensors:** Bosch/BMI‑class IMU for motion gestures (optional)
- **I/O:** UART bridge headers for CAN/diagnostics, USB‑C for power/debug

Pin assignments live in [`main/example_qspi_with_ram.c`](main/example_qspi_with_ram.c); change the `EXAMPLE_PIN_NUM_*` macros there if you spin different hardware. The backlight MOSFET polarity is controlled via `EXAMPLE_LCD_BK_LIGHT_ON_LEVEL`.

## Building & Flashing

```bash
idf.py set-target esp32s3   # only needed once
idf.py -p /dev/cu.usbmodemXXXX build flash monitor
```

The first build resolves component-manager dependencies (LVGL, SH8601 driver, MQTT). Subsequent iterations are incremental. Exit the monitor with `Ctrl+]`.

## Tooling Tips

- Use the ESP-IDF VS Code extension for menuconfig / flash buttons.
- UI layouts are edited in SquareLine Studio; exported assets go into `main/ui/`.
- HiveMQ credentials & CA live in `components/mqtt_manager/mqtt_manager.c`.

## Troubleshooting

| Symptom | Fix |
| --- | --- |
| Blank display after splash | Check panel ID (`read_lcd_id_bsp`) or LVGL buffer allocation logs. |
| Touch not responding | Verify I²C pull-ups and ensure FT3168 interrupt pin matches config. |
| MQTT disconnects | Confirm Wi‑Fi connectivity and that the HiveMQ TLS certificate matches `isrgrootx1.pem`. |
| Watchdog asserts | Ensure any custom LVGL calls are wrapped with `example_lvgl_lock()`/`unlock()`. |
