// SPDX-License-Identifier: MIT
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include "camera_pixels.h"
#include "linux_console.h"

int main(int argc, char **argv)
{
    const int preview = argc > 1 && !strcmp(argv[1], "--preview");
    const char *output = argc > 1 && !preview ? argv[1] : "/mnt/sd/camera.ppm";
    int seconds = preview ? (argc > 2 ? atoi(argv[2]) : 15) : 0;
    if(preview && seconds < 1) seconds = 15;
    int fd = open("/dev/video0", O_RDONLY | O_NONBLOCK);
    if(fd < 0) { perror("/dev/video0"); return 1; }
    struct v4l2_capability cap;
    if(ioctl(fd, VIDIOC_QUERYCAP, &cap)) { perror("VIDIOC_QUERYCAP"); close(fd); return 1; }
    struct v4l2_format fmt = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE};
    if(ioctl(fd, VIDIOC_G_FMT, &fmt) || (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_RGB565X && fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV)) {
        fprintf(stderr, "Expected RGB565X or YUYV camera format\n"); close(fd); return 1;
    }
    const unsigned width = fmt.fmt.pix.width, height = fmt.fmt.pix.height;
    unsigned char *frame = malloc(fmt.fmt.pix.sizeimage);
    if(!frame) { close(fd); return 1; }
    int fb = -1;
    uint8_t *pixels = MAP_FAILED;
    struct fb_fix_screeninfo fix;
    struct fb_var_screeninfo var;
    if(preview) {
        console_graphics();
        fb = open("/dev/fb0", O_RDWR);
        if(fb < 0 || ioctl(fb, FBIOGET_FSCREENINFO, &fix) || ioctl(fb, FBIOGET_VSCREENINFO, &var) || var.bits_per_pixel != 16) {
            fprintf(stderr, "RGB565 framebuffer required\n"); goto fail;
        }
        pixels = mmap(NULL, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fb, 0);
        if(pixels == MAP_FAILED) { perror("mmap fb"); goto fail; }
    }
    time_t start = time(NULL);
    unsigned frames = 0, errors = 0;
    do {
        struct pollfd pfd = {.fd = fd, .events = POLLIN};
        int ready = poll(&pfd, 1, 3000);
        if(ready <= 0) { fprintf(stderr, "Camera frame timeout\n"); goto fail; }
        ssize_t bytes = read(fd, frame, fmt.fmt.pix.sizeimage);
        if(bytes < 0 && (errno == EAGAIN || errno == EINTR)) continue;
        if(bytes != (ssize_t)fmt.fmt.pix.sizeimage) {
            if(++errors < 5) continue;
            fprintf(stderr, "Capture failed after %u frames: %s\n", frames, strerror(errno)); goto fail;
        }
        errors = 0; frames++;
        camera_to_rgb565(frame, bytes, fmt.fmt.pix.pixelformat);
        if(preview) {
            unsigned scale = var.yres >= height * 2 && var.xres >= width * 2 ? 2 : 1;
            unsigned xoff = (var.xres - width * scale)/2, yoff = (var.yres - height * scale)/2;
            for(unsigned y = 0; y < height * scale; y++) {
                uint16_t *row = (uint16_t *)(pixels + (y + yoff) * fix.line_length);
                for(unsigned x = 0; x < width * scale; x++) {
                    unsigned n = ((y / scale) * width + x / scale) * 2;
                    row[x + xoff] = frame[n] | ((uint16_t)frame[n + 1] << 8);
                }
            }
        } else {
            FILE *f = fopen(output, "wb");
            if(!f) { perror(output); goto fail; }
            fprintf(f, "P6\n%u %u\n255\n", width, height);
            for(unsigned i = 0; i < width * height; i++) {
                uint16_t px = frame[2*i] | ((uint16_t)frame[2*i + 1] << 8);
                unsigned char rgb[] = { ((px >> 11) & 31) * 255 / 31,
                                       ((px >> 5) & 63) * 255 / 63,
                                       (px & 31) * 255 / 31 };
                if(fwrite(rgb, 1, 3, f) != 3) { fclose(f); goto fail; }
            }
            if(fclose(f)) goto fail;
            printf("Saved %s (%ux%u)\n", output, width, height);
        }
    } while(!frames || (preview && time(NULL) - start < seconds));
    printf("Captured %u valid frames\n", frames);
    if(pixels != MAP_FAILED) munmap(pixels, fix.smem_len);
    if(fb >= 0) close(fb);
    free(frame); close(fd); return 0;
fail:
    if(pixels != MAP_FAILED) munmap(pixels, fix.smem_len);
    if(fb >= 0) close(fb);
    free(frame); close(fd); return 1;
}
