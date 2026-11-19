/* SPDX-License-Identifier: MIT
 * LVGL 9.2: cached direct/partial rendering, DMA2D flush, PPA fill/blend.
 * Unsupported draw tasks remain on the LVGL software renderer.
 */
#define _GNU_SOURCE
#include "korvo_ppa_lvgl.h"
#include "korvo_ppa.h"
#include <src/draw/lv_draw_private.h>
#include <src/draw/sw/lv_draw_sw.h>
#include <src/draw/sw/lv_draw_sw_mask_private.h>
#include <src/display/lv_display_private.h>
#include <src/core/lv_obj_draw_private.h>
#include <src/core/lv_obj_class_private.h>
#include <src/misc/lv_event_private.h>
#include <src/misc/lv_area_private.h>
#include <src/drivers/display/fb/lv_linux_fbdev.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define DRAW_ID 203
struct ppa_display {
    int fd, fb;
    uint8_t *buffer[2], *pixels;
    unsigned stride, size, render_size, capabilities;
    int direct;
    int failed;
    struct korvo_ppa_stats initial;
    uint64_t sw_us[16], sw_count[16], operation_us[6], operation_count[6];
    int profile, flush_only;
    lv_draw_unit_t *sw_unit;
    int32_t (*sw_dispatch)(lv_draw_unit_t *, lv_layer_t *);
    lv_display_t *display;
    lv_obj_t *scroll_obj;
    lv_area_t scroll_outer, scroll_flush_area;
    int32_t scroll_x, scroll_y;
    int scroll_flush_pending, scroll_disabled;
    uint64_t scroll_moves, scroll_pixels, scroll_calls, scroll_queued, scroll_dirty;
    unsigned scroll_logged;
    unsigned scroll_reason;
    lv_area_t scroll_dynamic[24];
    unsigned scroll_dynamic_count;
    uint64_t scroll_dynamic_pixels;
};
struct ppa_unit { lv_draw_unit_t base; struct ppa_display *ctx; };
static struct ppa_display *profile_ctx;
static struct ppa_display *active_ctx;
static int submit(struct ppa_display *c, struct korvo_ppa_request *q);
/* LVGL timer busy time includes DMA waits and page-in stalls. Account Linux
 * CPU time instead; idle+iowait do not consume the application's CPU.
 */
uint32_t korvo_linux_cpu_idle(void)
{
    static uint64_t previous_total, previous_idle;
    static uint32_t idle_percent = 100;
    char buffer[256];
    int fd = open("/proc/stat", O_RDONLY | O_CLOEXEC);
    if(fd < 0) return lv_timer_get_idle();
    ssize_t n = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);
    if(n < 4 || memcmp(buffer, "cpu ", 4)) return lv_timer_get_idle();
    buffer[n] = 0;
    char *p = buffer + 4, *end;
    uint64_t total = 0, idle = 0;
    for(unsigned i = 0; i < 8; i++) {
        uint64_t value = strtoull(p, &end, 10);
        if(end == p) return idle_percent;
        total += value;
        if(i == 3 || i == 4) idle += value;
        p = end;
    }
    if(previous_total && total > previous_total) {
        uint64_t delta = total - previous_total, free_time = idle - previous_idle;
        idle_percent = free_time >= delta ? 100 : free_time * 100 / delta;
    }
    previous_total = total; previous_idle = idle;
    return idle_percent;
}
static uint64_t microseconds(void)
{
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000;
}
static int custom_draw(lv_obj_t *obj)
{
    if(lv_obj_has_flag(obj, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS)) return 1;
    for(unsigned i = 0; i < lv_obj_get_event_count(obj); i++) {
        lv_event_code_t filter = lv_obj_get_event_dsc(obj, i)->filter & ~LV_EVENT_PREPROCESS;
        if(filter == LV_EVENT_ALL || (filter >= LV_EVENT_DRAW_MAIN_BEGIN && filter <= LV_EVENT_DRAW_POST_END)) return 1;
    }
    return 0;
}
static int floating_children(lv_obj_t *obj)
{
    if(custom_draw(obj)) return 1;
    for(unsigned i = 0; i < lv_obj_get_child_count(obj); i++) {
        lv_obj_t *child = lv_obj_get_child(obj, i);
        if(lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN)) continue;
        if(lv_obj_has_flag(child, LV_OBJ_FLAG_FLOATING) || floating_children(child)) return 1;
    }
    return 0;
}
static int pure_background_class(lv_obj_t *obj)
{
    if(lv_obj_check_type(obj, &lv_obj_class)) return 1;
#if LV_USE_TABVIEW
    if(lv_obj_check_type(obj, &lv_tabview_class)) return 1;
#endif
#if LV_USE_LIST
    if(lv_obj_check_type(obj, &lv_list_class)) return 1;
#endif
    return 0;
}
static int uniform_background(struct ppa_display *c, lv_obj_t *obj, const lv_area_t *inner)
{
    for(lv_obj_t *node = obj; node; node = lv_obj_get_parent(node)) {
        c->scroll_reason = 81;
        if(!pure_background_class(node) || (node != obj && custom_draw(node))) return 0;
        unsigned opa = lv_obj_get_style_bg_opa(node, LV_PART_MAIN);
        c->scroll_reason = 82;
        if((opa && lv_obj_get_style_bg_grad_dir(node, LV_PART_MAIN) != LV_GRAD_DIR_NONE) ||
           lv_obj_get_style_bg_image_src(node, LV_PART_MAIN)) return 0;
        if(opa == 255) { c->scroll_reason = 0; return 1; }
        c->scroll_reason = 83;
        if(lv_obj_get_style_shadow_width(node, LV_PART_MAIN) > 0 &&
           lv_obj_get_style_shadow_opa(node, LV_PART_MAIN) > LV_OPA_MIN) return 0;
        lv_obj_t *parent = lv_obj_get_parent(node);
        if(!parent) return 0;
        for(int32_t i = 0; i < lv_obj_get_index(node); i++) {
            c->scroll_reason = 84;
            lv_obj_t *sibling = lv_obj_get_child(parent, i);
            if(lv_obj_has_flag(sibling, LV_OBJ_FLAG_HIDDEN)) continue;
            lv_area_t bounds, overlap; lv_obj_get_coords(sibling, &bounds);
            lv_area_increase(&bounds, lv_obj_get_ext_draw_size(sibling), lv_obj_get_ext_draw_size(sibling));
            if(lv_area_intersect(&overlap, &bounds, inner)) return 0;
        }
    }
    return 0;
}
static int dynamic_regions(struct ppa_display *c, lv_obj_t *obj, const lv_area_t *inner, int dx, int dy)
{
    if(lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return 1;
    if(custom_draw(obj) || lv_obj_has_flag(obj, LV_OBJ_FLAG_FLOATING)) {
        lv_area_t bounds, clipped; lv_obj_get_coords(obj, &bounds);
        lv_area_increase(&bounds, lv_obj_get_ext_draw_size(obj), lv_obj_get_ext_draw_size(obj));
        if(lv_obj_has_flag(obj, LV_OBJ_FLAG_FLOATING)) {
            lv_area_t ghost = {bounds.x1 + dx, bounds.y1 + dy, bounds.x2 + dx, bounds.y2 + dy};
            lv_area_join(&bounds, &bounds, &ghost);
        }
        if(lv_area_intersect(&clipped, &bounds, inner)) {
            if(c->scroll_dynamic_count >= 24) return 0;
            c->scroll_dynamic[c->scroll_dynamic_count++] = clipped;
            c->scroll_dynamic_pixels += (uint64_t)lv_area_get_width(&clipped) * lv_area_get_height(&clipped);
        }
    }
    for(unsigned i = 0; i < lv_obj_get_child_count(obj); i++)
        if(!dynamic_regions(c, lv_obj_get_child(obj, i), inner, dx, dy)) return 0;
    return 1;
}
static int normal_transform(lv_obj_t *obj)
{
    return lv_obj_get_style_transform_rotation(obj, LV_PART_MAIN) == 0 &&
        lv_obj_get_style_transform_skew_x(obj, LV_PART_MAIN) == 0 &&
        lv_obj_get_style_transform_skew_y(obj, LV_PART_MAIN) == 0 &&
        lv_obj_get_style_transform_scale_x(obj, LV_PART_MAIN) == 256 &&
        lv_obj_get_style_transform_scale_y(obj, LV_PART_MAIN) == 256;
}
static int avoid_overlay(lv_area_t *area, lv_obj_t *overlay)
{
    lv_area_t bounds, intersection;
    if(lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN)) return 1;
    lv_obj_get_coords(overlay, &bounds);
    lv_area_increase(&bounds, lv_obj_get_ext_draw_size(overlay), lv_obj_get_ext_draw_size(overlay));
    if(!lv_area_intersect(&intersection, area, &bounds)) return 1;
    if(bounds.y2 >= area->y2 && bounds.y1 > area->y1) area->y2 = bounds.y1 - 1;
    else if(bounds.y1 <= area->y1 && bounds.y2 < area->y2) area->y1 = bounds.y2 + 1;
    else if(bounds.x2 >= area->x2 && bounds.x1 > area->x1) area->x2 = bounds.x1 - 1;
    else if(bounds.x1 <= area->x1 && bounds.x2 < area->x2) area->x1 = bounds.x2 + 1;
    else return 0;
    return 1;
}
static int scroll_region(struct ppa_display *c, lv_obj_t *obj, lv_area_t *outer, lv_area_t *inner)
{
    c->scroll_reason = 1;
    if(lv_obj_get_style_opa_recursive(obj, LV_PART_MAIN) != 255 ||
       lv_obj_has_flag(obj, LV_OBJ_FLAG_OVERFLOW_VISIBLE) ||
       lv_obj_get_style_blend_mode(obj, LV_PART_MAIN) != LV_BLEND_MODE_NORMAL ||
       lv_obj_get_style_bg_grad_dir(obj, LV_PART_MAIN) != LV_GRAD_DIR_NONE ||
       lv_obj_get_style_bg_image_src(obj, LV_PART_MAIN)) return 0;
    lv_obj_get_coords(obj, outer);
    lv_obj_get_content_coords(obj, inner);
    int32_t r = lv_obj_get_style_radius(obj, LV_PART_MAIN);
    r = LV_MIN(r, LV_MIN(lv_area_get_width(outer), lv_area_get_height(outer)) / 2);
    inner->x1 = LV_MAX(inner->x1, outer->x1 + r);
    inner->x2 = LV_MIN(inner->x2, outer->x2 - r);
    inner->y1 = LV_MAX(inner->y1, outer->y1 + r);
    inner->y2 = LV_MIN(inner->y2, outer->y2 - r);
    lv_area_t screen = {0, 0, 799, 479};
    c->scroll_reason = 2;
    if(!lv_area_intersect(outer, outer, &screen) || !lv_area_intersect(inner, inner, outer)) return 0;
    for(lv_obj_t *node = obj; node; node = lv_obj_get_parent(node)) {
        c->scroll_reason = 3;
        if(lv_obj_has_flag(node, LV_OBJ_FLAG_HIDDEN) || !normal_transform(node) ||
           lv_obj_get_style_blend_mode(node, LV_PART_MAIN) != LV_BLEND_MODE_NORMAL ||
           lv_obj_get_style_opa_layered(node, LV_PART_MAIN) != 255) return 0;
        lv_obj_t *parent = lv_obj_get_parent(node);
        if(!parent) break;
        lv_area_t clip;
        lv_obj_get_coords(parent, &clip);
        if(!lv_obj_has_flag(parent, LV_OBJ_FLAG_OVERFLOW_VISIBLE)) {
            c->scroll_reason = 4;
            if(!lv_area_intersect(outer, outer, &clip)) return 0;
            int32_t parent_r = LV_MIN(LV_MAX(lv_obj_get_style_radius(parent, LV_PART_MAIN),
                                             lv_obj_get_style_border_width(parent, LV_PART_MAIN)),
                                      LV_MIN(lv_area_get_width(&clip), lv_area_get_height(&clip)) / 2);
            lv_area_increase(&clip, -parent_r, -parent_r);
            if(!lv_area_intersect(inner, inner, &clip)) return 0;
        }
        for(int32_t i = lv_obj_get_index(node) + 1; i < (int32_t)lv_obj_get_child_count(parent); i++)
            if(!avoid_overlay(inner, lv_obj_get_child(parent, i))) { c->scroll_reason = 5; return 0; }
    }
    lv_obj_t *layers[2] = {lv_display_get_layer_top(c->display), lv_display_get_layer_sys(c->display)};
    for(unsigned layer = 0; layer < 2; layer++)
        for(unsigned i = 0; i < lv_obj_get_child_count(layers[layer]); i++)
            if(!avoid_overlay(inner, lv_obj_get_child(layers[layer], i))) { c->scroll_reason = 6; return 0; }
    c->scroll_reason = 7;
    return lv_area_get_width(inner) >= 128 && lv_area_get_height(inner) >= 64 && uniform_background(c, obj, inner);
}
/* Called by the optional LVGL core hook after coordinates/events update.
 * Defer movement until REFR_START, coalescing all pointer reports per frame.
 */
bool korvo_lvgl_scroll(lv_obj_t *obj, int32_t x, int32_t y)
{
    struct ppa_display *c = active_ctx;
    lv_area_t outer, inner;
    if(c) c->scroll_calls++;
    if(c && c->profile && c->scroll_logged < 4 && lv_obj_is_scrolling(obj)) {
        lv_obj_t *parent = lv_obj_get_parent(obj);
        lv_area_t bounds; lv_obj_get_coords(obj, &bounds);
        fprintf(stderr, "scroll-profile: reason=%u bg=%u opa=%u layered=%u grad=%d image=%p overflow=%d floating_or_draw=%d base=%d parent_bg=%u bounds=%d,%d,%d,%d\n", c->scroll_reason,
            lv_obj_get_style_bg_opa(obj, 0), lv_obj_get_style_opa_recursive(obj, 0),
            lv_obj_get_style_opa_layered(obj, 0), lv_obj_get_style_bg_grad_dir(obj, 0),
            lv_obj_get_style_bg_image_src(obj, 0), lv_obj_has_flag(obj, LV_OBJ_FLAG_OVERFLOW_VISIBLE),
            floating_children(obj), lv_obj_check_type(obj, &lv_obj_class),
            parent ? lv_obj_get_style_bg_opa(parent, 0) : 0,
            (int)bounds.x1, (int)bounds.y1, (int)bounds.x2, (int)bounds.y2);
        c->scroll_logged++;
        if(c->scroll_logged == 1) for(lv_obj_t *node = obj; node; node = lv_obj_get_parent(node))
            fprintf(stderr, "scroll-profile: ancestor=%s bg=%u draw=%d border=%d radius=%d opacity=%u\n",
                lv_obj_get_class(node)->name, lv_obj_get_style_bg_opa(node, 0), custom_draw(node),
                (int)lv_obj_get_style_border_width(node, 0), (int)lv_obj_get_style_radius(node, 0),
                lv_obj_get_style_opa_recursive(node, 0));
    }
    if(!c || !c->direct || c->failed || c->scroll_disabled ||
       lv_obj_get_display(obj) != c->display || c->display->rendering_in_progress ||
       lv_obj_get_screen(obj) != lv_display_get_screen_active(c->display) ||
       c->display->flushing || !scroll_region(c, obj, &outer, &inner)) return false;
    if(c->scroll_obj && c->scroll_obj != obj) {
        if(lv_obj_is_valid(c->scroll_obj)) lv_obj_invalidate(c->scroll_obj);
        c->scroll_obj = NULL; c->scroll_x = c->scroll_y = 0;
        return false;
    }
    if(!c->scroll_obj) { c->scroll_obj = obj; c->scroll_outer = outer; }
    c->scroll_x += x; c->scroll_y += y;
    c->scroll_queued++;
    lv_timer_resume(c->display->refr_timer);
    return true;
}
static void invalidate_rect(lv_obj_t *obj, int x1, int y1, int x2, int y2)
{
    if(x1 <= x2 && y1 <= y2) {
        lv_area_t a = {x1, y1, x2, y2}; lv_obj_invalidate_area(obj, &a);
    }
}
static void scroll_refresh(lv_event_t *event)
{
    struct ppa_display *c = lv_event_get_user_data(event);
    lv_obj_t *obj = c->scroll_obj;
    if(!obj) return;
    int32_t dx = c->scroll_x, dy = c->scroll_y;
    c->scroll_obj = NULL; c->scroll_x = c->scroll_y = 0;
    if(!lv_obj_is_valid(obj)) { lv_obj_invalidate(lv_screen_active()); return; }
    lv_area_t outer, inner, intersection;
    if(!scroll_region(c, obj, &outer, &inner) || memcmp(&outer, &c->scroll_outer, sizeof(outer)) ||
       abs(dx) >= lv_area_get_width(&inner) / 2 || abs(dy) >= lv_area_get_height(&inner) / 2) {
        lv_obj_invalidate(obj); return;
    }
    /* Existing dirty pixels cannot be used as a source. Style/animation and
     * content changes therefore keep the normal complete redraw path.
     */
    for(unsigned i = 0; i < c->display->inv_p; i++)
        if(lv_area_intersect(&intersection, &inner, &c->display->inv_areas[i])) {
            c->scroll_dirty++;
            lv_obj_invalidate(obj); return;
        }
    if(!dx && !dy) return;
    c->scroll_dynamic_count = 0; c->scroll_dynamic_pixels = 0;
    if(!dynamic_regions(c, obj, &inner, dx, dy) || c->scroll_dynamic_pixels >
       (uint64_t)lv_area_get_width(&inner) * lv_area_get_height(&inner) / 2) {
        lv_obj_invalidate(obj); return;
    }
    lv_area_t dst = {inner.x1 + dx, inner.y1 + dy, inner.x2 + dx, inner.y2 + dy};
    lv_area_intersect(&dst, &dst, &inner);
    unsigned bytes = lv_area_get_width(&dst) * 2, rows = lv_area_get_height(&dst);
    int accelerated = 0;
    if(c->capabilities & (1U << KORVO_PPA_REUSE)) {
        lv_area_t aligned = dst;
        if(aligned.x1 & 1) aligned.x1++;
        if(lv_area_get_width(&aligned) & 1) aligned.x2--;
        struct korvo_ppa_request q = {.operation = KORVO_PPA_REUSE,
            .source = {KORVO_PPA_FRAMEBUFFER, 0, 800, 480}, .destination = {0, 0, 800, 480},
            .source_x = aligned.x1 - dx, .source_y = aligned.y1 - dy,
            .destination_x = aligned.x1, .destination_y = aligned.y1,
            .width = lv_area_get_width(&aligned), .height = rows};
        if(!submit(c, &q)) {
            accelerated = 1;
            for(unsigned y = dst.y1; y <= (unsigned)dst.y2; y++) {
                if(aligned.x1 != dst.x1) memcpy(c->buffer[0] + y * 1600 + dst.x1 * 2,
                    c->pixels + (y - dy) * 1600 + (dst.x1 - dx) * 2, 2);
                if(aligned.x2 != dst.x2) memcpy(c->buffer[0] + y * 1600 + dst.x2 * 2,
                    c->pixels + (y - dy) * 1600 + (dst.x2 - dx) * 2, 2);
            }
        }
    }
    if(!accelerated) for(unsigned i = 0; i < rows; i++) {
        int y = dy > 0 ? dst.y2 - i : dst.y1 + i;
        /* A failed transfer may have partially overwritten the canvas.
         * The previous scanout image remains intact until final flush.
         */
        if(c->failed) memcpy(c->buffer[0] + y * 1600 + dst.x1 * 2,
                             c->pixels + (y - dy) * 1600 + (dst.x1 - dx) * 2, bytes);
        else memmove(c->buffer[0] + y * 1600 + dst.x1 * 2,
                     c->buffer[0] + (y - dy) * 1600 + (dst.x1 - dx) * 2, bytes);
    }
    c->scroll_moves++; c->scroll_pixels += (uint64_t)rows * bytes / 2;
    c->scroll_flush_area = outer; c->scroll_flush_pending = 1;
    invalidate_rect(obj, outer.x1, outer.y1, outer.x2, inner.y1 - 1);
    invalidate_rect(obj, outer.x1, inner.y2 + 1, outer.x2, outer.y2);
    invalidate_rect(obj, outer.x1, inner.y1, inner.x1 - 1, inner.y2);
    invalidate_rect(obj, inner.x2 + 1, inner.y1, outer.x2, inner.y2);
    if(dy > 0) invalidate_rect(obj, inner.x1, inner.y1, inner.x2, inner.y1 + dy - 1);
    if(dy < 0) invalidate_rect(obj, inner.x1, inner.y2 + dy + 1, inner.x2, inner.y2);
    if(dx > 0) invalidate_rect(obj, inner.x1, inner.y1, inner.x1 + dx - 1, inner.y2);
    if(dx < 0) invalidate_rect(obj, inner.x2 + dx + 1, inner.y1, inner.x2, inner.y2);
    for(unsigned i = 0; i < c->scroll_dynamic_count; i++)
        lv_obj_invalidate_area(obj, &c->scroll_dynamic[i]);
}
static int32_t profile_sw(lv_draw_unit_t *unit, lv_layer_t *layer)
{
    struct ppa_display *c = profile_ctx;
    lv_draw_task_t *t = lv_draw_get_next_available_task(layer, NULL, 1);
    unsigned type = t ? t->type : 15;
    uint64_t start = microseconds();
    int32_t result = c->sw_dispatch(unit, layer);
    if(result > 0 && type < 16) {
        c->sw_us[type] += microseconds() - start;
        c->sw_count[type]++;
    }
    return result;
}

static int submit(struct ppa_display *c, struct korvo_ppa_request *q)
{
    if(c->failed) return -1;
    uint64_t start = c->profile ? microseconds() : 0;
    int ret = ioctl(c->fd, KORVO_PPA_IOC_RUN, q);
    if(c->profile && q->operation < 6) {
        c->operation_us[q->operation] += microseconds() - start;
        c->operation_count[q->operation]++;
    }
    if(!ret) return 0;
    fprintf(stderr, "korvo-ppa: operation %u failed: %s; using software fallback\n",
            q->operation, strerror(errno));
    c->failed = 1;
    return -1;
}
static void round_area(lv_event_t *e)
{
    lv_area_t *a = lv_event_get_param(e);
    a->x1 &= ~1;
    a->x2 |= 1;
    if(a->x2 > 799) a->x2 = 799;
}
static void flush_rect(lv_display_t *display, const lv_area_t *a, uint8_t *pixels)
{
    struct ppa_display *c = lv_display_get_driver_data(display);
    unsigned w = lv_area_get_width(a), h = lv_area_get_height(a);
    unsigned stride = lv_draw_buf_width_to_stride(w, LV_COLOR_FORMAT_RGB565);
    struct korvo_ppa_request q = {
        .operation = KORVO_PPA_FLUSH,
        .source = {0, pixels - c->buffer[0], stride / 2, h},
        .destination = {KORVO_PPA_FRAMEBUFFER, 0, 800, 480},
        .destination_x = a->x1, .destination_y = a->y1, .width = w, .height = h
    };
    if(c->direct) {
        q.source = (struct korvo_ppa_surface){0, 0, 800, 480};
        q.source_x = a->x1; q.source_y = a->y1;
        stride = 1600;
    }
    if(submit(c, &q)) {
        if(c->direct) pixels = c->buffer[0] + a->y1 * stride + a->x1 * 2;
        for(unsigned y = 0; y < h; y++)
            memcpy(c->pixels + (a->y1 + y) * c->stride + a->x1 * 2,
                   pixels + y * stride, w * 2);
    }
}
static void flush(lv_display_t *display, const lv_area_t *a, uint8_t *pixels)
{
    struct ppa_display *c = lv_display_get_driver_data(display);
    if(c->scroll_flush_pending) {
        lv_area_join(&c->scroll_flush_area, &c->scroll_flush_area, a);
        if(lv_display_flush_is_last(display)) {
            c->scroll_flush_pending = 0;
            lv_area_t aligned = c->scroll_flush_area;
            aligned.x1 &= ~1; aligned.x2 |= 1;
            flush_rect(display, &aligned, c->buffer[0]);
        }
    } else {
        flush_rect(display, a, pixels);
    }
    lv_display_flush_ready(display);
}
static int rect(struct ppa_display *c, lv_draw_task_t *t, lv_area_t *a,
                struct korvo_ppa_request *q)
{
    lv_layer_t *layer = ((lv_draw_dsc_base_t *)t->draw_dsc)->layer;
    if(c->failed || layer->parent || layer->color_format != LV_COLOR_FORMAT_RGB565) return 0;
    if(!lv_area_intersect(a, &t->area, &t->clip_area) ||
       !lv_area_intersect(a, a, &layer->buf_area)) return 0;
    if(t->type == LV_DRAW_TASK_TYPE_FILL) {
        const lv_draw_fill_dsc_t *d = t->draw_dsc;
        int32_t r = LV_MIN(d->radius, LV_MIN(lv_area_get_width(&t->area), lv_area_get_height(&t->area)) / 2);
        /* Wide opaque controls need one fill, not multiple full-area alpha
         * transfers. Keep rounded edge strips on LVGL's exact SW renderer.
         */
        if(d->opa == 255 && r > 0 && lv_area_get_width(&t->area) > 2 * r + 128) {
            a->x1 = LV_MAX(a->x1, t->area.x1 + r);
            a->x2 = LV_MIN(a->x2, t->area.x2 - r);
        }
    }
    /* Accelerate the aligned interior; render at most two edge columns in SW. */
    if((a->x1 - layer->buf_area.x1) & 1) a->x1++;
    if((lv_area_get_width(a)) & 1) a->x2--;
    unsigned w = lv_area_get_width(a), h = lv_area_get_height(a);
    if(a->x2 < a->x1 || w * h < 512) return 0;
    if(!layer->draw_buf) return 1; /* Evaluated before allocation on some layers. */
    lv_draw_buf_t *b = layer->draw_buf;
    uintptr_t start = (uintptr_t)b->data, base = (uintptr_t)c->buffer[0];
    unsigned stride = b->header.stride;
    unsigned height = lv_area_get_height(&layer->buf_area);
    if(start < base || start - base >= c->render_size ||
       (start - base) & 3 || stride & 3 ||
       stride * height > c->render_size - (start - base)) return 0;
    q->destination = (struct korvo_ppa_surface){0, start - base, stride / 2, height};
    q->destination_x = a->x1 - layer->buf_area.x1;
    q->destination_y = a->y1 - layer->buf_area.y1;
    q->width = w; q->height = h;
    return 1;
}
static int image_supported(lv_draw_task_t *t)
{
    const lv_draw_image_dsc_t *d = t->draw_dsc;
    if(!d->opa || d->rotation || d->scale_x != 256 || d->scale_y != 256 ||
       d->skew_x || d->skew_y || d->recolor_opa || d->tile || d->clip_radius ||
       d->bitmap_mask_src || d->blend_mode != LV_BLEND_MODE_NORMAL ||
       lv_image_src_get_type(d->src) != LV_IMAGE_SRC_VARIABLE) return 0;
    const lv_image_dsc_t *im = d->src;
    return im->data && !(im->header.flags & (LV_IMAGE_FLAGS_COMPRESSED | LV_IMAGE_FLAGS_PREMULTIPLIED)) &&
        (im->header.cf == LV_COLOR_FORMAT_RGB565 || im->header.cf == LV_COLOR_FORMAT_ARGB8888) &&
        lv_area_get_width(&t->area) == im->header.w && lv_area_get_height(&t->area) == im->header.h;
}
static int32_t evaluate(lv_draw_unit_t *unit, lv_draw_task_t *t)
{
    struct ppa_display *c = ((struct ppa_unit *)unit)->ctx;
    lv_area_t a;
    struct korvo_ppa_request q = {0};
    if(c->flush_only || !rect(c, t, &a, &q)) return 0;
    if(t->type == LV_DRAW_TASK_TYPE_FILL) {
        const lv_draw_fill_dsc_t *d = t->draw_dsc;
        if(d->grad.dir != LV_GRAD_DIR_NONE || d->opa < LV_OPA_MIN) return 0;
        if(c->capabilities & KORVO_PPA_CAP_A8) goto supported;
        if(d->opa != 255) return 0;
        /* A clipped tile in the central band of a rounded rectangle has
         * no curved pixels. Corner tiles still use LVGL's antialiasing.
         */
        int32_t r = d->radius;
        int32_t max_r = LV_MIN(lv_area_get_width(&t->area), lv_area_get_height(&t->area)) / 2;
        if(r > max_r) r = max_r;
        if(r && !((a.x1 >= t->area.x1 + r && a.x2 <= t->area.x2 - r) ||
                  (a.y1 >= t->area.y1 + r && a.y2 <= t->area.y2 - r))) return 0;
    } else if(t->type != LV_DRAW_TASK_TYPE_IMAGE || !image_supported(t)) return 0;
supported:
    if(t->preference_score > 70) { t->preference_score = 70; t->preferred_draw_unit_id = DRAW_ID; }
    return 1;
}
static int draw_image(struct ppa_display *c, lv_draw_task_t *t, const lv_area_t *a,
                      struct korvo_ppa_request *q)
{
    const lv_draw_image_dsc_t *d = t->draw_dsc;
    const lv_image_dsc_t *im = d->src;
    unsigned bytes = im->header.cf == LV_COLOR_FORMAT_ARGB8888 ? 4 : 2;
    unsigned stride = im->header.stride ? im->header.stride : im->header.w * bytes;
    unsigned row_bytes = q->width * bytes;
    if(stride < im->header.w * bytes ||
       (uint64_t)stride * im->header.h > im->data_size) return -1;
    q->operation = bytes == 2 && d->opa == 255 ? KORVO_PPA_COPY : KORVO_PPA_BLEND;
    q->format = bytes == 4 ? KORVO_PPA_ARGB8888 : KORVO_PPA_RGB565;
    q->alpha = d->opa;
    unsigned height = q->height, dest_y = q->destination_y;
    for(unsigned y = 0; y < height;) {
        unsigned rows = KORVO_PPA_BUFFER_SIZE / row_bytes;
        if(rows > height - y) rows = height - y;
        for(unsigned r = 0; r < rows; r++)
            memcpy(c->buffer[1] + r * row_bytes,
                im->data + (a->y1 - t->area.y1 + y + r) * stride +
                (a->x1 - t->area.x1) * bytes, row_bytes);
        q->source = (struct korvo_ppa_surface){1, 0, q->width, rows};
        q->destination_y = dest_y + y; q->height = rows;
        if(submit(c, q)) return -1;
        y += rows;
    }
    return 0;
}
static int draw_fill(lv_draw_unit_t *unit, struct ppa_display *c, lv_draw_task_t *t, const lv_area_t *a,
                     struct korvo_ppa_request *q)
{
    lv_draw_fill_dsc_t *d = t->draw_dsc;
    q->color = lv_color_to_u16(d->color);
    int32_t radius = LV_MIN(d->radius, LV_MIN(lv_area_get_width(&t->area), lv_area_get_height(&t->area)) / 2);
    int solid = !radius ||
        (a->x1 >= t->area.x1 + radius && a->x2 <= t->area.x2 - radius) ||
        (a->y1 >= t->area.y1 + radius && a->y2 <= t->area.y2 - radius);
    if((solid || !(c->capabilities & KORVO_PPA_CAP_A8)) && d->opa == 255) {
        if(radius && q->width * q->height <= 65536) {
            /* A short, wide control fits the cache. One plain CPU fill
             * beats DMA ownership handoffs and avoids per-pixel masks.
             */
            lv_draw_fill_dsc_t plain = *d;
            const lv_area_t *saved_clip = unit->clip_area;
            plain.radius = 0; unit->clip_area = a;
            lv_draw_sw_fill(unit, &plain, &t->area);
            unit->clip_area = saved_clip;
            return 0;
        }
        q->operation = KORVO_PPA_FILL; return submit(c, q);
    }
    lv_draw_sw_mask_radius_param_t parameter;
    void *masks[2] = {&parameter, NULL};
    if(radius) lv_draw_sw_mask_radius_init(&parameter, &t->area, radius, false);
    q->operation = KORVO_PPA_BLEND; q->format = KORVO_PPA_A8; q->alpha = 255;
    unsigned height = q->height, dest_y = q->destination_y;
    int ret = 0;
    for(unsigned y = 0; y < height;) {
        unsigned rows = KORVO_PPA_BUFFER_SIZE / q->width;
        if(rows > height - y) rows = height - y;
        memset(c->buffer[1], d->opa, rows * q->width);
        if(radius) for(unsigned r = 0; r < rows; r++) {
            uint8_t *line = c->buffer[1] + r * q->width;
            lv_draw_sw_mask_res_t result = lv_draw_sw_mask_apply(masks, line, a->x1, a->y1 + y + r, q->width);
            if(result == LV_DRAW_SW_MASK_RES_TRANSP) memset(line, 0, q->width);
        }
        q->source = (struct korvo_ppa_surface){1, 0, q->width, rows};
        q->destination_y = dest_y + y; q->height = rows;
        if(submit(c, q)) { ret = -1; break; }
        y += rows;
    }
    if(radius) lv_draw_sw_mask_free_param(&parameter);
    return ret;
}
static void software_task(lv_draw_unit_t *unit, lv_draw_task_t *t)
{
    if(t->type == LV_DRAW_TASK_TYPE_FILL) lv_draw_sw_fill(unit, t->draw_dsc, &t->area);
    else lv_draw_sw_image(unit, t->draw_dsc, &t->area);
}
static int32_t dispatch(lv_draw_unit_t *unit, lv_layer_t *layer)
{
    struct ppa_display *c = ((struct ppa_unit *)unit)->ctx;
    lv_draw_task_t *t = lv_draw_get_next_available_task(layer, NULL, DRAW_ID);
    if(!t || t->preferred_draw_unit_id != DRAW_ID) return 0;
    if(!lv_draw_layer_alloc_buf(layer)) return -1;
    unit->target_layer = layer; unit->clip_area = &t->clip_area;
    t->state = LV_DRAW_TASK_STATE_IN_PROGRESS;
    lv_area_t a;
    struct korvo_ppa_request q = {0};
    int ret = -1;
    if(rect(c, t, &a, &q)) {
        if(t->type == LV_DRAW_TASK_TYPE_FILL) {
            ret = draw_fill(unit, c, t, &a, &q);
        } else ret = draw_image(c, t, &a, &q);
    }
    if(ret) software_task(unit, t);
    else {
        lv_area_t clip = t->clip_area;
        if(a.x1 > clip.x1) {
            clip.x2 = a.x1 - 1; unit->clip_area = &clip; software_task(unit, t);
        }
        clip = t->clip_area;
        if(a.x2 < clip.x2) {
            clip.x1 = a.x2 + 1; unit->clip_area = &clip; software_task(unit, t);
        }
        unit->clip_area = &t->clip_area;
    }
    t->state = LV_DRAW_TASK_STATE_READY;
    lv_draw_dispatch_request();
    return 1;
}
static void cleanup(lv_event_t *event)
{
    struct ppa_display *c = lv_event_get_user_data(event);
    if(active_ctx == c) active_ctx = NULL;
    if(c->profile) {
        for(unsigned i = 0; i < 16; i++) if(c->sw_count[i])
            fprintf(stderr, "ppa-profile: sw type=%u count=%llu us=%llu\n", i,
                (unsigned long long)c->sw_count[i], (unsigned long long)c->sw_us[i]);
        for(unsigned i = 0; i < 6; i++) if(c->operation_count[i])
            fprintf(stderr, "ppa-profile: hw op=%u count=%llu us=%llu\n", i,
                (unsigned long long)c->operation_count[i], (unsigned long long)c->operation_us[i]);
        c->sw_unit->dispatch_cb = c->sw_dispatch;
        profile_ctx = NULL;
    }
    if(getenv("KORVO_UI_STATS")) {
        fprintf(stderr, "korvo-scroll: calls=%llu queued=%llu dirty=%llu moves=%llu reused_pixels=%llu\n",
                (unsigned long long)c->scroll_calls, (unsigned long long)c->scroll_queued,
                (unsigned long long)c->scroll_dirty, (unsigned long long)c->scroll_moves,
                (unsigned long long)c->scroll_pixels);
        struct korvo_ppa_stats s;
        if(!ioctl(c->fd, KORVO_PPA_IOC_STATS, &s))
            fprintf(stderr, "korvo-ppa: copy=%llu fill=%llu flush=%llu blend=%llu errors=%llu\n",
                (unsigned long long)(s.copies - c->initial.copies),
                (unsigned long long)(s.fills - c->initial.fills),
                (unsigned long long)(s.flushes - c->initial.flushes),
                (unsigned long long)(s.blends - c->initial.blends),
                (unsigned long long)(s.errors - c->initial.errors));
    }
    munmap(c->buffer[0], c->render_size); munmap(c->buffer[1], KORVO_PPA_BUFFER_SIZE);
    munmap(c->pixels, c->size); close(c->fb); close(c->fd); free(c);
}
lv_display_t *korvo_ppa_display_create(void)
{
    if(getenv("KORVO_PPA_DISABLE")) goto software;
    struct ppa_display *c = calloc(1, sizeof(*c));
    if(!c) goto software;
    c->fd = open("/dev/esp32s31-ppa", O_RDWR | O_CLOEXEC);
    c->fb = open("/dev/fb0", O_RDWR | O_CLOEXEC);
    struct korvo_ppa_info info;
    struct fb_var_screeninfo v;
    struct fb_fix_screeninfo f;
    if(c->fd < 0 || c->fb < 0 || ioctl(c->fd, KORVO_PPA_IOC_INFO, &info) ||
       info.abi != KORVO_PPA_ABI || info.buffer_size != KORVO_PPA_BUFFER_SIZE ||
       ioctl(c->fb, FBIOGET_VSCREENINFO, &v) || ioctl(c->fb, FBIOGET_FSCREENINFO, &f) ||
       v.xres != 800 || v.yres != 480 || v.bits_per_pixel != 16 || f.line_length != 1600) goto fail;
    c->size = f.smem_len; c->stride = f.line_length;
    c->capabilities = info.capabilities;
    c->render_size = info.render_buffer_size ? info.render_buffer_size : info.buffer_size;
    c->direct = c->render_size >= 800 * 480 * 2 && !getenv("KORVO_PPA_PARTIAL");
    c->pixels = mmap(NULL, c->size, PROT_READ | PROT_WRITE, MAP_SHARED, c->fb, 0);
    for(unsigned i = 0; i < 2; i++) c->buffer[i] = mmap(NULL, i ? KORVO_PPA_BUFFER_SIZE : c->render_size,
            PROT_READ | PROT_WRITE, MAP_SHARED, c->fd, i * KORVO_PPA_BUFFER_SIZE);
    if(c->pixels == MAP_FAILED || c->buffer[0] == MAP_FAILED || c->buffer[1] == MAP_FAILED) goto fail;
    ioctl(c->fd, KORVO_PPA_IOC_STATS, &c->initial);
    /* Match the software fbdev's unblank without restarting an active LCD. */
    ioctl(c->fb, FBIOBLANK, FB_BLANK_UNBLANK);
    lv_display_t *display = lv_display_create(800, 480);
    if(!display) goto fail;
    c->display = display;
    c->scroll_disabled = getenv("KORVO_SCROLL_DISABLE") != NULL;
    active_ctx = c;
    lv_display_set_driver_data(display, c);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, c->buffer[0], NULL,
            c->direct ? c->render_size : KORVO_PPA_BUFFER_SIZE,
            c->direct ? LV_DISPLAY_RENDER_MODE_DIRECT : LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    lv_display_add_event_cb(display, round_area, LV_EVENT_INVALIDATE_AREA, NULL);
    lv_display_add_event_cb(display, scroll_refresh, LV_EVENT_REFR_START, c);
    lv_display_add_event_cb(display, cleanup, LV_EVENT_DELETE, c);
    struct ppa_unit *unit = lv_draw_create_unit(sizeof(*unit));
    unit->ctx = c; unit->base.evaluate_cb = evaluate; unit->base.dispatch_cb = dispatch;
    c->flush_only = getenv("KORVO_PPA_FLUSH_ONLY") != NULL;
    c->profile = getenv("KORVO_PPA_PROFILE") != NULL && unit->base.next && unit->base.next->dispatch_cb;
    if(c->profile) {
        c->sw_unit = unit->base.next; c->sw_dispatch = c->sw_unit->dispatch_cb;
        profile_ctx = c; c->sw_unit->dispatch_cb = profile_sw;
    }
    fprintf(stderr, "korvo-ppa: PPA fill/blend + DMA2D copy/flush, native 800x480, %s\n",
            c->direct ? "DIRECT canvas" : "partial tiles");
    return display;
fail:
    for(unsigned i = 0; i < 2; i++) if(c->buffer[i] && c->buffer[i] != MAP_FAILED)
        munmap(c->buffer[i], i ? KORVO_PPA_BUFFER_SIZE : c->render_size);
    if(c->pixels && c->pixels != MAP_FAILED) munmap(c->pixels, c->size);
    if(c->fd >= 0) close(c->fd);
    if(c->fb >= 0) close(c->fb);
    free(c);
software:;
    lv_display_t *display_sw = lv_linux_fbdev_create();
    if(display_sw) lv_linux_fbdev_set_file(display_sw, "/dev/fb0");
    fprintf(stderr, "korvo-ppa: software framebuffer fallback\n");
    return display_sw;
}
