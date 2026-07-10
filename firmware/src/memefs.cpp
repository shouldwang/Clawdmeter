#include "memefs.h"

// Only wired up on boards built with the PNG decoder enabled (currently
// waveshare_amoled_216 — see memefs.h). GIF files are indexed here even
// though playback is handled outside LVGL by gif_player.cpp. On every other
// board this whole file compiles down to the stub block at the bottom, so
// shared ui.cpp code never needs a #ifdef BOARD_* to call these functions.
#if LV_USE_LODEPNG

#include <Arduino.h>
#include <SPIFFS.h>
#include <lvgl.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define MEMEFS_LETTER 'S'
#define MEMES_DIR     "/memes"
#define MAX_MEMES     64
#define MAX_NAME_LEN  64

static char meme_names[MAX_MEMES][MAX_NAME_LEN];  // bare filenames, sorted
static int  meme_count = 0;

static bool has_ext(const char* name, const char* ext) {
    size_t nlen = strlen(name);
    size_t elen = strlen(ext);
    if (nlen < elen) return false;
    for (size_t i = 0; i < elen; i++) {
        if (tolower((unsigned char)name[nlen - elen + i]) != ext[i]) return false;
    }
    return true;
}

static int cmp_names(const void* a, const void* b) {
    return strcmp((const char*)a, (const char*)b);
}

void memefs_rescan(void) {
    meme_count = 0;

    // SPIFFS is a flat filesystem — there are no real directory entries, so
    // "opening /memes" and expecting openNextFile() to scope to that subtree
    // is not reliable. The documented-safe pattern is to walk the root and
    // filter by path prefix instead.
    File root = SPIFFS.open("/");
    if (!root || !root.isDirectory()) {
        Serial.println("memefs: SPIFFS root open failed");
        return;
    }

    const size_t prefix_len = strlen(MEMES_DIR "/");
    File entry = root.openNextFile();
    while (entry && meme_count < MAX_MEMES) {
        if (!entry.isDirectory()) {
            // File::name() (Arduino-ESP32 3.x) returns only the basename —
            // path() is the one that returns the full "/memes/xxx.png" path
            // needed for the prefix match below (confirmed by reading
            // vfs_api.cpp: name() = pathToFileName(path())).
            const char* full = entry.path();
            char full_abs[LV_FS_MAX_PATH_LENGTH];
            if (full[0] != '/') {
                snprintf(full_abs, sizeof(full_abs), "/%s", full);
            } else {
                strncpy(full_abs, full, sizeof(full_abs) - 1);
                full_abs[sizeof(full_abs) - 1] = '\0';
            }

            if (strncmp(full_abs, MEMES_DIR "/", prefix_len) == 0) {
                const char* base = full_abs + prefix_len;
                if (has_ext(base, ".png") || has_ext(base, ".gif")) {
                    strncpy(meme_names[meme_count], base, MAX_NAME_LEN - 1);
                    meme_names[meme_count][MAX_NAME_LEN - 1] = '\0';
                    meme_count++;
                }
            }
        }
        entry = root.openNextFile();
    }

    qsort(meme_names, meme_count, MAX_NAME_LEN, cmp_names);
    Serial.printf("memefs: found %d meme(s) in %s\n", meme_count, MEMES_DIR);
}

int memefs_count(void) {
    return meme_count;
}

const char* memefs_path(int i) {
    static char path_buf[LV_FS_MAX_PATH_LENGTH];
    if (i < 0 || i >= meme_count) return NULL;
    snprintf(path_buf, sizeof(path_buf), "%c:%s/%s", MEMEFS_LETTER, MEMES_DIR, meme_names[i]);
    return path_buf;
}

bool memefs_is_gif(int i) {
    if (i < 0 || i >= meme_count) return false;
    return has_ext(meme_names[i], ".gif");
}

// ---- LVGL fs driver, backed by Arduino SPIFFS ----
// Mirrors LVGL's own lv_fs_arduino_esp_littlefs.cpp driver (same File-based
// API), swapped to SPIFFS since that's the partition this board already has
// free (see docs/plans/usb-transport-lightbox.md "已查證的技術前提").

struct MemefsFile {
    File file;
};

static void* fs_open_cb(lv_fs_drv_t* drv, const char* path, lv_fs_mode_t mode) {
    LV_UNUSED(drv);
    if (mode != LV_FS_MODE_RD) return NULL;  // read-only driver

    // LVGL has already stripped the "S:" prefix by this point — `path` is
    // e.g. "/memes/01_busy.png", ready to hand straight to SPIFFS.
    File f = SPIFFS.open(path, FILE_READ);
    if (!f) return NULL;

    MemefsFile* mf = new MemefsFile{f};
    return (void*)mf;
}

static lv_fs_res_t fs_close_cb(lv_fs_drv_t* drv, void* file_p) {
    LV_UNUSED(drv);
    MemefsFile* mf = (MemefsFile*)file_p;
    mf->file.close();
    delete mf;
    return LV_FS_RES_OK;
}

static lv_fs_res_t fs_read_cb(lv_fs_drv_t* drv, void* file_p, void* buf, uint32_t btr, uint32_t* br) {
    LV_UNUSED(drv);
    MemefsFile* mf = (MemefsFile*)file_p;
    *br = mf->file.read((uint8_t*)buf, btr);
    return ((int32_t)(*br) < 0) ? LV_FS_RES_UNKNOWN : LV_FS_RES_OK;
}

static lv_fs_res_t fs_seek_cb(lv_fs_drv_t* drv, void* file_p, uint32_t pos, lv_fs_whence_t whence) {
    LV_UNUSED(drv);
    MemefsFile* mf = (MemefsFile*)file_p;
    SeekMode mode = SeekSet;
    if (whence == LV_FS_SEEK_CUR) mode = SeekCur;
    else if (whence == LV_FS_SEEK_END) mode = SeekEnd;
    return mf->file.seek(pos, mode) ? LV_FS_RES_OK : LV_FS_RES_UNKNOWN;
}

static lv_fs_res_t fs_tell_cb(lv_fs_drv_t* drv, void* file_p, uint32_t* pos_p) {
    LV_UNUSED(drv);
    MemefsFile* mf = (MemefsFile*)file_p;
    *pos_p = mf->file.position();
    return LV_FS_RES_OK;
}

static void register_fs_driver(void) {
    static lv_fs_drv_t drv;
    lv_fs_drv_init(&drv);
    drv.letter = MEMEFS_LETTER;
    drv.open_cb = fs_open_cb;
    drv.close_cb = fs_close_cb;
    drv.read_cb = fs_read_cb;
    drv.tell_cb = fs_tell_cb;
    drv.seek_cb = fs_seek_cb;
    lv_fs_drv_register(&drv);
}

void memefs_init(void) {
    if (!SPIFFS.begin(false)) {  // false: do not format on mount failure
        Serial.println("memefs: SPIFFS mount failed");
        return;
    }
    register_fs_driver();
    memefs_rescan();
}

#else  // !LV_USE_LODEPNG — stub for boards without lightbox media enabled

void memefs_init(void) {}
void memefs_rescan(void) {}
int  memefs_count(void) { return 0; }
const char* memefs_path(int) { return nullptr; }
bool memefs_is_gif(int) { return false; }

#endif
