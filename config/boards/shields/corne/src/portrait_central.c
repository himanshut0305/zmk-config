/*
 * Portrait central (left) display for SSD1306 128x32
 *
 * 4 rotated 32x32 canvases (top to bottom in portrait):
 *   0: Battery bar + connection icon
 *   1: BT profile + layer info
 *   2: (empty/dark — reserved)
 *   3: "Corne" label
 *
 * Font sizes calculated for 32px width:
 *   montserrat_16: ~11px/char, 19px tall — for layer number
 *   montserrat_10: ~6px/char, 11px tall — for labels/symbols
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

static lv_obj_t *c0, *c1, *c2, *c3;
static uint8_t buf0[CANVAS_BUF_SIZE], buf1[CANVAS_BUF_SIZE];
static uint8_t buf2[CANVAS_BUF_SIZE], buf3[CANVAS_BUF_SIZE];

static struct {
    uint8_t battery;
    bool charging;
    int profile_index;
    bool profile_connected;
    bool profile_bonded;
    uint8_t layer_index;
    const char *layer_label;
} state;

/* ── Canvas 0: Battery + connection icon ── */

static void draw_c0(void) {
    lv_canvas_fill_bg(c0, BG_COLOR, LV_OPA_COVER);

    /* Battery bar: y=4, 8px tall, 24px wide + nub */
    draw_battery_bar(c0, state.battery);

    /* Connection icon: y=16, montserrat_10 */
    lv_draw_label_dsc_t lbl;
    init_label_dsc(&lbl, FG_COLOR, &lv_font_montserrat_10, LV_TEXT_ALIGN_CENTER);

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
    canvas_draw_text(c0, 0, 18, 32, &lbl, icon);

    rotate_canvas(c0);
}

/* ── Canvas 1: BT profile + layer ── */

static void draw_c1(void) {
    lv_canvas_fill_bg(c1, BG_COLOR, LV_OPA_COVER);

    /* BT profile: montserrat_10, ~16px wide → centered in 32px */
    lv_draw_label_dsc_t sm;
    init_label_dsc(&sm, FG_COLOR, &lv_font_montserrat_10, LV_TEXT_ALIGN_CENTER);

    char bt_txt[8];
    snprintf(bt_txt, sizeof(bt_txt), LV_SYMBOL_BLUETOOTH "%d", state.profile_index + 1);
    canvas_draw_text(c1, 0, 2, 32, &sm, bt_txt);

    /* Layer number: montserrat_16 for big readable number */
    lv_draw_label_dsc_t lg;
    init_label_dsc(&lg, FG_COLOR, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);

    char layer_txt[4];
    snprintf(layer_txt, sizeof(layer_txt), "%d", state.layer_index);
    canvas_draw_text(c1, 0, 14, 32, &lg, layer_txt);

    rotate_canvas(c1);
}

/* ── Canvas 3: "Corne" label ── */

static void draw_c3(void) {
    lv_canvas_fill_bg(c3, BG_COLOR, LV_OPA_COVER);

    /* "Corne" in montserrat_10: C(7)+o(6)+r(4)+n(6)+e(6)=29px → fits in 32px */
    lv_draw_label_dsc_t lbl;
    init_label_dsc(&lbl, FG_COLOR, &lv_font_montserrat_10, LV_TEXT_ALIGN_CENTER);
    canvas_draw_text(c3, 0, 10, 32, &lbl, "Corne");

    rotate_canvas(c3);
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
    draw_c0();
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
    draw_c0();
    draw_c1();
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
    draw_c1();
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
    lv_obj_set_style_bg_color(screen, BG_COLOR, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    /* x=96 → top in portrait, x=0 → bottom in portrait */
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

    /* Init state */
    state.battery = zmk_battery_state_of_charge();
    state.profile_index = zmk_ble_active_profile_index();
    state.profile_connected = zmk_ble_active_profile_is_connected();
    state.profile_bonded = !zmk_ble_active_profile_is_open();
    state.layer_index = zmk_keymap_highest_layer_active();
    state.layer_label = zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(state.layer_index));

    draw_c0();
    draw_c1();

    /* Canvas 2: empty dark */
    lv_canvas_fill_bg(c2, BG_COLOR, LV_OPA_COVER);
    rotate_canvas(c2);

    draw_c3();

    p_central_bat_init();
    p_central_out_init();
    p_central_layer_init();

    return screen;
}
