#pragma once

#include <lvgl.h>

#ifndef CLAWDMETER_USE_GIF_PLAYER
#define CLAWDMETER_USE_GIF_PLAYER 0
#endif

// Lightbox GIF playback via upstream bitbank2/AnimatedGIF, driven directly in
// GIF_DRAW_COOKED mode instead of through LVGL's bundled lv_gif widget. The
// decoder owns GIF compositing (transparency, disposal, local palettes) and
// hands LVGL a complete RGB565 canvas per frame. Details in
// docs/plans/usb-transport-lightbox.md Phase 4.
//
// Stubs to no-ops on boards built without the GIF decoder (mirrors memefs.h)
// so shared ui.cpp code never needs a #ifdef BOARD_*.

void gif_player_attach(lv_obj_t* img);   // lv_image widget frames render into
bool gif_player_open(const char* path);  // LVGL fs path, e.g. "S:/memes/x.gif"
void gif_player_stop(void);              // close file, free buffers, pause timer
