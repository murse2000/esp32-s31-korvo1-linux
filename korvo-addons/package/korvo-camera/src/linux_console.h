// SPDX-License-Identifier: MIT
#ifndef KORVO_LINUX_CONSOLE_H
#define KORVO_LINUX_CONSOLE_H
#include <fcntl.h>
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
#endif
