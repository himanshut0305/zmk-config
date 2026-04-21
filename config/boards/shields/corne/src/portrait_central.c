/*
 * Portrait central (left) display for SSD1306 128x32
 *
 * 3 rotated 32x32 canvases arranged vertically:
 *   Top:    Battery bar + BT/USB icon
 *   Middle: BT profile number
 *   Bottom: Layer name
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/display.h>
#include <zmk/battery.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/keymap.h>
#include <zmk/usb.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/layer_state_changed.h>
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
#include <zmk/events/usb_conn_state_changed.h>
#endif

#include "portrait_util.h"

static lv_obj_t *top_canvas, *mid_canvas, *bot_canvas, *bottom_canvas;
static uint8_t cbuf_top[CANVAS_BUF_SIZE];
static uint8_t cbuf_mid[CANVAS_BUF_SIZE];
static uint8_t cbuf_bot[CANVAS_BUF_SIZE];
static uint8_t cbuf_bottom[CANVAS_BUF_SIZE];

static struct {
    uint8_t battery;
    bool charging;
    int profile_index;
    bool profile_connected;
    bool profile_bonded;
    uint8_t layer_index;
    const char *layer_label;
} state;

/* ── Drawing ── */

static void draw_top(void) {
    lv_canvas_fill_bg(top_canvas, BG_COLOR, LV_OPA_COVER);

    draw_battery_bar(top_canvas, state.battery);

    lv_draw_label_dsc_t lbl;
    init_label_dsc(&lbl, FG_COLOR, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);

    const char *icon;
    switch (zmk_endpoint_get_selected().transport) {
    case ZMK_TRANSPORT_USB:
        icon = LV_SYMBOL_USB;
        break;
    case ZMK_TRANSPORT_BLE:
    default:
        icon = state.profile_connected ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE;
        break;
    }
    canvas_draw_text(top_canvas, 0, 14, 32, &lbl, icon);

    rotate_canvas(top_canvas);
}

static void draw_mid(void) {
    lv_canvas_fill_bg(mid_canvas, BG_COLOR, LV_OPA_COVER);

    lv_draw_label_dsc_t lbl;
    init_label_dsc(&lbl, FG_COLOR, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);

    /* BT icon + profile number */
    char txt[8];
    snprintf(txt, sizeof(txt), LV_SYMBOL_BLUETOOTH "%d", state.profile_index + 1);
    canvas_draw_text(mid_canvas, 0, 8, 32, &lbl, txt);

    rotate_canvas(mid_canvas);
}

static void draw_bot(void) {
    lv_canvas_fill_bg(bot_canvas, BG_COLOR, LV_OPA_COVER);

    lv_draw_label_dsc_t lbl;
    init_label_dsc(&lbl, FG_COLOR, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);

    /* Keyboard icon + layer number */
    char txt[8];
    snprintf(txt, sizeof(txt), LV_SYMBOL_KEYBOARD "%d", state.layer_index);
    canvas_draw_text(bot_canvas, 0, 8, 32, &lbl, txt);

    rotate_canvas(bot_canvas);
}

/* ── Battery listener ── */

struct battery_state {
    uint8_t level;
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    bool usb_present;
#endif
};

static void battery_cb(struct battery_state s) {
    state.battery = s.level;
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    state.charging = s.usb_present;
#endif
    draw_top();
}

static struct battery_state battery_get(const zmk_event_t *eh) {
    const struct zmk_battery_state_changed *ev = as_zmk_battery_state_changed(eh);
    return (struct battery_state){
        .level = (ev != NULL) ? ev->state_of_charge : zmk_battery_state_of_charge(),
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .usb_present = zmk_usb_is_powered(),
#endif
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(p_central_bat, struct battery_state, battery_cb, battery_get)
ZMK_SUBSCRIPTION(p_central_bat, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(p_central_bat, zmk_usb_conn_state_changed);
#endif

/* ── Output listener ── */

struct output_state {
    int profile_index;
    bool connected;
    bool bonded;
};

static void output_cb(struct output_state s) {
    state.profile_index = s.profile_index;
    state.profile_connected = s.connected;
    state.profile_bonded = s.bonded;
    draw_top();
    draw_mid();
}

static struct output_state output_get(const zmk_event_t *eh) {
    return (struct output_state){
        .profile_index = zmk_ble_active_profile_index(),
        .connected = zmk_ble_active_profile_is_connected(),
        .bonded = !zmk_ble_active_profile_is_open(),
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(p_central_out, struct output_state, output_cb, output_get)
ZMK_SUBSCRIPTION(p_central_out, zmk_endpoint_changed);
#if defined(CONFIG_ZMK_BLE)
ZMK_SUBSCRIPTION(p_central_out, zmk_ble_active_profile_changed);
#endif

/* ── Layer listener ── */

struct layer_state {
    uint8_t index;
    const char *label;
};

static void layer_cb(struct layer_state s) {
    state.layer_index = s.index;
    state.layer_label = s.label;
    draw_bot();
}

static struct layer_state layer_get(const zmk_event_t *eh) {
    uint8_t idx = zmk_keymap_highest_layer_active();
    return (struct layer_state){
        .index = idx,
        .label = zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(idx)),
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(p_central_layer, struct layer_state, layer_cb, layer_get)
ZMK_SUBSCRIPTION(p_central_layer, zmk_layer_state_changed);

/* ── Screen ── */

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);

    /* Fill entire screen dark (white = dark on inverted SSD1306) */
    lv_obj_set_style_bg_color(screen, BG_COLOR, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    /* Canvas positions: rightmost = top in portrait after 270° rotation */
    top_canvas = lv_canvas_create(screen);
    lv_canvas_set_buffer(top_canvas, cbuf_top, CANVAS_SIZE, CANVAS_SIZE, CANVAS_COLOR_FORMAT);
    lv_obj_set_pos(top_canvas, 96, 0);

    mid_canvas = lv_canvas_create(screen);
    lv_canvas_set_buffer(mid_canvas, cbuf_mid, CANVAS_SIZE, CANVAS_SIZE, CANVAS_COLOR_FORMAT);
    lv_obj_set_pos(mid_canvas, 64, 0);

    bot_canvas = lv_canvas_create(screen);
    lv_canvas_set_buffer(bot_canvas, cbuf_bot, CANVAS_SIZE, CANVAS_SIZE, CANVAS_COLOR_FORMAT);
    lv_obj_set_pos(bot_canvas, 32, 0);

    /* 4th canvas at bottom — "CORNE" label */
    bottom_canvas = lv_canvas_create(screen);
    lv_canvas_set_buffer(bottom_canvas, cbuf_bottom, CANVAS_SIZE, CANVAS_SIZE, CANVAS_COLOR_FORMAT);
    lv_obj_set_pos(bottom_canvas, 0, 0);

    /* Initial draw */
    state.battery = zmk_battery_state_of_charge();
    state.profile_index = zmk_ble_active_profile_index();
    state.profile_connected = zmk_ble_active_profile_is_connected();
    state.profile_bonded = !zmk_ble_active_profile_is_open();
    state.layer_index = zmk_keymap_highest_layer_active();
    state.layer_label = zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(state.layer_index));

    draw_top();
    draw_mid();
    draw_bot();

    /* Draw CORNE label on bottom canvas */
    lv_canvas_fill_bg(bottom_canvas, BG_COLOR, LV_OPA_COVER);
    lv_draw_label_dsc_t corne_lbl;
    init_label_dsc(&corne_lbl, FG_COLOR, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);
    canvas_draw_text(bottom_canvas, 0, 8, 32, &corne_lbl, "CORNE");
    rotate_canvas(bottom_canvas);

    p_central_bat_init();
    p_central_out_init();
    p_central_layer_init();

    return screen;
}
