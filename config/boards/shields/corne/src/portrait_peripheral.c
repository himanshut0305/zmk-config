/*
 * Portrait peripheral (right) display for SSD1306 128x32
 *
 * 4 rotated 32x32 canvases (top to bottom in portrait):
 *   0: Battery bar + connection icon        (portrait y=0..31)
 *   1: Equalizer bars upper portion         (portrait y=32..63)
 *   2: Equalizer bars lower portion / base  (portrait y=64..95)
 *   3: "Corne" label                        (portrait y=96..127)
 *
 * Coordinate mapping (after LV_DISPLAY_ROTATION_270):
 *   portrait_x = canvas_x
 *   portrait_y = canvas_y + (96 - phys_x)
 *     c0 phys_x=96: portrait_y = canvas_y       (0..31)
 *     c1 phys_x=64: portrait_y = canvas_y + 32  (32..63)
 *     c2 phys_x=32: portrait_y = canvas_y + 64  (64..95)
 *     c3 phys_x=0:  portrait_y = canvas_y + 96  (96..127)
 *
 * Equalizer bars:
 *   7 bars, width=3px, gap=1px, base at portrait_y=95
 *   Heights 6..54px, growing upward (into c1 for tall bars)
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/display.h>
#include <zmk/battery.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/split/bluetooth/peripheral.h>
#include <zmk/events/split_peripheral_status_changed.h>
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
#include <zmk/usb.h>
#include <zmk/events/usb_conn_state_changed.h>
#endif

#include "portrait_util.h"

static lv_obj_t *c0, *c1, *c2, *c3;
static uint8_t buf0[CANVAS_BUF_SIZE], buf1[CANVAS_BUF_SIZE];
static uint8_t buf2[CANVAS_BUF_SIZE], buf3[CANVAS_BUF_SIZE];

static struct {
    uint8_t battery;
    bool connected;
} state;

/* ── Equalizer bars ── */

#define NUM_BARS       7
#define BAR_W          3
#define BAR_GAP        1
#define BAR_BASE_PY    95   /* portrait_y of bar base (bottom of c2) */
#define BAR_MIN_H      6
#define BAR_MAX_H      54   /* 54px reaches portrait_y=41, well into c1 */

/*
 * Bar portrait_x start positions (= canvas_x).
 * Total bar span = 7*3 + 6*1 = 27px. Start offset = (32-27)/2 = 2.
 */
static const uint8_t bar_start_x[NUM_BARS] = {2, 6, 10, 14, 18, 22, 26};

static uint8_t bar_h[NUM_BARS]      = {14, 28, 20, 40, 32, 18, 24};
static uint8_t bar_target[NUM_BARS] = {14, 28, 20, 40, 32, 18, 24};

static uint16_t eq_seed = 0xAB12;

static uint16_t eq_rand(void) {
    eq_seed ^= eq_seed << 7;
    eq_seed ^= eq_seed >> 9;
    eq_seed ^= eq_seed << 8;
    return eq_seed;
}

/*
 * Draw the portion of the equalizer bars that falls within one canvas section.
 * py_base: portrait_y where canvas_y=0 maps to (32 for c1, 64 for c2).
 */
static void draw_eq_section(lv_obj_t *canvas, int py_base) {
    int py_top = py_base;
    int py_bot = py_base + CANVAS_SIZE - 1;

    lv_canvas_fill_bg(canvas, BG_COLOR, LV_OPA_COVER);
    lv_draw_rect_dsc_t fg;
    init_rect_dsc(&fg, FG_COLOR);

    for (int i = 0; i < NUM_BARS; i++) {
        int h = bar_h[i];
        /* bar occupies portrait_y = (BAR_BASE_PY - h + 1) .. BAR_BASE_PY */
        int bar_py_top = BAR_BASE_PY - h + 1;
        int bar_py_bot = BAR_BASE_PY;

        /* clip to this section */
        int seg_top = bar_py_top > py_top ? bar_py_top : py_top;
        int seg_bot = bar_py_bot < py_bot ? bar_py_bot : py_bot;

        if (seg_top > seg_bot) {
            continue;
        }

        int cy_start = seg_top - py_base;   /* canvas_y start */
        int cy_len   = seg_bot - seg_top + 1;
        int cx       = bar_start_x[i];      /* canvas_x = portrait_x */

        canvas_draw_rect(canvas, cx, cy_start, BAR_W, cy_len, &fg);
    }

    rotate_canvas(canvas);
}

static void redraw_bars(void) {
    draw_eq_section(c1, 32);
    draw_eq_section(c2, 64);
}

static void eq_timer_cb(lv_timer_t *timer) {
    ARG_UNUSED(timer);

    /* Retarget bars periodically */
    static uint8_t retarget_count = 0;

    if (retarget_count == 0) {
        retarget_count = 5 + (eq_rand() % 10);
        for (int i = 0; i < NUM_BARS; i++) {
            bar_target[i] = BAR_MIN_H + (uint8_t)(eq_rand() % (BAR_MAX_H - BAR_MIN_H + 1));
        }
    }
    retarget_count--;

    /* Animate heights toward targets (2px/tick) */
    bool changed = false;
    for (int i = 0; i < NUM_BARS; i++) {
        if (bar_h[i] < bar_target[i]) {
            int step = bar_target[i] - bar_h[i];
            bar_h[i] += (step > 2) ? 2 : step;
            changed = true;
        } else if (bar_h[i] > bar_target[i]) {
            int step = bar_h[i] - bar_target[i];
            bar_h[i] -= (step > 2) ? 2 : step;
            changed = true;
        }
    }

    if (changed) {
        redraw_bars();
    }
}

/* ── Canvas 0: Battery + connection ── */

static void draw_c0(void) {
    lv_canvas_fill_bg(c0, BG_COLOR, LV_OPA_COVER);

    draw_battery_bar(c0, state.battery);

    lv_draw_label_dsc_t lbl;
    init_label_dsc(&lbl, FG_COLOR, &lv_font_montserrat_10, LV_TEXT_ALIGN_CENTER);
    canvas_draw_text(c0, 0, 18, 32, &lbl,
                     state.connected ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE);

    rotate_canvas(c0);
}

/* ── Canvas 3: "Corne" label ── */

static void draw_c3(void) {
    lv_canvas_fill_bg(c3, BG_COLOR, LV_OPA_COVER);

    lv_draw_label_dsc_t lbl;
    init_label_dsc(&lbl, FG_COLOR, &lv_font_montserrat_10, LV_TEXT_ALIGN_CENTER);
    canvas_draw_text(c3, 0, 10, 32, &lbl, "Corne");

    rotate_canvas(c3);
}

/* ── Battery listener ── */

struct battery_state { uint8_t level; };

static void battery_cb(struct battery_state s) {
    state.battery = s.level;
    draw_c0();
}

static struct battery_state battery_get(const zmk_event_t *eh) {
    return (struct battery_state){ .level = zmk_battery_state_of_charge() };
}

ZMK_DISPLAY_WIDGET_LISTENER(p_periph_bat, struct battery_state, battery_cb, battery_get)
ZMK_SUBSCRIPTION(p_periph_bat, zmk_battery_state_changed);

/* ── Connection listener ── */

struct conn_state { bool connected; };

static void conn_cb(struct conn_state s) {
    state.connected = s.connected;
    draw_c0();
}

static struct conn_state conn_get(const zmk_event_t *eh) {
    return (struct conn_state){ .connected = zmk_split_bt_peripheral_is_connected() };
}

ZMK_DISPLAY_WIDGET_LISTENER(p_periph_conn, struct conn_state, conn_cb, conn_get)
ZMK_SUBSCRIPTION(p_periph_conn, zmk_split_peripheral_status_changed);

/* ── Screen ── */

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, BG_COLOR, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    /* Physical x positions: c0=96 (portrait top), c3=0 (portrait bottom) */
    c0 = lv_canvas_create(screen);
    lv_canvas_set_buffer(c0, buf0, CANVAS_SIZE, CANVAS_SIZE, CANVAS_COLOR_FORMAT);
    lv_obj_set_pos(c0, 96, 0);

    c1 = lv_canvas_create(screen);
    lv_canvas_set_buffer(c1, buf1, CANVAS_SIZE, CANVAS_SIZE, CANVAS_COLOR_FORMAT);
    lv_obj_set_pos(c1, 64, 0);

    c2 = lv_canvas_create(screen);
    lv_canvas_set_buffer(c2, buf2, CANVAS_SIZE, CANVAS_SIZE, CANVAS_COLOR_FORMAT);
    lv_obj_set_pos(c2, 32, 0);

    c3 = lv_canvas_create(screen);
    lv_canvas_set_buffer(c3, buf3, CANVAS_SIZE, CANVAS_SIZE, CANVAS_COLOR_FORMAT);
    lv_obj_set_pos(c3, 0, 0);

    state.battery  = zmk_battery_state_of_charge();
    state.connected = zmk_split_bt_peripheral_is_connected();

    draw_c0();
    redraw_bars();
    draw_c3();

    /* ~100ms timer → smooth bar animation at ~10fps */
    lv_timer_create(eq_timer_cb, 100, NULL);

    p_periph_bat_init();
    p_periph_conn_init();

    return screen;
}
