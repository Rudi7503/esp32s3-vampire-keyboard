#include "keyboard.h"

#include <string.h>

#include "class/hid/hid_device.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "tinyusb.h"
#include "usb_descriptors.h"

#if !CONFIG_TINYUSB_HID_COUNT
#error "Set CONFIG_TINYUSB_HID_COUNT=1 (TinyUSB HID is disabled in sdkconfig)."
#endif

static const char *TAG = "kbd";

static volatile bool s_configured;
static uint8_t s_led_state;
static SemaphoreHandle_t s_tx_lock;

/* esp_tinyusb owns tud_mount_cb()/tud_umount_cb(); application events arrive
 * through this callback instead of redefining those symbols. */
static void usb_event_cb(tinyusb_event_t *event, void *arg)
{
    (void)arg;
    switch (event->id) {
    case TINYUSB_EVENT_ATTACHED:
        s_configured = true;
        ESP_LOGI(TAG, "host configured the keyboard (SET_CONFIGURATION)");
        break;
    case TINYUSB_EVENT_DETACHED:
        s_configured = false;
        ESP_LOGW(TAG, "keyboard detached / unconfigured");
        break;
    default:
        break;
    }
}

esp_err_t keyboard_init(void)
{
    ESP_RETURN_ON_ERROR(usb_descriptors_self_check(), TAG, "descriptor check failed");

    if (s_tx_lock == NULL) {
        s_tx_lock = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_tx_lock != NULL, ESP_ERR_NO_MEM, TAG,
                            "cannot create report mutex");
    }

    const tinyusb_config_t config = {
        .port = TINYUSB_PORT_FULL_SPEED_0,
        .phy = {
            .skip_setup = false,
            .self_powered = false,
            .vbus_monitor_io = -1,
        },
        .task = {
            .size = 4096,
            .priority = 5,
            .xCoreID = 0,
        },
        .descriptor = {
            .device = &usb_device_descriptor,
            .qualifier = NULL,
            .string = usb_string_descriptors,
            .string_count = usb_string_descriptor_count,
            .full_speed_config = usb_configuration_descriptor,
            .high_speed_config = NULL,
        },
        .pm_lock_enable = false,
        .event_cb = usb_event_cb,
        .event_arg = NULL,
    };

    return tinyusb_driver_install(&config);
}

bool keyboard_ready(void)
{
    /* Short-circuit: never call into TinyUSB before the driver is installed. */
    return s_configured && s_tx_lock != NULL && tud_hid_ready();
}

esp_err_t keyboard_wait_ready(uint32_t timeout_ms)
{
    const TickType_t start = xTaskGetTickCount();
    const TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

    while (!keyboard_ready()) {
        if ((xTaskGetTickCount() - start) >= timeout_ticks) {
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return ESP_OK;
}

esp_err_t keyboard_send_report(uint8_t modifiers, const uint8_t keycodes[6])
{
    if (!s_configured) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!tud_hid_ready()) {
        return ESP_ERR_NOT_FINISHED;
    }
    if (s_tx_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_tx_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    /* Re-check after taking the lock: another task may have just filled the
     * single HID IN endpoint. */
    if (!tud_hid_ready()) {
        xSemaphoreGive(s_tx_lock);
        return ESP_ERR_NOT_FINISHED;
    }

    uint8_t report[8] = {
        modifiers, /* byte 0: modifier bitmap */
        0x00,      /* byte 1: reserved */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    if (keycodes != NULL) {
        memcpy(&report[2], keycodes, 6);
    }

    const bool queued = tud_hid_report(0, report, sizeof(report));
    xSemaphoreGive(s_tx_lock);

    if (!queued) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t keyboard_release_all(void)
{
    esp_err_t err = ESP_FAIL;

    /* The host polls the interrupt endpoint; a report may still be in flight.
     * Retry briefly instead of leaving a key stuck down. */
    for (int i = 0; i < 50; i++) {
        err = keyboard_send_report(0, NULL);
        if (err == ESP_OK) {
            return ESP_OK;
        }
        if (err != ESP_ERR_NOT_FINISHED) {
            return err;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    return err;
}

esp_err_t keyboard_tap(uint8_t keycode, uint8_t modifiers, uint32_t hold_ms)
{
    if (keycode == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t keys[6] = {keycode, 0, 0, 0, 0, 0};
    esp_err_t err = keyboard_send_report(modifiers, keys);
    if (err != ESP_OK) {
        return err;
    }

    if (hold_ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(hold_ms));
    }

    err = keyboard_release_all();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "key release failed: %s", esp_err_to_name(err));
    }
    return err;
}

/* =========================================================================
 * TinyUSB HID class callbacks
 * ========================================================================= */

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return usb_hid_report_descriptor;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen)
{
    (void)instance;
    (void)report_id;

    if (report_type == HID_REPORT_TYPE_OUTPUT && buffer != NULL && reqlen >= 1) {
        buffer[0] = s_led_state;
        return 1;
    }
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize)
{
    (void)instance;
    (void)report_id;

    if (report_type == HID_REPORT_TYPE_OUTPUT && buffer != NULL && bufsize >= 1) {
        s_led_state = buffer[0];
        ESP_LOGD(TAG, "host LED report: 0x%02X", s_led_state);
    }
}

void tud_hid_set_protocol_cb(uint8_t instance, uint8_t protocol)
{
    (void)instance;
    /* HID SET_PROTOCOL: 0 = boot protocol, 1 = report protocol. */
    ESP_LOGI(TAG, "host selected %s protocol",
             protocol == 0 ? "BOOT" : "REPORT");
}
