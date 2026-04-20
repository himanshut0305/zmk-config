#pragma once

#include <lvgl.h>
#include <zephyr/kernel.h>
#include "util.h"

struct corne_widget_peripheral {
    sys_snode_t node;
    lv_obj_t *obj;
    uint8_t cbuf_top[CANVAS_BUF_SIZE];
    struct {
        uint8_t battery;
        bool charging;
        bool connected;
    } state;
};

int corne_widget_peripheral_init(struct corne_widget_peripheral *widget, lv_obj_t *parent);
lv_obj_t *corne_widget_peripheral_obj(struct corne_widget_peripheral *widget);
