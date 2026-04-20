/*
 * Custom Corne peripheral display
 * H keycap logo with glitch + ZMK built-in widgets
 * Based on dieselsaurav's P keycap implementation
 */

#include <stdbool.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/display.h>
#include <zmk/display/widgets/battery_status.h>
#include <zmk/display/widgets/peripheral_status.h>
#include <lvgl.h>

/* ── H keycap (24px wide x 32px tall, same keycap border as Parix P) ── */

static const uint8_t raw_h_keycap[4 * 24] = {
    /* Page 0 (rows 0-7) — top border */
      0,  0,192,224,112, 48, 48, 48, 48, 48, 48, 48,
     48, 48, 48, 48, 48, 48, 48, 48,112,224,192,  0,
    /* Page 1 (rows 8-15) — sides + H top half */
      0,  0,255,255,  0,  0,254,254, 56, 56, 56, 56,
     56, 56, 56, 56,254,254,  0,  0,  0,255,255,  0,
    /* Page 2 (rows 16-23) — sides + H bottom half */
      0,  0,255,255,128,  0, 63, 63,  0,  0,  0,  0,
      0,  0,  0,  0, 63, 63,  0,  0,128,255,255,  0,
    /* Page 3 (rows 24-31) — bottom border */
      0,  0,  7, 15, 31, 31, 31, 31, 31, 31, 31, 31,
     31, 31, 31, 31, 31, 31, 31, 31, 31, 15,  7,  0,
};

#define H_WIDTH  24
#define H_HEIGHT 32
#define H_STRIDE 3

/* ── Glitch engine ── */

#define GLITCH_INTERVAL_MIN_MS 2000
#define GLITCH_INTERVAL_RANGE_MS 6000
#define GLITCH_INITIAL_DELAY_MS 3000
#define GLITCH_FRAMES_MIN 2
#define GLITCH_FRAMES_RANGE 4
#define GLITCH_TIMER_MS 150

static uint16_t glitch_seed = 42;
static uint32_t glitch_next_ms;
static uint8_t glitch_frames_left = 0;

static uint16_t glitch_rand(void) {
    glitch_seed ^= glitch_seed << 7;
    glitch_seed ^= glitch_seed >> 9;
    glitch_seed ^= glitch_seed << 8;
    return glitch_seed;
}

static void apply_glitch(uint8_t *buf) {
    if (glitch_frames_left == 0) return;
    glitch_frames_left--;
    uint8_t fx = glitch_rand() % 4;

    if (fx == 0) {
        uint8_t page = glitch_rand() % 4;
        int8_t shift = (glitch_rand() % 7) - 3;
        if (shift > 0) {
            for (int c = H_WIDTH - 1; c >= shift; c--)
                buf[page * H_WIDTH + c] = buf[page * H_WIDTH + c - shift];
        } else if (shift < 0) {
            for (int c = 0; c < H_WIDTH + shift; c++)
                buf[page * H_WIDTH + c] = buf[page * H_WIDTH + c - shift];
        }
    } else if (fx == 1) {
        uint8_t page = glitch_rand() % 4;
        uint8_t col = glitch_rand() % (H_WIDTH - 4);
        uint8_t w = 3 + (glitch_rand() % 6);
        for (uint8_t c = col; c < col + w && c < H_WIDTH; c++)
            buf[page * H_WIDTH + c] = (uint8_t)glitch_rand();
    } else if (fx == 2) {
        uint8_t page = glitch_rand() % 4;
        uint8_t col = glitch_rand() % (H_WIDTH - 4);
        uint8_t w = 4 + (glitch_rand() % 10);
        for (uint8_t c = col; c < col + w && c < H_WIDTH; c++)
            buf[page * H_WIDTH + c] = ~buf[page * H_WIDTH + c];
    } else {
        uint8_t src = glitch_rand() % 4;
        uint8_t dst = (src + 1 + (glitch_rand() % 3)) % 4;
        uint8_t col = glitch_rand() % (H_WIDTH / 2);
        uint8_t w = H_WIDTH / 3 + (glitch_rand() % (H_WIDTH / 3));
        for (uint8_t c = col; c < col + w && c < H_WIDTH; c++)
            buf[dst * H_WIDTH + c] = buf[src * H_WIDTH + c];
    }
}

/* ── Logo image rendering ── */

static void convert_h_to_lvgl_i1(const uint8_t *qmk_buf, uint8_t *out) {
    out[0] = 0xFF; out[1] = 0xFF; out[2] = 0xFF; out[3] = 0xFF;
    out[4] = 0x00; out[5] = 0x00; out[6] = 0x00; out[7] = 0xFF;
    uint8_t *pixels = out + 8;
    memset(pixels, 0, H_STRIDE * H_HEIGHT);
    for (int page = 0; page < 4; page++) {
        for (int col = 0; col < H_WIDTH; col++) {
            uint8_t val = qmk_buf[page * H_WIDTH + col];
            for (int bit = 0; bit < 8; bit++) {
                if (val & (1 << bit)) {
                    int y = page * 8 + bit;
                    pixels[y * H_STRIDE + (col / 8)] |= (0x80 >> (col % 8));
                }
            }
        }
    }
}

static uint8_t img_buf[8 + H_STRIDE * H_HEIGHT];
static lv_image_dsc_t h_dsc = {
    .header = { .cf = LV_COLOR_FORMAT_I1, .w = H_WIDTH, .h = H_HEIGHT },
    .data_size = sizeof(img_buf),
    .data = img_buf,
};

static lv_obj_t *h_img;
static int64_t last_glitch_time;
static bool logo_dirty = true;

static void update_h(bool with_glitch) {
    uint8_t buf[4 * H_WIDTH];
    memcpy(buf, raw_h_keycap, sizeof(buf));
    if (with_glitch) apply_glitch(buf);
    convert_h_to_lvgl_i1(buf, img_buf);
    h_dsc.data = img_buf;
    h_dsc.header.cf = LV_COLOR_FORMAT_I1;
    lv_image_set_src(h_img, &h_dsc);
    lv_obj_invalidate(h_img);
}

static void glitch_timer_cb(lv_timer_t *timer) {
    int64_t now = k_uptime_get();
    if (glitch_frames_left == 0) {
        if ((now - last_glitch_time) > (int64_t)glitch_next_ms) {
            last_glitch_time = now;
            glitch_frames_left = GLITCH_FRAMES_MIN + (glitch_rand() % GLITCH_FRAMES_RANGE);
            glitch_next_ms = GLITCH_INTERVAL_MIN_MS + (glitch_rand() % GLITCH_INTERVAL_RANGE_MS);
        } else if (logo_dirty) {
            update_h(false);
            logo_dirty = false;
        }
        return;
    }
    update_h(true);
    logo_dirty = true;
}

/* ── Screen layout (128x32) ──
 *
 * +----------+---------+---------+
 * | BT status|         | Battery |
 * +----------+  [H]    +---------+
 * |          |         |         |
 * +----------+---------+---------+
 */

static struct zmk_widget_battery_status battery_widget;
static struct zmk_widget_peripheral_status peripheral_widget;

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);

    /* H keycap — center */
    h_img = lv_image_create(screen);
    lv_obj_align(h_img, LV_ALIGN_CENTER, 0, 0);
    update_h(false);
    logo_dirty = false;
    last_glitch_time = k_uptime_get();
    glitch_next_ms = GLITCH_INITIAL_DELAY_MS;
    lv_timer_create(glitch_timer_cb, GLITCH_TIMER_MS, NULL);

    /* Built-in peripheral status — top left */
    zmk_widget_peripheral_status_init(&peripheral_widget, screen);
    lv_obj_align(zmk_widget_peripheral_status_obj(&peripheral_widget),
                 LV_ALIGN_TOP_LEFT, 0, 0);

    /* Built-in battery status — top right */
    zmk_widget_battery_status_init(&battery_widget, screen);
    lv_obj_align(zmk_widget_battery_status_obj(&battery_widget),
                 LV_ALIGN_TOP_RIGHT, 0, 0);

    return screen;
}
