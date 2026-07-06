/*
 * SPDX-FileCopyrightText: 2019 Ha Thach (tinyusb.org)
 *
 * SPDX-License-Identifier: MIT
 *
 * SPDX-FileContributor: 2022-2024 Espressif Systems (Shanghai) CO LTD
 * 
 * Adaptado para ESP32 E-Drum Trigger - Descriptores USB MIDI
 */

#ifndef USB_DESCRIPTORS_H_
#define USB_DESCRIPTORS_H_

#include "tusb.h"

#ifdef __cplusplus
extern "C" {
#endif

// Device descriptor
extern const tusb_desc_device_t desc_device;

// Configuration descriptors
extern const uint8_t desc_fs_configuration[];
#if (TUD_OPT_HIGH_SPEED)
extern const uint8_t desc_hs_configuration[];
#endif

// String descriptors
extern const char* string_desc_arr[];

#ifdef __cplusplus
}
#endif

#endif /* USB_DESCRIPTORS_H_ */