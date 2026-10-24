#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include "common.h"
#include "platformdefs.h"
#include "gettime.h"
#include "runner.h"
#include "runner_gamepad.h"
#include "runner_keyboard.h"
#include "runner_mouse.h"
#include "sw_renderer.h"
#include "instance.h"
#include "int_rvalue_hashmap.h"
#include "vm.h"
#include "stb_ds.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <linux/input.h>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 480
#define MAX_INPUT_DEVS 4

static Runner *g_runner = NULL;
static bool g_initialized = false;

// Framebuffer state
static int s_fb_fd = -1;
static void *s_fb_ptr = NULL;
static size_t s_fb_size = 0;
static size_t s_line_length = 0;
static struct fb_var_screeninfo s_vinfo;
static struct fb_fix_screeninfo s_finfo;

// Evdev input state
static int s_input_fds[MAX_INPUT_DEVS] = {-1, -1, -1, -1};
static int s_num_fds = 0;

typedef struct {
    bool up;
    bool down;
    bool left;
    bool right;
    bool a;
    bool b;
    bool x;
    bool y;
    bool l1;
    bool r1;
    bool l2;
    bool r2;
    bool start;
    bool select;
    bool menu;
} MiyooInputState;

static MiyooInputState s_curr_state = {0};
static MiyooInputState s_prev_state = {0};

// Virtual mouse coordinates (defaults over start menu options)
static double s_mouse_x = 420.0;
static double s_mouse_y = 220.0;
static bool s_was_combat = false;

static bool is_gameplay_room(Runner *runner) {
    if (!runner) return false;
    int32_t idx = runner->currentRoomIndex;
    if ((idx >= 18 && idx <= 38) || idx == 41) return true;
    if (runner->currentRoom && runner->currentRoom->name) {
        const char *name = runner->currentRoom->name;
        if (strncmp(name, "r1_", 3) == 0 ||
            strncmp(name, "r2_", 3) == 0 ||
            strncmp(name, "r3_", 3) == 0 ||
            strncmp(name, "r4_", 3) == 0 ||
            strncmp(name, "r5_", 3) == 0 ||
            strncmp(name, "r6_", 3) == 0 ||
            strcmp(name, "rPigbeach") == 0) {
            return true;
        }
    }
    return false;
}

static bool is_game_paused(Runner *runner) {
    if (!runner || !runner->vmContext || !runner->vmContext->globalScopeInstance) return false;
    RValue rv = VM_structGetVariableByVarName(runner->vmContext, runner->vmContext->globalScopeInstance, "pause", -1);
    return RValue_toBool(rv);
}

static bool is_player_dead(Runner *runner) {
    if (!runner || !runner->instancesByObject || !runner->dataWin) return false;
    // Object 168 is oEfPlayerDead (created when player dies)
    if (168 < (int32_t)runner->dataWin->objt.count) {
        Instance **list = runner->instancesByObject[168];
        if (list != NULL && arrlen(list) > 0) return true;
    }
    // Object 167 is oP (Player). If oP does not exist in a gameplay room, player has died.
    if (167 < (int32_t)runner->dataWin->objt.count) {
        Instance **list = runner->instancesByObject[167];
        if (list == NULL || arrlen(list) == 0) return true;
    }
    return false;
}

static bool is_in_active_combat(Runner *runner) {
    if (!is_gameplay_room(runner)) return false;
    if (is_game_paused(runner)) return false;
    if (is_player_dead(runner)) return false;
    return true;
}

static int mmp_fb_init(const char *fb_device) {
    const char *dev = fb_device ? fb_device : "/dev/fb0";
    s_fb_fd = open(dev, O_RDWR);
    if (s_fb_fd < 0) {
        logWarn("Miyoo: Unable to open %s\n", dev);
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
    size_t y_res = s_vinfo.yres_virtual > 0 ? s_vinfo.yres_virtual : s_vinfo.yres;
    if (y_res == 0) y_res = SCREEN_HEIGHT;
    s_fb_size = y_res * s_line_length;

    logInfo("Miyoo FB Info: %ux%u (Virtual: %ux%u) | BPP: %u | LineLength: %zu | TotalSize: %zu\n",
            s_vinfo.xres, s_vinfo.yres, s_vinfo.xres_virtual, s_vinfo.yres_virtual,
            s_vinfo.bits_per_pixel, s_line_length, s_fb_size);

    s_fb_ptr = mmap(NULL, s_fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, s_fb_fd, 0);
    if (s_fb_ptr == MAP_FAILED) {
        s_fb_ptr = NULL;
        close(s_fb_fd);
        s_fb_fd = -1;
        return -4;
    }

    s_vinfo.xoffset = 0;
    s_vinfo.yoffset = 0;
    ioctl(s_fb_fd, FBIOPAN_DISPLAY, &s_vinfo);

    return 0;
}

static void mmp_fb_close(void) {
    if (s_fb_ptr && s_fb_ptr != MAP_FAILED) {
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

static void mmp_video_flip_180(const uint16_t *src_buffer, int width, int height) {
    if (!src_buffer || !s_fb_ptr) return;

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    if (s_vinfo.bits_per_pixel == 32) {
        // 32-bit Framebuffer (XRGB8888) - Standard on Miyoo Mini Plus
        // Optimized 128-bit ARM NEON vectorization processing 8 pixels per batch
        const uint16x8_t v_mask_g = vdupq_n_u16(0x3F);
        const uint16x8_t v_mask_b = vdupq_n_u16(0x1F);
        const uint8x8_t  v_alpha_ff = vdup_n_u8(0xFF);

        for (int y = 0; y < height; y++) {
            int dst_y = (height - 1) - y;
            uint32_t *dst_row = (uint32_t *)((uint8_t *)s_fb_ptr + dst_y * s_line_length);
            const uint16_t *src_row = src_buffer + y * width;

            int x = 0;
            for (; x <= width - 8; x += 8) {
                // 1. Load 8 consecutive RGB565 pixels sequentially
                uint16x8_t px = vld1q_u16(src_row + x);

                // 2. 180-degree horizontal inversion: reverse 8 pixels across lanes
                uint16x4_t hi_rev = vrev64_u16(vget_high_u16(px));
                uint16x4_t lo_rev = vrev64_u16(vget_low_u16(px));
                uint16x8_t px_rev = vcombine_u16(hi_rev, lo_rev);

                // 3. Channel separation from reversed pixels
                uint16x8_t r5 = vshrq_n_u16(px_rev, 11);
                uint16x8_t g6 = vandq_u16(vshrq_n_u16(px_rev, 5), v_mask_g);
                uint16x8_t b5 = vandq_u16(px_rev, v_mask_b);

                // 4. Bit-exact 8-bit channel expansion:
                // r8 = (r5 << 3) | (r5 >> 2)
                uint16x8_t r8_16 = vorrq_u16(vshlq_n_u16(r5, 3), vshrq_n_u16(r5, 2));
                // g8 = (g6 << 2) | (g6 >> 4)
                uint16x8_t g8_16 = vorrq_u16(vshlq_n_u16(g6, 2), vshrq_n_u16(g6, 4));
                // b8 = (b5 << 3) | (b5 >> 2)
                uint16x8_t b8_16 = vorrq_u16(vshlq_n_u16(b5, 3), vshrq_n_u16(b5, 2));

                // 5. Narrow to uint8x8
                uint8x8_t r8 = vmovn_u16(r8_16);
                uint8x8_t g8 = vmovn_u16(g8_16);
                uint8x8_t b8 = vmovn_u16(b8_16);

                // 6. Interleave as B, G, R, A (which is 0xAARRGGBB in little-endian uint32_t)
                uint8x8x4_t bgra;
                bgra.val[0] = b8;
                bgra.val[1] = g8;
                bgra.val[2] = r8;
                bgra.val[3] = v_alpha_ff;

                // 7. Write 8 pixels sequentially backwards
                int dst_x = (width - 8) - x;
                vst4_u8((uint8_t *)(dst_row + dst_x), bgra);
            }

            // Scalar remainder (if width not multiple of 8)
            for (; x < width; x++) {
                int dst_x = (width - 1) - x;
                uint16_t px = src_row[x];
                uint32_t r = (px >> 11) & 0x1F;
                uint32_t g = (px >> 5) & 0x3F;
                uint32_t b = px & 0x1F;
                uint8_t r8 = (r * 527 + 23) >> 6;
                uint8_t g8 = (g * 259 + 33) >> 6;
                uint8_t b8 = (b * 527 + 23) >> 6;
                dst_row[dst_x] = (0xFF << 24) | (r8 << 16) | (g8 << 8) | b8;
            }
        }
    } else {
        // 16-bit Framebuffer (RGB565) - Direct 128-bit vector reversal
        for (int y = 0; y < height; y++) {
            int dst_y = (height - 1) - y;
            uint16_t *dst_row = (uint16_t *)((uint8_t *)s_fb_ptr + dst_y * s_line_length);
            const uint16_t *src_row = src_buffer + y * width;

            int x = 0;
            for (; x <= width - 8; x += 8) {
                uint16x8_t px = vld1q_u16(src_row + x);
                uint16x4_t hi_rev = vrev64_u16(vget_high_u16(px));
                uint16x4_t lo_rev = vrev64_u16(vget_low_u16(px));
                uint16x8_t px_rev = vcombine_u16(hi_rev, lo_rev);

                int dst_x = (width - 8) - x;
                vst1q_u16(dst_row + dst_x, px_rev);
            }

            for (; x < width; x++) {
                int dst_x = (width - 1) - x;
                dst_row[dst_x] = src_row[x];
            }
        }
    }
#else
    if (s_vinfo.bits_per_pixel == 32) {
        // 32-bit Framebuffer (XRGB8888) - Scalar fallback
        for (int y = 0; y < height; y++) {
            int dst_y = (height - 1) - y;
            uint32_t *dst_row = (uint32_t *)((uint8_t *)s_fb_ptr + dst_y * s_line_length);
            const uint16_t *src_row = src_buffer + y * width;
            for (int x = 0; x < width; x++) {
                int dst_x = (width - 1) - x;
                uint16_t px = src_row[x];
                uint32_t r = (px >> 11) & 0x1F;
                uint32_t g = (px >> 5) & 0x3F;
                uint32_t b = px & 0x1F;
                uint8_t r8 = (r * 527 + 23) >> 6;
                uint8_t g8 = (g * 259 + 33) >> 6;
                uint8_t b8 = (b * 527 + 23) >> 6;
                dst_row[dst_x] = (0xFF << 24) | (r8 << 16) | (g8 << 8) | b8;
            }
        }
    } else {
        // 16-bit Framebuffer (RGB565) - Scalar fallback
        for (int y = 0; y < height; y++) {
            int dst_y = (height - 1) - y;
            uint16_t *dst_row = (uint16_t *)((uint8_t *)s_fb_ptr + dst_y * s_line_length);
            const uint16_t *src_row = src_buffer + y * width;
            for (int x = 0; x < width; x++) {
                int dst_x = (width - 1) - x;
                dst_row[dst_x] = src_row[x];
            }
        }
    }
#endif
}

typedef struct {
    uint16_t s0;
    uint16_t s1;
    uint16_t w1; // 0..256
    uint16_t w0; // 256 - w1
} ScaleMap;

static ScaleMap s_scale_cols_512[SCREEN_WIDTH];
static ScaleMap s_scale_rows_512[SCREEN_HEIGHT];
static bool s_scale_tables_512_initialized = false;

static ScaleMap s_scale_cols_426[SCREEN_WIDTH];
static ScaleMap s_scale_rows_426[SCREEN_HEIGHT];
static bool s_scale_tables_426_initialized = false;

static inline uint32_t mmp_rgb565_to_xrgb32(uint16_t px) {
    uint32_t r = (px >> 11) & 0x1F;
    uint32_t g = (px >> 5) & 0x3F;
    uint32_t b = px & 0x1F;
    uint32_t r8 = (r << 3) | (r >> 2);
    uint32_t g8 = (g << 2) | (g >> 4);
    uint32_t b8 = (b << 3) | (b >> 2);
    return (0xFFU << 24) | (r8 << 16) | (g8 << 8) | b8;
}

static void mmp_scale_init_tables_512(void) {
    if (s_scale_tables_512_initialized) return;

    const int srcW = 512;
    const int srcH = 384;
    const int dstW = SCREEN_WIDTH;  // 640
    const int dstH = SCREEN_HEIGHT; // 480
    const double scale_x = (double)dstW / (double)srcW;
    const double scale_y = (double)dstH / (double)srcH;

    // Sharp Bilinear: Preserves 100% solid square pixel centers and only applies
    // anti-aliased interpolation over a 1-pixel boundary zone, eliminating blur without shimmering.
    for (int x = 0; x < dstW; x++) {
        int x_log = (dstW - 1) - x; // 180 horizontal rotation
        double u = (x_log + 0.5) / (double)dstW;
        double texel = u * (double)srcW - 0.5;
        double texel_floor = floor(texel);
        double texel_fract = texel - texel_floor;
        double sharp_fract = (texel_fract - 0.5) * scale_x + 0.5;
        if (sharp_fract < 0.0) sharp_fract = 0.0;
        if (sharp_fract > 1.0) sharp_fract = 1.0;

        int s0 = (int)texel_floor;
        int s1 = s0 + 1;
        if (s0 < 0) s0 = 0;
        if (s1 >= srcW) s1 = srcW - 1;
        if (s0 >= srcW) s0 = srcW - 1;

        int w1 = (int)(sharp_fract * 256.0 + 0.5);
        if (w1 > 256) w1 = 256;
        if (w1 < 0) w1 = 0;
        int w0 = 256 - w1;

        if (w1 == 256) {
            s0 = s1;
            w1 = 0;
            w0 = 256;
        }

        s_scale_cols_512[x].s0 = (uint16_t)s0;
        s_scale_cols_512[x].s1 = (uint16_t)s1;
        s_scale_cols_512[x].w1 = (uint16_t)w1;
        s_scale_cols_512[x].w0 = (uint16_t)w0;
    }

    for (int y = 0; y < dstH; y++) {
        int y_log = (dstH - 1) - y; // 180 vertical rotation
        double v = (y_log + 0.5) / (double)dstH;
        double texel = v * (double)srcH - 0.5;
        double texel_floor = floor(texel);
        double texel_fract = texel - texel_floor;
        double sharp_fract = (texel_fract - 0.5) * scale_y + 0.5;
        if (sharp_fract < 0.0) sharp_fract = 0.0;
        if (sharp_fract > 1.0) sharp_fract = 1.0;

        int s0 = (int)texel_floor;
        int s1 = s0 + 1;
        if (s0 < 0) s0 = 0;
        if (s1 >= srcH) s1 = srcH - 1;
        if (s0 >= srcH) s0 = srcH - 1;

        int w1 = (int)(sharp_fract * 256.0 + 0.5);
        if (w1 > 256) w1 = 256;
        if (w1 < 0) w1 = 0;
        int w0 = 256 - w1;

        if (w1 == 256) {
            s0 = s1;
            w1 = 0;
            w0 = 256;
        }

        s_scale_rows_512[y].s0 = (uint16_t)s0;
        s_scale_rows_512[y].s1 = (uint16_t)s1;
        s_scale_rows_512[y].w1 = (uint16_t)w1;
        s_scale_rows_512[y].w0 = (uint16_t)w0;
    }

    s_scale_tables_512_initialized = true;
}

static void mmp_scale_init_tables_426(void) {
    if (s_scale_tables_426_initialized) return;

    const int srcW = 426;
    const int srcH = 320;
    const int dstW = SCREEN_WIDTH;  // 640
    const int dstH = SCREEN_HEIGHT; // 480
    const double scale_x = (double)dstW / (double)srcW;
    const double scale_y = (double)dstH / (double)srcH;

    // Sharp Bilinear: Preserves 100% solid square pixel centers and only applies
    // anti-aliased interpolation over a 1-pixel boundary zone, eliminating blur without shimmering.
    for (int x = 0; x < dstW; x++) {
        int x_log = (dstW - 1) - x; // 180 horizontal rotation
        double u = (x_log + 0.5) / (double)dstW;
        double texel = u * (double)srcW - 0.5;
        double texel_floor = floor(texel);
        double texel_fract = texel - texel_floor;
        double sharp_fract = (texel_fract - 0.5) * scale_x + 0.5;
        if (sharp_fract < 0.0) sharp_fract = 0.0;
        if (sharp_fract > 1.0) sharp_fract = 1.0;

        int s0 = (int)texel_floor;
        int s1 = s0 + 1;
        if (s0 < 0) s0 = 0;
        if (s1 >= srcW) s1 = srcW - 1;
        if (s0 >= srcW) s0 = srcW - 1;

        int w1 = (int)(sharp_fract * 256.0 + 0.5);
        if (w1 > 256) w1 = 256;
        if (w1 < 0) w1 = 0;
        int w0 = 256 - w1;

        if (w1 == 256) {
            s0 = s1;
            w1 = 0;
            w0 = 256;
        }

        s_scale_cols_426[x].s0 = (uint16_t)s0;
        s_scale_cols_426[x].s1 = (uint16_t)s1;
        s_scale_cols_426[x].w1 = (uint16_t)w1;
        s_scale_cols_426[x].w0 = (uint16_t)w0;
    }

    for (int y = 0; y < dstH; y++) {
        int y_log = (dstH - 1) - y; // 180 vertical rotation
        double v = (y_log + 0.5) / (double)dstH;
        double texel = v * (double)srcH - 0.5;
        double texel_floor = floor(texel);
        double texel_fract = texel - texel_floor;
        double sharp_fract = (texel_fract - 0.5) * scale_y + 0.5;
        if (sharp_fract < 0.0) sharp_fract = 0.0;
        if (sharp_fract > 1.0) sharp_fract = 1.0;

        int s0 = (int)texel_floor;
        int s1 = s0 + 1;
        if (s0 < 0) s0 = 0;
        if (s1 >= srcH) s1 = srcH - 1;
        if (s0 >= srcH) s0 = srcH - 1;

        int w1 = (int)(sharp_fract * 256.0 + 0.5);
        if (w1 > 256) w1 = 256;
        if (w1 < 0) w1 = 0;
        int w0 = 256 - w1;

        if (w1 == 256) {
            s0 = s1;
            w1 = 0;
            w0 = 256;
        }

        s_scale_rows_426[y].s0 = (uint16_t)s0;
        s_scale_rows_426[y].s1 = (uint16_t)s1;
        s_scale_rows_426[y].w1 = (uint16_t)w1;
        s_scale_rows_426[y].w0 = (uint16_t)w0;
    }

    s_scale_tables_426_initialized = true;
}

static void mmp_video_flip_180_scale_512_to_640(const uint16_t *src_buffer) {
    if (!src_buffer || !s_fb_ptr) return;
    mmp_scale_init_tables_512();

    const int srcW = 512;
    const int dstW = SCREEN_WIDTH;  // 640
    const int dstH = SCREEN_HEIGHT; // 480
    uint16_t v_row[512];

    if (s_vinfo.bits_per_pixel == 32) {
        for (int y = 0; y < dstH; y++) {
            uint32_t *dst_row = (uint32_t *)((uint8_t *)s_fb_ptr + y * s_line_length);
            const ScaleMap *ry = &s_scale_rows_512[y];
            const uint16_t *src0 = src_buffer + ry->s0 * srcW;
            const uint16_t *src1 = src_buffer + ry->s1 * srcW;
            const uint16_t *v_ptr;

            if (ry->w1 == 0 || ry->s0 == ry->s1) {
                v_ptr = src0;
            } else {
                uint32_t rw1 = ry->w1;
                uint32_t rw0 = ry->w0;
                for (int i = 0; i < srcW; i++) {
                    uint32_t c0 = src0[i];
                    uint32_t c1 = src1[i];
                    uint32_t r = (((c0 >> 11) & 0x1F) * rw0 + ((c1 >> 11) & 0x1F) * rw1) >> 8;
                    uint32_t g = (((c0 >> 5) & 0x3F) * rw0 + ((c1 >> 5) & 0x3F) * rw1) >> 8;
                    uint32_t b = ((c0 & 0x1F) * rw0 + (c1 & 0x1F) * rw1) >> 8;
                    v_row[i] = (uint16_t)((r << 11) | (g << 5) | b);
                }
                v_ptr = v_row;
            }

            for (int x = 0; x < dstW; x++) {
                const ScaleMap *cx = &s_scale_cols_512[x];
                uint16_t px;
                if (cx->w1 == 0 || cx->s0 == cx->s1) {
                    px = v_ptr[cx->s0];
                } else {
                    uint32_t c0 = v_ptr[cx->s0];
                    uint32_t c1 = v_ptr[cx->s1];
                    uint32_t r = (((c0 >> 11) & 0x1F) * cx->w0 + ((c1 >> 11) & 0x1F) * cx->w1) >> 8;
                    uint32_t g = (((c0 >> 5) & 0x3F) * cx->w0 + ((c1 >> 5) & 0x3F) * cx->w1) >> 8;
                    uint32_t b = ((c0 & 0x1F) * cx->w0 + (c1 & 0x1F) * cx->w1) >> 8;
                    px = (uint16_t)((r << 11) | (g << 5) | b);
                }
                dst_row[x] = mmp_rgb565_to_xrgb32(px);
            }
        }
    } else {
        // 16-bit Framebuffer fallback
        for (int y = 0; y < dstH; y++) {
            uint16_t *dst_row = (uint16_t *)((uint8_t *)s_fb_ptr + y * s_line_length);
            const ScaleMap *ry = &s_scale_rows_512[y];
            const uint16_t *src0 = src_buffer + ry->s0 * srcW;
            const uint16_t *src1 = src_buffer + ry->s1 * srcW;
            const uint16_t *v_ptr;

            if (ry->w1 == 0 || ry->s0 == ry->s1) {
                v_ptr = src0;
            } else {
                uint32_t rw1 = ry->w1;
                uint32_t rw0 = ry->w0;
                for (int i = 0; i < srcW; i++) {
                    uint32_t c0 = src0[i];
                    uint32_t c1 = src1[i];
                    uint32_t r = (((c0 >> 11) & 0x1F) * rw0 + ((c1 >> 11) & 0x1F) * rw1) >> 8;
                    uint32_t g = (((c0 >> 5) & 0x3F) * rw0 + ((c1 >> 5) & 0x3F) * rw1) >> 8;
                    uint32_t b = ((c0 & 0x1F) * rw0 + (c1 & 0x1F) * rw1) >> 8;
                    v_row[i] = (uint16_t)((r << 11) | (g << 5) | b);
                }
                v_ptr = v_row;
            }

            for (int x = 0; x < dstW; x++) {
                const ScaleMap *cx = &s_scale_cols_512[x];
                uint16_t px;
                if (cx->w1 == 0 || cx->s0 == cx->s1) {
                    px = v_ptr[cx->s0];
                } else {
                    uint32_t c0 = v_ptr[cx->s0];
                    uint32_t c1 = v_ptr[cx->s1];
                    uint32_t r = (((c0 >> 11) & 0x1F) * cx->w0 + ((c1 >> 11) & 0x1F) * cx->w1) >> 8;
                    uint32_t g = (((c0 >> 5) & 0x3F) * cx->w0 + ((c1 >> 5) & 0x3F) * cx->w1) >> 8;
                    uint32_t b = ((c0 & 0x1F) * cx->w0 + (c1 & 0x1F) * cx->w1) >> 8;
                    px = (uint16_t)((r << 11) | (g << 5) | b);
                }
                dst_row[x] = px;
            }
        }
    }
}

static void mmp_video_flip_180_scale_426_to_640(const uint16_t *src_buffer) {
    if (!src_buffer || !s_fb_ptr) return;
    mmp_scale_init_tables_426();

    const int srcW = 426;
    const int dstW = SCREEN_WIDTH;  // 640
    const int dstH = SCREEN_HEIGHT; // 480
    uint16_t v_row[426];

    if (s_vinfo.bits_per_pixel == 32) {
        for (int y = 0; y < dstH; y++) {
            uint32_t *dst_row = (uint32_t *)((uint8_t *)s_fb_ptr + y * s_line_length);
            const ScaleMap *ry = &s_scale_rows_426[y];
            const uint16_t *src0 = src_buffer + ry->s0 * srcW;
            const uint16_t *src1 = src_buffer + ry->s1 * srcW;
            const uint16_t *v_ptr;

            if (ry->w1 == 0 || ry->s0 == ry->s1) {
                v_ptr = src0;
            } else {
                uint32_t rw1 = ry->w1;
                uint32_t rw0 = ry->w0;
                for (int i = 0; i < srcW; i++) {
                    uint32_t c0 = src0[i];
                    uint32_t c1 = src1[i];
                    uint32_t r = (((c0 >> 11) & 0x1F) * rw0 + ((c1 >> 11) & 0x1F) * rw1) >> 8;
                    uint32_t g = (((c0 >> 5) & 0x3F) * rw0 + ((c1 >> 5) & 0x3F) * rw1) >> 8;
                    uint32_t b = ((c0 & 0x1F) * rw0 + (c1 & 0x1F) * rw1) >> 8;
                    v_row[i] = (uint16_t)((r << 11) | (g << 5) | b);
                }
                v_ptr = v_row;
            }

            for (int x = 0; x < dstW; x++) {
                const ScaleMap *cx = &s_scale_cols_426[x];
                uint16_t px;
                if (cx->w1 == 0 || cx->s0 == cx->s1) {
                    px = v_ptr[cx->s0];
                } else {
                    uint32_t c0 = v_ptr[cx->s0];
                    uint32_t c1 = v_ptr[cx->s1];
                    uint32_t r = (((c0 >> 11) & 0x1F) * cx->w0 + ((c1 >> 11) & 0x1F) * cx->w1) >> 8;
                    uint32_t g = (((c0 >> 5) & 0x3F) * cx->w0 + ((c1 >> 5) & 0x3F) * cx->w1) >> 8;
                    uint32_t b = ((c0 & 0x1F) * cx->w0 + (c1 & 0x1F) * cx->w1) >> 8;
                    px = (uint16_t)((r << 11) | (g << 5) | b);
                }
                dst_row[x] = mmp_rgb565_to_xrgb32(px);
            }
        }
    } else {
        // 16-bit Framebuffer fallback
        for (int y = 0; y < dstH; y++) {
            uint16_t *dst_row = (uint16_t *)((uint8_t *)s_fb_ptr + y * s_line_length);
            const ScaleMap *ry = &s_scale_rows_426[y];
            const uint16_t *src0 = src_buffer + ry->s0 * srcW;
            const uint16_t *src1 = src_buffer + ry->s1 * srcW;
            const uint16_t *v_ptr;

            if (ry->w1 == 0 || ry->s0 == ry->s1) {
                v_ptr = src0;
            } else {
                uint32_t rw1 = ry->w1;
                uint32_t rw0 = ry->w0;
                for (int i = 0; i < srcW; i++) {
                    uint32_t c0 = src0[i];
                    uint32_t c1 = src1[i];
                    uint32_t r = (((c0 >> 11) & 0x1F) * rw0 + ((c1 >> 11) & 0x1F) * rw1) >> 8;
                    uint32_t g = (((c0 >> 5) & 0x3F) * rw0 + ((c1 >> 5) & 0x3F) * rw1) >> 8;
                    uint32_t b = ((c0 & 0x1F) * rw0 + (c1 & 0x1F) * rw1) >> 8;
                    v_row[i] = (uint16_t)((r << 11) | (g << 5) | b);
                }
                v_ptr = v_row;
            }

            for (int x = 0; x < dstW; x++) {
                const ScaleMap *cx = &s_scale_cols_426[x];
                uint16_t px;
                if (cx->w1 == 0 || cx->s0 == cx->s1) {
                    px = v_ptr[cx->s0];
                } else {
                    uint32_t c0 = v_ptr[cx->s0];
                    uint32_t c1 = v_ptr[cx->s1];
                    uint32_t r = (((c0 >> 11) & 0x1F) * cx->w0 + ((c1 >> 11) & 0x1F) * cx->w1) >> 8;
                    uint32_t g = (((c0 >> 5) & 0x3F) * cx->w0 + ((c1 >> 5) & 0x3F) * cx->w1) >> 8;
                    uint32_t b = ((c0 & 0x1F) * cx->w0 + (c1 & 0x1F) * cx->w1) >> 8;
                    px = (uint16_t)((r << 11) | (g << 5) | b);
                }
                dst_row[x] = px;
            }
        }
    }
}

// ===[ Core 1 Asynchronous Presentation Thread ]===

static pthread_t s_worker_thread;
static pthread_mutex_t s_flip_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_flip_cond_work = PTHREAD_COND_INITIALIZER;
static pthread_cond_t s_flip_cond_done = PTHREAD_COND_INITIALIZER;
static bool s_worker_running = false;
static bool s_worker_work_pending = false;
static const uint16_t *s_worker_buf = NULL;
static int s_worker_tw = SCREEN_WIDTH;

static void* mmp_flip_worker(void* arg) {
    (void)arg;
#if defined(__linux__) && defined(_GNU_SOURCE)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(1, &cpuset); // Bind worker to Core 1
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif

    pthread_mutex_lock(&s_flip_mutex);
    while (s_worker_running) {
        while (!s_worker_work_pending && s_worker_running) {
            pthread_cond_wait(&s_flip_cond_work, &s_flip_mutex);
        }
        if (!s_worker_running) break;

        const uint16_t *buf = s_worker_buf;
        int tw = s_worker_tw;
        pthread_mutex_unlock(&s_flip_mutex);

        if (buf) {
            if (tw == 512) {
                mmp_video_flip_180_scale_512_to_640(buf);
            } else if (tw == 426) {
                mmp_video_flip_180_scale_426_to_640(buf);
            } else {
                mmp_video_flip_180(buf, SCREEN_WIDTH, SCREEN_HEIGHT);
            }
        }

        pthread_mutex_lock(&s_flip_mutex);
        s_worker_work_pending = false;
        pthread_cond_signal(&s_flip_cond_done);
    }
    pthread_mutex_unlock(&s_flip_mutex);
    return NULL;
}


static void mmp_frame_pacing(uint64_t target_frame_time_ns) {
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

        if (remaining_ns > 12000000ULL) {
            struct timespec sleep_ts = { 0, (long)(remaining_ns - 12000000ULL) };
            nanosleep(&sleep_ts, NULL);
        }

        do {
#if defined(__arm__) || defined(__aarch64__)
            __asm__ volatile("yield" ::: "memory");
#endif
            clock_gettime(CLOCK_MONOTONIC, &now_ts);
            elapsed = (int64_t)(now_ts.tv_sec - last_ts.tv_sec) * 1000000000LL + (now_ts.tv_nsec - last_ts.tv_nsec);
        } while (elapsed < (int64_t)target_frame_time_ns);
    }

    last_ts.tv_nsec += (long)(target_frame_time_ns % 1000000000ULL);
    last_ts.tv_sec += (time_t)(target_frame_time_ns / 1000000000ULL);
    if (last_ts.tv_nsec >= 1000000000L) {
        last_ts.tv_sec++;
        last_ts.tv_nsec -= 1000000000L;
    }

    int64_t lag = (int64_t)(now_ts.tv_sec - last_ts.tv_sec) * 1000000000LL + (now_ts.tv_nsec - last_ts.tv_nsec);
    if (lag > (int64_t)target_frame_time_ns) {
        last_ts = now_ts;
    }
}

static int mmp_input_init(void) {
    memset(&s_curr_state, 0, sizeof(s_curr_state));
    memset(&s_prev_state, 0, sizeof(s_prev_state));
    s_num_fds = 0;

    for (int i = 0; i < MAX_INPUT_DEVS; i++) {
        char dev_path[32];
        snprintf(dev_path, sizeof(dev_path), "/dev/input/event%d", i);
        int fd = open(dev_path, O_RDONLY | O_NONBLOCK);
        if (fd >= 0) {
            s_input_fds[s_num_fds++] = fd;
            logInfo("Miyoo Input: Opened %s\n", dev_path);
        }
    }

    return s_num_fds > 0 ? 0 : -1;
}

static void mmp_input_close(void) {
    for (int i = 0; i < s_num_fds; i++) {
        if (s_input_fds[i] >= 0) {
            close(s_input_fds[i]);
            s_input_fds[i] = -1;
        }
    }
    s_num_fds = 0;
}

// ===[ Model-Aware CPU Frequency Guard Thread ]===

static pthread_t s_clock_thread;
static volatile bool s_clock_running = false;
static int s_target_cpu_mhz = 1400; // Safe default for Miyoo Mini classic

static void mmp_apply_cpu_clock(int freq_mhz) {
    if (freq_mhz < 500 || freq_mhz > 2000) return;

    // Apply directly via sysfs to avoid any fork()/exec() overhead
    int fd = open("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", O_WRONLY);
    if (fd >= 0) {
        write(fd, "userspace", 9);
        close(fd);
    }
    fd = open("/sys/devices/system/cpu/cpu0/cpufreq/scaling_setspeed", O_WRONLY);
    if (fd >= 0) {
        char str[16];
        snprintf(str, sizeof(str), "%d000", freq_mhz);
        write(fd, str, strlen(str));
        close(fd);
    }

    fd = open("/sys/devices/system/cpu/cpu1/online", O_WRONLY);
    if (fd >= 0) {
        write(fd, "1", 1);
        close(fd);
    }
}

static void mmp_init_target_clock(void) {
    // 1. Check environment variable from launch.sh
    const char *env_mhz = getenv("TARGET_CPU_MHZ");
    if (env_mhz && atoi(env_mhz) >= 1000) {
        s_target_cpu_mhz = atoi(env_mhz);
        logInfo("[ClockGuard] Target clock configured via env: %d MHz\n", s_target_cpu_mhz);
        return;
    }

    // 2. Hardware model detection: 354 = Plus (1800 MHz), 283 = Classic (1400 MHz)
    FILE *fp = fopen("/tmp/deviceModel", "r");
    if (fp) {
        int model = 0;
        if (fscanf(fp, "%d", &model) == 1) {
            if (model == 354) s_target_cpu_mhz = 1800;
            else if (model == 283) s_target_cpu_mhz = 1400;
        }
        fclose(fp);
        logInfo("[ClockGuard] Target clock from /tmp/deviceModel: %d MHz\n", s_target_cpu_mhz);
        return;
    }

    // 3. Fallback based on factory NAND binary presence
    if (access("/customer/app/axp_test", F_OK) == 0) {
        s_target_cpu_mhz = 1800; // Miyoo Mini Plus
    } else {
        s_target_cpu_mhz = 1400; // Miyoo Mini classic safe threshold
    }
    logInfo("[ClockGuard] Target clock fallback: %d MHz\n", s_target_cpu_mhz);
}

static void* mmp_clock_guard_worker(void* arg) {
    (void)arg;
    struct timespec ts_prev;
    clock_gettime(CLOCK_MONOTONIC, &ts_prev);

    while (s_clock_running) {
        usleep(500000); // Poll every 500ms
        if (!s_clock_running) break;

        struct timespec ts_now;
        clock_gettime(CLOCK_MONOTONIC, &ts_now);
        int64_t diff_ms = (int64_t)(ts_now.tv_sec - ts_prev.tv_sec) * 1000 +
                          (ts_now.tv_nsec - ts_prev.tv_nsec) / 1000000;
        ts_prev = ts_now;

        // Detect resume from sleep (monotonic gap > 1200ms)
        bool woke_up = (diff_ms > 1200);
        if (woke_up) {
            mmp_apply_cpu_clock(s_target_cpu_mhz);
        }
    }
    return NULL;
}

// ===[ Butterscotch Platform API ]===

bool platformInit(int32_t reqW, int32_t reqH, const char *title, bool headless) {
    (void)reqW;
    (void)reqH;
    (void)title;
    (void)headless;

    logInfo("Miyoo platform backend initializing (%dx%d)...\n", SCREEN_WIDTH, SCREEN_HEIGHT);
    mmp_fb_init("/dev/fb0");
    mmp_input_init();

    s_worker_running = true;
    s_worker_work_pending = false;
    pthread_create(&s_worker_thread, NULL, mmp_flip_worker, NULL);

    mmp_init_target_clock();
    s_clock_running = true;
    pthread_create(&s_clock_thread, NULL, mmp_clock_guard_worker, NULL);

    g_initialized = true;
    return true;
}

void platformExit(void) {
    if (s_clock_running) {
        s_clock_running = false;
        pthread_join(s_clock_thread, NULL);
    }
    if (s_worker_running) {
        pthread_mutex_lock(&s_flip_mutex);
        s_worker_running = false;
        pthread_cond_signal(&s_flip_cond_work);
        pthread_mutex_unlock(&s_flip_mutex);
        pthread_join(s_worker_thread, NULL);
    }
    mmp_input_close();
    mmp_fb_close();
    g_initialized = false;
    logInfo("Miyoo platform backend shut down cleanly.\n");
}

bool platformGetWindowSize(int32_t *outW, int32_t *outH) {
    if (!outW || !outH) return false;
    *outW = SCREEN_WIDTH;
    *outH = SCREEN_HEIGHT;
    return true;
}

bool platformGetScaledWindowSize(int32_t *outW, int32_t *outH) {
    return platformGetWindowSize(outW, outH);
}

void platformSetWindowSize(int32_t width, int32_t height) {
    (void)width;
    (void)height;
}

void platformInitFunctions(Runner *runner) {
    g_runner = runner;
    runner->setCursor = NULL;
    runner->currentCursor = GML_CR_DEFAULT;
}

void platformSetWindowTitle(const char *title) {
    (void)title;
}

void platformGetMousePos(double *xPos, double *yPos) {
    if (xPos) *xPos = s_mouse_x;
    if (yPos) *yPos = s_mouse_y;
}

void platformSwapBuffers(void) {
    if (!g_runner || !g_runner->renderer) return;
    uint16_t *fb = SWRenderer_getFramebuffer(g_runner->renderer);
    if (!fb) return;

    int tw = SWRenderer_getTargetWidth(g_runner->renderer);

    pthread_mutex_lock(&s_flip_mutex);
    while (s_worker_work_pending) {
        pthread_cond_wait(&s_flip_cond_done, &s_flip_mutex);
    }

    s_worker_buf = fb;
    s_worker_tw = tw;
    s_worker_work_pending = true;

    // Flip SWRenderer backbuffer for zero-copy next frame on Core 0
    SWRenderer_flipBackbuffer(g_runner->renderer);

    pthread_cond_signal(&s_flip_cond_work);
    pthread_mutex_unlock(&s_flip_mutex);
}

void *platformGetProcAddress(const char *name) {
    (void)name;
    return NULL;
}

#define MAP_KEY(state_var, prev_var, gml_key) do { \
    if ((state_var) && !(prev_var)) { \
        RunnerKeyboard_onKeyDown(g_runner->keyboard, (gml_key)); \
    } else if (!(state_var) && (prev_var)) { \
        RunnerKeyboard_onKeyUp(g_runner->keyboard, (gml_key)); \
    } \
} while(0)

bool platformHandleEvents(void) {
    bool exit_requested = false;
    struct input_event ev;

    for (int i = 0; i < s_num_fds; i++) {
        while (read(s_input_fds[i], &ev, sizeof(ev)) == sizeof(ev)) {
            if (ev.type != EV_KEY) continue;
            bool pressed = (ev.value != 0);

            switch (ev.code) {
                // Physical MENU / Home key
                case KEY_HOME:       // 102
                case KEY_RIGHTALT:   // 100
                case KEY_ESC:        // 1
                    s_curr_state.menu = pressed;
                    if (pressed) exit_requested = true;
                    break;

                // D-Pad
                case KEY_UP:
                    s_curr_state.up = pressed;
                    break;
                case KEY_DOWN:
                    s_curr_state.down = pressed;
                    break;
                case KEY_LEFT:
                    s_curr_state.left = pressed;
                    break;
                case KEY_RIGHT:
                    s_curr_state.right = pressed;
                    break;

                // Action buttons (Miyoo Mini event0: A=KEY_SPACE, B=KEY_LEFTCTRL, Y=KEY_LEFTALT, X=KEY_LEFTSHIFT)
                case KEY_SPACE:
                    s_curr_state.a = pressed;
                    break;
                case KEY_LEFTCTRL:
                    s_curr_state.b = pressed;
                    break;
                case KEY_LEFTSHIFT:
                    s_curr_state.x = pressed;
                    break;
                case KEY_LEFTALT:
                case KEY_Z:
                case KEY_Y:
                    s_curr_state.y = pressed;
                    break;

                // Shoulders
                case KEY_E:
                    s_curr_state.l1 = pressed;
                    break;
                case KEY_T:
                    s_curr_state.r1 = pressed;
                    break;
                case KEY_TAB:
                    s_curr_state.l2 = pressed;
                    break;
                case KEY_BACKSPACE:
                    s_curr_state.r2 = pressed;
                    break;

                // Start & Select
                case KEY_ENTER:
                    s_curr_state.start = pressed;
                    break;
                case KEY_RIGHTCTRL:
                    s_curr_state.select = pressed;
                    break;

                default:
                    break;
            }

            if (s_curr_state.start && s_curr_state.select) {
                exit_requested = true;
            }
        }
    }

    if (exit_requested) {
        logInfo("Miyoo: Exit requested by user (MENU or START+SELECT).\n");
        return true;
    }

    if (g_runner && g_runner->gamepads && g_runner->keyboard) {
        // Room transition tracking: automatically park cursor on entering gameplay
        static int32_t s_last_room = -1;
        if (g_runner->currentRoomIndex != s_last_room) {
            s_last_room = g_runner->currentRoomIndex;
            if (is_gameplay_room(g_runner)) {
                s_mouse_x = -100.0;
                s_mouse_y = -100.0;
                s_was_combat = true;
            } else {
                s_mouse_x = 320.0;
                s_mouse_y = 240.0;
                s_was_combat = false;
            }
        }

        bool active_combat = is_in_active_combat(g_runner);

        // Transition detection between active combat and UI / menu / pause / death
        if (active_combat && !s_was_combat) {
            s_mouse_x = -100.0;
            s_mouse_y = -100.0;
            if (g_runner->mouse) {
                RunnerMouse_onButtonUp(g_runner->mouse, GML_MB_LEFT);
                RunnerMouse_onButtonUp(g_runner->mouse, GML_MB_RIGHT);
            }
        } else if (!active_combat && s_was_combat) {
            // When pausing, dying, or returning to menu: center the mouse cursor!
            s_mouse_x = 320.0;
            s_mouse_y = 240.0;
        }
        s_was_combat = active_combat;

        bool allow_mouse = !active_combat || s_curr_state.select;

        // 1. Update Gamepad slot 0
        GamepadSlot *slot = &g_runner->gamepads->slots[0];
        slot->connected = true;
        slot->jid = 0;
        strncpy(slot->description, "Miyoo Mini Controls", sizeof(slot->description) - 1);

        memcpy(slot->buttonDownPrev, slot->buttonDown, sizeof(slot->buttonDownPrev));
        memset(slot->buttonDown, 0, sizeof(slot->buttonDown));
        memset(slot->buttonPressed, 0, sizeof(slot->buttonPressed));
        memset(slot->buttonReleased, 0, sizeof(slot->buttonReleased));

        slot->buttonDown[0] = s_curr_state.a;                           // A (Enter, gp_face1)
        slot->buttonDown[1] = s_curr_state.b;                           // B (Jump, gp_face2)
        slot->buttonDown[2] = s_curr_state.l2;                          // L2 (Swap Item, gp_face3)
        slot->buttonDown[3] = s_curr_state.y;                           // Y (Ability 1 / basic, gp_face4)
        slot->buttonDown[4] = s_curr_state.l1;                          // L1 (Ability 2, gp_shoulderl)
        slot->buttonDown[5] = s_curr_state.r2;                          // R2 (Ability 3, gp_shoulderr)
        slot->buttonDown[6] = s_curr_state.x;                           // X (Use Equipment Item, gp_shoulderlb)
        slot->buttonDown[7] = s_curr_state.r1;                          // R1 (Ability 4, gp_shoulderrb)
        slot->buttonDown[8] = s_curr_state.select;                      // SELECT
        slot->buttonDown[9] = s_curr_state.start;                       // START
        slot->buttonDown[12] = s_curr_state.up;                         // UP
        slot->buttonDown[13] = s_curr_state.down;                       // DOWN
        slot->buttonDown[14] = s_curr_state.left;                       // LEFT
        slot->buttonDown[15] = s_curr_state.right;                      // RIGHT

        // Zero analog sticks to prevent GameMaker internal scripts from moving the cursor
        slot->axisValue[0] = 0.0f;
        slot->axisValue[1] = 0.0f;

        for (int btn = 0; btn < GP_BUTTON_COUNT; btn++) {
            slot->buttonValue[btn] = slot->buttonDown[btn] ? 1.0f : 0.0f;
            bool wasDown = slot->buttonDownPrev[btn];
            if (slot->buttonDown[btn] && !wasDown) slot->buttonPressed[btn] = true;
            if (!slot->buttonDown[btn] && wasDown) slot->buttonReleased[btn] = true;
        }
        g_runner->gamepads->connectedCount = 1;

        // 2. Mirror to Keyboard for native GameMaker key mappings
        // When inspecting in gameplay (SELECT held), do not send D-pad arrows to player movement
        if (!(active_combat && s_curr_state.select)) {
            MAP_KEY(s_curr_state.up, s_prev_state.up, VK_UP);
            MAP_KEY(s_curr_state.down, s_prev_state.down, VK_DOWN);
            MAP_KEY(s_curr_state.left, s_prev_state.left, VK_LEFT);
            MAP_KEY(s_curr_state.right, s_prev_state.right, VK_RIGHT);
        } else {
            if (s_prev_state.up)    RunnerKeyboard_onKeyUp(g_runner->keyboard, VK_UP);
            if (s_prev_state.down)  RunnerKeyboard_onKeyUp(g_runner->keyboard, VK_DOWN);
            if (s_prev_state.left)  RunnerKeyboard_onKeyUp(g_runner->keyboard, VK_LEFT);
            if (s_prev_state.right) RunnerKeyboard_onKeyUp(g_runner->keyboard, VK_RIGHT);
        }

        MAP_KEY(s_curr_state.b, s_prev_state.b, VK_SPACE);                             // Jump (B)
        MAP_KEY(s_curr_state.y, s_prev_state.y, 'Z');                                  // Ability 1 / basic (Y)
        MAP_KEY(s_curr_state.l1, s_prev_state.l1, 'X');                                // Ability 2 (L1)
        MAP_KEY(s_curr_state.r1, s_prev_state.r1, 'V');                                // Ability 4 (R1)
        MAP_KEY(s_curr_state.r2, s_prev_state.r2, 'C');                                // Ability 3 (R2)
        MAP_KEY(s_curr_state.l2, s_prev_state.l2, 'Q');                                // Swap Item (L2)
        MAP_KEY(s_curr_state.x, s_prev_state.x, 'G');                                  // Use Equipment Item (X)
        MAP_KEY(s_curr_state.a, s_prev_state.a, 'A');                                  // Enter / Confirm (A)
        if (!is_gameplay_room(g_runner)) {
            MAP_KEY(s_curr_state.start, s_prev_state.start, VK_ENTER);  // Select/Confirm in outer menus
        }

        // 3. Virtual Mouse Navigation (Only in menus OR when holding SELECT in gameplay)
        if (allow_mouse) {
            if (s_mouse_x < 0.0 || s_mouse_y < 0.0) {
                s_mouse_x = 320.0;
                s_mouse_y = 240.0;
            }
            const double mouse_speed = 6.0;
            if (s_curr_state.up)    s_mouse_y -= mouse_speed;
            if (s_curr_state.down)  s_mouse_y += mouse_speed;
            if (s_curr_state.left)  s_mouse_x -= mouse_speed;
            if (s_curr_state.right) s_mouse_x += mouse_speed;

            if (s_mouse_x < 0.0) s_mouse_x = 0.0;
            if (s_mouse_x > (SCREEN_WIDTH - 1)) s_mouse_x = SCREEN_WIDTH - 1;
            if (s_mouse_y < 0.0) s_mouse_y = 0.0;
            if (s_mouse_y > (SCREEN_HEIGHT - 1)) s_mouse_y = SCREEN_HEIGHT - 1;

            if (g_runner->mouse) {
                if (s_curr_state.a && !s_prev_state.a) {
                    RunnerMouse_onButtonDown(g_runner->mouse, GML_MB_LEFT);
                } else if (!s_curr_state.a && s_prev_state.a) {
                    RunnerMouse_onButtonUp(g_runner->mouse, GML_MB_LEFT);
                }
                if (s_curr_state.b && !s_prev_state.b) {
                    RunnerMouse_onButtonDown(g_runner->mouse, GML_MB_RIGHT);
                } else if (!s_curr_state.b && s_prev_state.b) {
                    RunnerMouse_onButtonUp(g_runner->mouse, GML_MB_RIGHT);
                }
            }
        } else {
            // Keep cursor parked off-screen during gameplay
            s_mouse_x = -100.0;
            s_mouse_y = -100.0;
            if (g_runner->mouse) {
                if (s_prev_state.a) RunnerMouse_onButtonUp(g_runner->mouse, GML_MB_LEFT);
                if (s_prev_state.b) RunnerMouse_onButtonUp(g_runner->mouse, GML_MB_RIGHT);
            }
        }

        s_prev_state = s_curr_state;
    }

    return false;
}

void platformSleepUntil(uint64_t time) {
    (void)time;
    // Paced at strict 60 FPS (16,666,666 ns)
    mmp_frame_pacing(16666666ULL);
}
