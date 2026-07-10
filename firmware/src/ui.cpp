#include "ui.h"
#include "splash.h"
#include <lvgl.h>
#include <time.h>
#include "logo.h"
#include "icons.h"
#include "memefs.h"
#include "gif_player.h"
#include "hal/board_caps.h"

// Custom fonts (scaled for 314 PPI, ~1.9x from original 165 PPI)
LV_FONT_DECLARE(font_tiempos_56);
LV_FONT_DECLARE(font_tiempos_34);
LV_FONT_DECLARE(font_styrene_48);
LV_FONT_DECLARE(font_styrene_28);
LV_FONT_DECLARE(font_tiempos_60);
LV_FONT_DECLARE(font_styrene_96);
LV_FONT_DECLARE(font_styrene_36);
LV_FONT_DECLARE(font_styrene_24);
LV_FONT_DECLARE(font_styrene_20);
LV_FONT_DECLARE(font_styrene_16);
LV_FONT_DECLARE(font_styrene_14);
LV_FONT_DECLARE(font_mono_32);

// Layout values computed from the active board's geometry. Populated once
// in ui_init() and treated as const for the rest of the program. Adding a
// new display size means extending compute_layout() with another
// breakpoint — never editing the screen-builder functions below.
struct Layout {
    int16_t scr_w, scr_h;
    int16_t margin;
    int16_t title_y;
    int16_t content_y;
    int16_t content_w;

    // Usage screen
    int16_t usage_panel_h;
    int16_t usage_panel_gap;
    int16_t usage_bar_y;
    int16_t usage_reset_y;

    // Bluetooth screen
    int16_t bt_info_panel_h;
    int16_t bt_reset_zone_h;
    const lv_font_t* bt_title_font;
    const lv_font_t* bt_status_font;
    const lv_font_t* bt_device_font;
    const lv_font_t* bt_credit_1_font;
    const lv_font_t* bt_credit_2_font;
};
static Layout L = {};

// Pick layout values from the active board's pixel dimensions. The two
// existing boards happen to land on the two breakpoints below; new ports
// inherit the closer one — visually OK, may need a polish pass for
// pixel-perfect alignment but never blocks the port from booting.
static void compute_layout(const BoardCaps& c) {
    L.scr_w = c.width;
    L.scr_h = c.height;
    L.margin = 20;
    L.title_y = 30;

    if (c.height >= 460) {
        // Large layout — tuned for 480x480 (AMOLED-2.16).
        L.content_y = 100;
        L.usage_panel_h = 150;
        L.usage_panel_gap = 16;
        L.usage_bar_y = 56;
        L.usage_reset_y = 94;
        L.bt_info_panel_h = 160;
        L.bt_reset_zone_h = 110;
        L.bt_title_font    = &font_tiempos_56;
        L.bt_status_font   = &font_styrene_48;
        L.bt_device_font   = &font_styrene_28;
        L.bt_credit_1_font = &font_styrene_24;
        L.bt_credit_2_font = &font_styrene_20;
    } else {
        // Compact layout — tuned for 368x448 (AMOLED-1.8).
        L.content_y = 85;
        L.usage_panel_h = 130;
        L.usage_panel_gap = 12;
        L.usage_bar_y = 48;
        L.usage_reset_y = 78;
        L.bt_info_panel_h = 140;
        L.bt_reset_zone_h = 90;
        L.bt_title_font    = &font_tiempos_34;
        L.bt_status_font   = &font_styrene_28;
        L.bt_device_font   = &font_styrene_20;
        L.bt_credit_1_font = &font_styrene_16;
        L.bt_credit_2_font = &font_styrene_14;
    }

    L.content_w = L.scr_w - 2 * L.margin;
}

// Anthropic brand palette — design tokens live in theme.h
#include "theme.h"
#define COL_BG        THEME_BG
#define COL_PANEL     THEME_PANEL
#define COL_TEXT      THEME_TEXT
#define COL_DIM       THEME_DIM
#define COL_ACCENT    THEME_ACCENT
#define COL_GREEN     THEME_GREEN
#define COL_AMBER     THEME_AMBER
#define COL_RED       THEME_RED
#define COL_BAR_BG    THEME_BAR_BG

// ---- Usage screen widgets (single non-splash view) ----
static lv_obj_t* usage_container;
static lv_obj_t* lightbox_container;
static lv_obj_t* lightbox_img;    // lv_image, shown for the current .png meme
static lv_obj_t* lightbox_gif;    // lv_image fed by gif_player, for .gif memes
static lv_obj_t* lbl_lightbox_empty;
static int lightbox_index = 0;
static lv_obj_t* stock_container;
static lv_obj_t* lbl_stock_symbol;
static lv_obj_t* lbl_stock_price;
static lv_obj_t* lbl_stock_change;
static lv_obj_t* lbl_stock_empty;

static StockQuote known_stocks[MAX_STOCKS];
static int known_stock_count = 0;
static int stock_display_index = 0;
static lv_obj_t* lbl_title;
// Clock fed by the daemon: base epoch (local wall-clock seconds) + the lv_tick at
// which it landed, so the title ticks forward locally between 60s payloads.
static long     clock_base_epoch = 0;
static uint32_t clock_base_ms = 0;
static int      clock_fmt = 24;   // 12 or 24, set from the daemon payload
static int      clock_last_min = -1;   // last rendered minute; avoids redrawing the title every tick
static lv_obj_t* usage_group;   // the two usage panels — shown when connected
static lv_obj_t* pair_group;    // pairing hint — shown when disconnected
static lv_obj_t* bar_session;
static lv_obj_t* lbl_session_pct;
static lv_obj_t* lbl_session_label;
static lv_obj_t* lbl_session_reset;
static lv_obj_t* bar_weekly;
static lv_obj_t* lbl_weekly_pct;
static lv_obj_t* lbl_weekly_label;
static lv_obj_t* lbl_weekly_reset;
static lv_obj_t* panel_session = nullptr;
static lv_obj_t* panel_weekly = nullptr;
// Enterprise-only widgets inside panel_session
static lv_obj_t* lbl_session_pct_sym = nullptr;  // "%" in smaller font
static lv_obj_t* lbl_spending_desc = nullptr;     // "of your monthly budget"
static lv_obj_t* lbl_spending_status = nullptr;   // "Under pace" / "On pace" / "Over pace"
static lv_obj_t* lbl_anim;      // status line: connection state + whimsical idle

// ---- Profile badge (shared, on top — replaces the old battery indicator) ----
static lv_obj_t* who_badge;
static lv_obj_t* logo_img;

// ---- Data availability → which usage sub-view to show ----
// Keep the last valid usage visible across transient API/poll failures. The idle
// "Zzz" screen is only for a connected host that has supplied no data since boot;
// the pairing hint is shown when the USB host is down.
static lv_obj_t* idle_group;            // the "Zzz" idle screen
static uint32_t  last_data_ms = 0;      // clock sync base for the last valid update
static bool      data_received = false; // any valid update since boot
static int       view_state = -1;       // -1 unknown / 0 pair / 1 idle / 2 usage

// ---- Shared ----
static lv_image_dsc_t logo_dsc;
static screen_t current_screen = SCREEN_USAGE;
static bool     s_usb_connected = false;   // cached USB host-connection state
static uint32_t connected_at_ms = 0;       // when we last entered CONNECTED ("Connected" dwell)

// Animation state
static uint32_t anim_last_ms = 0;
static uint8_t anim_spinner_idx = 0;
static uint8_t anim_phase = 0;
static uint8_t anim_msg_idx = 0;
static uint32_t anim_msg_start = 0;
#define ANIM_MSG_MS     4000

static const char* const spinner_frames[] = {
    "\xC2\xB7", "\xE2\x9C\xBB", "\xE2\x9C\xBD",
    "\xE2\x9C\xB6", "\xE2\x9C\xB3", "\xE2\x9C\xA2",
};
#define SPINNER_COUNT 6
#define SPINNER_PHASES (2 * (SPINNER_COUNT - 1))  // 10: ping-pong 0..5..0

static const uint16_t spinner_ms[SPINNER_COUNT] = {
    260, 130, 130, 130, 130, 260,
};

static const char* const anim_messages[] = {
    "Accomplishing", "Elucidating", "Perusing",
    "Actioning", "Enchanting", "Philosophising",
    "Actualizing", "Envisioning", "Pondering",
    "Baking", "Finagling", "Pontificating",
    "Booping", "Flibbertigibbeting", "Processing",
    "Brewing", "Forging", "Puttering",
    "Calculating", "Forming", "Puzzling",
    "Cerebrating", "Frolicking", "Reticulating",
    "Channelling", "Generating", "Ruminating",
    "Churning", "Germinating", "Scheming",
    "Clauding", "Hatching", "Schlepping",
    "Coalescing", "Herding", "Shimmying",
    "Cogitating", "Honking", "Shucking",
    "Combobulating", "Hustling", "Simmering",
    "Computing", "Ideating", "Smooshing",
    "Concocting", "Imagining", "Spelunking",
    "Conjuring", "Incubating", "Spinning",
    "Considering", "Inferring", "Stewing",
    "Contemplating", "Jiving", "Sussing",
    "Cooking", "Manifesting", "Synthesizing",
    "Crafting", "Marinating", "Thinking",
    "Creating", "Meandering", "Tinkering",
    "Crunching", "Moseying", "Transmuting",
    "Deciphering", "Mulling", "Unfurling",
    "Deliberating", "Mustering", "Unravelling",
    "Determining", "Musing", "Vibing",
    "Discombobulating", "Noodling", "Wandering",
    "Divining", "Percolating", "Whirring",
    "Doing", "Wibbling",
    "Effecting", "Wizarding",
    "Working", "Wrangling",
};
#define ANIM_MSG_COUNT (sizeof(anim_messages) / sizeof(anim_messages[0]))

static lv_color_t pct_color(float pct) {
    if (pct >= 80.0f) return COL_RED;
    if (pct >= 50.0f) return COL_AMBER;
    return COL_GREEN;
}

static void format_reset_time(int mins, char* buf, size_t len) {
    if (mins < 0) {
        snprintf(buf, len, "---");
    } else if (mins < 60) {
        snprintf(buf, len, "Resets in %dm", mins);
    } else if (mins < 1440) {
        snprintf(buf, len, "Resets in %dh %dm", mins / 60, mins % 60);
    } else {
        snprintf(buf, len, "Resets in %dd %dh", mins / 1440, (mins % 1440) / 60);
    }
}

// Forward decls — callbacks defined near ui_show_screen below
static void global_click_cb(lv_event_t* e);

static lv_obj_t* make_panel(lv_obj_t* parent, int x, int y, int w, int h) {
    lv_obj_t* panel = lv_obj_create(parent);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_style_bg_color(panel, COL_PANEL, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_left(panel, 16, 0);
    lv_obj_set_style_pad_right(panel, 16, 0);
    lv_obj_set_style_pad_top(panel, 12, 0);
    lv_obj_set_style_pad_bottom(panel, 12, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_EVENT_BUBBLE);
    return panel;
}

static lv_obj_t* make_bar(lv_obj_t* parent, int x, int y, int w, int h) {
    lv_obj_t* bar = lv_bar_create(parent);
    lv_obj_set_pos(bar, x, y);
    lv_obj_set_size(bar, w, h);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, COL_BAR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, COL_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 6, LV_PART_INDICATOR);
    return bar;
}

static void init_icon_dsc_rgb565a8(lv_image_dsc_t* dsc, int w, int h, const uint8_t* data) {
    dsc->header.w = w;
    dsc->header.h = h;
    dsc->header.cf = LV_COLOR_FORMAT_RGB565A8;
    dsc->header.stride = w * 2;
    dsc->data = data;
    dsc->data_size = w * h * 3;
}

static lv_obj_t* make_pill(lv_obj_t* parent, const char* text) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &font_styrene_28, 0);
    lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
    lv_obj_set_style_bg_color(lbl, COL_BAR_BG, 0);
    lv_obj_set_style_bg_opa(lbl, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(lbl, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_left(lbl, 18, 0);
    lv_obj_set_style_pad_right(lbl, 18, 0);
    lv_obj_set_style_pad_top(lbl, 6, 0);
    lv_obj_set_style_pad_bottom(lbl, 6, 0);
    return lbl;
}

// ======== Usage Screen ========

static lv_obj_t* make_usage_panel(lv_obj_t* parent, int y, const char* pill_text,
                                  lv_obj_t** out_pct, lv_obj_t** out_pill,
                                  lv_obj_t** out_bar, lv_obj_t** out_reset) {
    lv_obj_t* panel = make_panel(parent, L.margin, y, L.content_w, L.usage_panel_h);

    *out_pct = lv_label_create(panel);
    lv_label_set_text(*out_pct, "---%");
    lv_obj_set_style_text_font(*out_pct, &font_styrene_48, 0);
    lv_obj_set_style_text_color(*out_pct, COL_TEXT, 0);
    lv_obj_set_pos(*out_pct, 0, 0);

    *out_pill = make_pill(panel, pill_text);
    lv_obj_align(*out_pill, LV_ALIGN_TOP_RIGHT, 0, 1);

    *out_bar = make_bar(panel, 0, L.usage_bar_y, L.content_w - 32, 24);

    *out_reset = lv_label_create(panel);
    lv_label_set_text(*out_reset, "---");
    lv_obj_set_style_text_font(*out_reset, &font_styrene_28, 0);
    lv_obj_set_style_text_color(*out_reset, COL_DIM, 0);
    lv_obj_set_pos(*out_reset, 0, L.usage_reset_y);

    return panel;
}

// USB-disconnected hint — shown when the host hasn't opened the serial port
// yet, so the screen isn't empty while waiting to be plugged in.
static void build_pair_group(lv_obj_t* parent) {
    pair_group = lv_obj_create(parent);
    lv_obj_set_size(pair_group, L.scr_w, L.scr_h - L.content_y);
    lv_obj_set_pos(pair_group, 0, L.content_y);
    lv_obj_set_style_bg_opa(pair_group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pair_group, 0, 0);
    lv_obj_set_style_pad_all(pair_group, 0, 0);
    lv_obj_clear_flag(pair_group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(pair_group, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t* l1 = lv_label_create(pair_group);
    lv_label_set_text(l1, "USB not connected");
    lv_obj_set_style_text_font(l1, L.bt_status_font, 0);
    lv_obj_set_style_text_color(l1, COL_TEXT, 0);
    lv_obj_align(l1, LV_ALIGN_TOP_MID, 0, 40);

    lv_obj_t* l2 = lv_label_create(pair_group);
    lv_label_set_text(l2, "plug in the USB-C cable");
    lv_obj_set_style_text_font(l2, L.bt_device_font, 0);
    lv_obj_set_style_text_color(l2, COL_DIM, 0);
    lv_obj_align(l2, LV_ALIGN_TOP_MID, 0, 120);

    lv_obj_t* l3 = lv_label_create(pair_group);
    lv_label_set_text(l3, "and start the daemon on your Mac");
    lv_obj_set_style_text_font(l3, L.bt_device_font, 0);
    lv_obj_set_style_text_color(l3, COL_DIM, 0);
    lv_obj_align(l3, LV_ALIGN_TOP_MID, 0, 160);

    lv_obj_add_flag(pair_group, LV_OBJ_FLAG_HIDDEN);  // ui_update_usb_status decides
}

// Idle "Zzz" screen — shown when the host is connected but no usage update has
// landed recently (token expired, daemon down, host asleep…). Full-screen, like
// the pairing hint, so we never render hours-old numbers as if they were live.
static void build_idle_group(lv_obj_t* parent) {
    idle_group = lv_obj_create(parent);
    lv_obj_set_size(idle_group, L.scr_w, L.scr_h - L.content_y);
    lv_obj_set_pos(idle_group, 0, L.content_y);
    lv_obj_set_style_bg_opa(idle_group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(idle_group, 0, 0);
    lv_obj_set_style_pad_all(idle_group, 0, 0);
    lv_obj_clear_flag(idle_group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(idle_group, LV_OBJ_FLAG_EVENT_BUBBLE);

    // A shrunk-down sleeping creature (reused claudepix "expression sleep" art)
    // sits between the header and the status line; the animated "Listening…"
    // status line carries the words, so no extra text is needed here.
    lv_obj_t* creature = splash_mini_create(idle_group, "expression sleep", 160);
    if (creature) lv_obj_align(creature, LV_ALIGN_CENTER, 0, -20);

    lv_obj_add_flag(idle_group, LV_OBJ_FLAG_HIDDEN);  // update_view_state decides
}

static void init_usage_screen(lv_obj_t* scr) {
    usage_container = lv_obj_create(scr);
    lv_obj_set_size(usage_container, L.scr_w, L.scr_h);
    lv_obj_set_pos(usage_container, 0, 0);
    lv_obj_set_style_bg_opa(usage_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(usage_container, 0, 0);
    lv_obj_set_style_pad_all(usage_container, 0, 0);
    lv_obj_clear_flag(usage_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(usage_container, global_click_cb, LV_EVENT_CLICKED, NULL);

    lbl_title = lv_label_create(usage_container);
    lv_label_set_text(lbl_title, "Usage");
    lv_obj_set_style_text_font(lbl_title, &font_tiempos_56, 0);
    lv_obj_set_style_text_color(lbl_title, COL_TEXT, 0);
    // x/y re-centered in ui_init() once the logo and badge exist and their
    // real heights are known — this initial align just gets it on-screen.
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, L.title_y);

    // Usage panels (shown when connected) live in a transparent full-size group
    // so they can be toggled against the pairing hint as one unit.
    usage_group = lv_obj_create(usage_container);
    lv_obj_set_size(usage_group, L.scr_w, L.scr_h);
    lv_obj_set_pos(usage_group, 0, 0);
    lv_obj_set_style_bg_opa(usage_group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(usage_group, 0, 0);
    lv_obj_set_style_pad_all(usage_group, 0, 0);
    lv_obj_clear_flag(usage_group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(usage_group, LV_OBJ_FLAG_EVENT_BUBBLE);

    panel_session = make_usage_panel(usage_group, L.content_y, "Current",
                     &lbl_session_pct, &lbl_session_label,
                     &bar_session, &lbl_session_reset);

    // Enterprise-only overlays inside panel_session — hidden until enterprise data arrives
    lbl_session_pct_sym = lv_label_create(panel_session);
    lv_label_set_text(lbl_session_pct_sym, "%");
    lv_obj_set_style_text_font(lbl_session_pct_sym, &font_styrene_28, 0);
    lv_obj_set_style_text_color(lbl_session_pct_sym, COL_TEXT, 0);
    lv_obj_add_flag(lbl_session_pct_sym, LV_OBJ_FLAG_HIDDEN);

    lbl_spending_desc = lv_label_create(panel_session);
    lv_label_set_text(lbl_spending_desc, "of your monthly budget");
    lv_obj_set_style_text_font(lbl_spending_desc, &font_styrene_28, 0);
    lv_obj_set_style_text_color(lbl_spending_desc, COL_DIM, 0);
    lv_obj_set_pos(lbl_spending_desc, 0, L.usage_reset_y);
    lv_obj_add_flag(lbl_spending_desc, LV_OBJ_FLAG_HIDDEN);

    lbl_spending_status = lv_label_create(panel_session);
    lv_label_set_text(lbl_spending_status, "");
    lv_obj_set_style_text_font(lbl_spending_status, &font_styrene_16, 0);
    lv_obj_set_pos(lbl_spending_status, 0, L.usage_reset_y + 20);
    lv_obj_add_flag(lbl_spending_status, LV_OBJ_FLAG_HIDDEN);

    panel_weekly = make_usage_panel(usage_group,
                     L.content_y + L.usage_panel_h + L.usage_panel_gap, "Weekly",
                     &lbl_weekly_pct, &lbl_weekly_label,
                     &bar_weekly, &lbl_weekly_reset);
    // Recolor enabled so enterprise period box can color pace and reset separately
    lv_label_set_recolor(lbl_weekly_reset, true);

    build_pair_group(usage_container);
    build_idle_group(usage_container);

    // Status line — always visible on the usage view. Driven by ui_tick_anim().
    lbl_anim = lv_label_create(usage_container);
    lv_label_set_text(lbl_anim, "");
    lv_obj_set_style_text_font(lbl_anim, &font_mono_32, 0);
    lv_obj_set_style_text_color(lbl_anim, COL_ACCENT, 0);
    lv_obj_align(lbl_anim, LV_ALIGN_BOTTOM_MID, 0, -15);
}

// Lightbox screen (docs/plans/usb-transport-lightbox.md Phase 4) — displays
// memes read from the device's SPIFFS partition via memefs.cpp. On boards
// without lightbox media enabled, memefs reports zero memes and this screen
// just shows its empty state — no #ifdef BOARD_* needed here.
static void init_lightbox_screen(lv_obj_t* scr) {
    lightbox_container = lv_obj_create(scr);
    lv_obj_set_size(lightbox_container, L.scr_w, L.scr_h);
    lv_obj_set_pos(lightbox_container, 0, 0);
    lv_obj_set_style_bg_color(lightbox_container, COL_BG, 0);
    lv_obj_set_style_bg_opa(lightbox_container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(lightbox_container, 0, 0);
    lv_obj_set_style_pad_all(lightbox_container, 0, 0);
    lv_obj_clear_flag(lightbox_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(lightbox_container, global_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(lightbox_container, LV_OBJ_FLAG_HIDDEN);

    // Both widgets are pinned full-screen with COVER alignment: the source
    // keeps its aspect ratio, is scaled until it fills the screen, and
    // whatever overflows is cropped. GIF playback also renders into a plain
    // lv_image, so the same property drives both.
    lightbox_img = lv_image_create(lightbox_container);
    lv_obj_set_size(lightbox_img, L.scr_w, L.scr_h);
    lv_obj_set_pos(lightbox_img, 0, 0);
    lv_image_set_inner_align(lightbox_img, LV_IMAGE_ALIGN_COVER);
    lv_obj_add_flag(lightbox_img, LV_OBJ_FLAG_HIDDEN);

#if CLAWDMETER_USE_GIF_PLAYER
    // Plain lv_image driven by gif_player (COOKED-mode AnimatedGIF), NOT an
    // lv_gif widget — LVGL's bundled GIF decoder is intentionally disabled
    // for this board to avoid symbol clashes with upstream AnimatedGIF.
    lightbox_gif = lv_image_create(lightbox_container);
    lv_obj_set_size(lightbox_gif, L.scr_w, L.scr_h);
    lv_obj_set_pos(lightbox_gif, 0, 0);
    lv_image_set_inner_align(lightbox_gif, LV_IMAGE_ALIGN_COVER);
    // An upscaled GIF redraws the full screen every frame; bilinear sampling
    // can't keep up on this panel (visible top-to-bottom sweep), so use
    // nearest-neighbor for the GIF only — memes don't miss the filtering.
    lv_image_set_antialias(lightbox_gif, false);
    gif_player_attach(lightbox_gif);
    lv_obj_add_flag(lightbox_gif, LV_OBJ_FLAG_HIDDEN);
#endif

    lbl_lightbox_empty = lv_label_create(lightbox_container);
    lv_label_set_text(lbl_lightbox_empty, "No memes found");
    lv_obj_set_style_text_font(lbl_lightbox_empty, &font_styrene_28, 0);
    lv_obj_set_style_text_color(lbl_lightbox_empty, COL_DIM, 0);
    lv_obj_center(lbl_lightbox_empty);
    lv_obj_add_flag(lbl_lightbox_empty, LV_OBJ_FLAG_HIDDEN);
}

static void redraw_lightbox_screen(void) {
    if (!lightbox_container) return;

    int n = memefs_count();
    if (n == 0) {
        lv_obj_add_flag(lightbox_img, LV_OBJ_FLAG_HIDDEN);
#if CLAWDMETER_USE_GIF_PLAYER
        gif_player_stop();
        lv_obj_add_flag(lightbox_gif, LV_OBJ_FLAG_HIDDEN);
#endif
        lv_obj_clear_flag(lbl_lightbox_empty, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_add_flag(lbl_lightbox_empty, LV_OBJ_FLAG_HIDDEN);

    if (lightbox_index >= n) lightbox_index = 0;
    const char* path = memefs_path(lightbox_index);

#if CLAWDMETER_USE_GIF_PLAYER
    if (memefs_is_gif(lightbox_index)) {
        lv_obj_add_flag(lightbox_img, LV_OBJ_FLAG_HIDDEN);
        if (gif_player_open(path)) {
            lv_obj_clear_flag(lightbox_gif, LV_OBJ_FLAG_HIDDEN);
        } else {
            // Open failure already logged over serial; leave the widget
            // hidden rather than showing a stale frame.
            lv_obj_add_flag(lightbox_gif, LV_OBJ_FLAG_HIDDEN);
        }
    } else
#endif
    {
#if CLAWDMETER_USE_GIF_PLAYER
        // The GIF widget is the later sibling (drawn on top): leaving it
        // visible covers the PNG entirely. Stopping the player also frees
        // its decode buffers and halts per-frame CPU work.
        gif_player_stop();
        lv_obj_add_flag(lightbox_gif, LV_OBJ_FLAG_HIDDEN);
#endif
        lv_image_set_src(lightbox_img, path);
        lv_obj_clear_flag(lightbox_img, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_lightbox_next(void) {
    int n = memefs_count();
    if (n == 0) return;
    lightbox_index = (lightbox_index + 1) % n;
    redraw_lightbox_screen();
}

static void init_stock_screen(lv_obj_t* scr) {
    stock_container = lv_obj_create(scr);
    lv_obj_set_size(stock_container, L.scr_w, L.scr_h);
    lv_obj_set_pos(stock_container, 0, 0);
    lv_obj_set_style_bg_opa(stock_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(stock_container, 0, 0);
    lv_obj_set_style_pad_all(stock_container, 0, 0);
    lv_obj_clear_flag(stock_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(stock_container, global_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(stock_container, LV_OBJ_FLAG_HIDDEN);

    // Full-bleed card (screen minus L.margin on every side) — symbol pinned
    // to the top, price+change grouped and pinned to the bottom, via flex
    // column with space-between (mirrors the confirmed HTML mockup).
    lv_obj_t* panel = lv_obj_create(stock_container);
    lv_obj_set_pos(panel, L.margin, L.margin);
    lv_obj_set_size(panel, L.scr_w - 2 * L.margin, L.scr_h - 2 * L.margin);
    lv_obj_set_style_bg_color(panel, COL_PANEL, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 32, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lbl_stock_symbol = lv_label_create(panel);
    lv_label_set_text(lbl_stock_symbol, "---");
    lv_obj_set_style_text_font(lbl_stock_symbol, &font_tiempos_60, 0);
    lv_obj_set_style_text_color(lbl_stock_symbol, COL_TEXT, 0);

    lv_obj_t* bottom_group = lv_obj_create(panel);
    lv_obj_set_size(bottom_group, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(bottom_group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bottom_group, 0, 0);
    lv_obj_set_style_pad_all(bottom_group, 0, 0);
    lv_obj_clear_flag(bottom_group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bottom_group, LV_OBJ_FLAG_EVENT_BUBBLE);

    lbl_stock_price = lv_label_create(bottom_group);
    lv_label_set_text(lbl_stock_price, "---");
    lv_obj_set_style_text_font(lbl_stock_price, &font_styrene_96, 0);
    lv_obj_set_style_text_color(lbl_stock_price, COL_TEXT, 0);
    lv_obj_set_pos(lbl_stock_price, 0, 0);

    lbl_stock_change = lv_label_create(bottom_group);
    lv_label_set_text(lbl_stock_change, "---");
    lv_obj_set_style_text_font(lbl_stock_change, &font_styrene_36, 0);
    lv_obj_set_style_text_color(lbl_stock_change, COL_TEXT, 0);
    lv_obj_align_to(lbl_stock_change, lbl_stock_price, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 16);

    lbl_stock_empty = lv_label_create(stock_container);
    lv_label_set_text(lbl_stock_empty, "No stocks configured");
    lv_obj_set_style_text_font(lbl_stock_empty, &font_styrene_28, 0);
    lv_obj_set_style_text_color(lbl_stock_empty, COL_DIM, 0);
    lv_obj_center(lbl_stock_empty);
    lv_obj_add_flag(lbl_stock_empty, LV_OBJ_FLAG_HIDDEN);
}

// Up: green + up-triangle (U+25B2). Down: red + down-triangle (U+25BC).
// Flat: neutral text, no triangle. Color and glyph are always shown
// together (never color-only) so the state reads correctly for colorblind
// users too. THEME_GREEN/THEME_RED (COL_GREEN/COL_RED) are the same tokens
// already used by the usage screen's pace indicator — no new colors. The
// price label now carries the same color as the change line. The daemon
// only sends price + pct_change, so the absolute change shown in the
// "(N.NN)" part is derived on-device from those two values.
static void render_stock_quote(const StockQuote& q) {
    lv_label_set_text(lbl_stock_symbol, q.symbol);
    char price_buf[16];
    snprintf(price_buf, sizeof(price_buf), "%.2f", q.price);
    lv_label_set_text(lbl_stock_price, price_buf);

    char buf[32];
    if (q.pct_change > 0.0f) {
        float prev_close = q.price / (1.0f + q.pct_change / 100.0f);
        float abs_change = q.price - prev_close;
        snprintf(buf, sizeof(buf), "\xE2\x96\xB2 %.2f (%.2f%%)", abs_change, q.pct_change);
        lv_obj_set_style_text_color(lbl_stock_price, COL_GREEN, 0);
        lv_obj_set_style_text_color(lbl_stock_change, COL_GREEN, 0);
    } else if (q.pct_change < 0.0f) {
        float prev_close = q.price / (1.0f + q.pct_change / 100.0f);
        float abs_change = prev_close - q.price;
        snprintf(buf, sizeof(buf), "\xE2\x96\xBC %.2f (%.2f%%)", abs_change, -q.pct_change);
        lv_obj_set_style_text_color(lbl_stock_price, COL_RED, 0);
        lv_obj_set_style_text_color(lbl_stock_change, COL_RED, 0);
    } else {
        snprintf(buf, sizeof(buf), "%.2f%%", q.pct_change);
        lv_obj_set_style_text_color(lbl_stock_price, COL_TEXT, 0);
        lv_obj_set_style_text_color(lbl_stock_change, COL_TEXT, 0);
    }
    lv_label_set_text(lbl_stock_change, buf);
}

static void redraw_stock_screen(void) {
    if (!stock_container) return;
    if (known_stock_count == 0) {
        lv_obj_add_flag(lbl_stock_symbol, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_stock_price, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_stock_change, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(lbl_stock_empty, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(lbl_stock_symbol, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(lbl_stock_price, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(lbl_stock_change, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(lbl_stock_empty, LV_OBJ_FLAG_HIDDEN);
    if (stock_display_index >= known_stock_count) stock_display_index = 0;
    render_stock_quote(known_stocks[stock_display_index]);
}

// Merges this cycle's quotes into the persistent known_stocks[] store by
// symbol match. A symbol missing from this cycle's array (a transient
// fetch failure upstream, per the daemon's error-handling contract) keeps
// showing its last known value here rather than blanking — new symbols are
// appended (bounded by MAX_STOCKS, which matches the daemon-side cap so
// this can never overflow in practice).
static void merge_stock_quotes(const StockQuote* incoming, int count) {
    for (int i = 0; i < count; i++) {
        int idx = -1;
        for (int j = 0; j < known_stock_count; j++) {
            if (strcmp(known_stocks[j].symbol, incoming[i].symbol) == 0) { idx = j; break; }
        }
        if (idx == -1 && known_stock_count < MAX_STOCKS) idx = known_stock_count++;
        if (idx != -1) known_stocks[idx] = incoming[i];
    }
}

void ui_stock_next(void) {
    if (known_stock_count == 0) return;
    stock_display_index = (stock_display_index + 1) % known_stock_count;
    redraw_stock_screen();
}

// ======== Public API ========

void ui_init(void) {
    compute_layout(board_caps());

    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, COL_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    init_icon_dsc_rgb565a8(&logo_dsc, LOGO_WIDTH, LOGO_HEIGHT, logo_data);

    init_usage_screen(scr);
    init_lightbox_screen(scr);
    init_stock_screen(scr);
    splash_init(scr);

    if (splash_get_root()) {
        lv_obj_add_event_cb(splash_get_root(), global_click_cb, LV_EVENT_CLICKED, NULL);
    }

    logo_img = lv_image_create(usage_container);
    lv_image_set_src(logo_img, &logo_dsc);
    lv_obj_set_pos(logo_img, L.margin, L.title_y - 10);

    who_badge = lv_label_create(usage_container);
    lv_label_set_text(who_badge, "");
    lv_obj_set_style_text_font(who_badge, &font_styrene_28, 0);
    lv_obj_set_style_text_color(who_badge, COL_TEXT, 0);
    lv_obj_set_style_bg_color(who_badge, COL_ACCENT, 0);
    lv_obj_set_style_bg_opa(who_badge, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(who_badge, 8, 0);
    lv_obj_set_style_pad_left(who_badge, 12, 0);
    lv_obj_set_style_pad_right(who_badge, 12, 0);
    lv_obj_set_style_pad_top(who_badge, 3, 0);
    lv_obj_set_style_pad_bottom(who_badge, 3, 0);
    lv_obj_add_flag(who_badge, LV_OBJ_FLAG_HIDDEN);

    // Header row: logo, "Usage" title, and the Self/Work badge share one
    // horizontal line. The logo is tallest and fixed at the top, so the
    // other two are vertically re-centered to its middle using their own
    // measured height (font metrics differ, so a hand-picked offset drifts
    // whenever a font changes).
    // A freshly created/styled label's auto-sized height isn't resolved into
    // obj->coords until a layout pass runs — lv_obj_get_height() right after
    // creation would read a stale (zero) value. Force that pass now.
    lv_obj_update_layout(scr);
    int16_t header_center_y = (L.title_y - 10) + LOGO_HEIGHT / 2;
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, header_center_y - lv_obj_get_height(lbl_title) / 2);
    lv_obj_align(who_badge, LV_ALIGN_TOP_RIGHT, -L.margin, header_center_y - lv_obj_get_height(who_badge) / 2);
}

static void apply_who_badge_visibility(void);

void ui_update(const UsageData* data) {
    if (!data->valid) return;
    last_data_ms = lv_tick_get();   // a valid usage update just landed → dot goes green
    data_received = true;
    lv_label_set_text(who_badge, data->who);
    apply_who_badge_visibility();

    if (data->clock_epoch > 0) {    // daemon supplied wall-clock time → drive the title clock
        clock_base_epoch = data->clock_epoch;
        clock_base_ms = last_data_ms;
        clock_fmt = data->clock_fmt;
    } else if (clock_base_epoch != 0) {   // clock turned off daemon-side → revert title to "Usage"
        clock_base_epoch = 0;
        clock_last_min = -1;
        lv_label_set_text(lbl_title, "Usage");
    }

    int s_pct = (int)(data->session_pct + 0.5f);

    if (data->enterprise) {
        // Spending box: big number-only label + small "%" symbol + desc + pace
        lv_obj_set_style_text_font(lbl_session_pct, &font_tiempos_56, 0);
        lv_label_set_text(lbl_session_label, "Spending");
        lv_obj_add_flag(lbl_session_reset, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(lbl_session_pct_sym, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(lbl_spending_desc,   LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_spending_status,   LV_OBJ_FLAG_HIDDEN);
        if (panel_weekly) lv_obj_clear_flag(panel_weekly, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_set_style_text_font(lbl_session_pct, &font_styrene_48, 0);
        lv_label_set_text(lbl_session_label, "Current");
        lv_obj_clear_flag(lbl_session_reset, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_session_pct_sym, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_spending_desc,   LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_spending_status, LV_OBJ_FLAG_HIDDEN);
        if (panel_weekly) lv_obj_clear_flag(panel_weekly, LV_OBJ_FLAG_HIDDEN);
    }

    char buf[48];

    // Pace vars used in both enterprise blocks below
    const char* pace_text = "Under pace";
    lv_color_t  pace_color = COL_GREEN;
    const char* pace_hex   = "788c5d";   // matches THEME_GREEN
    if (data->session_pct > (float)data->time_pct + 15.0f) {
        pace_text = "Over pace";  pace_color = COL_RED;   pace_hex = "c0392b";
    } else if (data->session_pct > (float)data->time_pct - 15.0f) {
        pace_text = "On pace";    pace_color = COL_AMBER; pace_hex = "d97757";
    }

    if (data->enterprise) {
        lv_label_set_text_fmt(lbl_session_pct, "%d", s_pct);
        lv_obj_align_to(lbl_session_pct_sym, lbl_session_pct,
                        LV_ALIGN_OUT_RIGHT_TOP, 4, 12);
    } else {
        lv_label_set_text_fmt(lbl_session_pct, "%d%%", s_pct);
        format_reset_time(data->session_reset_mins, buf, sizeof(buf));
        lv_label_set_text(lbl_session_reset, buf);
    }

    lv_bar_set_value(bar_session, s_pct, LV_ANIM_ON);
    lv_obj_set_style_bg_color(bar_session, pct_color(data->session_pct), LV_PART_INDICATOR);

    if (data->enterprise) {
        // Period box: time % + dynamic pace color + "Resets <date>" label
        lv_label_set_text(lbl_weekly_label, "Period");
        lv_label_set_text_fmt(lbl_weekly_pct, "%d%%", data->time_pct);
        lv_bar_set_value(bar_weekly, data->time_pct, LV_ANIM_ON);
        lv_color_t bar_pace = (data->session_pct <= (float)data->time_pct) ? COL_GREEN :
                              (data->session_pct <= (float)data->time_pct + 15.0f) ? COL_AMBER :
                              COL_RED;
        lv_obj_set_style_bg_color(bar_weekly, bar_pace, LV_PART_INDICATOR);
        snprintf(buf, sizeof(buf), "#%s %s# - #faf9f5 Resets %s#",
                 pace_hex, pace_text, data->reset_date);
        lv_label_set_text(lbl_weekly_reset, buf);
    } else {
        int w_pct = (int)(data->weekly_pct + 0.5f);
        lv_label_set_text_fmt(lbl_weekly_pct, "%d%%", w_pct);
        lv_bar_set_value(bar_weekly, w_pct, LV_ANIM_ON);
        lv_obj_set_style_bg_color(bar_weekly, pct_color(data->weekly_pct), LV_PART_INDICATOR);
        format_reset_time(data->weekly_reset_mins, buf, sizeof(buf));
        lv_label_set_text(lbl_weekly_reset, buf);
    }

    merge_stock_quotes(data->stock, data->stock_count);
    if (current_screen == SCREEN_STOCK) redraw_stock_screen();
}

// Pick the usage-view sub-screen: USB-disconnected hint, the idle "Zzz" screen
// (connected but no valid data received since boot), or the usage panels holding
// the latest valid values. Only re-lays-out on an actual change.
static void update_view_state(void) {
    if (!usage_group || !pair_group || !idle_group) return;
    int v;
    if (!s_usb_connected) {
        v = 0;  // USB-disconnected hint
    } else if (data_received) {
        v = 2;  // latest valid usage, retained across poll failures
    } else {
        v = 1;  // idle / Zzz
    }
    if (v == view_state) return;
    view_state = v;
    lv_obj_add_flag(pair_group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(idle_group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(usage_group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(v == 0 ? pair_group : v == 1 ? idle_group : usage_group,
                      LV_OBJ_FLAG_HIDDEN);
}

void ui_tick_anim(void) {
    if (current_screen != SCREEN_USAGE) return;
    update_view_state();
    if (view_state == 1) splash_mini_tick();   // animate the sleeping creature on the idle screen

    uint32_t now = lv_tick_get();

    // Title clock: once the daemon has sent wall-clock time, replace "Usage" with
    // the live time, advanced locally so it ticks every minute between payloads.
    if (clock_base_epoch > 0) {
        time_t cur = (time_t)(clock_base_epoch + (now - clock_base_ms) / 1000);
        struct tm tmv;
        gmtime_r(&cur, &tmv);   // epoch is already local wall-clock → gmtime keeps it as-is
        if (tmv.tm_min != clock_last_min) {   // only rewrite the title when the minute changes
            clock_last_min = tmv.tm_min;
            char tbuf[12];
            if (clock_fmt == 12) {
                int h12 = tmv.tm_hour % 12;
                if (h12 == 0) h12 = 12;
                snprintf(tbuf, sizeof(tbuf), "%d:%02d %s", h12, tmv.tm_min,
                         tmv.tm_hour < 12 ? "AM" : "PM");
            } else {
                snprintf(tbuf, sizeof(tbuf), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
            }
            lv_label_set_text(lbl_title, tbuf);
        }
    }

    if (now - anim_msg_start >= ANIM_MSG_MS) {
        anim_msg_idx = (anim_msg_idx + 1) % ANIM_MSG_COUNT;
        anim_msg_start = now;
    }

    if (now - anim_last_ms < spinner_ms[anim_spinner_idx]) return;
    anim_last_ms = now;
    anim_phase = (anim_phase + 1) % SPINNER_PHASES;
    anim_spinner_idx = (anim_phase < SPINNER_COUNT) ? anim_phase
                                                    : (SPINNER_PHASES - anim_phase);

    // Status text by priority. Whimsical messages only when connected & settled.
    const char* text;
    if (!s_usb_connected) {
        text = "Waiting";              // waiting for the USB host to connect
    } else if (view_state == 1) {      // idle — alternate so it reads as alive AND data-less
        text = (anim_msg_idx & 1) ? "No data" : "Listening";
    } else if (now - connected_at_ms < 5000) {
        text = "Connected";
    } else {
        text = anim_messages[anim_msg_idx];
    }

    // All states share the whimsical style: "<glyph> <Title-case word>…"
    static char buf[80];
    snprintf(buf, sizeof(buf), "%s %s\xE2\x80\xA6",
             spinner_frames[anim_spinner_idx], text);
    lv_label_set_text(lbl_anim, buf);
}

static screen_t prev_non_splash_screen = SCREEN_USAGE;
// who_badge is a child of usage_container, so it's already hidden whenever
// that container is (splash/lightbox/stock); this only needs to additionally
// hide it on the usage screen itself when there's no `who` value yet.
static void apply_who_badge_visibility(void) {
    if (!who_badge) return;
    bool has_who = lv_label_get_text(who_badge)[0] != '\0';
    if (current_screen == SCREEN_USAGE && has_who) {
        lv_obj_clear_flag(who_badge, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(who_badge, LV_OBJ_FLAG_HIDDEN);
    }
}

static void global_click_cb(lv_event_t* e) {
    (void)e;
    ui_toggle_splash();
}

void ui_show_screen(screen_t screen) {
    lv_obj_add_flag(usage_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(lightbox_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(stock_container, LV_OBJ_FLAG_HIDDEN);
    splash_hide();
    // Leaving (or re-entering) the lightbox: release the GIF decode buffers.
    // redraw_lightbox_screen() below re-opens the current meme if needed.
    gif_player_stop();

    switch (screen) {
    case SCREEN_SPLASH:   splash_show(); break;
    case SCREEN_USAGE:    lv_obj_clear_flag(usage_container, LV_OBJ_FLAG_HIDDEN); break;
    case SCREEN_LIGHTBOX:
        lv_obj_clear_flag(lightbox_container, LV_OBJ_FLAG_HIDDEN);
        // Re-scan on entry so a fresh `pio run -t uploadfs` (per the spec's
        // update flow) is picked up without a reboot.
        memefs_rescan();
        redraw_lightbox_screen();
        break;
    case SCREEN_STOCK:
        lv_obj_clear_flag(stock_container, LV_OBJ_FLAG_HIDDEN);
        redraw_stock_screen();
        break;
    default: break;
    }

    // logo_img is a child of usage_container, so its visibility already
    // follows the container's hidden flag set above — no separate toggle
    // needed here.

    if (screen != SCREEN_SPLASH) prev_non_splash_screen = screen;
    current_screen = screen;
    apply_who_badge_visibility();
}

void ui_toggle_splash(void) {
    if (current_screen == SCREEN_SPLASH) ui_show_screen(prev_non_splash_screen);
    else                                  ui_show_screen(SCREEN_SPLASH);
}

void ui_cycle_screen(void) {
    screen_t next = (screen_t)((current_screen + 1) % SCREEN_COUNT);
    ui_show_screen(next);
}

screen_t ui_get_current_screen(void) {
    return current_screen;
}

void ui_update_usb_status(bool connected) {
    bool was_connected = s_usb_connected;
    s_usb_connected = connected;

    if (s_usb_connected && !was_connected) connected_at_ms = lv_tick_get();
    // pair / idle / usage — picked from connection + data availability.
    update_view_state();
}
