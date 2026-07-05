#include "usb_hid.h"
#include <Arduino.h>
#include "hal/board_caps.h"

// USBHIDKeyboard.h self-guards on SOC_USB_OTG_SUPPORTED / CONFIG_TINYUSB_HID_ENABLED
// (empty header on chips without it), so this include is safe on every board —
// including ESP32-C6, which has USB-Serial-JTAG only and no USB-OTG peripheral.
#include "soc/soc_caps.h"
#include "USB.h"
#include "USBHIDKeyboard.h"

#if SOC_USB_OTG_SUPPORTED && CONFIG_TINYUSB_HID_ENABLED

static USBHIDKeyboard keyboard;
static bool s_started = false;

void usb_hid_init(void) {
    if (!board_caps().has_usb_hid) return;  // e.g. an S3 port that opts out
    USB.begin();
    keyboard.begin();
    s_started = true;
}

void usb_hid_press_shift_tab(void) {
    if (!s_started) return;
    keyboard.press(KEY_LEFT_SHIFT);
    keyboard.press(KEY_TAB);
}

void usb_hid_release(void) {
    if (!s_started) return;
    keyboard.releaseAll();
}

#else

// No USB-OTG on this SoC (ESP32-C6: USB-Serial-JTAG only, can't do TinyUSB
// HID). Keyboard actions degrade to no-ops — see BOARD_HAS_USB_HID.
void usb_hid_init(void) {}
void usb_hid_press_shift_tab(void) {}
void usb_hid_release(void) {}

#endif

// Serial itself reports host-connection state on both transports: USBCDC's
// operator bool() reflects DTR on TinyUSB boards, HWCDC's reflects the
// USB-Serial-JTAG connection on C6. Same expression works everywhere.
bool usb_hid_link_up(void) {
    return (bool)Serial;
}
