/*
 * Corne central (left) status display — portrait SSD1306 128x32
 *
 * Layout (after rotation, top to bottom):
 *   Section 0 (32x32): Battery bar + BT/USB icon
 *   Section 1 (32x32): BT profile number
 *   Section 2 (32x32): Layer name
 *   Section 3 (32x32): empty (reserved)
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

#include "corne_status.h"

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

/* ── Drawing ── */

static void draw_top(lv_obj_t *widget, struct corne_widget_status *w) {
    lv_obj_t *canvas = lv_obj_get_child(widget, 0);

    lv_draw_rect_dsc_t bg, fg;
    init_rect_dsc(&bg, BG_COLOR);
    init_rect_dsc(&fg, FG_COLOR);

    lv_draw_label_dsc_t lbl;
    init_label_dsc(&lbl, FG_COLOR, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);

    /* clear */
    lv_canvas_fill_bg(canvas, BG_COLOR, LV_OPA_COVER);

    /* battery bar: outline + fill */
    canvas_draw_rect(canvas, 2, 2, 22, 10, &fg);
    canvas_draw_rect(canvas, 3, 3, 20, 8, &bg);
    int fill = (w->state.battery * 18) / 100;
    if (fill < 1 && w->state.battery > 0) fill = 1;
    canvas_draw_rect(canvas, 4, 4, fill, 6, &fg);
    /* battery nub */
    canvas_draw_rect(canvas, 24, 5, 2, 4, &fg);

    /* BT/USB icon */
    const char *icon;
    switch (zmk_endpoint_get_selected().transport) {
    case ZMK_TRANSPORT_USB:
        icon = LV_SYMBOL_USB;
        break;
    case ZMK_TRANSPORT_BLE:
    default:
        icon = w->state.profile_connected ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE;
        break;
    }
    canvas_draw_text(canvas, 0, 14, 32, &lbl, icon);

    rotate_canvas(canvas);
}

static void draw_mid(lv_obj_t *widget, struct corne_widget_status *w) {
    lv_obj_t *canvas = lv_obj_get_child(widget, 1);

    lv_draw_label_dsc_t lbl;
    init_label_dsc(&lbl, FG_COLOR, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);

    lv_canvas_fill_bg(canvas, BG_COLOR, LV_OPA_COVER);

    /* BT profile number */
    char txt[4];
    snprintf(txt, sizeof(txt), "BT%d", w->state.profile_index + 1);
    canvas_draw_text(canvas, 0, 8, 32, &lbl, txt);

    rotate_canvas(canvas);
}

static void draw_bot(lv_obj_t *widget, struct corne_widget_status *w) {
    lv_obj_t *canvas = lv_obj_get_child(widget, 2);

    lv_draw_label_dsc_t lbl;
    init_label_dsc(&lbl, FG_COLOR, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);

    lv_canvas_fill_bg(canvas, BG_COLOR, LV_OPA_COVER);

    /* layer name or number */
    if (w->state.layer_label && strlen(w->state.layer_label) > 0) {
        canvas_draw_text(canvas, 0, 8, 32, &lbl, w->state.layer_label);
    } else {
        char txt[6];
        snprintf(txt, sizeof(txt), "L %d", w->state.layer_index);
        canvas_draw_text(canvas, 0, 8, 32, &lbl, txt);
    }

    rotate_canvas(canvas);
}

/* ── Battery listener ── */

struct battery_status_state {
    uint8_t level;
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    bool usb_present;
#endif
};

static void battery_update_cb(struct battery_status_state state) {
    struct corne_widget_status *w;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, w, node) {
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        w->state.charging = state.usb_present;
#endif
        w->state.battery = state.level;
        draw_top(w->obj, w);
    }
}

static struct battery_status_state battery_get_state(const zmk_event_t *eh) {
    const struct zmk_battery_state_changed *ev = as_zmk_battery_state_changed(eh);
    return (struct battery_status_state){
        .level = (ev != NULL) ? ev->state_of_charge : zmk_battery_state_of_charge(),
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .usb_present = zmk_usb_is_powered(),
#endif
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(corne_battery, struct battery_status_state,
                            battery_update_cb, battery_get_state)
ZMK_SUBSCRIPTION(corne_battery, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(corne_battery, zmk_usb_conn_state_changed);
#endif

/* ── BT output listener ── */

struct output_status_state {
    int active_profile_index;
    bool active_profile_connected;
    bool active_profile_bonded;
};

static void output_update_cb(struct output_status_state state) {
    struct corne_widget_status *w;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, w, node) {
        w->state.profile_index = state.active_profile_index;
        w->state.profile_connected = state.active_profile_connected;
        w->state.profile_bonded = state.active_profile_bonded;
        draw_top(w->obj, w);
        draw_mid(w->obj, w);
    }
}

static struct output_status_state output_get_state(const zmk_event_t *eh) {
    return (struct output_status_state){
        .active_profile_index = zmk_ble_active_profile_index(),
        .active_profile_connected = zmk_ble_active_profile_is_connected(),
        .active_profile_bonded = !zmk_ble_active_profile_is_open(),
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(corne_output, struct output_status_state,
                            output_update_cb, output_get_state)
ZMK_SUBSCRIPTION(corne_output, zmk_endpoint_changed);
#if defined(CONFIG_ZMK_BLE)
ZMK_SUBSCRIPTION(corne_output, zmk_ble_active_profile_changed);
#endif

/* ── Layer listener ── */

struct layer_status_state {
    uint8_t index;
    const char *label;
};

static void layer_update_cb(struct layer_status_state state) {
    struct corne_widget_status *w;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, w, node) {
        w->state.layer_index = state.index;
        w->state.layer_label = state.label;
        draw_bot(w->obj, w);
    }
}

static struct layer_status_state layer_get_state(const zmk_event_t *eh) {
    uint8_t index = zmk_keymap_highest_layer_active();
    return (struct layer_status_state){
        .index = index,
        .label = zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(index)),
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(corne_layer, struct layer_status_state,
                            layer_update_cb, layer_get_state)
ZMK_SUBSCRIPTION(corne_layer, zmk_layer_state_changed);

/* ── Widget init ── */

int corne_widget_status_init(struct corne_widget_status *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, 128, 32);

    /* 4 canvas slots along 128px (x-axis). After 270° rotation:
     *   x=96 → top of portrait, x=0 → bottom of portrait */
    lv_obj_t *top = lv_canvas_create(widget->obj);
    lv_canvas_set_buffer(top, widget->cbuf_top, CANVAS_SIZE, CANVAS_SIZE, CANVAS_COLOR_FORMAT);
    lv_obj_set_pos(top, 96, 0);

    lv_obj_t *mid = lv_canvas_create(widget->obj);
    lv_canvas_set_buffer(mid, widget->cbuf_mid, CANVAS_SIZE, CANVAS_SIZE, CANVAS_COLOR_FORMAT);
    lv_obj_set_pos(mid, 64, 0);

    lv_obj_t *bot = lv_canvas_create(widget->obj);
    lv_canvas_set_buffer(bot, widget->cbuf_bot, CANVAS_SIZE, CANVAS_SIZE, CANVAS_COLOR_FORMAT);
    lv_obj_set_pos(bot, 32, 0);

    sys_slist_append(&widgets, &widget->node);

    corne_battery_init();
    corne_output_init();
    corne_layer_init();

    return 0;
}

lv_obj_t *corne_widget_status_obj(struct corne_widget_status *widget) { return widget->obj; }

/* ── Screen entry point ── */

static struct corne_widget_status main_widget;

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    corne_widget_status_init(&main_widget, screen);
    lv_obj_align(corne_widget_status_obj(&main_widget), LV_ALIGN_CENTER, 0, 0);
    return screen;
}
