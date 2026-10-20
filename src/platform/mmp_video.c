#define _GNU_SOURCE
#include "mmp_video.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>

static int s_fb_fd = -1;
static uint16_t *s_fb_ptr = NULL;
static size_t s_fb_size = 0;
static size_t s_line_length = 0;
static struct fb_var_screeninfo s_vinfo;
static struct fb_fix_screeninfo s_finfo;

void mmp_frame_pacing(uint64_t target_frame_time_ns) {
    static struct timespec last_ts;
    static bool initialized = false;

    struct timespec now_ts;
    clock_gettime(CLOCK_MONOTONIC, &now_ts);

    if (!initialized) {
        last_ts = now_ts;
        initialized = true;
        return;
    }

    int64_t elapsed = (int64_t)(now_ts.tv_sec - last_ts.tv_sec) * 1000000000LL + (now_ts.tv_nsec - last_ts.tv_nsec);

    if (elapsed < (int64_t)target_frame_time_ns) {
        uint64_t remaining_ns = target_frame_time_ns - elapsed;

        // On Miyoo Mini (CONFIG_HZ=100), sleep ticks last 10ms.
        // Only nanosleep when more than 12ms remain.
        if (remaining_ns > 12000000ULL) {
            struct timespec sleep_ts = { 0, (long)(remaining_ns - 12000000ULL) };
            nanosleep(&sleep_ts, NULL);
        }

        // Sub-millisecond spin-wait with CPU yield instruction.
        do {
#if defined(__arm__) || defined(__aarch64__)
            __asm__ volatile("yield" ::: "memory");
#endif
            clock_gettime(CLOCK_MONOTONIC, &now_ts);
            elapsed = (int64_t)(now_ts.tv_sec - last_ts.tv_sec) * 1000000000LL + (now_ts.tv_nsec - last_ts.tv_nsec);
        } while (elapsed < (int64_t)target_frame_time_ns);
    }

    // Advance timeline to prevent long-term drift
    last_ts.tv_nsec += (long)(target_frame_time_ns % 1000000000ULL);
    last_ts.tv_sec += (time_t)(target_frame_time_ns / 1000000000ULL);
    if (last_ts.tv_nsec >= 1000000000L) {
        last_ts.tv_sec++;
        last_ts.tv_nsec -= 1000000000L;
    }

    // Re-anchor if we dropped more than a full frame
    int64_t lag = (int64_t)(now_ts.tv_sec - last_ts.tv_sec) * 1000000000LL + (now_ts.tv_nsec - last_ts.tv_nsec);
    if (lag > (int64_t)target_frame_time_ns) {
        last_ts = now_ts;
    }
}

void mmp_invert_coords_180(int x, int y, int width, int height, int *out_x, int *out_y) {
    if (out_x) *out_x = (width - 1) - x;
    if (out_y) *out_y = (height - 1) - y;
}

int mmp_fb_init(const char *fb_device) {
    const char *dev = fb_device ? fb_device : "/dev/fb0";
    s_fb_fd = open(dev, O_RDWR);
    if (s_fb_fd < 0) {
        return -1;
    }

    if (ioctl(s_fb_fd, FBIOGET_VSCREENINFO, &s_vinfo) < 0) {
        close(s_fb_fd);
        s_fb_fd = -1;
        return -2;
    }

    if (ioctl(s_fb_fd, FBIOGET_FSCREENINFO, &s_finfo) < 0) {
        close(s_fb_fd);
        s_fb_fd = -1;
        return -3;
    }

    s_line_length = s_finfo.line_length > 0 ? s_finfo.line_length : (s_vinfo.xres * (s_vinfo.bits_per_pixel / 8));
    s_fb_size = s_vinfo.yres_virtual * s_line_length;
    s_fb_ptr = (uint16_t *)mmap(NULL, s_fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, s_fb_fd, 0);
    if (s_fb_ptr == MAP_FAILED) {
        close(s_fb_fd);
        s_fb_fd = -1;
        return -4;
    }

    return 0;
}

void mmp_fb_close(void) {
    if (s_fb_ptr && s_fb_ptr != MAP_FAILED) {
        // Clear framebuffer to black before unmapping to prevent frozen screen artifacts
        memset(s_fb_ptr, 0, s_fb_size);
        if (s_fb_fd >= 0) {
            ioctl(s_fb_fd, FBIOPAN_DISPLAY, &s_vinfo);
        }
        munmap(s_fb_ptr, s_fb_size);
        s_fb_ptr = NULL;
    }
    if (s_fb_fd >= 0) {
        close(s_fb_fd);
        s_fb_fd = -1;
    }
}

void mmp_video_flip_180(const uint16_t *src_buffer, int width, int height) {
    if (!src_buffer || !s_fb_ptr) return;

    size_t stride_pixels = s_line_length > 0 ? (s_line_length / sizeof(uint16_t)) : (size_t)width;

    for (int y = 0; y < height; y++) {
        int dst_y = (height - 1) - y;
        uint16_t *dst_row = s_fb_ptr + dst_y * stride_pixels;
        for (int x = 0; x < width; x++) {
            int dst_x = (width - 1) - x;
            dst_row[dst_x] = src_buffer[y * width + x];
        }
    }
}
