#include "gif_player.h"

// Only wired up on boards built with the upstream GIF player enabled
// (currently waveshare_amoled_216). On every other board this whole file
// compiles down to the stub block at the bottom.
#if CLAWDMETER_USE_GIF_PLAYER

#include <Arduino.h>
#include <AnimatedGIF.h>
#include <SPIFFS.h>
#include <esp_heap_caps.h>
#include <new>
#include <string.h>

// lv_image_cache_drop() lives outside the lvgl.h public surface.
#include <src/misc/cache/instance/lv_image_cache.h>

static lv_obj_t*      gif_img;
static lv_timer_t*    gif_timer;
static AnimatedGIF*   gif;       // large decoder state, allocated per open
static uint8_t*       filebuf;   // whole GIF file, preloaded into PSRAM
static uint8_t*       framebuf;  // W*H 8-bit canvas + W*H*2 cooked RGB565
static uint8_t*       turbobuf;  // W*H index + upstream TURBO_BUFFER_SIZE
static int            frame_delay_ms = 10;
static lv_image_dsc_t frame_dsc;

static const char* spiffs_path_from_lvgl_path(const char* path) {
    if (!path) return "";
    if (path[0] == 'S' && path[1] == ':') return path + 2;
    return path;
}

static void* psram_calloc(size_t bytes) {
    return heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

// Decode scratch only (not persistent, not handed to LVGL) -- keep it off
// PSRAM so the turbo path doesn't pay PSRAM access latency (see Phase 4 GIF
// player investigation, docs/plans/usb-transport-lightbox.md).
static void* sram_calloc(size_t bytes) {
    return heap_caps_calloc(1, bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

static void destroy_decoder(void) {
    if (gif) {
        gif->close();
        delete gif;
        gif = NULL;
    }
}

static bool play_frame(bool first_frame, const char* source_path) {
    if (!gif || !gif_img) return false;

    int delay_ms = 0;
    uint32_t decode_started = millis();
    int rc = gif->playFrame(false, &delay_ms, NULL);
    uint32_t decode_ms = millis() - decode_started;
    int err = gif->getLastError();

    if (rc < 0 || (rc == 0 && err != GIF_SUCCESS && err != GIF_EMPTY_FRAME)) {
        Serial.printf("gif_player: playFrame error %d\n", err);
        gif_player_stop();
        return false;
    }

    if (rc == 0 && err == GIF_EMPTY_FRAME) {
        if (first_frame) {
            Serial.printf("gif_player: first frame of %s is empty\n", source_path);
            gif_player_stop();
            return false;
        }
        gif->reset();
        frame_delay_ms = 10;
        if (gif_timer) lv_timer_set_period(gif_timer, frame_delay_ms);
        return true;
    }

    if (delay_ms < 10) delay_ms = 10;
    frame_delay_ms = delay_ms;
    if (first_frame) {
        Serial.printf("gif_player: %s first frame decode %ums (budget %dms)\n",
                      source_path, decode_ms, delay_ms);
    } else if (decode_ms * 2 >= (uint32_t)delay_ms) {
        Serial.printf("gif_player: decode %ums (budget %dms)\n", decode_ms, delay_ms);
    }

    if (gif_timer) lv_timer_set_period(gif_timer, frame_delay_ms);
    lv_image_cache_drop(&frame_dsc);
    lv_obj_invalidate(gif_img);

    if (rc == 0) {
        gif->reset();
    }
    return true;
}

static void render_next_frame(lv_timer_t* t) {
    // Safety net: never burn CPU decoding while the widget can't be seen
    // (same guard lv_gif's auto_pause_invisible provided).
    if (!gif_img || !lv_obj_is_visible(gif_img)) {
        lv_timer_pause(t);
        return;
    }

    play_frame(false, NULL);
}

void gif_player_attach(lv_obj_t* img) {
    gif_img = img;
}

bool gif_player_open(const char* path) {
    gif_player_stop();
    if (!gif_img || !path) return false;

    gif = new (std::nothrow) AnimatedGIF();
    if (!gif) {
        Serial.println("gif_player: decoder alloc failed");
        return false;
    }

    File file = SPIFFS.open(spiffs_path_from_lvgl_path(path), FILE_READ);
    if (!file) {
        Serial.printf("gif_player: open %s failed\n", path);
        destroy_decoder();
        return false;
    }
    size_t file_size = file.size();
    filebuf = (uint8_t*)psram_calloc(file_size);
    if (!filebuf) {
        Serial.printf("gif_player: %s file buffer alloc (%u bytes) failed\n", path, (unsigned)file_size);
        file.close();
        destroy_decoder();
        return false;
    }
    size_t bytes_read = file.read(filebuf, file_size);
    file.close();
    if (bytes_read != file_size) {
        Serial.printf("gif_player: %s short read (%u of %u bytes)\n",
                      path, (unsigned)bytes_read, (unsigned)file_size);
        gif_player_stop();
        return false;
    }

    gif->begin(GIF_PALETTE_RGB565_LE);
    if (!gif->open(filebuf, (int)file_size, NULL)) {
        Serial.printf("gif_player: open %s failed (err %d)\n", path, gif->getLastError());
        gif_player_stop();
        return false;
    }

    int w = gif->getCanvasWidth();
    int h = gif->getCanvasHeight();
    if (w <= 0 || h <= 0) {
        Serial.printf("gif_player: invalid canvas %dx%d for %s\n", w, h, path);
        gif_player_stop();
        return false;
    }

    size_t canvas_px = (size_t)w * (size_t)h;
    size_t frame_size = canvas_px * 3;
    framebuf = (uint8_t*)psram_calloc(frame_size);
    if (!framebuf) {
        Serial.printf("gif_player: %dx%d framebuffer alloc (%u bytes) failed\n",
                      w, h, (unsigned)frame_size);
        gif_player_stop();
        return false;
    }

    size_t turbo_size = canvas_px + TURBO_BUFFER_SIZE;
    turbobuf = (uint8_t*)sram_calloc(turbo_size);
    if (!turbobuf) {
        Serial.printf("gif_player: %dx%d turbo buffer alloc (%u bytes) failed\n",
                      w, h, (unsigned)turbo_size);
        gif_player_stop();
        return false;
    }

    gif->setDrawType(GIF_DRAW_COOKED);
    gif->setFrameBuf(framebuf);
    gif->setTurboBuf(turbobuf);

    memset(&frame_dsc, 0, sizeof frame_dsc);
    frame_dsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
    frame_dsc.header.cf     = LV_COLOR_FORMAT_RGB565;
    frame_dsc.header.w      = w;
    frame_dsc.header.h      = h;
    frame_dsc.header.stride = w * 2;
    frame_dsc.data_size     = (uint32_t)(canvas_px * 2);
    frame_dsc.data          = framebuf + canvas_px;
    lv_image_set_src(gif_img, &frame_dsc);

    // Play the first frame here, not through render_next_frame(): the caller
    // only unhides the widget after this function succeeds, so the timer
    // callback's visibility guard would pause before the first frame rendered.
    if (!play_frame(true, path)) return false;

    if (!gif_timer) gif_timer = lv_timer_create(render_next_frame, frame_delay_ms, NULL);
    else            lv_timer_set_period(gif_timer, frame_delay_ms);
    lv_timer_resume(gif_timer);
    lv_timer_reset(gif_timer);
    return true;
}

void gif_player_stop(void) {
    if (gif_timer) lv_timer_pause(gif_timer);
    if (gif_img && lv_image_get_src(gif_img) == &frame_dsc) {
        lv_image_set_src(gif_img, NULL);
    }
    lv_image_cache_drop(&frame_dsc);
    destroy_decoder();
    if (filebuf) {
        heap_caps_free(filebuf);
        filebuf = NULL;
    }
    if (framebuf) {
        heap_caps_free(framebuf);
        framebuf = NULL;
    }
    if (turbobuf) {
        heap_caps_free(turbobuf);
        turbobuf = NULL;
    }
    memset(&frame_dsc, 0, sizeof frame_dsc);
}

#else  // !CLAWDMETER_USE_GIF_PLAYER

void gif_player_attach(lv_obj_t*) {}
bool gif_player_open(const char*) { return false; }
void gif_player_stop(void) {}

#endif
