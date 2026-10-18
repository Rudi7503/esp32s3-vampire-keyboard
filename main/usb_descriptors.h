/*
 * USB descriptors for a single-interface boot-protocol keyboard.
 *
 * All byte arrays in this file are deliberately written as raw USB bytes:
 * this is the place where you paste the descriptors captured from a keyboard
 * that is known to enumerate on the Vampire V4.
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "tinyusb.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * 1. VID / PID
 *
 * 0x1209 is the pid.codes shared VID.  Do not ship 0x0001 as a product PID;
 * request/choose a PID you are allowed to use, or use your own VID/PID.
 * ------------------------------------------------------------------------- */
#define USB_KEYBOARD_VID 0x1209
#define USB_KEYBOARD_PID 0x0001

/* Keep these in sync with the arrays below.  They are checked at boot. */
#define USB_KEYBOARD_EP0_SIZE 8
#define USB_KEYBOARD_CONFIG_DESC_LEN 34
#define USB_KEYBOARD_HID_REPORT_DESC_LEN 65

extern const tusb_desc_device_t usb_device_descriptor;
extern const uint8_t usb_configuration_descriptor[USB_KEYBOARD_CONFIG_DESC_LEN];
extern const uint8_t usb_hid_report_descriptor[USB_KEYBOARD_HID_REPORT_DESC_LEN];
extern const char *usb_string_descriptors[];
extern const int usb_string_descriptor_count;

/* Checks bLength, wTotalLength, the descriptor chain, HID report length and
 * the boot-keyboard interface fields.  Call it before tinyusb_driver_install(). */
esp_err_t usb_descriptors_self_check(void);

#ifdef __cplusplus
}
#endif
