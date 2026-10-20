#define _GNU_SOURCE
#include "mmp_video.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sched.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>

static int s_fb_fd = -1;
static uint16_t *s_fb_ptr = NULL;
static size_t s_fb_size = 0;
static struct fb_var_screeninfo s_vinfo;

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

    uint64_t elapsed = (now_ts.tv_sec - last_ts.tv_sec) * 1000000000ULL + (now_ts.tv_nsec - last_ts.tv_nsec);

    if (elapsed < target_frame_time_ns) {
        uint64_t remaining_ns = target_frame_time_ns - elapsed;

        // On Miyoo Mini (CONFIG_HZ=100), sleep ticks last 10ms.
        // Only nanosleep when more than 12ms remain.
        if (remaining_ns > 12000000ULL) {
            struct timespec sleep_ts = { 0, (long)(remaining_ns - 12000000ULL) };
            nanosleep(&sleep_ts, NULL);
        }

        // Precision spinning with sched_yield during the final sub-millisecond window
        do {
            sched_yield();
            clock_gettime(CLOCK_MONOTONIC, &now_ts);
            elapsed = (now_ts.tv_sec - last_ts.tv_sec) * 1000000000ULL + (now_ts.tv_nsec - last_ts.tv_nsec);
        } while (elapsed < target_frame_time_ns);
    }

    clock_gettime(CLOCK_MONOTONIC, &last_ts);
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

    s_fb_size = s_vinfo.yres_virtual * s_vinfo.xres * (s_vinfo.bits_per_pixel / 8);
    s_fb_ptr = (uint16_t *)mmap(NULL, s_fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, s_fb_fd, 0);
    if (s_fb_ptr == MAP_FAILED) {
        close(s_fb_fd);
        s_fb_fd = -1;
        return -3;
    }

    return 0;
}

void mmp_fb_close(void) {
    if (s_fb_ptr && s_fb_ptr != MAP_FAILED) {
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

    for (int y = 0; y < height; y++) {
        int dst_y = (height - 1) - y;
        for (int x = 0; x < width; x++) {
            int dst_x = (width - 1) - x;
            s_fb_ptr[dst_y * width + dst_x] = src_buffer[y * width + x];
        }
    }
}
