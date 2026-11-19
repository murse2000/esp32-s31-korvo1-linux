// SPDX-License-Identifier: MIT
#define _POSIX_C_SOURCE 200809L
#include "korvo_ppa.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
static void die(const char *s) { perror(s); exit(1); }
int main(int argc, char **argv)
{
    int fd = open("/dev/esp32s31-ppa", O_RDWR | O_CLOEXEC);
    if(fd < 0) die("open PPA");
    struct korvo_ppa_info info;
    struct korvo_ppa_stats stats;
    if(ioctl(fd, KORVO_PPA_IOC_INFO, &info)) die("info");
    printf("PPA ABI %u, render %u bytes, scratch %u bytes, capabilities 0x%x\n",
           info.abi, info.render_buffer_size, info.buffer_size, info.capabilities);
    if(argc > 1 && !strcmp(argv[1], "--stats")) goto stats;
    uint16_t *a = mmap(NULL, info.buffer_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    uint16_t *b = mmap(NULL, info.buffer_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, info.buffer_size);
    if(a == MAP_FAILED || b == MAP_FAILED) die("mmap");
    for(unsigned i = 0; i < info.buffer_size / 2; i++) { a[i] = 0x1234; b[i] = (i * 131U) ^ 0x7c31; }
    struct korvo_ppa_request q = {.operation = KORVO_PPA_COPY, .format = KORVO_PPA_RGB565,
        .source = {1, 0, 128, 64}, .destination = {0, 0, 128, 64},
        .source_x = 6, .source_y = 5, .destination_x = 10, .destination_y = 7,
        .width = 48, .height = 25};
    if(ioctl(fd, KORVO_PPA_IOC_RUN, &q)) die("DMA2D copy");
    for(unsigned y = 0; y < 64; y++) for(unsigned x = 0; x < 128; x++) {
        uint16_t expected = y >= 7 && y < 32 && x >= 10 && x < 58 ? b[(y - 7 + 5) * 128 + x - 10 + 6] : 0x1234;
        if(a[y * 128 + x] != expected) {
            fprintf(stderr, "copy mismatch (%u,%u): %04x != %04x\n", x, y, a[y * 128 + x], expected);
            return 1;
        }
    }
    puts("DMA2D cropped/strided RGB565 copy and surrounding pixels: OK");
    struct korvo_ppa_request saved = q;
    for(unsigned i = 0; i < info.buffer_size / 2; i++) a[i] = 0x1234;
    q.destination = (struct korvo_ppa_surface){0, 0, 512, 32};
    q.destination_y = 3; q.width = 8;
    if(ioctl(fd, KORVO_PPA_IOC_RUN, &q)) die("narrow DMA2D copy");
    for(unsigned y = 0; y < 32; y++) for(unsigned x = 0; x < 512; x++) {
        uint16_t expected = y >= 3 && y < 28 && x >= 10 && x < 18 ? b[(y - 3 + 5) * 128 + x - 10 + 6] : 0x1234;
        if(a[y * 512 + x] != expected) { fputs("narrow copy or surrounding pixels mismatch\n", stderr); return 1; }
    }
    q = saved;
    puts("Narrow rectangle cache synchronization and surrounding pixels: OK");
    if(info.capabilities & (1U << KORVO_PPA_REUSE)) {
        int fb = open("/dev/fb0", O_RDONLY | O_CLOEXEC);
        if(fb < 0) die("framebuffer reference");
        uint16_t *pixels = mmap(NULL, 800 * 480 * 2, PROT_READ, MAP_SHARED, fb, 0);
        if(pixels == MAP_FAILED) die("framebuffer mmap");
        for(unsigned odd = 0; odd < 2; odd++) {
            for(unsigned i = 0; i < info.buffer_size / 2; i++) a[i] = 0x1234;
            q = saved; q.operation = KORVO_PPA_REUSE;
            q.source = (struct korvo_ppa_surface){KORVO_PPA_FRAMEBUFFER, 0, 800, 480};
            q.source_x = 20 + odd; q.source_y = 230;
            if(ioctl(fd, KORVO_PPA_IOC_RUN, &q)) die("framebuffer reuse");
            for(unsigned y = 0; y < 64; y++) for(unsigned x = 0; x < 128; x++) {
                uint16_t expected = y >= 7 && y < 32 && x >= 10 && x < 58 ?
                    pixels[(y - 7 + 230) * 800 + x - 10 + 20 + odd] : 0x1234;
                if(a[y * 128 + x] != expected) { fputs("framebuffer reuse mismatch\n", stderr); return 1; }
            }
        }
        munmap(pixels, 800 * 480 * 2); close(fb); q = saved;
        puts("Framebuffer reuse, even/odd source X and untouched surrounding pixels: OK");
    }
    const uint16_t colors[] = {0xf800, 0x07e0, 0x001f, 0xffff, 0x0000};
    for(unsigned c = 0; c < sizeof(colors)/sizeof(colors[0]); c++) {
        for(unsigned i = 0; i < info.buffer_size / 2; i++) a[i] = 0x1234;
        q.operation = KORVO_PPA_FILL; q.color = colors[c];
        if(ioctl(fd, KORVO_PPA_IOC_RUN, &q)) die("PPA fill");
        for(unsigned y = 0; y < 64; y++) for(unsigned x = 0; x < 128; x++) {
            uint16_t expected = y >= 7 && y < 32 && x >= 10 && x < 58 ? colors[c] : 0x1234;
            if(a[y * 128 + x] != expected) {
                fprintf(stderr, "fill mismatch (%u,%u): %04x != %04x\n", x, y, a[y * 128 + x], expected);
                return 1;
            }
        }
    }
    puts("PPA RGB565 red/green/blue/white/black fills: OK");
    /* Red over blue: compare all channels, allowing one RGB565 quantization step. */
    for(unsigned format = 0; format < (info.capabilities & KORVO_PPA_CAP_A8 ? 3 : 2); format++) {
        for(unsigned i = 0; i < info.buffer_size / 2; i++) a[i] = 0x001f;
        if(format == KORVO_PPA_A8) {
            memset(b, 128, info.buffer_size);
        } else if(format) {
            for(unsigned i = 0; i < info.buffer_size / 4; i++) ((uint32_t *)b)[i] = 0x80ff0000;
        } else {
            for(unsigned i = 0; i < info.buffer_size / 2; i++) b[i] = 0xf800;
        }
        q.operation = KORVO_PPA_BLEND; q.format = format; q.color = 0xf800;
        q.source = (struct korvo_ppa_surface){1, 0, 128, 64};
        q.alpha = format ? 255 : 128;
        if(ioctl(fd, KORVO_PPA_IOC_RUN, &q)) die("PPA blend");
        for(unsigned y = 0; y < 64; y++) for(unsigned x = 0; x < 128; x++) {
            uint16_t v = a[y * 128 + x];
            if(y >= 7 && y < 32 && x >= 10 && x < 58) {
                if((v >> 11) < 15 || (v >> 11) > 16 || (v & 31) < 14 || (v & 31) > 16 || (v & 0x7e0)) {
                    fprintf(stderr, "blend format %u mismatch (%u,%u): %04x\n", format, x, y, v); return 1;
                }
            } else if(v != 0x001f) { fputs("blend damaged surrounding pixels\n", stderr); return 1; }
        }
    }
    puts("PPA RGB565 / ARGB8888 / supported A8 alpha blends: OK");
    q.operation = KORVO_PPA_FILL; q.format = 0;
    q.destination.offset = info.render_buffer_size;
    errno = 0;
    if(ioctl(fd, KORVO_PPA_IOC_RUN, &q) != -1 || errno != EINVAL) {
        fputs("out-of-bounds request was not rejected\n", stderr); return 1;
    }
    puts("Out-of-bounds request rejected: OK");
    munmap(a, info.buffer_size); munmap(b, info.buffer_size);
stats:
    if(ioctl(fd, KORVO_PPA_IOC_STATS, &stats)) die("stats");
    printf("hardware copy=%llu fill=%llu flush=%llu blend=%llu errors=%llu bytes=%llu ns=%llu\n",
           (unsigned long long)stats.copies, (unsigned long long)stats.fills,
           (unsigned long long)stats.flushes, (unsigned long long)stats.blends,
           (unsigned long long)stats.errors, (unsigned long long)stats.bytes,
           (unsigned long long)stats.nanoseconds);
    close(fd);
    return 0;
}
