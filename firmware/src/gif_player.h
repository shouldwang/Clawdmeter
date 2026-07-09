#pragma once

#include <lvgl.h>

// Lightbox GIF playback via the AnimatedGIF engine that ships inside LVGL,
// driven directly in GIF_DRAW_COOKED mode instead of through the lv_gif
// widget. lv_gif re-implements frame compositing in its own blend layer and
// mishandles transparency (transparent pixels punch alpha holes into the
// canvas instead of keeping the previous frame's pixels), which black-screens
// any inter-frame-optimized GIF — e.g. ffmpeg's default "transdiff" output.
// COOKED mode lets the engine composite frames itself (transparency,
// disposal, local palettes) and hands us a complete RGB565 canvas per frame.
// Approach borrowed from github.com/Glebka/esp32-animated-badge; details in
// docs/plans/usb-transport-lightbox.md Phase 4.
//
// Stubs to no-ops on boards built without the GIF decoder (mirrors memefs.h)
// so shared ui.cpp code never needs a #ifdef BOARD_*.

void gif_player_attach(lv_obj_t* img);   // lv_image widget frames render into
bool gif_player_open(const char* path);  // LVGL fs path, e.g. "S:/memes/x.gif"
void gif_player_stop(void);              // close file, free buffers, pause timer
