/*
 * SPDX-FileCopyrightText: 2019 Ha Thach (tinyusb.org)
 *
 * SPDX-License-Identifier: MIT
 *
 * SPDX-FileContributor: 2022-2024 Espressif Systems (Shanghai) CO LTD
 * 
 * Adaptado para ESP32 E-Drum Trigger - Descriptores USB MIDI
 */

#include "tusb.h"
#include "sdkconfig.h"
#include <string.h>

/** Helper defines **/

// Interface counter
enum interface_count {
#if CFG_TUD_MIDI
    ITF_NUM_MIDI = 0,
    ITF_NUM_MIDI_STREAMING,
#endif
#if CFG_TUD_CDC
    ITF_NUM_CDC_COM,
    ITF_NUM_CDC_DATA,
#endif
    ITF_COUNT
};

// USB Endpoint numbers
enum usb_endpoints {
    // Available USB Endpoints: 5 IN/OUT EPs and 1 IN EP
    EP_EMPTY = 0,
#if CFG_TUD_MIDI
    EPNUM_MIDI,
#endif
#if CFG_TUD_CDC
    EPNUM_CDC_NOTIF,
    EPNUM_CDC_OUT,
    EPNUM_CDC_IN,
#endif
};

/** TinyUSB descriptors **/

#define TUSB_DESCRIPTOR_TOTAL_LEN (TUD_CONFIG_DESC_LEN \
    + CFG_TUD_MIDI * TUD_MIDI_DESC_LEN \
    + CFG_TUD_CDC * TUD_CDC_DESC_LEN)

/**
 * @brief Device descriptor
 */
const tusb_desc_device_t desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0xCafe,  // Vendor ID temporal
    .idProduct = 0x4001, // Product ID para MIDI
    .bcdDevice = 0x0100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01
};

/**
 * @brief String descriptor
 */
const char* string_desc_arr[] = {
    // array of pointer to string descriptors
    (char[]){0x09, 0x04},        // 0: is supported language is English (0x0409)
    "ESP32 E-Drum",              // 1: Manufacturer
    "E-Drum MIDI Trigger",       // 2: Product
    "123456",                    // 3: Serials, should use chip ID
    "E-Drum MIDI Interface",     // 4: MIDI
#if CFG_TUD_CDC
    "E-Drum Serial Port",        // 5: CDC
#endif
};

/**
 * @brief Configuration descriptor
 *
 * Composite device: MIDI + CDC ACM (serial console over same USB-C)
 */
const uint8_t desc_fs_configuration[] = {
    // Configuration number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, ITF_COUNT, 0, TUSB_DESCRIPTOR_TOTAL_LEN, 0, 100),

    // MIDI: Interface number, string index, EP Out & EP In address, EP size
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 4, EPNUM_MIDI, (0x80 | EPNUM_MIDI), 64),

    // CDC: Interface number, string index, EP notification, notif size, EP Out, EP In, EP size
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_COM, 5, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
};

#if (TUD_OPT_HIGH_SPEED)
/**
 * @brief High Speed configuration descriptor
 *
 * This is a simple configuration descriptor that defines 1 configuration and a MIDI interface
 */
const uint8_t desc_hs_configuration[] = {
    // Configuration number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, ITF_COUNT, 0, TUSB_DESCRIPTOR_TOTAL_LEN, 0, 100),

    // Interface number, string index, EP Out & EP In address, EP size
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 4, EPNUM_MIDI, (0x80 | EPNUM_MIDI), 512),
};
#endif // TUD_OPT_HIGH_SPEED

// Note: Las funciones callback tud_descriptor_*_cb() son proporcionadas por esp_tinyusb
// Solo definimos los datos de descriptores aquí