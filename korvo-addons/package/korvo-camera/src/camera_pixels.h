// SPDX-License-Identifier: MIT
#ifndef KORVO_CAMERA_PIXELS_H
#define KORVO_CAMERA_PIXELS_H
#include <stdint.h>
#include <linux/videodev2.h>
static inline int clip_byte(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }
static inline uint16_t yuv565(int y, int u, int v)
{
    int c = y - 16, d = u - 128, e = v - 128;
    int r = clip_byte((298*c + 409*e + 128) >> 8);
    int g = clip_byte((298*c - 100*d - 208*e + 128) >> 8);
    int b = clip_byte((298*c + 516*d + 128) >> 8);
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}
/* Convert the camera's wire format in place to little-endian RGB565. */
static inline void camera_to_rgb565(unsigned char *pixels, unsigned bytes, uint32_t format)
{
    if(format == V4L2_PIX_FMT_YUYV) {
        for(unsigned i = 0; i + 3 < bytes; i += 4) {
            int y0 = pixels[i], u = pixels[i + 1], y1 = pixels[i + 2], v = pixels[i + 3];
            uint16_t a = yuv565(y0, u, v), b = yuv565(y1, u, v);
            pixels[i] = a; pixels[i + 1] = a >> 8;
            pixels[i + 2] = b; pixels[i + 3] = b >> 8;
        }
    } else {
        for(unsigned i = 0; i + 1 < bytes; i += 2) {
            unsigned char b = pixels[i]; pixels[i] = pixels[i + 1]; pixels[i + 1] = b;
        }
    }
}
#endif
