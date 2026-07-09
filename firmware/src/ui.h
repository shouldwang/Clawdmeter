#pragma once
#include "data.h"

enum screen_t {
    SCREEN_SPLASH,
    SCREEN_USAGE,
    SCREEN_LIGHTBOX,   // placeholder screen for now — see docs/plans/usb-transport-lightbox.md
    SCREEN_STOCK,
    SCREEN_COUNT,
};

void ui_init(void);
void ui_update(const UsageData* data);
void ui_tick_anim(void);
void ui_show_screen(screen_t screen);
void ui_toggle_splash(void);
// Cycles splash -> usage -> lightbox -> stock -> splash. Driven by the
// PRIMARY button.
void ui_cycle_screen(void);
screen_t ui_get_current_screen(void);
void ui_update_usb_status(bool connected);
void ui_stock_next(void);   // advances the stock-ticker screen to the next symbol
