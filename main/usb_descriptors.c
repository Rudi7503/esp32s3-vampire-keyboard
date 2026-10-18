#include "usb_descriptors.h"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "tusb.h"

#if CFG_TUD_ENDPOINT0_SIZE != USB_KEYBOARD_EP0_SIZE
#error "CFG_TUD_ENDPOINT0_SIZE must match USB_KEYBOARD_EP0_SIZE"
#endif

static const char *TAG = "usb_desc";

/* =========================================================================
 * DEVICE DESCRIPTOR
 *
 * Reference: Logitech K120 (VID 046d, PID c31c) from usbtest.pcapng.
 * VID/PID are deliberately not cloned; use your own values here.
 * ========================================================================= */
const tusb_desc_device_t usb_device_descriptor = {
    .bLength            = sizeof(tusb_desc_device_t), /* must be 18 */
    .bDescriptorType    = TUSB_DESC_DEVICE,           /* 0x01 */
    .bcdUSB             = 0x0110,                     /* USB 1.10, as captured */
    .bDeviceClass       = 0x00,                       /* class is in the interface */
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = USB_KEYBOARD_EP0_SIZE,      /* 8, as captured */
    .idVendor           = USB_KEYBOARD_VID,
    .idProduct          = USB_KEYBOARD_PID,
    .bcdDevice          = 0x6402,                     /* as captured */
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x00,                       /* reference has no serial */
    .bNumConfigurations = 0x01,
};

/* =========================================================================
 * CONFIGURATION + INTERFACE + HID + ENDPOINT DESCRIPTORS
 *
 * Single-interface subset of the captured Logitech K120 configuration.
 * The original capture has a second HID interface (consumer control,
 * report descriptor 184 bytes, endpoint 0x82).  A boot keyboard does not
 * need it; omitting it makes the descriptor shorter and simpler.  The
 * bytes below are otherwise taken from the capture.
 *
 * Offsets in this blob:
 *   0  .. 8   configuration (9 bytes)
 *   9  .. 17  interface 0   (9 bytes)
 *   18 .. 26  HID descriptor (9 bytes)
 *   27 .. 33  endpoint IN 0x81 (7 bytes)
 * ========================================================================= */
const uint8_t usb_configuration_descriptor[USB_KEYBOARD_CONFIG_DESC_LEN] = {
    /* Configuration descriptor */
    0x09,       /* bLength */
    0x02,       /* bDescriptorType = CONFIGURATION */
    0x22, 0x00, /* wTotalLength = 34 */
    0x01,       /* bNumInterfaces = 1 */
    0x01,       /* bConfigurationValue = 1 */
    0x03,       /* iConfiguration = 3 */
    0xA0,       /* bmAttributes = bus-powered + remote wakeup */
    0x2D,       /* bMaxPower = 90 mA (captured value) */

    /* Interface descriptor: HID boot keyboard */
    0x09,       /* bLength */
    0x04,       /* bDescriptorType = INTERFACE */
    0x00,       /* bInterfaceNumber = 0 */
    0x00,       /* bAlternateSetting = 0 */
    0x01,       /* bNumEndpoints = 1 */
    0x03,       /* bInterfaceClass = HID */
    0x01,       /* bInterfaceSubClass = boot interface */
    0x01,       /* bInterfaceProtocol = keyboard */
    0x02,       /* iInterface = 2 (product string) */

    /* HID descriptor */
    0x09,       /* bLength */
    0x21,       /* bDescriptorType = HID */
    0x10, 0x01, /* bcdHID = 1.10 (captured value) */
    0x00,       /* bCountryCode = 0 */
    0x01,       /* bNumDescriptors = 1 */
    0x22,       /* bDescriptorType = REPORT */
    0x41, 0x00, /* wDescriptorLength = 65 */

    /* Endpoint descriptor: interrupt IN, 8-byte reports, 10 ms */
    0x07,       /* bLength */
    0x05,       /* bDescriptorType = ENDPOINT */
    0x81,       /* bEndpointAddress = IN 1 */
    0x03,       /* bmAttributes = interrupt */
    0x08, 0x00, /* wMaxPacketSize = 8 */
    0x0A,       /* bInterval = 10 ms */
};

/* =========================================================================
 * HID REPORT DESCRIPTOR: 65 bytes, captured from the working keyboard
 *
 * Byte 0 is the modifier bitmap, byte 1 is reserved, bytes 2..7 are up to six
 * simultaneously pressed key usage IDs.  This is exactly the 8-byte report
 * sent by keyboard_send_report().
 *
 * The only difference from the canonical 63-byte descriptor is the wider
 * logical/usage maximum (0x00FF instead of 0x0065); the 8-byte input report
 * layout is unchanged.
 * ========================================================================= */
const uint8_t usb_hid_report_descriptor[USB_KEYBOARD_HID_REPORT_DESC_LEN] = {
    0x05, 0x01,       /* Usage Page (Generic Desktop) */
    0x09, 0x06,       /* Usage (Keyboard) */
    0xA1, 0x01,       /* Collection (Application) */
    0x05, 0x07,       /*   Usage Page (Keyboard/Keypad) */
    0x19, 0xE0,       /*   Usage Minimum (Left Control) */
    0x29, 0xE7,       /*   Usage Maximum (Right GUI) */
    0x15, 0x00,       /*   Logical Minimum (0) */
    0x25, 0x01,       /*   Logical Maximum (1) */
    0x75, 0x01,       /*   Report Size (1) */
    0x95, 0x08,       /*   Report Count (8) */
    0x81, 0x02,       /*   Input (Data, Variable, Absolute) */
    0x95, 0x01,       /*   Report Count (1) */
    0x75, 0x08,       /*   Report Size (8) */
    0x81, 0x01,       /*   Input (Constant) = reserved byte */
    0x95, 0x05,       /*   Report Count (5) */
    0x75, 0x01,       /*   Report Size (1) */
    0x05, 0x08,       /*   Usage Page (LEDs) */
    0x19, 0x01,       /*   Usage Minimum (Num Lock) */
    0x29, 0x05,       /*   Usage Maximum (Kana) */
    0x91, 0x02,       /*   Output (Data, Variable, Absolute) */
    0x95, 0x01,       /*   Report Count (1) */
    0x75, 0x03,       /*   Report Size (3) */
    0x91, 0x01,       /*   Output (Constant) = LED padding */
    0x95, 0x06,       /*   Report Count (6) */
    0x75, 0x08,       /*   Report Size (8) */
    0x15, 0x00,       /*   Logical Minimum (0) */
    0x26, 0xFF, 0x00, /*   Logical Maximum (255) */
    0x05, 0x07,       /*   Usage Page (Keyboard/Keypad) */
    0x19, 0x00,       /*   Usage Minimum (0) */
    0x2A, 0xFF, 0x00, /*   Usage Maximum (255) */
    0x81, 0x00,       /*   Input (Data, Array, Absolute) */
    0xC0,             /* End Collection */
};

/* =========================================================================
 * STRING DESCRIPTORS
 *
 * Index 0 is not a string: esp_tinyusb expects the two language-ID bytes
 * (0x0409 = English/US) here.  Indices 1..3 are converted to UTF-16LE by
 * esp_tinyusb.  The captured keyboard has iSerialNumber=0; index 3 is the
 * configuration string referenced by iConfiguration=3.
 * ========================================================================= */
const char *usb_string_descriptors[] = {
    (const char[]){0x09, 0x04},          /* 0: LangID 0x0409 */
    "Papa",                              /* 1: iManufacturer */
    "USB Keyboard",                      /* 2: iProduct, iInterface */
    "USB Keyboard",                      /* 3: iConfiguration */
};

const int usb_string_descriptor_count =
    sizeof(usb_string_descriptors) / sizeof(usb_string_descriptors[0]);

/* =========================================================================
 * BOOT-TIME SELF CHECK
 * ========================================================================= */
esp_err_t usb_descriptors_self_check(void)
{
    ESP_RETURN_ON_FALSE(usb_device_descriptor.bLength == 18,
                        ESP_ERR_INVALID_SIZE, TAG,
                        "device descriptor bLength is %u, expected 18",
                        usb_device_descriptor.bLength);
    ESP_RETURN_ON_FALSE(usb_device_descriptor.bDescriptorType == TUSB_DESC_DEVICE,
                        ESP_ERR_INVALID_ARG, TAG, "device descriptor type is wrong");
    ESP_RETURN_ON_FALSE(usb_device_descriptor.bNumConfigurations == 1,
                        ESP_ERR_INVALID_ARG, TAG, "bNumConfigurations must be 1");
    ESP_RETURN_ON_FALSE(usb_device_descriptor.bMaxPacketSize0 == 8 ||
                        usb_device_descriptor.bMaxPacketSize0 == 16 ||
                        usb_device_descriptor.bMaxPacketSize0 == 32 ||
                        usb_device_descriptor.bMaxPacketSize0 == 64,
                        ESP_ERR_INVALID_ARG, TAG, "invalid bMaxPacketSize0");
    ESP_RETURN_ON_FALSE(usb_device_descriptor.bMaxPacketSize0 == CFG_TUD_ENDPOINT0_SIZE,
                        ESP_ERR_INVALID_ARG, TAG,
                        "bMaxPacketSize0=%u but CFG_TUD_ENDPOINT0_SIZE=%u",
                        usb_device_descriptor.bMaxPacketSize0,
                        (unsigned)CFG_TUD_ENDPOINT0_SIZE);

    ESP_RETURN_ON_FALSE(usb_configuration_descriptor[0] == 9,
                        ESP_ERR_INVALID_SIZE, TAG,
                        "configuration bLength must be 9");
    ESP_RETURN_ON_FALSE(usb_configuration_descriptor[1] == 0x02,
                        ESP_ERR_INVALID_ARG, TAG,
                        "first descriptor is not a configuration descriptor");
    const uint16_t total_len =
        (uint16_t)usb_configuration_descriptor[2] |
        ((uint16_t)usb_configuration_descriptor[3] << 8);
    ESP_RETURN_ON_FALSE(total_len == sizeof(usb_configuration_descriptor),
                        ESP_ERR_INVALID_SIZE, TAG,
                        "wTotalLength=%u, array is %u bytes",
                        total_len, (unsigned)sizeof(usb_configuration_descriptor));
    ESP_RETURN_ON_FALSE(usb_configuration_descriptor[4] == 1,
                        ESP_ERR_INVALID_ARG, TAG,
                        "bNumInterfaces must be 1 for this keyboard");
    ESP_RETURN_ON_FALSE(usb_configuration_descriptor[5] == 1,
                        ESP_ERR_INVALID_ARG, TAG,
                        "bConfigurationValue must be 1");

    const uint8_t *p = usb_configuration_descriptor;
    const uint8_t *const end = p + sizeof(usb_configuration_descriptor);
    p += p[0]; /* skip configuration descriptor */

    bool saw_interface = false;
    bool saw_hid = false;
    bool saw_endpoint = false;

    while (p < end) {
        const uint8_t len = p[0];
        const uint8_t type = p[1];

        ESP_RETURN_ON_FALSE(len >= 2, ESP_ERR_INVALID_SIZE, TAG,
                            "descriptor at offset %u has bLength=%u",
                            (unsigned)(p - usb_configuration_descriptor), len);
        ESP_RETURN_ON_FALSE((size_t)(end - p) >= len, ESP_ERR_INVALID_SIZE, TAG,
                            "descriptor at offset %u overruns wTotalLength",
                            (unsigned)(p - usb_configuration_descriptor));

        if (type == TUSB_DESC_INTERFACE) {
            ESP_RETURN_ON_FALSE(len == 9, ESP_ERR_INVALID_SIZE, TAG,
                                "interface descriptor length is %u", len);
            ESP_RETURN_ON_FALSE(p[2] == 0, ESP_ERR_INVALID_ARG, TAG,
                                "bInterfaceNumber must be 0");
            ESP_RETURN_ON_FALSE(p[3] == 0, ESP_ERR_INVALID_ARG, TAG,
                                "bAlternateSetting must be 0");
            ESP_RETURN_ON_FALSE(p[4] == 1, ESP_ERR_INVALID_ARG, TAG,
                                "bNumEndpoints must be 1");
            ESP_RETURN_ON_FALSE(p[5] == 0x03, ESP_ERR_INVALID_ARG, TAG,
                                "bInterfaceClass must be HID (0x03)");
            ESP_RETURN_ON_FALSE(p[6] == 0x01, ESP_ERR_INVALID_ARG, TAG,
                                "bInterfaceSubClass must be boot (0x01)");
            ESP_RETURN_ON_FALSE(p[7] == 0x01, ESP_ERR_INVALID_ARG, TAG,
                                "bInterfaceProtocol must be keyboard (0x01)");
            saw_interface = true;
        } else if (type == 0x21) { /* HID descriptor */
            ESP_RETURN_ON_FALSE(len == 9, ESP_ERR_INVALID_SIZE, TAG,
                                "HID descriptor length is %u", len);
            ESP_RETURN_ON_FALSE(p[5] == 1, ESP_ERR_INVALID_ARG, TAG,
                                "bNumDescriptors must be 1");
            ESP_RETURN_ON_FALSE(p[6] == 0x22, ESP_ERR_INVALID_ARG, TAG,
                                "HID descriptor does not point to a report descriptor");
            const uint16_t report_len = (uint16_t)p[7] | ((uint16_t)p[8] << 8);
            ESP_RETURN_ON_FALSE(report_len == sizeof(usb_hid_report_descriptor),
                                ESP_ERR_INVALID_SIZE, TAG,
                                "HID wDescriptorLength=%u, report array is %u bytes",
                                report_len, (unsigned)sizeof(usb_hid_report_descriptor));
            saw_hid = true;
        } else if (type == TUSB_DESC_ENDPOINT) {
            ESP_RETURN_ON_FALSE(len == 7, ESP_ERR_INVALID_SIZE, TAG,
                                "endpoint descriptor length is %u", len);
            ESP_RETURN_ON_FALSE(p[2] == 0x81, ESP_ERR_INVALID_ARG, TAG,
                                "endpoint address must be 0x81 (IN 1)");
            ESP_RETURN_ON_FALSE((p[3] & 0x03) == 0x03, ESP_ERR_INVALID_ARG, TAG,
                                "endpoint must be interrupt type");
            const uint16_t mps = (uint16_t)p[4] | ((uint16_t)p[5] << 8);
            ESP_RETURN_ON_FALSE(mps == 8, ESP_ERR_INVALID_ARG, TAG,
                                "endpoint wMaxPacketSize must be 8, is %u", mps);
            ESP_RETURN_ON_FALSE(p[6] >= 1, ESP_ERR_INVALID_ARG, TAG,
                                "endpoint bInterval must be >= 1");
            ESP_RETURN_ON_FALSE(p + len == end, ESP_ERR_INVALID_ARG, TAG,
                                "endpoint descriptor must be the last descriptor");
            saw_endpoint = true;
        }

        p += len;
    }

    ESP_RETURN_ON_FALSE(p == end, ESP_ERR_INVALID_SIZE, TAG,
                        "descriptor chain does not end at wTotalLength");
    ESP_RETURN_ON_FALSE(saw_interface, ESP_ERR_INVALID_ARG, TAG,
                        "interface descriptor missing");
    ESP_RETURN_ON_FALSE(saw_hid, ESP_ERR_INVALID_ARG, TAG,
                        "HID descriptor missing");
    ESP_RETURN_ON_FALSE(saw_endpoint, ESP_ERR_INVALID_ARG, TAG,
                        "endpoint descriptor missing");

    ESP_RETURN_ON_FALSE(sizeof(usb_hid_report_descriptor) >= 20 &&
                        sizeof(usb_hid_report_descriptor) <= 255,
                        ESP_ERR_INVALID_SIZE, TAG,
                        "report descriptor length is implausible");
    ESP_RETURN_ON_FALSE(usb_hid_report_descriptor[0] == 0x05 &&
                        usb_hid_report_descriptor[1] == 0x01 &&
                        usb_hid_report_descriptor[2] == 0x09 &&
                        usb_hid_report_descriptor[3] == 0x06,
                        ESP_ERR_INVALID_ARG, TAG,
                        "report descriptor does not start with Generic Desktop/Keyboard");
    ESP_RETURN_ON_FALSE(usb_hid_report_descriptor[sizeof(usb_hid_report_descriptor) - 1] == 0xC0,
                        ESP_ERR_INVALID_ARG, TAG,
                        "report descriptor does not end with End Collection");
    if (sizeof(usb_hid_report_descriptor) != 63 &&
        sizeof(usb_hid_report_descriptor) != 65) {
        ESP_LOGW(TAG, "report descriptor is %u bytes, not the known 63/65-byte "
                      "boot keyboard layout; make sure it still produces an 8-byte input report",
                 (unsigned)sizeof(usb_hid_report_descriptor));
    }

    ESP_RETURN_ON_FALSE(usb_string_descriptor_count >= 4,
                        ESP_ERR_INVALID_ARG, TAG,
                        "manufacturer/product/configuration strings are missing");
    ESP_RETURN_ON_FALSE((uint8_t)usb_string_descriptors[0][0] == 0x09 &&
                        (uint8_t)usb_string_descriptors[0][1] == 0x04,
                        ESP_ERR_INVALID_ARG, TAG,
                        "string 0 must contain LangID 0x0409");

    ESP_LOGI(TAG, "descriptor check passed: config=%u bytes, HID report=%u bytes",
             (unsigned)sizeof(usb_configuration_descriptor),
             (unsigned)sizeof(usb_hid_report_descriptor));
    return ESP_OK;
}
