#pragma once
#include <stdint.h>

// Meme image source for the lightbox screen (docs/plans/usb-transport-lightbox.md
// Phase 4). Backed by the device's built-in flash SPIFFS partition, not a TF
// card — see that doc for why. Only wired up on boards built with
// LV_USE_LODEPNG + LV_USE_GIF (currently waveshare_amoled_216); on any other
// board this reports zero memes so the lightbox screen falls back to its
// empty state instead of failing to compile.
//
// Registers an LVGL fs driver on the 'S' letter, so paths returned by
// memefs_path() are ready to hand straight to lv_image_set_src /
// lv_gif_set_src.
void memefs_init(void);       // mount SPIFFS, register the LVGL fs driver, scan /memes/
void memefs_rescan(void);     // re-scan /memes/ for the current file list
int  memefs_count(void);      // number of memes found (0 if none / mount failed)
// Full "S:/memes/xxx.png" path for index i (0 <= i < memefs_count()).
// Returns NULL if i is out of range.
const char* memefs_path(int i);
bool memefs_is_gif(int i);    // true if the file at index i is a .gif (vs .png)
