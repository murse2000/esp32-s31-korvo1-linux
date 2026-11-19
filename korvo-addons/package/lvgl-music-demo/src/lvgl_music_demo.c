#define _POSIX_C_SOURCE 200809L
#include <lvgl.h>
#include <src/drivers/display/fb/lv_linux_fbdev.h>
#include <src/drivers/evdev/lv_evdev.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <math.h>
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

#define SAMPLE_RATE 22050
#define SONG_SECONDS 14
#define PI 3.14159265358979323846

static pid_t player = -1;
static lv_obj_t *status_label;
static lv_obj_t *time_label;
static uint64_t play_started_ms;
static const char *song_path = "/tmp/korvo-lvgl-demo.wav";
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

static void write_demo_wav(void)
{
    const uint32_t frames = SAMPLE_RATE * SONG_SECONDS;
    FILE *f = fopen(song_path, "wb");
    if(!f) return;
    uint32_t data_bytes = frames * 4;
    uint8_t h[44] = { 'R','I','F','F' };
    uint32_t riff_size = data_bytes + 36, byte_rate = SAMPLE_RATE * 4;
    uint16_t one = 1, channels = 2, align = 4, bits = 16;
    memcpy(h + 4, &riff_size, 4); memcpy(h + 8, "WAVEfmt ", 8);
    uint32_t fmt_size = 16; memcpy(h + 16, &fmt_size, 4);
    memcpy(h + 20, &one, 2); memcpy(h + 22, &channels, 2);
    memcpy(h + 24, &((uint32_t){SAMPLE_RATE}), 4);
    memcpy(h + 28, &byte_rate, 4); memcpy(h + 32, &align, 2);
    memcpy(h + 34, &bits, 2); memcpy(h + 36, "data", 4);
    memcpy(h + 40, &data_bytes, 4); fwrite(h, 1, sizeof(h), f);

    /* A short original chiptune-style melody, stored as stereo PCM WAV. */
    static const double notes[] = { 523.25,659.25,783.99,659.25,587.33,698.46,
                                    880.00,698.46,523.25,659.25,783.99,1046.5,
                                    880.00,783.99,659.25,523.25 };
    const uint32_t note_frames = frames / (sizeof(notes) / sizeof(notes[0]));
    for(uint32_t i = 0; i < frames; ++i) {
        uint32_t n = (i / note_frames) % (sizeof(notes) / sizeof(notes[0]));
        uint32_t pos = i % note_frames;
        double edge = pos < 350 ? pos / 350.0 : (note_frames - pos < 650 ? (note_frames - pos) / 650.0 : 1.0);
        double phase = (double)i * notes[n] * 2.0 * PI / SAMPLE_RATE;
        double s = (sin(phase) + 0.22 * sin(phase * 2.0) + 0.08 * sin(phase * 3.0)) * edge * 0.22;
        int16_t sample = (int16_t)(s * 32767.0);
        fwrite(&sample, sizeof(sample), 1, f); fwrite(&sample, sizeof(sample), 1, f);
    }
    fclose(f);
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

static void make_wav(void)
{
    struct stat st;
    if(stat(song_path, &st) || st.st_size < 44) write_demo_wav();
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
    make_wav();
    if(access(song_path, R_OK)) { set_status("Could not prepare demo audio"); return; }
    player = fork();
    if(player == 0) {
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
    lv_display_t *disp = lv_linux_fbdev_create();
    if(!disp) { fprintf(stderr, "cannot create LVGL framebuffer display\n"); return 1; }
    lv_linux_fbdev_set_file(disp, "/dev/fb0");
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
        lv_timer_handler();
        { struct timespec delay = {0, 5000000L}; nanosleep(&delay, NULL); }
    }
    stop_audio();
    lv_deinit();
    if(go_home) execl("/usr/bin/korvo-ui", "korvo-ui", "menu", (char *)NULL);
    return 0;
}
