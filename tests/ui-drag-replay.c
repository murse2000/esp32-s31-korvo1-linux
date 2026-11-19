/* SPDX-License-Identifier: MIT
 * Usage: ui-drag-replay /dev/input/eventN X1 Y1 X2 Y2 steps interval-ms
 */
#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <linux/input.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
static void event(int fd, int type, int code, int value)
{
    struct input_event e = {0}; e.type = type; e.code = code; e.value = value;
    if(write(fd, &e, sizeof(e)) != sizeof(e)) _exit(2);
}
static void point(int fd, int x, int y)
{
    event(fd, EV_ABS, ABS_MT_POSITION_X, x); event(fd, EV_ABS, ABS_MT_POSITION_Y, y);
    event(fd, EV_ABS, ABS_X, x); event(fd, EV_ABS, ABS_Y, y);
    event(fd, EV_SYN, SYN_REPORT, 0);
}
int main(int argc, char **argv)
{
    if(argc != 8) return 2;
    int x1 = atoi(argv[2]), y1 = atoi(argv[3]), x2 = atoi(argv[4]), y2 = atoi(argv[5]);
    int steps = atoi(argv[6]), interval = atoi(argv[7]);
    if(steps < 1 || steps > 10000 || interval < 1 || interval > 1000) return 2;
    int fd = open(argv[1], O_WRONLY | O_CLOEXEC);
    if(fd < 0) return 1;
    event(fd, EV_ABS, ABS_MT_SLOT, 0); event(fd, EV_ABS, ABS_MT_TRACKING_ID, 9);
    event(fd, EV_KEY, BTN_TOUCH, 1); point(fd, x1, y1);
    struct timespec delay = {interval / 1000, (interval % 1000) * 1000000L};
    for(int i = 1; i <= steps; i++) {
        nanosleep(&delay, NULL);
        point(fd, x1 + (x2 - x1) * i / steps, y1 + (y2 - y1) * i / steps);
    }
    event(fd, EV_ABS, ABS_MT_TRACKING_ID, -1); event(fd, EV_KEY, BTN_TOUCH, 0);
    event(fd, EV_SYN, SYN_REPORT, 0); close(fd);
    return 0;
}
