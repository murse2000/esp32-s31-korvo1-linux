// SPDX-License-Identifier: MIT
#define _POSIX_C_SOURCE 200809L
#include <lvgl.h>
#include <demos/lv_demos.h>
#include <src/drivers/display/fb/lv_linux_fbdev.h>
#include "korvo_ppa_lvgl.h"
#include <src/drivers/evdev/lv_evdev.h>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/videodev2.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include "camera_pixels.h"
#include "linux_console.h"

static volatile sig_atomic_t running = 1;
static const char *next_demo;
static const char *self;
static char self_path[4096];
static unsigned render_scene;
static int camera_fd = -1;
#define CAMERA_BYTES (240U * 240U * 2U)
static unsigned char *camera_pixels;
static lv_image_dsc_t camera_image;
static lv_obj_t *camera_view, *camera_status;
static unsigned camera_frames;
static uint32_t camera_format;
static const char *demos[] = {
    "widgets", "music", "flex_layout", "scroll", "transform", "multilang",
    "keypad_encoder", "benchmark", "stress", "render",
    "vector_graphic_buffered", "vector_graphic_not_buffered", "audio", "camera"
};
static uint32_t tick(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}
static uint32_t frame_start, render_start, flush_start, frame_ms, render_ms, flush_ms;
static uint32_t sample_start;
static int frame_flushed;
static unsigned frame_count;
static void measure_frame(lv_event_t *event)
{
    uint32_t now = tick();
    switch(lv_event_get_code(event)) {
        case LV_EVENT_REFR_START: frame_start = now; frame_flushed = 0; break;
        case LV_EVENT_RENDER_START: render_start = now; break;
        case LV_EVENT_RENDER_READY: render_ms += now - render_start; break;
        case LV_EVENT_FLUSH_START: flush_start = now; frame_flushed = 1; break;
        case LV_EVENT_FLUSH_FINISH: flush_ms += now - flush_start; break;
        case LV_EVENT_REFR_READY:
            if(!frame_flushed) break;
            if(!frame_count) sample_start = frame_start;
            frame_ms += now - frame_start;
            if(++frame_count == 20) {
                fprintf(stderr, "korvo-ui: 20 frames wall=%u work=%u render=%u flush=%u ms\n",
                        now - sample_start, frame_ms, render_ms, flush_ms);
                frame_count = frame_ms = render_ms = flush_ms = 0;
            }
            break;
        default: break;
    }
}
static void quit(int sig) { (void)sig; running = 0; }
static void select_demo(lv_event_t *event)
{
    next_demo = lv_event_get_user_data(event);
    running = 0;
}
static lv_obj_t *button(lv_obj_t *parent, const char *label, const char *demo)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_add_event_cb(b, select_demo, LV_EVENT_CLICKED, (void *)demo);
    lv_obj_t *text = lv_label_create(b);
    lv_label_set_text(text, label);
    lv_obj_center(text);
    return b;
}
static void inputs(void)
{
    DIR *dir = opendir("/dev/input");
    if(!dir) return;
    struct dirent *entry;
    while((entry = readdir(dir))) {
        if(strncmp(entry->d_name, "event", 5)) continue;
        char path[300], name[256] = {0};
        snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if(fd < 0) continue;
        ioctl(fd, EVIOCGNAME(sizeof(name)), name);
        close(fd);
        lv_indev_type_t type = strstr(name, "Goodix") || strstr(name, "Touch") ? LV_INDEV_TYPE_POINTER : LV_INDEV_TYPE_KEYPAD;
        lv_indev_t *indev = lv_evdev_create(type, path);
        if(indev) lv_timer_set_period(lv_indev_get_read_timer(indev), 10);
        if(indev && type == LV_INDEV_TYPE_POINTER)
            lv_evdev_set_calibration(indev, 0, 0, 799, 479);
    }
    closedir(dir);
}
static void render_next(lv_timer_t *timer)
{
    (void)timer;
    lv_obj_clean(lv_screen_active());
    lv_demo_render(render_scene++ % LV_DEMO_RENDER_SCENE_NUM, LV_OPA_COVER);
}
static void camera_update(lv_timer_t *timer)
{
    (void)timer;
    ssize_t n = read(camera_fd, camera_pixels, CAMERA_BYTES);
    if(n != (ssize_t)CAMERA_BYTES) return;
    camera_to_rgb565(camera_pixels, CAMERA_BYTES, camera_format);
    lv_obj_invalidate(camera_view);
    lv_label_set_text_fmt(camera_status, "Camera 240x240\nFrames: %u", ++camera_frames);
    if(camera_frames == 1) { puts("korvo-ui: first camera frame displayed"); fflush(stdout); }
}
static void camera_save(lv_event_t *event)
{
    (void)event;
    if(!camera_frames) return;
    char path[100];
    snprintf(path, sizeof(path), "/mnt/sd/camera-%ld.ppm", (long)time(NULL));
    FILE *f = fopen(path, "wb");
    if(!f) { lv_label_set_text(camera_status, "Could not save to microSD"); return; }
    fprintf(f, "P6\n240 240\n255\n");
    int failed = 0;
    for(unsigned i = 0; i < CAMERA_BYTES; i += 2) {
        uint16_t px = camera_pixels[i] | ((uint16_t)camera_pixels[i + 1] << 8);
        unsigned char rgb[] = { ((px >> 11) & 31) * 255 / 31,
                               ((px >> 5) & 63) * 255 / 63, (px & 31) * 255 / 31 };
        if(fwrite(rgb, 1, 3, f) != 3) { failed = 1; break; }
    }
    if(fclose(f)) failed = 1;
    lv_label_set_text(camera_status, failed ? "Photo save failed" : "Photo saved to microSD");
}
static void camera_screen(void)
{
    camera_status = lv_label_create(lv_screen_active());
    lv_obj_align(camera_status, LV_ALIGN_RIGHT_MID, -40, -30);
    camera_fd = open("/dev/video0", O_RDONLY | O_NONBLOCK);
    if(camera_fd < 0) {
        lv_label_set_text(camera_status, "Camera not available\nCheck camera module connection.");
        return;
    }
    struct v4l2_format fmt = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE};
    if(ioctl(camera_fd, VIDIOC_G_FMT, &fmt) || fmt.fmt.pix.sizeimage != CAMERA_BYTES) {
        lv_label_set_text(camera_status, "Camera format unavailable");
        close(camera_fd); camera_fd = -1; return;
    }
    camera_pixels = calloc(1, CAMERA_BYTES);
    if(!camera_pixels) {
        lv_label_set_text(camera_status, "Not enough memory for camera");
        close(camera_fd); camera_fd = -1; return;
    }
    camera_format = fmt.fmt.pix.pixelformat;
    camera_image.header.magic = LV_IMAGE_HEADER_MAGIC;
    camera_image.header.cf = LV_COLOR_FORMAT_RGB565;
    camera_image.header.w = 240; camera_image.header.h = 240;
    camera_image.header.stride = 480;
    camera_image.data_size = CAMERA_BYTES;
    camera_image.data = camera_pixels;
    camera_view = lv_image_create(lv_screen_active());
    lv_image_set_src(camera_view, &camera_image);
    lv_obj_align(camera_view, LV_ALIGN_LEFT_MID, 55, 0);
    lv_label_set_text(camera_status, "Waiting for camera frames...");
    lv_obj_t *save = lv_button_create(lv_screen_active());
    lv_obj_set_size(save, 180, 54);
    lv_obj_align(save, LV_ALIGN_RIGHT_MID, -50, 55);
    lv_obj_add_event_cb(save, camera_save, LV_EVENT_CLICKED, NULL);
    lv_obj_t *text = lv_label_create(save);
    lv_label_set_text(text, "SAVE PHOTO"); lv_obj_center(text);
    lv_timer_create(camera_update, 100, NULL);
}
static void menu(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x111b2b), 0);
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "KORVO-1  |  LVGL 9.2.2 DEMOS");
    lv_obj_set_style_text_color(title, lv_color_hex(0xe8f0ff), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 14);
    lv_obj_t *list = lv_obj_create(scr);
    lv_obj_set_size(list, 774, 398);
    lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_all(list, 12, 0);
    for(unsigned i = 0; i < sizeof(demos)/sizeof(demos[0]); i++) {
        lv_obj_t *b = button(list, demos[i], demos[i]);
        lv_obj_set_size(b, 232, 62);
    }
}
int main(int argc, char **argv)
{
    /* The audio HOME action uses argv[0]="korvo-ui". execl does not
     * search PATH, so resolve the actual executable before re-exec.
     */
    ssize_t self_len = readlink("/proc/self/exe", self_path, sizeof(self_path) - 1);
    if(self_len > 0) {
        self_path[self_len] = '\0';
        self = self_path;
    }
    else self = "/usr/bin/korvo-ui";
    const char *demo = argc > 1 ? argv[1] : "menu";
    if(!strcmp(demo, "--list")) {
        puts("menu");
        for(unsigned i = 0; i < sizeof(demos)/sizeof(demos[0]); i++) puts(demos[i]);
        return 0;
    }
    if(!strcmp(demo, "audio")) {
        console_prepare_exec();
        const char *player = strstr(self, "korvo-ui-extra") ? "/usr/bin/lvgl-music-demo-extra" : "/usr/bin/lvgl-music-demo";
        execl(player, player, (char *)NULL);
        perror("lvgl-music-demo");
        return 1;
    }
    signal(SIGTERM, quit); signal(SIGINT, quit);
    console_graphics();
    lv_init();
    lv_tick_set_cb(tick);
    lv_display_t *display = korvo_ppa_display_create();
    if(!display) return 1;
    if(getenv("KORVO_UI_STATS"))
        lv_display_add_event_cb(display, measure_frame, LV_EVENT_ALL, NULL);
    inputs();
    if(!strcmp(demo, "menu")) menu();
    else {
        if(!strcmp(demo, "camera")) camera_screen();
        else if(!strcmp(demo, "render")) {
            render_next(NULL);
            lv_timer_create(render_next, 5000, NULL);
        } else if(!lv_demos_create((char **)&demo, 1)) return 2;
        lv_obj_t *home = button(lv_layer_top(), "HOME", "menu");
        lv_obj_set_size(home, 90, 40);
        lv_obj_align(home, LV_ALIGN_TOP_RIGHT, -4, 4);
    }
    printf("korvo-ui: running %s on /dev/fb0\n", demo);
    fflush(stdout);
    const uint32_t start = tick();
    const unsigned seconds = argc > 2 ? (unsigned)atoi(argv[2]) : 0;
    while(running && (!seconds || tick() - start < seconds * 1000U)) {
        uint32_t delay = lv_timer_handler();
        if(delay > 10) delay = 10;
        if(delay < 2) delay = 2;
        struct timespec pause = {0, delay * 1000000L};
        nanosleep(&pause, NULL);
    }
    if(camera_fd >= 0) close(camera_fd);
    if(camera_frames) printf("korvo-ui: displayed %u camera frames\n", camera_frames);
    lv_deinit();
    free(camera_pixels);
    if(next_demo) {
        console_prepare_exec();
        execl(self, self, next_demo, (char *)NULL);
        perror("exec demo");
        return 1;
    }
    return 0;
}
