#define _POSIX_C_SOURCE 200809L
#include <lvgl.h>
#include <src/drivers/display/fb/lv_linux_fbdev.h>
#include "korvo_ppa_lvgl.h"
#include <src/drivers/evdev/lv_evdev.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "linux_console.h"

#define SONG_SECONDS 14

static pid_t player = -1;
static lv_obj_t *status_label;
static lv_obj_t *time_label;
static uint64_t play_started_ms;
static const char *song_path = "/usr/share/sounds/korvo-lvgl-demo.wav";
static volatile sig_atomic_t running = 1;
static int go_home;

static void quit(int sig) { (void)sig; running = 0; }

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static uint32_t tick(void) { return (uint32_t)now_ms(); }

static void home_cb(lv_event_t *e)
{
    (void)e;
    go_home = 1;
    running = 0;
}

static char *find_touch(void)
{
    DIR *d = opendir("/dev/input");
    if(!d) return NULL;
    struct dirent *e;
    char path[128], name[256];
    char *found = NULL;
    while((e = readdir(d))) {
        if(strncmp(e->d_name, "event", 5)) continue;
        snprintf(path, sizeof(path), "/dev/input/%s", e->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if(fd < 0) continue;
        memset(name, 0, sizeof(name));
        if(ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) >= 0 &&
           (strstr(name, "Goodix") || strstr(name, "goodix") || strstr(name, "GT115"))) {
            found = strdup(path);
            close(fd);
            break;
        }
        close(fd);
    }
    closedir(d);
    return found;
}

static void stop_audio(void)
{
    if(player > 0) {
        kill(player, SIGTERM);
        waitpid(player, NULL, 0);
        player = -1;
    }
}

static void set_status(const char *text)
{
    if(status_label) lv_label_set_text(status_label, text);
}

static void play_cb(lv_event_t *e)
{
    (void)e;
    if(player > 0) { set_status("Already playing"); return; }
    if(access(song_path, R_OK)) { set_status("Could not prepare demo audio"); return; }
    player = fork();
    if(player == 0) {
        console_prepare_exec();
        execlp("aplay", "aplay", "-q", "-D", "hw:0,0", "--period-size=256", "--buffer-size=1024", song_path, (char *)NULL);
        execlp("aplay", "aplay", "-q", song_path, (char *)NULL);
        _exit(127);
    }
    if(player < 0) { player = -1; set_status("Could not start ALSA player"); return; }
    play_started_ms = now_ms();
    set_status("Playing Korvo demo tune");
}

static void stop_cb(lv_event_t *e)
{
    (void)e;
    stop_audio();
    set_status("Stopped");
    lv_label_set_text(time_label, "00:00 / 00:14");
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *text, int w, int h,
                             lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    return b;
}

static void timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if(player <= 0) return;
    int status;
    pid_t r = waitpid(player, &status, WNOHANG);
    if(r == player) {
        player = -1;
        if(WIFEXITED(status) && WEXITSTATUS(status) == 127)
            set_status("aplay is missing; install alsa-utils");
        else if(WIFEXITED(status) && WEXITSTATUS(status) != 0)
            set_status("ALSA playback failed; check aplay -l / dmesg");
        else set_status("Track finished");
        lv_label_set_text(time_label, "00:14 / 00:14");
        return;
    }
    unsigned sec = (unsigned)((now_ms() - play_started_ms) / 1000);
    if(sec > SONG_SECONDS) sec = SONG_SECONDS;
    char text[32];
    snprintf(text, sizeof(text), "00:%02u / 00:14", sec);
    lv_label_set_text(time_label, text);
}

int main(void)
{
    signal(SIGCHLD, SIG_DFL);
    console_graphics();
    signal(SIGTERM, quit);
    signal(SIGINT, quit);
    lv_init();
    lv_tick_set_cb(tick);
    lv_display_t *disp = korvo_ppa_display_create();
    if(!disp) { fprintf(stderr, "cannot create LVGL framebuffer display\n"); return 1; }
    char *touch = find_touch();
    if(touch) {
        lv_indev_t *indev = lv_evdev_create(LV_INDEV_TYPE_POINTER, touch);
        if(indev) lv_evdev_set_calibration(indev, 0, 0, 799, 479);
        free(touch);
    }

    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x101827), 0);
    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);
    lv_obj_t *title = lv_label_create(lv_screen_active());
    lv_label_set_text(title, "KORVO MUSIC");
    lv_obj_set_style_text_color(title, lv_color_hex(0xEAF2FF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 34);

    lv_obj_t *card = lv_obj_create(lv_screen_active());
    lv_obj_set_size(card, 580, 265);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 28);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1B2940), 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 24, 0);
    lv_obj_set_style_pad_all(card, 24, 0);

    lv_obj_t *album = lv_obj_create(card);
    lv_obj_set_size(album, 150, 150);
    lv_obj_align(album, LV_ALIGN_LEFT_MID, 16, -8);
    lv_obj_set_style_bg_color(album, lv_color_hex(0x24B7A5), 0);
    lv_obj_set_style_border_width(album, 0, 0);
    lv_obj_set_style_radius(album, 24, 0);
    lv_obj_t *note = lv_label_create(album);
    lv_label_set_text(note, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_color(note, lv_color_white(), 0);
    lv_obj_set_style_text_font(note, &lv_font_montserrat_48, 0);
    lv_obj_center(note);

    lv_obj_t *song = lv_label_create(card);
    lv_label_set_text(song, "S31 Sunrise");
    lv_obj_set_style_text_color(song, lv_color_white(), 0);
    lv_obj_set_style_text_font(song, &lv_font_montserrat_24, 0);
    lv_obj_align(song, LV_ALIGN_TOP_LEFT, 198, 34);
    lv_obj_t *artist = lv_label_create(card);
    lv_label_set_text(artist, "Korvo-1 original demo tune");
    lv_obj_set_style_text_color(artist, lv_color_hex(0x9BAFC9), 0);
    lv_obj_align_to(artist, song, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);

    time_label = lv_label_create(card);
    lv_label_set_text(time_label, "00:00 / 00:14");
    lv_obj_set_style_text_color(time_label, lv_color_hex(0xB8C8DD), 0);
    lv_obj_align(time_label, LV_ALIGN_BOTTOM_LEFT, 198, -67);

    lv_obj_t *play = make_button(card, LV_SYMBOL_PLAY "  PLAY", 150, 54, play_cb);
    lv_obj_align(play, LV_ALIGN_BOTTOM_LEFT, 198, -2);
    lv_obj_set_style_bg_color(play, lv_color_hex(0x24B7A5), 0);
    lv_obj_t *stop = make_button(card, LV_SYMBOL_STOP "  STOP", 120, 54, stop_cb);
    lv_obj_align_to(stop, play, LV_ALIGN_OUT_RIGHT_MID, 14, 0);
    lv_obj_set_style_bg_color(stop, lv_color_hex(0x41536C), 0);

    status_label = lv_label_create(lv_screen_active());
    lv_label_set_text(status_label, "Touch PLAY to test audio output");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xB8C8DD), 0);
    lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, -22);
    lv_timer_create(timer_cb, 200, NULL);

    lv_obj_t *home = make_button(lv_layer_top(), "HOME", 90, 40, home_cb);
    lv_obj_align(home, LV_ALIGN_TOP_RIGHT, -4, 4);
    while(running) {
        uint32_t delay_ms = lv_timer_handler();
        if(delay_ms > 33) delay_ms = 33;
        if(delay_ms < 2) delay_ms = 2;
        struct timespec delay = {0, delay_ms * 1000000L};
        nanosleep(&delay, NULL);
    }
    stop_audio();
    lv_deinit();
    if(go_home) {
        console_prepare_exec();
        char executable[256];
        ssize_t count = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
        if(count >= 0) executable[count] = 0;
        const char *launcher = count >= 0 && strstr(executable, "lvgl-music-demo-extra") ? "/usr/bin/korvo-ui-extra" : "/usr/bin/korvo-ui";
        execl(launcher, launcher, "menu", (char *)NULL);
    }
    return 0;
}
