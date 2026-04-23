/*
 * Portrait central (left) display for SSD1306 128x32
 *
 * 4 rotated 32x32 canvases (top to bottom in portrait):
 *   0: Battery bar + transport icon       (portrait y=0..31)
 *   1: BT profile indicator               (portrait y=32..63)
 *   2: Layer icon + layer number          (portrait y=64..95)
 *   3: "Corne" label                      (portrait y=96..127)
 *
 * Coordinate mapping (after LV_DISPLAY_ROTATION_270):
 *   portrait_x = canvas_x  (0..31)
 *   portrait_y = canvas_y + (96 - phys_x)
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
} state;

/* ── Canvas 0: Battery bar + transport icon ── */

static void draw_c0(void) {
    lv_canvas_fill_bg(c0, BG_COLOR, LV_OPA_COVER);

    draw_battery_bar(c0, state.battery);

    /* Transport icon in montserrat_16 for size — centered below battery */
    lv_draw_label_dsc_t lbl;
    init_label_dsc(&lbl, FG_COLOR, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);

    const char *icon;
    bool show_charge = false;
    switch (zmk_endpoint_get_selected().transport) {
    case ZMK_TRANSPORT_USB:
        icon = LV_SYMBOL_USB; /* USB symbol already implies power */
        break;
    case ZMK_TRANSPORT_BLE:
    default:
        icon = state.profile_connected ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE;
        show_charge = state.charging; /* BLE but USB power present → charging */
        break;
    }

    if (show_charge) {
        /* Append "+" to signal charging while on BLE */
        char icon_txt[24];
        snprintf(icon_txt, sizeof(icon_txt), "%s+", icon);
        canvas_draw_text(c0, 0, 15, 32, &lbl, icon_txt);
    } else {
        canvas_draw_text(c0, 0, 15, 32, &lbl, icon);
    }

    rotate_canvas(c0);
}

/* ── Canvas 1: BT profile ── */

static void draw_c1(void) {
    lv_canvas_fill_bg(c1, BG_COLOR, LV_OPA_COVER);

    lv_draw_label_dsc_t lbl;
    init_label_dsc(&lbl, FG_COLOR, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);

    /*
     * BT symbol + profile number side-by-side, montserrat_16.
     * Vertically centered in 32px canvas (baseline at ~canvas_y=22 for
     * 16px-tall glyphs; start the text box at canvas_y=6).
     */
    char bt_txt[8];
    snprintf(bt_txt, sizeof(bt_txt), LV_SYMBOL_BLUETOOTH " %d",
             state.profile_index + 1);
    canvas_draw_text(c1, 0, 6, 32, &lbl, bt_txt);

    rotate_canvas(c1);
}

/* ── Canvas 2: Layer icon + layer number ── */

/*
 * One icon per layer — visible at-a-glance on the OLED.
 *   0 default  → HOME   (⌂)
 *   1 lower    → DOWN   (↓)
 *   2 raise    → UP     (↑)
 *   3 command  → SETTINGS (⚙)
 */
static const char *layer_icon(uint8_t idx) {
    switch (idx) {
    case 0:  return LV_SYMBOL_HOME;
    case 1:  return LV_SYMBOL_DOWN;
    case 2:  return LV_SYMBOL_UP;
    case 3:  return LV_SYMBOL_SETTINGS;
    default: return LV_SYMBOL_LIST;
    }
}

static void draw_c2(void) {
    lv_canvas_fill_bg(c2, BG_COLOR, LV_OPA_COVER);

    lv_draw_label_dsc_t lbl;
    init_label_dsc(&lbl, FG_COLOR, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);

    /* Layer icon: top half of canvas (canvas_y=1..16) */
    canvas_draw_text(c2, 0, 1, 32, &lbl, layer_icon(state.layer_index));

    /* Layer number: bottom half (canvas_y=17..32) */
    char num[4];
    snprintf(num, sizeof(num), "%d", state.layer_index);
    canvas_draw_text(c2, 0, 17, 32, &lbl, num);

    rotate_canvas(c2);
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
    state.profile_index  = s.profile_index;
    state.profile_connected = s.connected;
    state.profile_bonded = s.bonded;
    draw_c0();
    draw_c1();
}

static struct output_state output_get(const zmk_event_t *eh) {
    return (struct output_state){
        .profile_index = zmk_ble_active_profile_index(),
        .connected     = zmk_ble_active_profile_is_connected(),
        .bonded        = !zmk_ble_active_profile_is_open(),
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
};

static void layer_cb(struct layer_state s) {
    state.layer_index = s.index;
    draw_c2();
}

static struct layer_state layer_get(const zmk_event_t *eh) {
    return (struct layer_state){
        .index = zmk_keymap_highest_layer_active(),
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(p_central_layer, struct layer_state, layer_cb, layer_get)
ZMK_SUBSCRIPTION(p_central_layer, zmk_layer_state_changed);

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

    /* Init state */
    state.battery           = zmk_battery_state_of_charge();
    state.profile_index     = zmk_ble_active_profile_index();
    state.profile_connected = zmk_ble_active_profile_is_connected();
    state.profile_bonded    = !zmk_ble_active_profile_is_open();
    state.layer_index       = zmk_keymap_highest_layer_active();

    draw_c0();
    draw_c1();
    draw_c2();
    draw_c3();

    p_central_bat_init();
    p_central_out_init();
    p_central_layer_init();

    return screen;
}
