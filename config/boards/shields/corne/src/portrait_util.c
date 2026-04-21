/*
 * Portrait display rotation and drawing utilities
 * Adapted from ZMK's nice!view for SSD1306 128x32 with inversion-on
 */
#include <zephyr/kernel.h>
#include <string.h>
#include "portrait_util.h"

void rotate_canvas(lv_obj_t *canvas) {
    uint8_t *buf = lv_canvas_get_draw_buf(canvas)->data;
    static uint8_t buf_copy[CANVAS_BUF_SIZE];
    memcpy(buf_copy, buf, sizeof(buf_copy));

    const uint32_t stride = lv_draw_buf_width_to_stride(CANVAS_SIZE, CANVAS_COLOR_FORMAT);
    lv_draw_sw_rotate(buf_copy, buf, CANVAS_SIZE, CANVAS_SIZE, stride, stride,
                      LV_DISPLAY_ROTATION_270, CANVAS_COLOR_FORMAT);
}

void init_label_dsc(lv_draw_label_dsc_t *dsc, lv_color_t color, const lv_font_t *font,
                    lv_text_align_t align) {
    lv_draw_label_dsc_init(dsc);
    dsc->color = color;
    dsc->font = font;
    dsc->align = align;
}

void init_rect_dsc(lv_draw_rect_dsc_t *dsc, lv_color_t color) {
    lv_draw_rect_dsc_init(dsc);
    dsc->bg_color = color;
}

void canvas_draw_rect(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                      lv_draw_rect_dsc_t *dsc) {
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);
    lv_area_t coords = {x, y, x + w - 1, y + h - 1};
    lv_draw_rect(&layer, dsc, &coords);
    lv_canvas_finish_layer(canvas, &layer);
}

void canvas_draw_text(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, lv_coord_t max_w,
                      lv_draw_label_dsc_t *dsc, const char *txt) {
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);
    dsc->text = txt;
    lv_area_t coords = {x, y, x + max_w - 1, y + CANVAS_SIZE};
    lv_draw_label(&layer, dsc, &coords);
    lv_canvas_finish_layer(canvas, &layer);
}

void draw_battery_bar(lv_obj_t *canvas, uint8_t level) {
    lv_draw_rect_dsc_t fg_dsc, bg_dsc;
    init_rect_dsc(&fg_dsc, FG_COLOR);
    init_rect_dsc(&bg_dsc, BG_COLOR);

    /* battery outline */
    canvas_draw_rect(canvas, 1, 2, 24, 10, &fg_dsc);
    canvas_draw_rect(canvas, 2, 3, 22, 8, &bg_dsc);
    /* fill proportional to level */
    int fill = (level * 20) / 100;
    if (fill < 1 && level > 0) fill = 1;
    canvas_draw_rect(canvas, 3, 4, fill, 6, &fg_dsc);
    /* nub */
    canvas_draw_rect(canvas, 25, 5, 2, 4, &fg_dsc);
}
