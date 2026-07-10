#include "gif_player.h"

// Only wired up on boards built with the GIF decoder enabled (currently
// waveshare_amoled_216 — see gif_player.h). On every other board this whole
// file compiles down to the stub block at the bottom.
#if LV_USE_GIF

#include <Arduino.h>
#include <string.h>
#include <stdlib.h>

// lv_image_cache_drop() lives outside the lvgl.h public surface.
#include <src/misc/cache/instance/lv_image_cache.h>

// The engine's C API declarations carry no extern "C" guards of their own.
extern "C" {
#include <src/libs/gif/AnimatedGIF.h>
}

static lv_obj_t*      gif_img;
static lv_timer_t*    gif_timer;
static GIFIMAGE*      gif;       // ~24KB, heap'd (lands in PSRAM), not BSS
static uint8_t*       framebuf;  // W*H 8-bit index canvas + W*H*2 cooked RGB565
static lv_image_dsc_t frame_dsc;

static void render_next_frame(lv_timer_t* t) {
    // Safety net: never burn CPU decoding while the widget can't be seen
    // (same guard lv_gif's auto_pause_invisible provided).
    if (!gif_img || !lv_obj_is_visible(gif_img)) {
        lv_timer_pause(t);
        return;
    }

    int delay_ms = 0;
    uint32_t decode_started = millis();
    int rc = GIF_playFrame(gif, &delay_ms, NULL);
    uint32_t decode_ms = millis() - decode_started;
    if (rc < 0) {
        Serial.printf("gif_player: playFrame error %d\n", GIF_getLastError(gif));
        gif_player_stop();
        return;
    }
    // rc == 0 means the last frame just played — the engine seeks back to the
    // start on the next call, so keep the timer running to loop forever.
    if (delay_ms < 10) delay_ms = 10;
    // Temporary diagnostic (see docs/plans/usb-transport-lightbox.md Phase 4):
    // only prints when decode time eats a meaningful share of the frame
    // budget, so it doubles as a live "are we dropping frames" signal without
    // flooding serial on the common case.
    if (decode_ms * 2 >= (uint32_t)delay_ms) {
        Serial.printf("gif_player: decode %ums (budget %dms)\n", decode_ms, delay_ms);
    }
    lv_timer_set_period(t, delay_ms);
    lv_image_cache_drop(&frame_dsc);
    lv_obj_invalidate(gif_img);
}

void gif_player_attach(lv_obj_t* img) {
    gif_img = img;
}

bool gif_player_open(const char* path) {
    gif_player_stop();
    if (!gif_img) return false;

    gif = (GIFIMAGE*)calloc(1, sizeof(GIFIMAGE));
    if (!gif) return false;
    GIF_begin(gif, GIF_PALETTE_RGB565_LE);
    if (!GIF_openFile(gif, path, NULL)) {
        Serial.printf("gif_player: open %s failed (err %d)\n", path, GIF_getLastError(gif));
        free(gif);
        gif = NULL;
        return false;
    }

    int w = GIF_getCanvasWidth(gif);
    int h = GIF_getCanvasHeight(gif);
    framebuf = (uint8_t*)calloc(1, (size_t)w * h * 3);
    if (!framebuf) {
        Serial.printf("gif_player: %dx%d framebuffer alloc failed\n", w, h);
        GIF_close(gif);
        free(gif);
        gif = NULL;
        return false;
    }
    // COOKED + a frame buffer + no draw callback: the engine composites each
    // frame into the 8-bit index canvas at framebuf[0..w*h) and maintains a
    // complete RGB565 rendition of it right behind, at framebuf + w*h.
    //
    // Turbo mode (pTurboBuffer) was tried and reverted here 2026-07-10: it
    // measurably cut decode time (a meme that cost 45ms/90ms of frame budget
    // dropped to no measurable steady-state cost), but it visibly corrupted
    // playback on hardware. Root cause: DecodeLZWTurbo's COOKED compositing
    // writes each converted scanline into an ephemeral scratch slot inside
    // pTurboBuffer (reused every line), then hands it to a pfnDraw callback
    // — unlike this no-callback path, which writes straight into the
    // *persistent* pFrameBuffer. For disposal 0/1/3 "keep as-is" transparent
    // pixels, DrawCooked deliberately skips writing so the destination's
    // existing value shows through — that's correct when the destination is
    // persistent (here), but in turbo mode the destination is the scratch
    // buffer, which holds leftover LZW-dictionary garbage, not the true
    // previous-frame pixel. Any callback that relays the scratch line
    // verbatim (as ours did) bakes that garbage into transparent regions.
    // Turbo mode would only be safe on GIFs pre-processed to have no
    // inter-frame transparency dependency (gifsicle --unoptimize/coalesce —
    // see docs/plans/usb-transport-lightbox.md Phase 4), which none of our
    // current memes are. Not worth the fragility for now: revisit only
    // after the asset pipeline can guarantee coalesced input.
    gif->ucDrawType   = GIF_DRAW_COOKED;
    gif->pFrameBuffer = framebuf;

    memset(&frame_dsc, 0, sizeof frame_dsc);
    frame_dsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
    frame_dsc.header.cf     = LV_COLOR_FORMAT_RGB565;
    frame_dsc.header.w      = w;
    frame_dsc.header.h      = h;
    frame_dsc.header.stride = w * 2;
    frame_dsc.data_size     = (uint32_t)w * h * 2;
    frame_dsc.data          = framebuf + (size_t)w * h;
    lv_image_set_src(gif_img, &frame_dsc);

    // Play the first frame HERE, not through render_next_frame(): the caller
    // only unhides the widget after this function succeeds, so the timer
    // callback's visibility guard would pause the timer before the first
    // frame ever rendered — and nothing would ever resume it.
    int delay_ms = 0;
    uint32_t decode_started = millis();
    int rc = GIF_playFrame(gif, &delay_ms, NULL);
    uint32_t decode_ms = millis() - decode_started;
    if (rc < 0) {
        Serial.printf("gif_player: first frame of %s failed (err %d)\n",
                      path, GIF_getLastError(gif));
        gif_player_stop();
        return false;
    }
    Serial.printf("gif_player: %s first frame decode %ums (budget %dms)\n",
                  path, decode_ms, delay_ms);
    if (delay_ms < 10) delay_ms = 10;

    if (!gif_timer) gif_timer = lv_timer_create(render_next_frame, delay_ms, NULL);
    else            lv_timer_set_period(gif_timer, delay_ms);
    lv_timer_resume(gif_timer);
    lv_timer_reset(gif_timer);
    lv_image_cache_drop(&frame_dsc);
    lv_obj_invalidate(gif_img);
    return true;
}

void gif_player_stop(void) {
    if (gif_timer) lv_timer_pause(gif_timer);
    if (gif_img && lv_image_get_src(gif_img) == &frame_dsc) {
        lv_image_set_src(gif_img, NULL);
    }
    lv_image_cache_drop(&frame_dsc);
    if (gif) {
        GIF_close(gif);
        free(gif);
        gif = NULL;
    }
    if (framebuf) {
        free(framebuf);
        framebuf = NULL;
    }
}

#else  // !LV_USE_GIF — stub for boards without the GIF decoder enabled

void gif_player_attach(lv_obj_t*) {}
bool gif_player_open(const char*) { return false; }
void gif_player_stop(void) {}

#endif
