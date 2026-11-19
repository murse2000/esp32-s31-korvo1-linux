/* SPDX-License-Identifier: MIT
 * Reproducible native-resolution scroll/slider rendering and CPU accounting.
 * Stop S97lvgl-music-demo before running; argv: scroll|slider [frames].
 */
/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "korvo_ppa_lvgl.h"
#include "linux_console.h"
#include <src/others/sysmon/lv_sysmon.h>
#include <src/core/lv_obj_scroll_private.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>
static void capture(const char *prefix, int frame)
{
    if(!prefix) return;
    if(frame != 0 && frame != 1 && frame != 5 && frame != 29 && frame != 30 && frame != 31 && frame != 60 && frame != 61 && frame != 89) return;
    char name[256]; snprintf(name, sizeof(name), "%s-%02d.rgb565", prefix, frame);
    FILE *in = fopen("/dev/fb0", "rb"), *out = fopen(name, "wb");
    if(!in || !out) { perror("capture"); exit(1); }
    unsigned char row[1600];
    for(unsigned y = 0; y < 480; y++)
        if(fread(row, 1, sizeof(row), in) != sizeof(row) || fwrite(row, 1, sizeof(row), out) != sizeof(row)) exit(1);
    fclose(in); fclose(out);
}
static uint64_t now_us(void)
{
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000;
}
static uint32_t tick(void) { return now_us() / 1000; }
static uint64_t cpu_us(struct rusage *r)
{
    return (uint64_t)(r->ru_utime.tv_sec + r->ru_stime.tv_sec) * 1000000 +
        r->ru_utime.tv_usec + r->ru_stime.tv_usec;
}
static void custom_paint(lv_event_t *event)
{
    lv_obj_t *obj = lv_event_get_target(event);
    lv_area_t area; lv_obj_get_coords(obj, &area);
    lv_draw_rect_dsc_t dsc; lv_draw_rect_dsc_init(&dsc);
    dsc.bg_opa = 255; dsc.bg_color = lv_color_hex((area.y1 & 32) ? 0x2050b0 : 0xd05020);
    lv_draw_rect(lv_event_get_layer(event), &dsc, &area);
}
int main(int argc, char **argv)
{
    if(argc < 2) return 2;
    int scroll = !strncmp(argv[1], "scroll", 6), horizontal = !strcmp(argv[1], "scroll-x");
    int frames = argc > 2 ? atoi(argv[2]) : 90;
    if(frames < 1 || frames > 10000) return 2;
    console_graphics(); lv_init(); lv_tick_set_cb(tick);
    lv_display_t *d = korvo_ppa_display_create();
    if(!d) return 1;
    lv_sysmon_hide_performance(d);
    lv_obj_t *screen = lv_screen_active(), *control;
    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, scroll ? "Native 800x480 scrolling" : "Native 800x480 slider");
    lv_obj_set_pos(title, 24, 10);
    if(scroll) {
        if(getenv("KORVO_INTERACTION_TABVIEW")) {
            lv_obj_t *tabs = lv_tabview_create(screen);
            control = lv_tabview_add_tab(tabs, "Scrolling");
            lv_tabview_add_tab(tabs, "Other");
        } else {
            control = lv_obj_create(screen);
            lv_obj_set_pos(control, 20, 45); lv_obj_set_size(control, 760, 415);
        }
        lv_obj_set_flex_flow(control, horizontal ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN);
        for(unsigned i = 0; i < 32; i++) {
            lv_obj_t *row = lv_button_create(control);
            lv_obj_set_size(row, 690, horizontal ? 330 : 60);
            lv_obj_t *label = lv_label_create(row);
            lv_label_set_text_fmt(label, "Row %02u: scrolling text and rounded controls", i);
            lv_obj_center(label);
        }
        if(getenv("KORVO_INTERACTION_FLOATING")) {
            lv_obj_t *fixed = lv_label_create(control);
            lv_obj_add_flag(fixed, LV_OBJ_FLAG_FLOATING);
            lv_label_set_text(fixed, "Fixed child"); lv_obj_set_pos(fixed, 90, 90);
        }
        if(getenv("KORVO_INTERACTION_OVERLAY")) {
            lv_obj_t *overlay = lv_button_create(lv_layer_top());
            lv_obj_set_pos(overlay, 350, 200); lv_obj_set_size(overlay, 80, 60);
        }
        if(getenv("KORVO_INTERACTION_GRADIENT")) {
            lv_obj_set_style_bg_grad_color(control, lv_color_hex(0x80a0c0), 0);
            lv_obj_set_style_bg_grad_dir(control, LV_GRAD_DIR_VER, 0);
        }
        if(getenv("KORVO_INTERACTION_TRANSPARENT")) lv_obj_set_style_bg_opa(control, 0, 0);
        if(getenv("KORVO_INTERACTION_CUSTOM"))
            lv_obj_add_event_cb(lv_obj_get_child(control, 4), custom_paint, LV_EVENT_DRAW_MAIN, NULL);
    } else {
        control = lv_slider_create(screen);
        lv_obj_set_pos(control, 70, 200); lv_obj_set_size(control, 660, 36);
        lv_slider_set_range(control, 0, 1000);
    }
    lv_refr_now(d);
    struct rusage before, after;
    getrusage(RUSAGE_SELF, &before);
    uint64_t start = now_us(), render_us = 0;
    for(int i = 0; i < frames; i++) {
        uint64_t begin = now_us();
        int position = i % 120;
        if(position > 60) position = 120 - position;
        if(scroll) {
            int steps = getenv("KORVO_INTERACTION_COALESCE") ? 3 : 1;
            int current = horizontal ? lv_obj_get_scroll_x(control) : lv_obj_get_scroll_y(control);
            int delta = current - position * 9;
            for(int sub = 0; sub < steps; sub++) {
                int part = delta / (steps - sub); delta -= part;
                lv_obj_scroll_by_raw(control, horizontal ? part : 0, horizontal ? 0 : part);
            }
        }
        else lv_slider_set_value(control, position * 16, LV_ANIM_OFF);
        if(scroll && getenv("KORVO_INTERACTION_CONTENT_CHANGE") && i == 30) {
            lv_obj_t *row = lv_obj_get_child(control, horizontal ? 0 : 5);
            lv_label_set_text(lv_obj_get_child(row, 0), "Changed while scrolling");
            lv_obj_set_style_bg_color(control, lv_color_hex(0xc0d0e0), 0);
        }
        lv_refr_now(d);
        capture(getenv("KORVO_INTERACTION_CAPTURE"), i);
        render_us += now_us() - begin;
        uint64_t due = start + (i + 1) * 33333ULL, now = now_us();
        if(due > now) {
            struct timespec pause = {(due - now) / 1000000, ((due - now) % 1000000) * 1000};
            nanosleep(&pause, NULL);
        }
    }
    uint64_t wall = now_us() - start;
    getrusage(RUSAGE_SELF, &after);
    printf("interaction %s frames=%d wall_us=%llu render_us=%llu cpu_us=%llu cpu_pct=%u minflt=%ld majflt=%ld\n",
        argv[1], frames, (unsigned long long)wall, (unsigned long long)render_us,
        (unsigned long long)(cpu_us(&after) - cpu_us(&before)),
        (unsigned)((cpu_us(&after) - cpu_us(&before)) * 100 / wall),
        after.ru_minflt - before.ru_minflt, after.ru_majflt - before.ru_majflt);
    lv_deinit();
    return 0;
}
