#include "keyboard.h"

#include "class/hid/hid.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "app";

typedef struct {
    uint8_t keycode;
    uint8_t modifier;
} key_press_t;

/* "Hallo" followed by Return: H needs LeftShift, the other keys do not. */
static const key_press_t HELLO[] = {
    {HID_KEY_H, KEYBOARD_MODIFIER_LEFTSHIFT},
    {HID_KEY_A, 0},
    {HID_KEY_L, 0},
    {HID_KEY_L, 0},
    {HID_KEY_O, 0},
    {HID_KEY_ENTER, 0},
};

static esp_err_t send_hello_line(void)
{
    for (size_t i = 0; i < sizeof(HELLO) / sizeof(HELLO[0]); i++) {
        esp_err_t err = keyboard_tap(HELLO[i].keycode, HELLO[i].modifier, 30);
        if (err != ESP_OK) {
            return err;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return ESP_OK;
}

static void hello_task(void *arg)
{
    (void)arg;
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        /* Wait until the host has configured the keyboard.  This also
         * survives unplug/replug: after a detach the task waits again. */
        esp_err_t err = keyboard_wait_ready(1000);
        if (err == ESP_OK) {
            err = send_hello_line();
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "sent \"Hallo\" + Return");
            } else {
                ESP_LOGW(TAG, "could not send \"Hallo\" + Return: %s", esp_err_to_name(err));
            }
        }

        /* Keep a 3.000 s period from start to start, independent of how
         * long typing the six reports took. */
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(3000));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-S3 USB boot keyboard starting");
    ESP_LOGI(TAG, "test mode: typing \"Hallo\" + Return every 3 seconds after enumeration");

    ESP_ERROR_CHECK(keyboard_init());

    BaseType_t ok = xTaskCreate(hello_task, "hello", 4096, NULL, 5, NULL);
    ESP_ERROR_CHECK(ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);

    /* The hello task and the esp_tinyusb task keep running. */
}
