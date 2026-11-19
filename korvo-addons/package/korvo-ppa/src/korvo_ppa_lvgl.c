/* SPDX-License-Identifier: MIT
 * LVGL 9.2: cached partial render buffer, DMA2D flush, PPA fill/blend.
 * Unsupported draw tasks remain on the LVGL software renderer.
 */
#define _GNU_SOURCE
#include "korvo_ppa_lvgl.h"
#include "korvo_ppa.h"
#include <src/draw/lv_draw_private.h>
#include <src/draw/sw/lv_draw_sw.h>
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
#include <unistd.h>

#define DRAW_ID 203
struct ppa_display {
    int fd, fb;
    uint8_t *buffer[2], *pixels;
    unsigned stride, size, render_size;
    int direct;
    int failed;
    struct korvo_ppa_stats initial;
};
struct ppa_unit { lv_draw_unit_t base; struct ppa_display *ctx; };

static int submit(struct ppa_display *c, struct korvo_ppa_request *q)
{
    if(c->failed) return -1;
    if(!ioctl(c->fd, KORVO_PPA_IOC_RUN, q)) return 0;
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
static void flush(lv_display_t *display, const lv_area_t *a, uint8_t *pixels)
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
    lv_display_flush_ready(display);
}
static int rect(struct ppa_display *c, lv_draw_task_t *t, lv_area_t *a,
                struct korvo_ppa_request *q)
{
    lv_layer_t *layer = ((lv_draw_dsc_base_t *)t->draw_dsc)->layer;
    if(c->failed || layer->parent || layer->color_format != LV_COLOR_FORMAT_RGB565) return 0;
    if(!lv_area_intersect(a, &t->area, &t->clip_area) ||
       !lv_area_intersect(a, a, &layer->buf_area)) return 0;
    unsigned w = lv_area_get_width(a), h = lv_area_get_height(a);
    /* Kernel RX writes complete 32-bit words. Leave odd edges to software. */
    if(((a->x1 - layer->buf_area.x1) | w) & 1 || w * h < 512) return 0;
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
    if(!rect(c, t, &a, &q)) return 0;
    if(t->type == LV_DRAW_TASK_TYPE_FILL) {
        const lv_draw_fill_dsc_t *d = t->draw_dsc;
        if(d->grad.dir != LV_GRAD_DIR_NONE || d->opa != 255) return 0;
        /* A clipped tile in the central band of a rounded rectangle has
         * no curved pixels. Corner tiles still use LVGL's antialiasing.
         */
        int32_t r = d->radius;
        int32_t max_r = LV_MIN(lv_area_get_width(&t->area), lv_area_get_height(&t->area)) / 2;
        if(r > max_r) r = max_r;
        if(r && !((a.x1 >= t->area.x1 + r && a.x2 <= t->area.x2 - r) ||
                  (a.y1 >= t->area.y1 + r && a.y2 <= t->area.y2 - r))) return 0;
    } else if(t->type != LV_DRAW_TASK_TYPE_IMAGE || !image_supported(t)) return 0;
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
            const lv_draw_fill_dsc_t *d = t->draw_dsc;
            q.operation = KORVO_PPA_FILL; q.color = lv_color_to_u16(d->color);
            ret = submit(c, &q);
        } else ret = draw_image(c, t, &a, &q);
    }
    if(ret) {
        if(t->type == LV_DRAW_TASK_TYPE_FILL) lv_draw_sw_fill(unit, t->draw_dsc, &t->area);
        else lv_draw_sw_image(unit, t->draw_dsc, &t->area);
    }
    t->state = LV_DRAW_TASK_STATE_READY;
    lv_draw_dispatch_request();
    return 1;
}
static void cleanup(lv_event_t *event)
{
    struct ppa_display *c = lv_event_get_user_data(event);
    if(getenv("KORVO_UI_STATS")) {
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
    lv_display_set_driver_data(display, c);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, c->buffer[0], NULL,
            c->direct ? c->render_size : KORVO_PPA_BUFFER_SIZE,
            c->direct ? LV_DISPLAY_RENDER_MODE_DIRECT : LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    lv_display_add_event_cb(display, round_area, LV_EVENT_INVALIDATE_AREA, NULL);
    lv_display_add_event_cb(display, cleanup, LV_EVENT_DELETE, c);
    struct ppa_unit *unit = lv_draw_create_unit(sizeof(*unit));
    unit->ctx = c; unit->base.evaluate_cb = evaluate; unit->base.dispatch_cb = dispatch;
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
