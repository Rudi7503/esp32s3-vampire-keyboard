#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Install TinyUSB with the descriptors from usb_descriptors.c. */
esp_err_t keyboard_init(void);

/* True after SET_CONFIGURATION and while the IN endpoint can accept a report. */
bool keyboard_ready(void);

/* Wait until keyboard_ready() is true, or return ESP_ERR_TIMEOUT. */
esp_err_t keyboard_wait_ready(uint32_t timeout_ms);

/*
 * Send one 8-byte boot-keyboard report:
 *   report[0] = modifier bitmap (bit 0 = Left Ctrl ... bit 7 = Right GUI)
 *   report[1] = reserved, always 0
 *   report[2..7] = up to six HID keyboard usage IDs (0 = empty)
 *
 * keycodes may be NULL, which sends six zero bytes (all keys released).
 * Returns ESP_ERR_NOT_FINISHED if the previous report has not been sent yet;
 * retry after a few milliseconds.
 */
esp_err_t keyboard_send_report(uint8_t modifiers, const uint8_t keycodes[6]);

/* Release all keys (8-byte zero report), with a short retry loop. */
esp_err_t keyboard_release_all(void);

/*
 * Press one key, wait hold_ms, then release all keys.
 * keycode is a HID usage ID, e.g. HID_KEY_A (0x04) or HID_KEY_ENTER (0x28).
 */
esp_err_t keyboard_tap(uint8_t keycode, uint8_t modifiers, uint32_t hold_ms);

#ifdef __cplusplus
}
#endif
