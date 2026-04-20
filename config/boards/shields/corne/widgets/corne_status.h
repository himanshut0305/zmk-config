#pragma once

#include <lvgl.h>
#include <zephyr/kernel.h>
#include "util.h"

struct corne_widget_status {
    sys_snode_t node;
    lv_obj_t *obj;
    uint8_t cbuf_top[CANVAS_BUF_SIZE];
    uint8_t cbuf_mid[CANVAS_BUF_SIZE];
    uint8_t cbuf_bot[CANVAS_BUF_SIZE];
    struct {
        uint8_t battery;
        bool charging;
        int profile_index;
        bool profile_connected;
        bool profile_bonded;
        uint8_t layer_index;
        const char *layer_label;
    } state;
};

int corne_widget_status_init(struct corne_widget_status *widget, lv_obj_t *parent);
lv_obj_t *corne_widget_status_obj(struct corne_widget_status *widget);
