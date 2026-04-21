/*
 * Portrait peripheral (right) display for SSD1306 128x32
 *
 * 3 rotated 32x32 canvases:
 *   Top:    Battery bar + connection icon
 *   Middle: Diamond shape with glitch animation
 *   Bottom: "CORNE" text
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

static lv_obj_t *top_canvas, *mid_canvas, *bot_canvas;
static uint8_t cbuf_top[CANVAS_BUF_SIZE];
static uint8_t cbuf_mid[CANVAS_BUF_SIZE];
static uint8_t cbuf_bot[CANVAS_BUF_SIZE];

static struct {
    uint8_t battery;
    bool connected;
} state;

/* ── Diamond drawing + glitch ── */

static uint16_t glitch_seed = 77;
static uint8_t glitch_frames_left = 0;
static int64_t last_glitch_time;
static uint32_t glitch_next_ms;
static bool diamond_dirty = true;

static uint16_t glitch_rand(void) {
    glitch_seed ^= glitch_seed << 7;
    glitch_seed ^= glitch_seed >> 9;
    glitch_seed ^= glitch_seed << 8;
    return glitch_seed;
}

static void draw_diamond(lv_obj_t *canvas, bool with_glitch) {
    lv_canvas_fill_bg(canvas, BG_COLOR, LV_OPA_COVER);

    lv_draw_rect_dsc_t fg_dsc;
    init_rect_dsc(&fg_dsc, FG_COLOR);

    /* Diamond: 16 rows of horizontal bars, centered */
    for (int i = 0; i < 16; i++) {
        int half = (i < 8) ? (i + 1) : (16 - i);
        int w = half * 2;
        int x = (32 - w) / 2;
        int y = i + 8;

        if (with_glitch && glitch_frames_left > 0) {
            /* Randomly shift some rows */
            if (glitch_rand() % 4 == 0) {
                x += (glitch_rand() % 5) - 2;
                w += (glitch_rand() % 3) - 1;
            }
        }

        if (w > 0 && x >= 0 && x + w <= 32) {
            canvas_draw_rect(canvas, x, y, w, 1, &fg_dsc);
        }
    }

    if (with_glitch && glitch_frames_left > 0) {
        glitch_frames_left--;
    }

    rotate_canvas(canvas);
}

static void glitch_timer_cb(lv_timer_t *timer) {
    int64_t now = k_uptime_get();
    if (glitch_frames_left == 0) {
        if ((now - last_glitch_time) > (int64_t)glitch_next_ms) {
            last_glitch_time = now;
            glitch_frames_left = 2 + (glitch_rand() % 4);
            glitch_next_ms = 2000 + (glitch_rand() % 6000);
        } else if (diamond_dirty) {
            draw_diamond(mid_canvas, false);
            diamond_dirty = false;
        }
        return;
    }
    draw_diamond(mid_canvas, true);
    diamond_dirty = true;
}

/* ── Drawing ── */

static void draw_top(void) {
    lv_canvas_fill_bg(top_canvas, BG_COLOR, LV_OPA_COVER);

    draw_battery_bar(top_canvas, state.battery);

    lv_draw_label_dsc_t lbl;
    init_label_dsc(&lbl, FG_COLOR, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);

    canvas_draw_text(top_canvas, 0, 14, 32, &lbl,
                     state.connected ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE);

    rotate_canvas(top_canvas);
}

static void draw_bot(void) {
    lv_canvas_fill_bg(bot_canvas, BG_COLOR, LV_OPA_COVER);

    lv_draw_label_dsc_t lbl;
    init_label_dsc(&lbl, FG_COLOR, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);

    canvas_draw_text(bot_canvas, 2, 8, 30, &lbl, "CORNE");

    rotate_canvas(bot_canvas);
}

/* ── Battery listener ── */

struct battery_state {
    uint8_t level;
};

static void battery_cb(struct battery_state s) {
    state.battery = s.level;
    draw_top();
}

static struct battery_state battery_get(const zmk_event_t *eh) {
    return (struct battery_state){
        .level = zmk_battery_state_of_charge(),
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(p_periph_bat, struct battery_state, battery_cb, battery_get)
ZMK_SUBSCRIPTION(p_periph_bat, zmk_battery_state_changed);

/* ── Connection listener ── */

struct conn_state {
    bool connected;
};

static void conn_cb(struct conn_state s) {
    state.connected = s.connected;
    draw_top();
}

static struct conn_state conn_get(const zmk_event_t *eh) {
    return (struct conn_state){
        .connected = zmk_split_bt_peripheral_is_connected(),
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(p_periph_conn, struct conn_state, conn_cb, conn_get)
ZMK_SUBSCRIPTION(p_periph_conn, zmk_split_peripheral_status_changed);

/* ── Screen ── */

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);

    /* Fill entire screen dark */
    lv_obj_set_style_bg_color(screen, BG_COLOR, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    top_canvas = lv_canvas_create(screen);
    lv_canvas_set_buffer(top_canvas, cbuf_top, CANVAS_SIZE, CANVAS_SIZE, CANVAS_COLOR_FORMAT);
    lv_obj_set_pos(top_canvas, 96, 0);

    mid_canvas = lv_canvas_create(screen);
    lv_canvas_set_buffer(mid_canvas, cbuf_mid, CANVAS_SIZE, CANVAS_SIZE, CANVAS_COLOR_FORMAT);
    lv_obj_set_pos(mid_canvas, 64, 0);

    bot_canvas = lv_canvas_create(screen);
    lv_canvas_set_buffer(bot_canvas, cbuf_bot, CANVAS_SIZE, CANVAS_SIZE, CANVAS_COLOR_FORMAT);
    lv_obj_set_pos(bot_canvas, 32, 0);

    state.battery = zmk_battery_state_of_charge();
    state.connected = zmk_split_bt_peripheral_is_connected();

    draw_top();
    draw_diamond(mid_canvas, false);
    diamond_dirty = false;
    draw_bot();

    /* Start glitch timer */
    last_glitch_time = k_uptime_get();
    glitch_next_ms = 3000;
    lv_timer_create(glitch_timer_cb, 150, NULL);

    p_periph_bat_init();
    p_periph_conn_init();

    return screen;
}
