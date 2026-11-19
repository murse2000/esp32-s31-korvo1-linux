/* SPDX-License-Identifier: MIT */
#ifndef KORVO_PPA_UAPI_H
#define KORVO_PPA_UAPI_H
#include <linux/types.h>
#include <linux/ioctl.h>
#define KORVO_PPA_ABI 1
#define KORVO_PPA_BUFFER_SIZE 32768U
#define KORVO_PPA_FRAMEBUFFER 2U
#define KORVO_PPA_COPY 1U
#define KORVO_PPA_FILL 2U
#define KORVO_PPA_FLUSH 3U
#define KORVO_PPA_BLEND 4U
#define KORVO_PPA_REUSE 5U
#define KORVO_PPA_RGB565 0U
#define KORVO_PPA_ARGB8888 1U
#define KORVO_PPA_A8 2U
#define KORVO_PPA_CAP_A8 (1U << 8)
#define KORVO_PPA_CAP_IRQ (1U << 9)
struct korvo_ppa_info {
    __u32 abi, buffer_size, buffers, capabilities;
    __u32 screen_width, screen_height, render_buffer_size, reserved;
};
struct korvo_ppa_surface { __u32 buffer, offset, width, height; };
struct korvo_ppa_request {
    __u32 operation, format;
    struct korvo_ppa_surface source, destination;
    __u32 source_x, source_y, destination_x, destination_y;
    __u32 width, height, color, alpha;
};
struct korvo_ppa_stats {
    __u64 copies, fills, flushes, blends, errors, bytes, nanoseconds;
};
#define KORVO_PPA_IOC_INFO _IOR('P', 0x40, struct korvo_ppa_info)
#define KORVO_PPA_IOC_RUN _IOW('P', 0x41, struct korvo_ppa_request)
#define KORVO_PPA_IOC_STATS _IOR('P', 0x42, struct korvo_ppa_stats)
#endif
