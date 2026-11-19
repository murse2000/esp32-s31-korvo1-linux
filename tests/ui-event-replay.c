// SPDX-License-Identifier: MIT
// Usage: ui-event-replay /dev/input/eventN X Y
// Emit a short touch through Linux evdev, including complete SYN reports.
#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <linux/input.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
static void event(int fd, int type, int code, int value)
{
    struct input_event e = {0};
    e.type = type; e.code = code; e.value = value;
    if(write(fd, &e, sizeof(e)) != sizeof(e)) _exit(2);
}
int main(int argc, char **argv)
{
    if(argc != 4) return 2;
    int fd = open(argv[1], O_WRONLY | O_CLOEXEC);
    if(fd < 0) return 1;
    event(fd, EV_ABS, ABS_MT_SLOT, 0);
    event(fd, EV_ABS, ABS_MT_TRACKING_ID, 7);
    event(fd, EV_ABS, ABS_MT_POSITION_X, atoi(argv[2]));
    event(fd, EV_ABS, ABS_MT_POSITION_Y, atoi(argv[3]));
    event(fd, EV_ABS, ABS_X, atoi(argv[2]));
    event(fd, EV_ABS, ABS_Y, atoi(argv[3]));
    event(fd, EV_KEY, BTN_TOUCH, 1);
    event(fd, EV_SYN, SYN_REPORT, 0);
    struct timespec hold = {0, 150000000};
    nanosleep(&hold, NULL);
    event(fd, EV_ABS, ABS_MT_TRACKING_ID, -1);
    event(fd, EV_KEY, BTN_TOUCH, 0);
    event(fd, EV_SYN, SYN_REPORT, 0);
    close(fd);
    return 0;
}
