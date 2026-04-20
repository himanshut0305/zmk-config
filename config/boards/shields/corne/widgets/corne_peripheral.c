/*
 * Corne peripheral (right) status display — portrait SSD1306 128x32
 *
 * Layout (after rotation, top to bottom):
 *   Section 0 (32x32): Battery bar + connection icon
 *   Sections 1-3: empty
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

#include "corne_peripheral.h"

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

/* ── Drawing ── */

static void draw_top(lv_obj_t *widget, struct corne_widget_peripheral *w) {
    lv_obj_t *canvas = lv_obj_get_child(widget, 0);

    lv_draw_rect_dsc_t bg, fg;
    init_rect_dsc(&bg, BG_COLOR);
    init_rect_dsc(&fg, FG_COLOR);

    lv_draw_label_dsc_t lbl;
    init_label_dsc(&lbl, FG_COLOR, &lv_font_montserrat_16, LV_TEXT_ALIGN_CENTER);

    /* clear */
    lv_canvas_fill_bg(canvas, BG_COLOR, LV_OPA_COVER);

    /* battery bar */
    canvas_draw_rect(canvas, 2, 2, 22, 10, &fg);
    canvas_draw_rect(canvas, 3, 3, 20, 8, &bg);
    int fill = (w->state.battery * 18) / 100;
    if (fill < 1 && w->state.battery > 0) fill = 1;
    canvas_draw_rect(canvas, 4, 4, fill, 6, &fg);
    canvas_draw_rect(canvas, 24, 5, 2, 4, &fg);

    /* connection icon */
    canvas_draw_text(canvas, 0, 14, 32, &lbl,
                     w->state.connected ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE);

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
    struct corne_widget_peripheral *w;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, w, node) {
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        w->state.charging = state.usb_present;
#endif
        w->state.battery = state.level;
        draw_top(w->obj, w);
    }
}

static struct battery_status_state battery_get_state(const zmk_event_t *eh) {
    return (struct battery_status_state){
        .level = zmk_battery_state_of_charge(),
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .usb_present = zmk_usb_is_powered(),
#endif
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(corne_p_battery, struct battery_status_state,
                            battery_update_cb, battery_get_state)
ZMK_SUBSCRIPTION(corne_p_battery, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(corne_p_battery, zmk_usb_conn_state_changed);
#endif

/* ── Connection listener ── */

struct peripheral_status_state {
    bool connected;
};

static void connection_update_cb(struct peripheral_status_state state) {
    struct corne_widget_peripheral *w;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, w, node) {
        w->state.connected = state.connected;
        draw_top(w->obj, w);
    }
}

static struct peripheral_status_state connection_get_state(const zmk_event_t *eh) {
    return (struct peripheral_status_state){
        .connected = zmk_split_bt_peripheral_is_connected(),
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(corne_p_conn, struct peripheral_status_state,
                            connection_update_cb, connection_get_state)
ZMK_SUBSCRIPTION(corne_p_conn, zmk_split_peripheral_status_changed);

/* ── Widget init ── */

int corne_widget_peripheral_init(struct corne_widget_peripheral *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, 128, 32);

    lv_obj_t *top = lv_canvas_create(widget->obj);
    lv_canvas_set_buffer(top, widget->cbuf_top, CANVAS_SIZE, CANVAS_SIZE, CANVAS_COLOR_FORMAT);
    lv_obj_set_pos(top, 96, 0); /* rightmost = top in portrait after 270° rotation */

    sys_slist_append(&widgets, &widget->node);

    corne_p_battery_init();
    corne_p_conn_init();

    return 0;
}

lv_obj_t *corne_widget_peripheral_obj(struct corne_widget_peripheral *widget) {
    return widget->obj;
}

/* ── Screen entry point ── */

static struct corne_widget_peripheral main_widget;

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    corne_widget_peripheral_init(&main_widget, screen);
    lv_obj_align(corne_widget_peripheral_obj(&main_widget), LV_ALIGN_CENTER, 0, 0);
    return screen;
}
