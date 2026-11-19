// SPDX-License-Identifier: MIT
#ifndef KORVO_LINUX_CONSOLE_H
#define KORVO_LINUX_CONSOLE_H
#include <fcntl.h>
#include <dirent.h>
#include <linux/kd.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>
static int console_fd = -1;
static void console_text(void)
{
    if(console_fd >= 0) {
        ioctl(console_fd, KDSETMODE, KD_TEXT);
        close(console_fd);
        console_fd = -1;
    }
}
static void console_graphics(void)
{
    console_fd = open("/dev/tty0", O_RDWR | O_NOCTTY | O_CLOEXEC);
    if(console_fd >= 0 && ioctl(console_fd, KDSETMODE, KD_GRAPHICS)) {
        close(console_fd); console_fd = -1;
    }
    atexit(console_text);
}
static inline void console_prepare_exec(void)
{
    /* LVGL 9.2 fbdev/evdev descriptors are not close-on-exec. Keep only
     * stdin/out/err across demo switches or an ALSA player exec.
     */
    DIR *dir = opendir("/proc/self/fd");
    if(!dir) return;
    struct dirent *entry;
    while((entry = readdir(dir))) {
        char *end;
        long fd = strtol(entry->d_name, &end, 10);
        if(*end == '\0' && fd >= 3)
            fcntl((int)fd, F_SETFD, FD_CLOEXEC);
    }
    closedir(dir);
}
#endif
