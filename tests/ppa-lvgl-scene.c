/* SPDX-License-Identifier: MIT
 * Deterministic software/PPA integration comparison. Run with GUI stopped:
 * KORVO_PPA_DISABLE=1 ./ppa-lvgl-scene /root/ppa-sw.rgb565
 * ./ppa-lvgl-scene /root/ppa-hw.rgb565
 * Compare with compare-ppa-scene.py on the host.
 */
#define _GNU_SOURCE
#include "korvo_ppa_lvgl.h"
#include "linux_console.h"
#include <src/others/sysmon/lv_sysmon.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
static uint16_t rgb[96 * 64];
static uint32_t argb[96 * 64];
static lv_image_dsc_t images[2];
static uint32_t canvas_pixels[64 * 32];
static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, unsigned color, int radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y); lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, 255, 0);
    lv_obj_set_style_radius(o, radius, 0);
    return o;
}
int main(int argc, char **argv)
{
    if(argc != 2) return 2;
    console_graphics();
    lv_init();
    lv_display_t *d = korvo_ppa_display_create();
    if(!d) return 1;
#if LV_USE_PERF_MONITOR
    lv_sysmon_hide_performance(d);
#endif
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x183048), 0);
    lv_obj_t *shadowed = box(screen, 20, 20, 300, 80, 0xe8622a, 0);
    lv_obj_set_style_shadow_width(shadowed, 12, 0);
    lv_obj_set_style_shadow_opa(shadowed, 120, 0);
    lv_obj_set_style_shadow_ofs_y(shadowed, 3, 0);
    shadowed = box(screen, 350, 20, 300, 80, 0x52a010, 24);
    lv_obj_set_style_shadow_width(shadowed, 10, 0);
    lv_obj_set_style_shadow_opa(shadowed, 150, 0);
    box(screen, 21, 120, 299, 81, 0xa050ee, 0); /* odd coordinates/width */
    lv_obj_t *gradient = box(screen, 350, 120, 300, 80, 0x2050c0, 12);
    lv_obj_set_style_bg_grad_color(gradient, lv_color_hex(0x50d040), 0);
    lv_obj_set_style_bg_grad_dir(gradient, LV_GRAD_DIR_HOR, 0);
    for(unsigned y = 0; y < 64; y++) for(unsigned x = 0; x < 96; x++) {
        rgb[y * 96 + x] = ((x & 31) << 11) | ((y & 63) << 5) | ((x + y) & 31);
        argb[y * 96 + x] = ((x * 255 / 95) << 24) | 0xff8020;
    }
    for(unsigned i = 0; i < 2; i++) {
        images[i].header.magic = LV_IMAGE_HEADER_MAGIC;
        images[i].header.cf = i ? LV_COLOR_FORMAT_ARGB8888 : LV_COLOR_FORMAT_RGB565;
        images[i].header.w = 96; images[i].header.h = 64;
        images[i].header.stride = 96 * (i ? 4 : 2);
        images[i].data = i ? (uint8_t *)argb : (uint8_t *)rgb;
        images[i].data_size = 96 * 64 * (i ? 4 : 2);
    }
    for(unsigned i = 0; i < 4; i++) {
        lv_obj_t *im = lv_image_create(screen);
        lv_image_set_src(im, &images[i & 1]);
        lv_obj_set_pos(im, 20 + i * 150, 230);
        if(i >= 2) lv_obj_set_style_image_opa(im, 128, 0);
    }
    lv_obj_t *clip = box(screen, 20, 330, 80, 52, 0x123456, 0);
    lv_obj_t *im = lv_image_create(clip);
    lv_image_set_src(im, &images[0]); lv_obj_set_pos(im, -10, -6);
    lv_obj_t *translucent = box(screen, 350, 330, 200, 60, 0x1234ff, 18);
    lv_obj_set_style_bg_opa(translucent, 128, 0);
    translucent = box(screen, 677, 120, 79, 55, 0xe0a040, 9);
    lv_obj_set_style_bg_opa(translucent, 170, 0);
    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, "Native 800x480: fill / rounded / gradient / RGB565 / ARGB alpha / clipped image");
    lv_obj_set_width(label, 740); lv_obj_set_pos(label, 20, 420);
    label = lv_label_create(screen);
    lv_label_set_text(label, "Cached colored text\nwith wrapping / symbols " LV_SYMBOL_OK);
    lv_obj_set_width(label, 220); lv_obj_set_pos(label, 111, 331);
    lv_obj_set_style_text_color(label, lv_color_hex(0x6bc59d), 0);
    lv_obj_set_style_text_opa(label, 180, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    /* A unit with no eligible task must report IDLE. Reporting busy here
     * can spin forever inside lv_canvas_finish_layer, before the UI starts. */
    lv_obj_t *canvas = lv_canvas_create(screen);
    lv_canvas_set_buffer(canvas, canvas_pixels, 64, 32, LV_COLOR_FORMAT_ARGB8888);
    lv_obj_set_pos(canvas, 600, 350);
    lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);
    lv_layer_t canvas_layer;
    lv_canvas_init_layer(canvas, &canvas_layer);
    lv_draw_rect_dsc_t canvas_rect;
    lv_draw_rect_dsc_init(&canvas_rect);
    canvas_rect.bg_color = lv_color_hex(0x2040c0); canvas_rect.bg_opa = 255;
    lv_area_t canvas_area = {0, 0, 63, 31};
    lv_draw_rect(&canvas_layer, &canvas_rect, &canvas_area);
    lv_canvas_finish_layer(canvas, &canvas_layer);
    if((canvas_pixels[16 * 64 + 32] & 0xffffff) != 0x2040c0) return 1;
    puts("Offscreen ARGB canvas completes and renders: OK");
    lv_refr_now(d);
    /* Exercise cache hits with alternating shadow radii, not only misses. */
    for(unsigned repeat = 0; repeat < 2; repeat++) {
        lv_obj_invalidate(screen);
        lv_refr_now(d);
    }
    int fd = open("/dev/fb0", O_RDONLY | O_CLOEXEC);
    FILE *out = fopen(argv[1], "wb");
    if(fd < 0 || !out) { perror("capture"); return 1; }
    unsigned char row[1600];
    for(unsigned y = 0; y < 480; y++) {
        if(read(fd, row, sizeof(row)) != sizeof(row) || fwrite(row, 1, sizeof(row), out) != sizeof(row)) return 1;
    }
    close(fd); fclose(out); lv_deinit();
    /* Keep KD_GRAPHICS until the diagnostic has checked the patterned RGB565
     * image, including odd source X, rather than a uniform console background.
     */
    if(getenv("KORVO_PPA_DEVICE_TEST") && system("/usr/bin/ppa-test")) return 1;
    return 0;
}
