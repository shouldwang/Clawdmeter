#pragma once
#include <stdint.h>

// USB HID keyboard — replaces the BLE HID keyboard the physical buttons used
// to drive. Only boards with native USB-OTG (BOARD_HAS_USB_HID, see
// hal/board_caps.h's has_usb_hid) actually send key events; on boards without
// it (e.g. ESP32-C6, which has USB-Serial-JTAG only) these are no-ops.

void usb_hid_init(void);

// Secondary-button action: Shift+Tab (mode toggle). No plain-key press is
// exposed here — the primary button now drives ui_cycle_screen() locally
// instead of an HID keystroke.
void usb_hid_press_shift_tab(void);
void usb_hid_release(void);

// True while a USB host has the port open (S3: TinyUSB/CDC DTR-equivalent
// connection state; boards without USB HID report Serial's own connection
// state, which is "always true" on chips that fall back to UART).
bool usb_hid_link_up(void);
