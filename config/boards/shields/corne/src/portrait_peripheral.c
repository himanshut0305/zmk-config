/*
 * Portrait peripheral (right) display for SSD1306 128x32
 *
 * 1 rotated 32x32 canvas:
 *   Top: Battery bar + connection status icon
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

static lv_obj_t *top_canvas;
static uint8_t cbuf_top[CANVAS_BUF_SIZE];

static struct {
    uint8_t battery;
    bool connected;
} state;

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

    top_canvas = lv_canvas_create(screen);
    lv_canvas_set_buffer(top_canvas, cbuf_top, CANVAS_SIZE, CANVAS_SIZE, CANVAS_COLOR_FORMAT);
    lv_obj_set_pos(top_canvas, 96, 0);

    state.battery = zmk_battery_state_of_charge();
    state.connected = zmk_split_bt_peripheral_is_connected();

    draw_top();

    p_periph_bat_init();
    p_periph_conn_init();

    return screen;
}
