/*
 * Corne SSD1306 128x32 portrait display utilities
 * Uses L8 canvas rotation (same technique as nice!view)
 */

#pragma once

#include <lvgl.h>

/* SSD1306 128x32 in portrait: 32px wide, 128px tall
 * Canvas must be square, matching the narrow dimension */
#define CANVAS_SIZE 32
#define CANVAS_COLOR_FORMAT LV_COLOR_FORMAT_L8 /* smallest type supported by sw_rotate */
#define CANVAS_BUF_SIZE                                                                            \
    LV_CANVAS_BUF_SIZE(CANVAS_SIZE, CANVAS_SIZE, LV_COLOR_FORMAT_GET_BPP(CANVAS_COLOR_FORMAT),     \
                       LV_DRAW_BUF_STRIDE_ALIGN)

#define BG_COLOR lv_color_black()
#define FG_COLOR lv_color_white()

void rotate_canvas(lv_obj_t *canvas);

void init_label_dsc(lv_draw_label_dsc_t *dsc, lv_color_t color, const lv_font_t *font,
                    lv_text_align_t align);
void init_rect_dsc(lv_draw_rect_dsc_t *dsc, lv_color_t color);

void canvas_draw_rect(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                      lv_draw_rect_dsc_t *dsc);
void canvas_draw_text(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, lv_coord_t max_w,
                      lv_draw_label_dsc_t *dsc, const char *txt);
