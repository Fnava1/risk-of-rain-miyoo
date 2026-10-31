#include "sw_renderer.h"
#include "common.h"
#include "renderer.h"
#include "runner.h"
#include "data_win.h"
#include "image/image_decoder.h"
#include "text_utils.h"
#include "utils.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 480
#define COMBAT_OFFSET_Y 60
#define COMBAT_HEIGHT 360

// ===[ RGB565 and Color Helpers ]===

static inline uint16_t make_rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static inline uint16_t bgr_to_rgb565(uint32_t bgr) {
    uint8_t r = bgr & 0xFF;
    uint8_t g = (bgr >> 8) & 0xFF;
    uint8_t b = (bgr >> 16) & 0xFF;
    return make_rgb565(r, g, b);
}

static inline uint16_t tint_rgb565(uint16_t pixel, uint8_t tr, uint8_t tg, uint8_t tb) {
    if (tr == 255 && tg == 255 && tb == 255) return pixel;
    uint32_t r = (pixel >> 11) & 0x1F;
    uint32_t g = (pixel >> 5) & 0x3F;
    uint32_t b = pixel & 0x1F;
    r = (r * (tr + 1)) >> 8;
    g = (g * (tg + 1)) >> 8;
    b = (b * (tb + 1)) >> 8;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static inline uint16_t blend_pixel_rgb565(uint16_t d, uint16_t s, uint8_t alpha) {
    if (alpha == 255) return s;
    if (alpha == 0) return d;

    uint32_t a = (uint32_t)alpha + 1;
    uint32_t inv_a = 256 - a;

    uint32_t s_r = (s >> 11) & 0x1F;
    uint32_t s_g = (s >> 5) & 0x3F;
    uint32_t s_b = s & 0x1F;

    uint32_t d_r = (d >> 11) & 0x1F;
    uint32_t d_g = (d >> 5) & 0x3F;
    uint32_t d_b = d & 0x1F;

    uint32_t r = (s_r * a + d_r * inv_a) >> 8;
    uint32_t g = (s_g * a + d_g * inv_a) >> 8;
    uint32_t b = (s_b * a + d_b * inv_a) >> 8;

    return (uint16_t)((r << 11) | (g << 5) | b);
}

static inline uint16_t blend_pixel_rgb565_add(uint16_t d, uint16_t s, uint8_t alpha) {
    if (alpha == 0) return d;
    uint32_t a = (uint32_t)alpha + 1;
    uint32_t s_r = (((s >> 11) & 0x1F) * a) >> 8;
    uint32_t s_g = (((s >> 5) & 0x3F) * a) >> 8;
    uint32_t s_b = ((s & 0x1F) * a) >> 8;

    uint32_t r = ((d >> 11) & 0x1F) + s_r;
    uint32_t g = ((d >> 5) & 0x3F) + s_g;
    uint32_t b = (d & 0x1F) + s_b;

    if (r > 31) r = 31;
    if (g > 63) g = 63;
    if (b > 31) b = 31;

    return (uint16_t)((r << 11) | (g << 5) | b);
}

static inline uint16_t blend_pixel_rgb565_mode(uint16_t d, uint16_t s, uint8_t alpha, int32_t mode) {
    if (mode == bm_add) return blend_pixel_rgb565_add(d, s, alpha);
    return blend_pixel_rgb565(d, s, alpha);
}

// ===[ Texture Cache Structs ]===

typedef struct {
    uint16_t *rgb;
    uint8_t  *alpha;
    int32_t  width;
    int32_t  height;
    bool     loaded;
} SWTexturePage;

typedef struct {
    uint16_t *pixels;
    int32_t  width;
    int32_t  height;
    bool     exists;
} SWSurface;

typedef struct {
    float x, y;
    float u, v;
    uint32_t color;
    float alpha;
} SWVertex;

// ===[ SWRenderer Struct ]===

typedef struct {
    Renderer base;

    uint16_t *framebuffer; // pointer to active backbuffer
    uint16_t *buffers[2];  // double-buffers for async Core 1 presentation
    int      activeBuffer;
    int32_t  fbWidth;
    int32_t  fbHeight;
    int32_t  fbCurrentWidth;
    int32_t  fbCurrentHeight;

    // Active Render Target
    uint16_t *currentTarget;
    int32_t  targetWidth;
    int32_t  targetHeight;
    int32_t  activeSurfaceId;

    // View & Camera State
    bool     inGUI;
    int32_t  guiW, guiH;
    int32_t  viewX, viewY, viewW, viewH;
    int32_t  portX, portY, portW, portH;
    int32_t  clipX0, clipY0, clipX1, clipY1;

    // Textures
    SWTexturePage *pages;
    uint32_t      pageCount;

    // Surfaces
    SWSurface *surfaces;
    uint32_t  surfaceCount;
    uint32_t  surfaceCapacity;

    // GPU emulation state
    bool blendEnable;
    int32_t blendMode;
    BlendFactors blendFactors;

    // Primitive Batching
    int32_t   primType;
    int32_t   primTexture;
    int32_t   primVertexCount;
    int32_t   primVertexCapacity;
    SWVertex *primVertices;
} SWRenderer;

static RendererVtable swVtable;

uint16_t* SWRenderer_getFramebuffer(Renderer* renderer) {
    if (!renderer) return NULL;
    SWRenderer *sw = (SWRenderer *)renderer;
    return sw->framebuffer;
}

int SWRenderer_getTargetWidth(Renderer* renderer) {
    if (!renderer) return SCREEN_WIDTH;
    SWRenderer *sw = (SWRenderer *)renderer;
    return (sw->targetWidth > 0) ? sw->targetWidth : SCREEN_WIDTH;
}

int SWRenderer_getTargetHeight(Renderer* renderer) {
    if (!renderer) return SCREEN_HEIGHT;
    SWRenderer *sw = (SWRenderer *)renderer;
    return (sw->targetHeight > 0) ? sw->targetHeight : SCREEN_HEIGHT;
}

void SWRenderer_flipBackbuffer(Renderer* renderer) {
    if (!renderer) return;
    SWRenderer *sw = (SWRenderer *)renderer;
    sw->activeBuffer ^= 1;
    sw->framebuffer = sw->buffers[sw->activeBuffer];
    if (sw->activeSurfaceId == RENDER_TARGET_HOST_FRAMEBUFFER || sw->activeSurfaceId == sw->base.runner->applicationSurfaceId) {
        sw->currentTarget = sw->framebuffer;
    }
}

void SWRenderer_clearFrameBuffer(Renderer* renderer, uint32_t color) {
    if (!renderer) return;
    SWRenderer *sw = (SWRenderer *)renderer;
    if (sw->framebuffer) {
        uint16_t c565 = bgr_to_rgb565(color);
        int total = (sw->targetWidth > 0 && sw->targetHeight > 0) ? (sw->targetWidth * sw->targetHeight) : (sw->fbWidth * sw->fbHeight);
        if (c565 == 0) {
            memset(sw->framebuffer, 0, (size_t)total * sizeof(uint16_t));
            return;
        }
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
        uint16x8_t v_c = vdupq_n_u16(c565);
        int i = 0;
        for (; i <= total - 8; i += 8) {
            vst1q_u16(sw->framebuffer + i, v_c);
        }
        for (; i < total; i++) {
            sw->framebuffer[i] = c565;
        }
#else
        uint32_t c32 = ((uint32_t)c565 << 16) | c565;
        uint32_t *dst32 = (uint32_t *)sw->framebuffer;
        int total32 = total / 2;
        for (int i = 0; i < total32; i++) dst32[i] = c32;
        if (total & 1) sw->framebuffer[total - 1] = c565;
#endif
    }
}

// ===[ Texture Loading ]===

static bool ensureTextureLoaded(SWRenderer *sw, uint32_t pageId) {
    if (pageId < sw->pageCount && sw->pages[pageId].loaded) return true;
    DataWin *dw = sw->base.dataWin;
    if (!dw || pageId >= dw->txtr.count) return false;

    DataWin_loadTxtrIfNeeded(dw, pageId);
    Texture *txtr = &dw->txtr.textures[pageId];
    if (!txtr || !txtr->blobData || txtr->blobSize == 0) return false;

    int w = 0, h = 0;
    uint8_t *rgba = ImageDecoder_decodeToRgba(txtr->blobData, txtr->blobSize, false, &w, &h);
    if (!rgba || w <= 0 || h <= 0) {
        logWarn("SWRenderer: Failed to decode TXTR page %u\n", pageId);
        return false;
    }

    if (pageId >= sw->pageCount) {
        uint32_t newCap = pageId + 1;
        sw->pages = (SWTexturePage *)safeRealloc(sw->pages, newCap * sizeof(SWTexturePage));
        for (uint32_t i = sw->pageCount; i < newCap; i++) {
            memset(&sw->pages[i], 0, sizeof(SWTexturePage));
        }
        sw->pageCount = newCap;
    }

    SWTexturePage *p = &sw->pages[pageId];
    p->width = w;
    p->height = h;
    p->rgb = (uint16_t *)safeMalloc((size_t)w * h * sizeof(uint16_t));
    p->alpha = (uint8_t *)safeMalloc((size_t)w * h * sizeof(uint8_t));

    int totalPixels = w * h;
    for (int i = 0; i < totalPixels; i++) {
        uint8_t r = rgba[i * 4 + 0];
        uint8_t g = rgba[i * 4 + 1];
        uint8_t b = rgba[i * 4 + 2];
        uint8_t a = rgba[i * 4 + 3];

        p->rgb[i] = make_rgb565(r, g, b);
        p->alpha[i] = a;
    }

    free(rgba);

    if (!txtr->mapped) {
        free(txtr->blobData);
        txtr->blobData = NULL;
    }

    p->loaded = true;
    logInfo("SWRenderer: Loaded TXTR page %u (%dx%d, %zu KB)\n", pageId, w, h, ((size_t)w * h * 3) / 1024);
    return true;
}

// ===[ Surface Capacity Helper ]===

static void ensureSurfaceCapacity(SWRenderer *sw, uint32_t needed) {
    if (needed <= sw->surfaceCapacity) return;
    uint32_t newCap = sw->surfaceCapacity ? sw->surfaceCapacity * 2 : 16;
    while (newCap < needed) newCap *= 2;
    sw->surfaces = (SWSurface *)safeRealloc(sw->surfaces, newCap * sizeof(SWSurface));
    for (uint32_t i = sw->surfaceCapacity; i < newCap; i++) {
        memset(&sw->surfaces[i], 0, sizeof(SWSurface));
    }
    sw->surfaceCapacity = newCap;
    if (needed > sw->surfaceCount) sw->surfaceCount = needed;
}

// ===[ Vtable Implementation ]===

static void swInit(Renderer *renderer, DataWin *dataWin) {
    SWRenderer *sw = (SWRenderer *)renderer;
    sw->base.dataWin = dataWin;
    sw->fbWidth = SCREEN_WIDTH;
    sw->fbHeight = SCREEN_HEIGHT;
    sw->fbCurrentWidth = SCREEN_WIDTH;
    sw->fbCurrentHeight = SCREEN_HEIGHT;
    sw->buffers[0] = (uint16_t *)safeCalloc(SCREEN_WIDTH * SCREEN_HEIGHT, sizeof(uint16_t));
    sw->buffers[1] = (uint16_t *)safeCalloc(SCREEN_WIDTH * SCREEN_HEIGHT, sizeof(uint16_t));
    sw->activeBuffer = 0;
    sw->framebuffer = sw->buffers[0];
    sw->currentTarget = sw->framebuffer;
    sw->targetWidth = SCREEN_WIDTH;
    sw->targetHeight = SCREEN_HEIGHT;
    sw->activeSurfaceId = RENDER_TARGET_HOST_FRAMEBUFFER;

    sw->clipX0 = 0;
    sw->clipY0 = 0;
    sw->clipX1 = SCREEN_WIDTH;
    sw->clipY1 = SCREEN_HEIGHT;

    sw->pageCount = dataWin ? dataWin->txtr.count : 0;
    if (sw->pageCount > 0) {
        sw->pages = (SWTexturePage *)safeCalloc(sw->pageCount, sizeof(SWTexturePage));
    }

    // Initialize primitive batch buffer
    sw->primType = 0;
    sw->primTexture = -1;
    sw->primVertexCount = 0;
    sw->primVertexCapacity = 512;
    sw->primVertices = (SWVertex *)safeCalloc(512, sizeof(SWVertex));

    Matrix4f world;
    Matrix4f_identity(&world);
    sw->base.gmlMatrices[MATRIX_WORLD] = world;
    logInfo("SWRenderer initialized (640x480 RGB565)\n");
}

static void swDestroy(Renderer *renderer) {
    SWRenderer *sw = (SWRenderer *)renderer;
    if (sw->primVertices) {
        free(sw->primVertices);
        sw->primVertices = NULL;
    }
    if (sw->buffers[0]) {
        free(sw->buffers[0]);
        sw->buffers[0] = NULL;
    }
    if (sw->buffers[1]) {
        free(sw->buffers[1]);
        sw->buffers[1] = NULL;
    }
    sw->framebuffer = NULL;
    if (sw->pages) {
        for (uint32_t i = 0; i < sw->pageCount; i++) {
            if (sw->pages[i].loaded) {
                free(sw->pages[i].rgb);
                free(sw->pages[i].alpha);
            }
        }
        free(sw->pages);
        sw->pages = NULL;
    }
    if (sw->surfaces) {
        for (uint32_t i = 0; i < sw->surfaceCount; i++) {
            if (sw->surfaces[i].exists && sw->surfaces[i].pixels) {
                free(sw->surfaces[i].pixels);
            }
        }
        free(sw->surfaces);
        sw->surfaces = NULL;
    }
    free(sw);
}

static void swBeginFrame(MAYBE_UNUSED Renderer *renderer, int32_t gameW, int32_t gameH, MAYBE_UNUSED int32_t windowW, MAYBE_UNUSED int32_t windowH) {
    SWRenderer *sw = (SWRenderer *)renderer;
    int tw = (gameW > 0) ? gameW : SCREEN_WIDTH;
    int th = (gameH > 0) ? gameH : SCREEN_HEIGHT;
    sw->fbCurrentWidth = tw;
    sw->fbCurrentHeight = th;
    sw->currentTarget = sw->framebuffer;
    sw->targetWidth = tw;
    sw->targetHeight = th;
    sw->activeSurfaceId = RENDER_TARGET_HOST_FRAMEBUFFER;
    sw->inGUI = false;
    sw->clipX0 = 0;
    sw->clipY0 = 0;
    sw->clipX1 = tw;
    sw->clipY1 = th;
}

static void swEndFrameInit(MAYBE_UNUSED Renderer *renderer) {}
static void swEndFrameEnd(MAYBE_UNUSED Renderer *renderer) {}


static inline void sw_transform_point(const SWRenderer *sw, float x, float y, float *outX, float *outY) {
    if (!sw->inGUI) {
        float scaleX = (sw->viewW > 0) ? ((float)sw->portW / (float)sw->viewW) : 1.0f;
        float scaleY = (sw->viewH > 0) ? ((float)sw->portH / (float)sw->viewH) : 1.0f;
        *outX = (float)sw->portX + (x - (float)sw->viewX) * scaleX;
        *outY = (float)sw->portY + (y - (float)sw->viewY) * scaleY;
    } else {
        float scaleX = (sw->guiW > 0) ? ((float)sw->portW / (float)sw->guiW) : 1.0f;
        float scaleY = (sw->guiH > 0) ? ((float)sw->portH / (float)sw->guiH) : 1.0f;
        *outX = (float)sw->portX + x * scaleX;
        *outY = (float)sw->portY + y * scaleY;
    }
}

static void swBeginView(Renderer *renderer, int32_t viewX, int32_t viewY, int32_t viewW, int32_t viewH, int32_t portX, int32_t portY, int32_t portW, int32_t portH, MAYBE_UNUSED float viewAngle) {
    SWRenderer *sw = (SWRenderer *)renderer;
    sw->inGUI = false;
    sw->viewX = viewX;
    sw->viewY = viewY;
    sw->viewW = (viewW > 0) ? viewW : sw->targetWidth;
    sw->viewH = (viewH > 0) ? viewH : sw->targetHeight;

    sw->portX = portX;
    sw->portY = portY;
    sw->portW = (portW > 0) ? portW : sw->targetWidth;
    sw->portH = (portH > 0) ? portH : sw->targetHeight;

    sw->clipX0 = sw->portX;
    sw->clipY0 = sw->portY;
    sw->clipX1 = sw->portX + sw->portW;
    sw->clipY1 = sw->portY + sw->portH;

    if (sw->clipX0 < 0) sw->clipX0 = 0;
    if (sw->clipY0 < 0) sw->clipY0 = 0;
    if (sw->clipX1 > sw->targetWidth) sw->clipX1 = sw->targetWidth;
    if (sw->clipY1 > sw->targetHeight) sw->clipY1 = sw->targetHeight;
}

static void swEndView(Renderer *renderer) {
    SWRenderer *sw = (SWRenderer *)renderer;
    sw->clipX0 = 0;
    sw->clipY0 = 0;
    sw->clipX1 = sw->targetWidth;
    sw->clipY1 = sw->targetHeight;
}

static void swApplyProjection(MAYBE_UNUSED Renderer *renderer, MAYBE_UNUSED const Matrix4f *viewMatrix, MAYBE_UNUSED const Matrix4f *projectionMatrix) {}

static void swBeginGUI(Renderer *renderer, int32_t guiW, int32_t guiH, int32_t portX, int32_t portY, int32_t portW, int32_t portH, MAYBE_UNUSED int32_t targetSurfaceId) {
    SWRenderer *sw = (SWRenderer *)renderer;
    sw->inGUI = true;
    sw->guiW = (guiW > 0) ? guiW : sw->targetWidth;
    sw->guiH = (guiH > 0) ? guiH : sw->targetHeight;
    sw->portX = portX;
    sw->portY = portY;
    sw->portW = (portW > 0) ? portW : sw->targetWidth;
    sw->portH = (portH > 0) ? portH : sw->targetHeight;

    sw->clipX0 = 0;
    sw->clipY0 = 0;
    sw->clipX1 = sw->targetWidth;
    sw->clipY1 = sw->targetHeight;
}

static void swSetGuiProjection(MAYBE_UNUSED Renderer *renderer, MAYBE_UNUSED int32_t guiW, MAYBE_UNUSED int32_t guiH, MAYBE_UNUSED int32_t portW, MAYBE_UNUSED int32_t portH, MAYBE_UNUSED bool renderingToUserSurface) {}

static void swEndGUI(Renderer *renderer) {
    SWRenderer *sw = (SWRenderer *)renderer;
    sw->inGUI = false;
}

// ===[ Core Blitter ]===

static void blitSubRect(SWRenderer *sw, uint32_t pageId,
                        int srcX, int srcY, int srcW, int srcH,
                        float dstX, float dstY, float xscale, float yscale,
                        uint32_t bgrColor, float globalAlpha) {
    if (!ensureTextureLoaded(sw, pageId) || srcW <= 0 || srcH <= 0) return;
    if (globalAlpha <= 0.0f) return;

    SWTexturePage *page = &sw->pages[pageId];
    uint8_t tr = bgrColor & 0xFF;
    uint8_t tg = (bgrColor >> 8) & 0xFF;
    uint8_t tb = (bgrColor >> 16) & 0xFF;
    bool needsTint = (tr != 255 || tg != 255 || tb != 255);

    // Coordinate translation: World vs GUI with camera projection & scale
    float scaleX, scaleY;
    float screenX, screenY;
    if (!sw->inGUI) {
        scaleX = (sw->viewW > 0) ? ((float)sw->portW / (float)sw->viewW) : 1.0f;
        scaleY = (sw->viewH > 0) ? ((float)sw->portH / (float)sw->viewH) : 1.0f;
        screenX = (float)sw->portX + (dstX - (float)sw->viewX) * scaleX;
        screenY = (float)sw->portY + (dstY - (float)sw->viewY) * scaleY;
    } else {
        scaleX = (sw->guiW > 0) ? ((float)sw->portW / (float)sw->guiW) : 1.0f;
        scaleY = (sw->guiH > 0) ? ((float)sw->portH / (float)sw->guiH) : 1.0f;
        screenX = (float)sw->portX + dstX * scaleX;
        screenY = (float)sw->portY + dstY * scaleY;
    }

    float effScaleX = xscale * scaleX;
    float effScaleY = yscale * scaleY;

    // 1:1 Fast path (unscaled, axis-aligned)
    if (fabsf(effScaleX - 1.0f) < 0.001f && fabsf(effScaleY - 1.0f) < 0.001f) {
        int dx = (int)roundf(screenX);
        int dy = (int)roundf(screenY);
        int sx = srcX;
        int sy = srcY;
        int w = srcW;
        int h = srcH;

        // Clip to current viewport
        if (dx < sw->clipX0) { int d = sw->clipX0 - dx; sx += d; w -= d; dx = sw->clipX0; }
        if (dy < sw->clipY0) { int d = sw->clipY0 - dy; sy += d; h -= d; dy = sw->clipY0; }
        if (dx + w > sw->clipX1) w = sw->clipX1 - dx;
        if (dy + h > sw->clipY1) h = sw->clipY1 - dy;

        if (w <= 0 || h <= 0) return;

        uint16_t *target = sw->currentTarget;
        int targetStride = sw->targetWidth;
        int pageStride = page->width;

        if (!needsTint && globalAlpha >= 0.999f && sw->blendMode == bm_normal) {
            for (int r = 0; r < h; r++) {
                uint16_t *d_ptr = target + (dy + r) * targetStride + dx;
                const uint16_t *s_rgb = page->rgb + (sy + r) * pageStride + sx;
                const uint8_t  *s_alpha = page->alpha + (sy + r) * pageStride + sx;

                int c = 0;
                // Fast path: 4 pixels at a time for contiguous opaque / transparent spans
                for (; c <= w - 4; c += 4) {
                    uint32_t a4;
                    memcpy(&a4, s_alpha + c, sizeof(uint32_t));
                    if (a4 == 0xFFFFFFFFU) {
                        // All 4 pixels 100% opaque: bulk copy 64 bits
                        memcpy(d_ptr + c, s_rgb + c, 4 * sizeof(uint16_t));
                        continue;
                    }
                    if (a4 == 0x00000000U) {
                        // All 4 pixels transparent: skip
                        continue;
                    }
                    for (int k = 0; k < 4; k++) {
                        uint8_t a = s_alpha[c + k];
                        if (a == 255) {
                            d_ptr[c + k] = s_rgb[c + k];
                        } else if (a != 0) {
                            d_ptr[c + k] = blend_pixel_rgb565(d_ptr[c + k], s_rgb[c + k], a);
                        }
                    }
                }
                for (; c < w; c++) {
                    uint8_t a = s_alpha[c];
                    if (a == 255) {
                        d_ptr[c] = s_rgb[c];
                    } else if (a != 0) {
                        d_ptr[c] = blend_pixel_rgb565(d_ptr[c], s_rgb[c], a);
                    }
                }
            }
        } else {
            uint32_t ga_int = (uint32_t)(globalAlpha * 255.0f);
            for (int r = 0; r < h; r++) {
                uint16_t *d_ptr = target + (dy + r) * targetStride + dx;
                const uint16_t *s_rgb = page->rgb + (sy + r) * pageStride + sx;
                const uint8_t  *s_alpha = page->alpha + (sy + r) * pageStride + sx;

                for (int c = 0; c < w; c++) {
                    uint8_t a = (uint8_t)((s_alpha[c] * ga_int) >> 8);
                    if (a == 0) continue;
                    uint16_t pix = needsTint ? tint_rgb565(s_rgb[c], tr, tg, tb) : s_rgb[c];
                    if (sw->blendMode == bm_add) {
                        d_ptr[c] = blend_pixel_rgb565_add(d_ptr[c], pix, a);
                    } else if (a == 255 && !needsTint && globalAlpha >= 0.999f) {
                        d_ptr[c] = pix;
                    } else {
                        d_ptr[c] = blend_pixel_rgb565(d_ptr[c], pix, a);
                    }
                }
            }
        }
        return;
    }

    // Scaled blit (nearest-neighbor)
    float drawW = (float)srcW * fabsf(effScaleX);
    float drawH = (float)srcH * fabsf(effScaleY);
    if (drawW < 1.0f || drawH < 1.0f) return;

    int dx0 = (int)floorf(screenX);
    int dy0 = (int)floorf(screenY);
    int dx1 = (int)ceilf(screenX + drawW);
    int dy1 = (int)ceilf(screenY + drawH);

    int clipDx0 = dx0 < sw->clipX0 ? sw->clipX0 : dx0;
    int clipDy0 = dy0 < sw->clipY0 ? sw->clipY0 : dy0;
    int clipDx1 = dx1 > sw->clipX1 ? sw->clipX1 : dx1;
    int clipDy1 = dy1 > sw->clipY1 ? sw->clipY1 : dy1;

    if (clipDx0 >= clipDx1 || clipDy0 >= clipDy1) return;

    uint16_t *target = sw->currentTarget;
    int targetStride = sw->targetWidth;
    int pageStride = page->width;
    bool flipX = (effScaleX < 0.0f);
    bool flipY = (effScaleY < 0.0f);

    float invScaleX = 1.0f / fabsf(effScaleX);
    float invScaleY = 1.0f / fabsf(effScaleY);

    uint32_t stepX = (uint32_t)(invScaleX * 65536.0f + 0.5f);
    uint32_t stepY = (uint32_t)(invScaleY * 65536.0f + 0.5f);
    uint32_t initY = (uint32_t)((float)(clipDy0 - dy0) * invScaleY * 65536.0f);
    uint32_t initX = (uint32_t)((float)(clipDx0 - dx0) * invScaleX * 65536.0f);

    uint32_t curY = initY;
    for (int y = clipDy0; y < clipDy1; y++, curY += stepY) {
        int sy = (int)(curY >> 16);
        if (flipY) sy = (srcH - 1) - sy;
        if (sy < 0) sy = 0; else if (sy >= srcH) sy = srcH - 1;

        int sampleY = srcY + sy;
        uint16_t *d_ptr = target + y * targetStride;
        const uint16_t *s_rgb_row = page->rgb + sampleY * pageStride;
        const uint8_t  *s_alpha_row = page->alpha + sampleY * pageStride;

        uint32_t curX = initX;
        for (int x = clipDx0; x < clipDx1; x++, curX += stepX) {
            int sx = (int)(curX >> 16);
            if (flipX) sx = (srcW - 1) - sx;
            if (sx < 0) sx = 0; else if (sx >= srcW) sx = srcW - 1;

            int sampleX = srcX + sx;
            uint8_t a = s_alpha_row[sampleX];
            if (a == 0) continue;

            if (globalAlpha < 0.999f) a = (uint8_t)((a * (int)(globalAlpha * 255.0f)) >> 8);
            if (a == 0) continue;

            uint16_t pix = s_rgb_row[sampleX];
            if (needsTint) pix = tint_rgb565(pix, tr, tg, tb);

            if (sw->blendMode == bm_add) {
                d_ptr[x] = blend_pixel_rgb565_add(d_ptr[x], pix, a);
            } else if (a == 255 && !needsTint && globalAlpha >= 0.999f) {
                d_ptr[x] = pix;
            } else {
                d_ptr[x] = blend_pixel_rgb565(d_ptr[x], pix, a);
            }
        }
    }
}

// ===[ Draw Sprite Implementations ]===

static void blitRotatedSubRect(SWRenderer *sw, uint32_t pageId,
                               int srcX, int srcY, int srcW, int srcH,
                               float sx0, float sy0, float sx1, float sy1,
                               float sx2, float sy2, float sx3, float sy3,
                               uint32_t bgrColor, float globalAlpha) {
    if (!ensureTextureLoaded(sw, pageId) || srcW <= 0 || srcH <= 0) return;
    if (globalAlpha <= 0.0f) return;

    // Bounding box of the 4 transformed corners in screen space
    float minX = fminf(fminf(sx0, sx1), fminf(sx2, sx3));
    float maxX = fmaxf(fmaxf(sx0, sx1), fmaxf(sx2, sx3));
    float minY = fminf(fminf(sy0, sy1), fminf(sy2, sy3));
    float maxY = fmaxf(fmaxf(sy0, sy1), fmaxf(sy2, sy3));

    int bbX0 = (int)floorf(minX);
    int bbY0 = (int)floorf(minY);
    int bbX1 = (int)ceilf(maxX);
    int bbY1 = (int)ceilf(maxY);

    if (bbX0 < sw->clipX0) bbX0 = sw->clipX0;
    if (bbY0 < sw->clipY0) bbY0 = sw->clipY0;
    if (bbX1 > sw->clipX1) bbX1 = sw->clipX1;
    if (bbY1 > sw->clipY1) bbY1 = sw->clipY1;

    if (bbX0 >= bbX1 || bbY0 >= bbY1) return;

    // Forward affine mapping:
    // (sx - sx0) = dUx * u + dVx * v
    // (sy - sy0) = dUy * u + dVy * v
    float dUx = (sx1 - sx0) / (float)srcW;
    float dUy = (sy1 - sy0) / (float)srcW;
    float dVx = (sx2 - sx0) / (float)srcH;
    float dVy = (sy2 - sy0) / (float)srcH;

    float det = dUx * dVy - dVx * dUy;
    if (fabsf(det) < 1e-6f) return;

    float invDet = 1.0f / det;
    float M00 = dVy * invDet;
    float M01 = -dVx * invDet;
    float M10 = -dUy * invDet;
    float M11 = dUx * invDet;

    int32_t stepU_X = (int32_t)roundf(M00 * 65536.0f);
    int32_t stepV_X = (int32_t)roundf(M10 * 65536.0f);

    float initPx = ((float)bbX0 + 0.5f) - sx0;

    SWTexturePage *page = &sw->pages[pageId];
    uint8_t tr = bgrColor & 0xFF;
    uint8_t tg = (bgrColor >> 8) & 0xFF;
    uint8_t tb = (bgrColor >> 16) & 0xFF;
    bool needsTint = (tr != 255 || tg != 255 || tb != 255);

    uint16_t *target = sw->currentTarget;
    if (!target) return;
    int targetStride = sw->targetWidth;
    int pageStride = page->width;

    if (!needsTint && globalAlpha >= 0.999f) {
        for (int y = bbY0; y < bbY1; y++) {
            float py = ((float)y + 0.5f) - sy0;
            int32_t curU = (int32_t)roundf((M00 * initPx + M01 * py) * 65536.0f);
            int32_t curV = (int32_t)roundf((M10 * initPx + M11 * py) * 65536.0f);
            uint16_t *d_row = target + y * targetStride;

            for (int x = bbX0; x < bbX1; x++) {
                int u = curU >> 16;
                int v = curV >> 16;
                curU += stepU_X;
                curV += stepV_X;

                if ((unsigned)u < (unsigned)srcW && (unsigned)v < (unsigned)srcH) {
                    int sampleIdx = (srcY + v) * pageStride + (srcX + u);
                    uint8_t a = page->alpha[sampleIdx];
                    if (a == 0) continue;

                    uint16_t pix = page->rgb[sampleIdx];
                    if (sw->blendMode == bm_add) {
                        d_row[x] = blend_pixel_rgb565_add(d_row[x], pix, a);
                    } else if (a == 255) {
                        d_row[x] = pix;
                    } else {
                        d_row[x] = blend_pixel_rgb565(d_row[x], pix, a);
                    }
                }
            }
        }
    } else {
        uint32_t ga_int = (uint32_t)(globalAlpha * 255.0f);
        for (int y = bbY0; y < bbY1; y++) {
            float py = ((float)y + 0.5f) - sy0;
            int32_t curU = (int32_t)roundf((M00 * initPx + M01 * py) * 65536.0f);
            int32_t curV = (int32_t)roundf((M10 * initPx + M11 * py) * 65536.0f);
            uint16_t *d_row = target + y * targetStride;

            for (int x = bbX0; x < bbX1; x++) {
                int u = curU >> 16;
                int v = curV >> 16;
                curU += stepU_X;
                curV += stepV_X;

                if ((unsigned)u < (unsigned)srcW && (unsigned)v < (unsigned)srcH) {
                    int sampleIdx = (srcY + v) * pageStride + (srcX + u);
                    uint8_t a = page->alpha[sampleIdx];
                    if (a == 0) continue;

                    if (globalAlpha < 0.999f) {
                        a = (uint8_t)((a * ga_int) >> 8);
                        if (a == 0) continue;
                    }

                    uint16_t pix = page->rgb[sampleIdx];
                    if (needsTint) pix = tint_rgb565(pix, tr, tg, tb);

                    if (sw->blendMode == bm_add) {
                        d_row[x] = blend_pixel_rgb565_add(d_row[x], pix, a);
                    } else if (a == 255 && !needsTint && globalAlpha >= 0.999f) {
                        d_row[x] = pix;
                    } else {
                        d_row[x] = blend_pixel_rgb565(d_row[x], pix, a);
                    }
                }
            }
        }
    }
}

static void swDrawSprite(Renderer *renderer, int32_t tpagIndex, float x, float y, float originX, float originY, float xscale, float yscale, float angleDeg, uint32_t color, float alpha) {
    SWRenderer *sw = (SWRenderer *)renderer;
    DataWin *dw = renderer->dataWin;
    if (!dw || tpagIndex < 0 || (uint32_t)tpagIndex >= dw->tpag.count) return;

    TexturePageItem *tpag = &dw->tpag.items[tpagIndex];
    int16_t pageId = tpag->texturePageId;
    if (pageId < 0) return;

    float normAngle = fmodf(angleDeg, 360.0f);
    if (normAngle < 0.0f) normAngle += 360.0f;
    bool hasRotation = (normAngle > 0.001f && normAngle < 359.999f);

    if (!hasRotation) {
        float localX0 = (float)tpag->targetX - originX;
        float localY0 = (float)tpag->targetY - originY;
        float dstX = (xscale >= 0.0f) ? (x + localX0 * xscale)
                                      : (x + (localX0 + (float)tpag->sourceWidth) * xscale);
        float dstY = (yscale >= 0.0f) ? (y + localY0 * yscale)
                                      : (y + (localY0 + (float)tpag->sourceHeight) * yscale);

        blitSubRect(sw, (uint32_t)pageId,
                    tpag->sourceX, tpag->sourceY, tpag->sourceWidth, tpag->sourceHeight,
                    dstX, dstY, xscale, yscale, color, alpha);
        return;
    }

    float localX0 = (float)tpag->targetX - originX;
    float localY0 = (float)tpag->targetY - originY;
    float localX1 = localX0 + (float)tpag->sourceWidth;
    float localY1 = localY0 + (float)tpag->sourceHeight;

    float angleRad = -angleDeg * ((float)M_PI / 180.0f);
    float cosA = cosf(angleRad);
    float sinA = sinf(angleRad);

    float lx0_s = localX0 * xscale, ly0_s = localY0 * yscale;
    float lx1_s = localX1 * xscale, ly1_s = localY1 * yscale;

    float wx0 = x + cosA * lx0_s - sinA * ly0_s;
    float wy0 = y + sinA * lx0_s + cosA * ly0_s;

    float wx1 = x + cosA * lx1_s - sinA * ly0_s;
    float wy1 = y + sinA * lx1_s + cosA * ly0_s;

    float wx2 = x + cosA * lx0_s - sinA * ly1_s;
    float wy2 = y + sinA * lx0_s + cosA * ly1_s;

    float wx3 = x + cosA * lx1_s - sinA * ly1_s;
    float wy3 = y + sinA * lx1_s + cosA * ly1_s;

    float sx0, sy0, sx1, sy1, sx2, sy2, sx3, sy3;
    sw_transform_point(sw, wx0, wy0, &sx0, &sy0);
    sw_transform_point(sw, wx1, wy1, &sx1, &sy1);
    sw_transform_point(sw, wx2, wy2, &sx2, &sy2);
    sw_transform_point(sw, wx3, wy3, &sx3, &sy3);

    blitRotatedSubRect(sw, (uint32_t)pageId,
                       tpag->sourceX, tpag->sourceY, tpag->sourceWidth, tpag->sourceHeight,
                       sx0, sy0, sx1, sy1, sx2, sy2, sx3, sy3,
                       color, alpha);
}

static void swDrawSpritePart(Renderer *renderer, int32_t tpagIndex, float srcOffX, float srcOffY, float srcW, float srcH, float x, float y, float xscale, float yscale, float angleDeg, float pivotX, float pivotY, uint32_t color, float alpha) {
    SWRenderer *sw = (SWRenderer *)renderer;
    DataWin *dw = renderer->dataWin;
    if (!dw || tpagIndex < 0 || (uint32_t)tpagIndex >= dw->tpag.count) return;

    TexturePageItem *tpag = &dw->tpag.items[tpagIndex];
    int16_t pageId = tpag->texturePageId;
    if (pageId < 0) return;

    int sx = tpag->sourceX + (int)srcOffX;
    int sy = tpag->sourceY + (int)srcOffY;
    int sw_w = (int)srcW;
    int sw_h = (int)srcH;

    float normAngle = fmodf(angleDeg, 360.0f);
    if (normAngle < 0.0f) normAngle += 360.0f;
    bool hasRotation = (normAngle > 0.001f && normAngle < 359.999f);

    if (!hasRotation) {
        float dstX = (xscale >= 0.0f) ? x : (x + (float)sw_w * xscale);
        float dstY = (yscale >= 0.0f) ? y : (y + (float)sw_h * yscale);

        blitSubRect(sw, (uint32_t)pageId,
                    sx, sy, sw_w, sw_h,
                    dstX, dstY, xscale, yscale, color, alpha);
        return;
    }

    float angleRad = -angleDeg * ((float)M_PI / 180.0f);
    float cosA = cosf(angleRad);
    float sinA = sinf(angleRad);

    float qx0 = x,                         qy0 = y;
    float qx1 = x + (float)sw_w * xscale,  qy1 = y;
    float qx2 = x,                         qy2 = y + (float)sw_h * yscale;
    float qx3 = x + (float)sw_w * xscale,  qy3 = y + (float)sw_h * yscale;

    float dx, dy;
    dx = qx0 - pivotX; dy = qy0 - pivotY;
    float wx0 = cosA * dx - sinA * dy + pivotX;
    float wy0 = sinA * dx + cosA * dy + pivotY;

    dx = qx1 - pivotX; dy = qy1 - pivotY;
    float wx1 = cosA * dx - sinA * dy + pivotX;
    float wy1 = sinA * dx + cosA * dy + pivotY;

    dx = qx2 - pivotX; dy = qy2 - pivotY;
    float wx2 = cosA * dx - sinA * dy + pivotX;
    float wy2 = sinA * dx + cosA * dy + pivotY;

    dx = qx3 - pivotX; dy = qy3 - pivotY;
    float wx3 = cosA * dx - sinA * dy + pivotX;
    float wy3 = sinA * dx + cosA * dy + pivotY;

    float sx0, sy0, sx1, sy1, sx2, sy2, sx3, sy3;
    sw_transform_point(sw, wx0, wy0, &sx0, &sy0);
    sw_transform_point(sw, wx1, wy1, &sx1, &sy1);
    sw_transform_point(sw, wx2, wy2, &sx2, &sy2);
    sw_transform_point(sw, wx3, wy3, &sx3, &sy3);

    blitRotatedSubRect(sw, (uint32_t)pageId,
                       sx, sy, sw_w, sw_h,
                       sx0, sy0, sx1, sy1, sx2, sy2, sx3, sy3,
                       color, alpha);
}

static void swDrawSpritePartColor(Renderer *renderer, int32_t tpagIndex, float srcOffX, float srcOffY, float srcW, float srcH, float x, float y, float xscale, float yscale, float angleDeg, float pivotX, float pivotY, uint32_t color1, MAYBE_UNUSED uint32_t color2, MAYBE_UNUSED uint32_t color3, MAYBE_UNUSED uint32_t color4, float alpha) {
    swDrawSpritePart(renderer, tpagIndex, srcOffX, srcOffY, srcW, srcH, x, y, xscale, yscale, angleDeg, pivotX, pivotY, color1, alpha);
}

static void swDrawSpritePos(Renderer *renderer, int32_t tpagIndex, float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4, float alpha) {
    SWRenderer *sw = (SWRenderer *)renderer;
    DataWin *dw = renderer->dataWin;
    if (!dw || tpagIndex < 0 || (uint32_t)tpagIndex >= dw->tpag.count) return;

    TexturePageItem *tpag = &dw->tpag.items[tpagIndex];
    int16_t pageId = tpag->texturePageId;
    if (pageId < 0) return;

    // GM convention for draw_sprite_pos:
    // (x1, y1) = TL, (x2, y2) = TR, (x3, y3) = BR, (x4, y4) = BL
    float sx0, sy0, sx1, sy1, sx2, sy2, sx3, sy3;
    sw_transform_point(sw, x1, y1, &sx0, &sy0);
    sw_transform_point(sw, x2, y2, &sx1, &sy1);
    sw_transform_point(sw, x4, y4, &sx2, &sy2);
    sw_transform_point(sw, x3, y3, &sx3, &sy3);

    blitRotatedSubRect(sw, (uint32_t)pageId,
                       tpag->sourceX, tpag->sourceY, tpag->sourceWidth, tpag->sourceHeight,
                       sx0, sy0, sx1, sy1, sx2, sy2, sx3, sy3,
                       0xFFFFFF, alpha);
}

static void swDrawSpriteTiled(Renderer *renderer, int32_t tpagIndex, float originX, float originY, float x, float y, float xscale, float yscale, bool tileX, bool tileY, float roomW, float roomH, uint32_t color, float alpha) {
    DataWin *dw = renderer->dataWin;
    if (!dw || tpagIndex < 0 || (uint32_t)tpagIndex >= dw->tpag.count) return;

    TexturePageItem *tpag = &dw->tpag.items[tpagIndex];
    float tileW = (float)tpag->boundingWidth * fabsf(xscale);
    float tileH = (float)tpag->boundingHeight * fabsf(yscale);
    if (tileW <= 0.0f || tileH <= 0.0f) return;

    float startX = tileX ? fmodf(x - originX, tileW) : (x - originX);
    if (startX > 0.0f && tileX) startX -= tileW;
    float startY = tileY ? fmodf(y - originY, tileH) : (y - originY);
    if (startY > 0.0f && tileY) startY -= tileH;

    float endX = tileX ? roomW : (startX + tileW);
    float endY = tileY ? roomH : (startY + tileH);

    for (float curY = startY; curY < endY; curY += tileH) {
        for (float curX = startX; curX < endX; curX += tileW) {
            swDrawSprite(renderer, tpagIndex, curX + originX, curY + originY, originX, originY, xscale, yscale, 0.0f, color, alpha);
        }
    }
}

// ===[ Shape Drawing ]===

static void swDrawRectangle(Renderer *renderer, float x1, float y1, float x2, float y2, uint32_t color, float alpha, bool outline) {
    SWRenderer *sw = (SWRenderer *)renderer;
    if (alpha <= 0.0f) return;

    float sx1, sy1, sx2, sy2;
    sw_transform_point(sw, x1, y1, &sx1, &sy1);
    sw_transform_point(sw, x2, y2, &sx2, &sy2);

    int rx0 = (int)floorf(fminf(sx1, sx2));
    int ry0 = (int)floorf(fminf(sy1, sy2));
    int rx1 = (int)ceilf(fmaxf(sx1, sx2));
    int ry1 = (int)ceilf(fmaxf(sy1, sy2));

    uint16_t c565 = bgr_to_rgb565(color);
    uint8_t a = (uint8_t)(alpha * 255.0f);

    if (outline) {
        // Outline rectangle
        for (int x = rx0; x <= rx1; x++) {
            if (x >= sw->clipX0 && x < sw->clipX1) {
                if (ry0 >= sw->clipY0 && ry0 < sw->clipY1) sw->currentTarget[ry0 * sw->targetWidth + x] = c565;
                if (ry1 >= sw->clipY0 && ry1 < sw->clipY1) sw->currentTarget[ry1 * sw->targetWidth + x] = c565;
            }
        }
        for (int y = ry0; y <= ry1; y++) {
            if (y >= sw->clipY0 && y < sw->clipY1) {
                if (rx0 >= sw->clipX0 && rx0 < sw->clipX1) sw->currentTarget[y * sw->targetWidth + rx0] = c565;
                if (rx1 >= sw->clipX0 && rx1 < sw->clipX1) sw->currentTarget[y * sw->targetWidth + rx1] = c565;
            }
        }
        return;
    }

    // Filled rectangle
    int cx0 = rx0 < sw->clipX0 ? sw->clipX0 : rx0;
    int cy0 = ry0 < sw->clipY0 ? sw->clipY0 : ry0;
    int cx1 = rx1 > sw->clipX1 ? sw->clipX1 : rx1;
    int cy1 = ry1 > sw->clipY1 ? sw->clipY1 : ry1;

    if (cx0 >= cx1 || cy0 >= cy1) return;

    int stride = sw->targetWidth;
    if (a == 255) {
        for (int y = cy0; y < cy1; y++) {
            uint16_t *row = sw->currentTarget + y * stride;
            for (int x = cx0; x < cx1; x++) row[x] = c565;
        }
    } else {
        // Fast-path: precalculate constant source terms outside row loops
        uint32_t a5 = ((uint32_t)a + 4) >> 3; // Scale 0..255 to 0..32
        uint32_t inv_a5 = 32 - a5;
        uint32_t const_s_rb = (uint32_t)(c565 & 0xF81F) * a5;
        uint32_t const_s_g  = (uint32_t)(c565 & 0x07E0) * a5;

        for (int y = cy0; y < cy1; y++) {
            uint16_t *row = sw->currentTarget + y * stride;
            int x = cx0;
            // 2-pixel unrolled loop
            for (; x + 1 < cx1; x += 2) {
                uint32_t p0 = row[x];
                uint32_t p1 = row[x + 1];
                uint32_t rb0 = ((const_s_rb + (p0 & 0xF81F) * inv_a5) >> 5) & 0xF81F;
                uint32_t g0  = ((const_s_g  + (p0 & 0x07E0) * inv_a5) >> 5) & 0x07E0;
                uint32_t rb1 = ((const_s_rb + (p1 & 0xF81F) * inv_a5) >> 5) & 0xF81F;
                uint32_t g1  = ((const_s_g  + (p1 & 0x07E0) * inv_a5) >> 5) & 0x07E0;
                row[x]     = (uint16_t)(rb0 | g0);
                row[x + 1] = (uint16_t)(rb1 | g1);
            }
            if (x < cx1) {
                uint32_t p0 = row[x];
                uint32_t rb0 = ((const_s_rb + (p0 & 0xF81F) * inv_a5) >> 5) & 0xF81F;
                uint32_t g0  = ((const_s_g  + (p0 & 0x07E0) * inv_a5) >> 5) & 0x07E0;
                row[x] = (uint16_t)(rb0 | g0);
            }
        }
    }
}

static void swDrawRectangleColor(Renderer *renderer, float x1, float y1, float x2, float y2, uint32_t color1, MAYBE_UNUSED uint32_t color2, MAYBE_UNUSED uint32_t color3, MAYBE_UNUSED uint32_t color4, float alpha, bool outline) {
    swDrawRectangle(renderer, x1, y1, x2, y2, color1, alpha, outline);
}

static void sw_draw_line_internal(SWRenderer *sw, int px0, int py0, int px1, int py1, uint32_t color, float alpha) {
    if (alpha <= 0.0f) return;

    int dx = abs(px1 - px0), sx = px0 < px1 ? 1 : -1;
    int dy = -abs(py1 - py0), sy = py0 < py1 ? 1 : -1;
    int err = dx + dy, e2;

    uint16_t c565 = bgr_to_rgb565(color);
    uint8_t a = (uint8_t)(alpha * 255.0f);
    int stride = sw->targetWidth;

    while (1) {
        if (px0 >= sw->clipX0 && px0 < sw->clipX1 && py0 >= sw->clipY0 && py0 < sw->clipY1) {
            uint16_t *ptr = sw->currentTarget + py0 * stride + px0;
            if (a == 255) *ptr = c565;
            else *ptr = blend_pixel_rgb565(*ptr, c565, a);
        }
        if (px0 == px1 && py0 == py1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; px0 += sx; }
        if (e2 <= dx) { err += dx; py0 += sy; }
    }
}

static void swDrawLine(Renderer *renderer, float x1, float y1, float x2, float y2, MAYBE_UNUSED float width, uint32_t color, float alpha) {
    SWRenderer *sw = (SWRenderer *)renderer;
    if (alpha <= 0.0f) return;

    float sx1, sy1, sx2, sy2;
    sw_transform_point(sw, x1, y1, &sx1, &sy1);
    sw_transform_point(sw, x2, y2, &sx2, &sy2);

    sw_draw_line_internal(sw, (int)roundf(sx1), (int)roundf(sy1), (int)roundf(sx2), (int)roundf(sy2), color, alpha);
}

static void swDrawLineColor(Renderer *renderer, float x1, float y1, float x2, float y2, float width, uint32_t color1, MAYBE_UNUSED uint32_t color2, float alpha) {
    swDrawLine(renderer, x1, y1, x2, y2, width, color1, alpha);
}

static void sw_rasterize_triangle(SWRenderer *sw, float x0, float y0, float x1, float y1, float x2, float y2, uint32_t color, float alpha) {
    if (alpha <= 0.0f) return;

    // Sort vertices by Y ascending: y0 <= y1 <= y2
    if (y0 > y1) { float tx = x0; x0 = x1; x1 = tx; float ty = y0; y0 = y1; y1 = ty; }
    if (y0 > y2) { float tx = x0; x0 = x2; x2 = tx; float ty = y0; y0 = y2; y2 = ty; }
    if (y1 > y2) { float tx = x1; x1 = x2; x2 = tx; float ty = y1; y1 = y2; y2 = ty; }

    int y_start = (int)ceilf(y0);
    int y_end   = (int)floorf(y2);

    if (y_start < sw->clipY0) y_start = sw->clipY0;
    if (y_end >= sw->clipY1)  y_end = sw->clipY1 - 1;
    if (y_start > y_end) return;

    float total_height = y2 - y0;
    if (total_height <= 0.0001f) return;

    uint16_t c565 = bgr_to_rgb565(color);
    uint8_t a = (uint8_t)(alpha * 255.0f);
    if (a == 0) return;

    int stride = sw->targetWidth;
    uint32_t a5 = ((uint32_t)a + 4) >> 3;
    uint32_t inv_a5 = 32 - a5;
    uint32_t const_s_rb = (uint32_t)(c565 & 0xF81F) * a5;
    uint32_t const_s_g  = (uint32_t)(c565 & 0x07E0) * a5;

    for (int y = y_start; y <= y_end; y++) {
        float fy = (float)y;
        float alpha_long = (fy - y0) / total_height;
        float x_long = x0 + (x2 - x0) * alpha_long;

        float x_short;
        if (fy < y1) {
            float seg_h = y1 - y0;
            if (seg_h <= 0.0001f) {
                x_short = x0;
            } else {
                x_short = x0 + (x1 - x0) * ((fy - y0) / seg_h);
            }
        } else {
            float seg_h = y2 - y1;
            if (seg_h <= 0.0001f) {
                x_short = x1;
            } else {
                x_short = x1 + (x2 - x1) * ((fy - y1) / seg_h);
            }
        }

        int x_min = (int)ceilf(fminf(x_long, x_short));
        int x_max = (int)floorf(fmaxf(x_long, x_short));

        if (x_min > x_max) {
            int x_mid = (int)roundf((x_long + x_short) * 0.5f);
            if (x_mid >= sw->clipX0 && x_mid < sw->clipX1) {
                uint16_t *ptr = sw->currentTarget + y * stride + x_mid;
                if (a == 255 || !sw->blendEnable) *ptr = c565;
                else {
                    uint32_t p0 = *ptr;
                    uint32_t rb0 = ((const_s_rb + (p0 & 0xF81F) * inv_a5) >> 5) & 0xF81F;
                    uint32_t g0  = ((const_s_g  + (p0 & 0x07E0) * inv_a5) >> 5) & 0x07E0;
                    *ptr = (uint16_t)(rb0 | g0);
                }
            }
            continue;
        }

        if (x_min < sw->clipX0) x_min = sw->clipX0;
        if (x_max >= sw->clipX1) x_max = sw->clipX1 - 1;

        if (x_min <= x_max) {
            uint16_t *row = sw->currentTarget + y * stride;
            if (a == 255 || !sw->blendEnable) {
                for (int x = x_min; x <= x_max; x++) {
                    row[x] = c565;
                }
            } else {
                int x = x_min;
                for (; x + 1 <= x_max; x += 2) {
                    uint32_t p0 = row[x];
                    uint32_t p1 = row[x + 1];
                    uint32_t rb0 = ((const_s_rb + (p0 & 0xF81F) * inv_a5) >> 5) & 0xF81F;
                    uint32_t g0  = ((const_s_g  + (p0 & 0x07E0) * inv_a5) >> 5) & 0x07E0;
                    uint32_t rb1 = ((const_s_rb + (p1 & 0xF81F) * inv_a5) >> 5) & 0xF81F;
                    uint32_t g1  = ((const_s_g  + (p1 & 0x07E0) * inv_a5) >> 5) & 0x07E0;
                    row[x]     = (uint16_t)(rb0 | g0);
                    row[x + 1] = (uint16_t)(rb1 | g1);
                }
                if (x <= x_max) {
                    uint32_t p0 = row[x];
                    uint32_t rb0 = ((const_s_rb + (p0 & 0xF81F) * inv_a5) >> 5) & 0xF81F;
                    uint32_t g0  = ((const_s_g  + (p0 & 0x07E0) * inv_a5) >> 5) & 0x07E0;
                    row[x] = (uint16_t)(rb0 | g0);
                }
            }
        }
    }
}

static void sw_rasterize_triangle_textured(SWRenderer *sw, const SWVertex *v0, const SWVertex *v1, const SWVertex *v2) {
    if (!sw->currentTarget) return;
    float alpha = v2->alpha;
    if (alpha <= 0.0f) return;

    if (sw->primTexture <= 0) {
        sw_rasterize_triangle(sw, v0->x, v0->y, v1->x, v1->y, v2->x, v2->y, v2->color, alpha);
        return;
    }

    int32_t tpagIndex = sw->primTexture - 1;
    DataWin *dw = sw->base.dataWin;
    if (!dw || tpagIndex < 0 || (uint32_t)tpagIndex >= dw->tpag.count) {
        sw_rasterize_triangle(sw, v0->x, v0->y, v1->x, v1->y, v2->x, v2->y, v2->color, alpha);
        return;
    }

    TexturePageItem *tpag = &dw->tpag.items[tpagIndex];
    int16_t pageId = tpag->texturePageId;
    if (pageId < 0 || !ensureTextureLoaded(sw, (uint32_t)pageId)) {
        sw_rasterize_triangle(sw, v0->x, v0->y, v1->x, v1->y, v2->x, v2->y, v2->color, alpha);
        return;
    }

    SWTexturePage *page = &sw->pages[pageId];
    if (!page->rgb || page->width <= 0 || page->height <= 0) return;

    int pageW = page->width;
    uint16_t *srcRgb = page->rgb;
    uint8_t *srcAlpha = page->alpha;
    int srcX = tpag->sourceX;
    int srcY = tpag->sourceY;
    int srcW = tpag->sourceWidth;
    int srcH = tpag->sourceHeight;
    if (srcW <= 0 || srcH <= 0) return;

    uint32_t color = v2->color;
    uint8_t tr = color & 0xFF;
    uint8_t tg = (color >> 8) & 0xFF;
    uint8_t tb = (color >> 16) & 0xFF;
    bool needsTint = (tr != 255 || tg != 255 || tb != 255);

    float x0 = v0->x, y0 = v0->y, u0 = v0->u, vert_v0 = v0->v;
    float x1 = v1->x, y1 = v1->y, u1 = v1->u, vert_v1 = v1->v;
    float x2 = v2->x, y2 = v2->y, u2 = v2->u, vert_v2 = v2->v;

    float dx1 = x1 - x0;
    float dy1 = y1 - y0;
    float dx2 = x2 - x0;
    float dy2 = y2 - y0;

    float det = dx1 * dy2 - dx2 * dy1;
    if (fabsf(det) < 0.0001f) return;

    float invDet = 1.0f / det;
    float du1 = u1 - u0;
    float du2 = u2 - u0;
    float dv1 = vert_v1 - vert_v0;
    float dv2 = vert_v2 - vert_v0;

    float dudx = (du1 * dy2 - du2 * dy1) * invDet;
    float dudy = (du2 * dx1 - du1 * dx2) * invDet;
    float dvdx = (dv1 * dy2 - dv2 * dy1) * invDet;
    float dvdy = (dv2 * dx1 - dv1 * dx2) * invDet;

    // Sort vertices by Y ascending for scanline bounds: py0 <= py1 <= py2
    float px0 = x0, py0 = y0;
    float px1 = x1, py1 = y1;
    float px2 = x2, py2 = y2;

    if (py0 > py1) { float tx = px0; px0 = px1; px1 = tx; float ty = py0; py0 = py1; py1 = ty; }
    if (py0 > py2) { float tx = px0; px0 = px2; px2 = tx; float ty = py0; py0 = py2; py2 = ty; }
    if (py1 > py2) { float tx = px1; px1 = px2; px2 = tx; float ty = py1; py1 = py2; py2 = ty; }

    int y_start = (int)ceilf(py0);
    int y_end   = (int)floorf(py2);

    if (y_start < sw->clipY0) y_start = sw->clipY0;
    if (y_end >= sw->clipY1)  y_end = sw->clipY1 - 1;
    if (y_start > y_end) return;

    float total_height = py2 - py0;
    if (total_height <= 0.0001f) return;

    int stride = sw->targetWidth;
    int32_t stepU = (int32_t)roundf(dudx * 65536.0f);
    int32_t stepV = (int32_t)roundf(dvdx * 65536.0f);
    uint32_t ga_int = (uint32_t)(alpha * 255.0f);

    for (int y = y_start; y <= y_end; y++) {
        float fy = (float)y;
        float alpha_long = (fy - py0) / total_height;
        float x_long = px0 + (px2 - px0) * alpha_long;

        float x_short;
        if (fy < py1) {
            float seg_h = py1 - py0;
            x_short = (seg_h <= 0.0001f) ? px0 : px0 + (px1 - px0) * ((fy - py0) / seg_h);
        } else {
            float seg_h = py2 - py1;
            x_short = (seg_h <= 0.0001f) ? px1 : px1 + (px2 - px1) * ((fy - py1) / seg_h);
        }

        int x_min = (int)ceilf(fminf(x_long, x_short));
        int x_max = (int)floorf(fmaxf(x_long, x_short));

        if (x_min > x_max) {
            int x_mid = (int)roundf((x_long + x_short) * 0.5f);
            if (x_mid >= sw->clipX0 && x_mid < sw->clipX1) {
                x_min = x_mid;
                x_max = x_mid;
            } else {
                continue;
            }
        }

        if (x_min < sw->clipX0) x_min = sw->clipX0;
        if (x_max >= sw->clipX1) x_max = sw->clipX1 - 1;
        if (x_min > x_max) continue;

        float startU = u0 + ((float)x_min - x0) * dudx + (fy - y0) * dudy;
        float startV = vert_v0 + ((float)x_min - x0) * dvdx + (fy - y0) * dvdy;
        int32_t fixU = (int32_t)roundf(startU * 65536.0f);
        int32_t fixV = (int32_t)roundf(startV * 65536.0f);

        uint16_t *row = sw->currentTarget + y * stride;

        for (int x = x_min; x <= x_max; x++) {
            uint32_t u_frac = (uint32_t)fixU & 0xFFFF;
            uint32_t v_frac = (uint32_t)fixV & 0xFFFF;

            int tx = srcX + (int)((u_frac * (uint32_t)srcW) >> 16);
            int ty = srcY + (int)((v_frac * (uint32_t)srcH) >> 16);

            int sampleIdx = ty * pageW + tx;
            uint8_t a = srcAlpha ? srcAlpha[sampleIdx] : 255;
            if (a > 0) {
                if (alpha < 0.999f) a = (uint8_t)((a * ga_int) >> 8);
                if (a > 0) {
                    uint16_t pix = srcRgb[sampleIdx];
                    if (needsTint) pix = tint_rgb565(pix, tr, tg, tb);
                    if (a == 255 || !sw->blendEnable) {
                        row[x] = pix;
                    } else {
                        row[x] = blend_pixel_rgb565(row[x], pix, a);
                    }
                }
            }

            fixU += stepU;
            fixV += stepV;
        }
    }
}

static void swDrawTriangle(Renderer *renderer, float x1, float y1, float x2, float y2, float x3, float y3, uint32_t color1, MAYBE_UNUSED uint32_t color2, MAYBE_UNUSED uint32_t color3, float alpha, bool outline) {
    SWRenderer *sw = (SWRenderer *)renderer;
    if (outline) {
        swDrawLine(renderer, x1, y1, x2, y2, 1.0f, color1, alpha);
        swDrawLine(renderer, x2, y2, x3, y3, 1.0f, color1, alpha);
        swDrawLine(renderer, x3, y3, x1, y1, 1.0f, color1, alpha);
    } else {
        float sx1, sy1, sx2, sy2, sx3, sy3;
        sw_transform_point(sw, x1, y1, &sx1, &sy1);
        sw_transform_point(sw, x2, y2, &sx2, &sy2);
        sw_transform_point(sw, x3, y3, &sx3, &sy3);
        sw_rasterize_triangle(sw, sx1, sy1, sx2, sy2, sx3, sy3, color1, alpha);
    }
}

static void swDrawCircleColor(Renderer *renderer, float cx, float cy, float radius, uint32_t col1, MAYBE_UNUSED uint32_t col2, float alpha, bool outline) {
    SWRenderer *sw = (SWRenderer *)renderer;
    if (alpha <= 0.0f || radius <= 0.0f) return;

    float scx, scy;
    sw_transform_point(sw, cx, cy, &scx, &scy);

    float scaleX = (!sw->inGUI) ? ((sw->viewW > 0) ? ((float)sw->portW / (float)sw->viewW) : 1.0f)
                                : ((sw->guiW > 0) ? ((float)sw->portW / (float)sw->guiW) : 1.0f);
    float scaleY = (!sw->inGUI) ? ((sw->viewH > 0) ? ((float)sw->portH / (float)sw->viewH) : 1.0f)
                                : ((sw->guiH > 0) ? ((float)sw->portH / (float)sw->guiH) : 1.0f);

    float srx = radius * fabsf(scaleX);
    float sry = radius * fabsf(scaleY);
    if (srx <= 0.0f || sry <= 0.0f) return;

    // Fast reject if completely outside clip rect
    if (scx + srx < (float)sw->clipX0 || scx - srx >= (float)sw->clipX1 ||
        scy + sry < (float)sw->clipY0 || scy - sry >= (float)sw->clipY1) {
        return;
    }

    uint16_t c565 = bgr_to_rgb565(col1);
    uint8_t a = (uint8_t)(alpha * 255.0f);
    int stride = sw->targetWidth;

    int icx = (int)roundf(scx);
    int icy = (int)roundf(scy);
    int ir = (int)roundf((srx + sry) * 0.5f);
    if (ir <= 0) return;

    if (outline) {
        uint16_t outCol565 = (col1 == col2) ? c565 : bgr_to_rgb565(col2);

        int x = 0;
        int y = ir;
        int d = 3 - 2 * ir;

        #define PLOT_CIRCLE_PIXEL(px, py) do {             if ((px) >= sw->clipX0 && (px) < sw->clipX1 && (py) >= sw->clipY0 && (py) < sw->clipY1) {                 uint16_t *ptr = sw->currentTarget + (py) * stride + (px);                 if (a == 255) *ptr = outCol565;                 else *ptr = blend_pixel_rgb565(*ptr, outCol565, a);             }         } while (0)

        while (y >= x) {
            PLOT_CIRCLE_PIXEL(icx + x, icy + y);
            PLOT_CIRCLE_PIXEL(icx - x, icy + y);
            PLOT_CIRCLE_PIXEL(icx + x, icy - y);
            PLOT_CIRCLE_PIXEL(icx - x, icy - y);
            PLOT_CIRCLE_PIXEL(icx + y, icy + x);
            PLOT_CIRCLE_PIXEL(icx - y, icy + x);
            PLOT_CIRCLE_PIXEL(icx + y, icy - x);
            PLOT_CIRCLE_PIXEL(icx - y, icy - x);

            x++;
            if (d > 0) {
                y--;
                d = d + 4 * (x - y) + 10;
            } else {
                d = d + 4 * x + 6;
            }
        }
        #undef PLOT_CIRCLE_PIXEL
    } else {
        // Fast-path for small radii (ir <= 6) avoiding floating-point sqrtf
        if (ir <= 6) {
            static const uint8_t isqrt_lut[37] = {
                0, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3,
                4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5,
                5, 5, 5, 5, 6
            };
            int y0 = icy - ir;
            int y1 = icy + ir;
            if (y0 < sw->clipY0) y0 = sw->clipY0;
            if (y1 >= sw->clipY1) y1 = sw->clipY1 - 1;

            int r2 = ir * ir;
            uint32_t a5 = ((uint32_t)a + 4) >> 3;
            uint32_t inv_a5 = 32 - a5;
            uint32_t const_s_rb = (uint32_t)(c565 & 0xF81F) * a5;
            uint32_t const_s_g  = (uint32_t)(c565 & 0x07E0) * a5;

            for (int y = y0; y <= y1; y++) {
                int dy = y - icy;
                int dy2 = dy * dy;
                if (dy2 > r2) continue;
                int dx = isqrt_lut[r2 - dy2];
                int x0 = icx - dx;
                int x1 = icx + dx;
                if (x0 < sw->clipX0) x0 = sw->clipX0;
                if (x1 >= sw->clipX1) x1 = sw->clipX1 - 1;
                if (x0 > x1) continue;

                uint16_t *row = sw->currentTarget + y * stride;
                if (a == 255) {
                    for (int x = x0; x <= x1; x++) row[x] = c565;
                } else {
                    for (int x = x0; x <= x1; x++) {
                        uint32_t p = row[x];
                        uint32_t rb = ((const_s_rb + (p & 0xF81F) * inv_a5) >> 5) & 0xF81F;
                        uint32_t g  = ((const_s_g  + (p & 0x07E0) * inv_a5) >> 5) & 0x07E0;
                        row[x] = (uint16_t)(rb | g);
                    }
                }
            }
            return;
        }

        int y0 = (int)ceilf(scy - sry);
        int y1 = (int)floorf(scy + sry);
        if (y0 < sw->clipY0) y0 = sw->clipY0;
        if (y1 >= sw->clipY1) y1 = sw->clipY1 - 1;

        float invSrySq = 1.0f / (sry * sry);
        for (int y = y0; y <= y1; y++) {
            float dy = (float)y - scy;
            float dySq = dy * dy;
            float ratio = dySq * invSrySq;
            if (ratio > 1.0f) continue;

            float dx = srx * sqrtf(1.0f - ratio);
            int x0 = (int)roundf(scx - dx);
            int x1 = (int)roundf(scx + dx);
            if (x0 < sw->clipX0) x0 = sw->clipX0;
            if (x1 >= sw->clipX1) x1 = sw->clipX1 - 1;
            if (x0 > x1) continue;

            uint16_t *row = sw->currentTarget + y * stride;
            if (a == 255) {
                for (int x = x0; x <= x1; x++) {
                    row[x] = c565;
                }
            } else {
                for (int x = x0; x <= x1; x++) {
                    row[x] = blend_pixel_rgb565(row[x], c565, a);
                }
            }
        }
    }
}

// ===[ Text Rendering ]===

static void swDrawText(Renderer *renderer, const char *text, float x, float y, float xscale, float yscale, MAYBE_UNUSED float angleDeg, float lineSeparation) {
    SWRenderer *sw = (SWRenderer *)renderer;
    DataWin *dw = renderer->dataWin;
    if (!text || !dw) return;

    int32_t fontIndex = renderer->drawFont;
    if (fontIndex < 0 || (uint32_t)fontIndex >= dw->font.count) return;

    Font *font = &dw->font.fonts[fontIndex];
    int32_t textLen = (int32_t)strlen(text);
    if (textLen == 0) return;

    int32_t lineCount = TextUtils_countLines(text, textLen);
    float lineStride = (0.0f > lineSeparation) ? TextUtils_lineStride(font) : (lineSeparation / (font->scaleY != 0.0f ? font->scaleY : 1.0f));

    float totalHeight = (float)lineCount * lineStride;
    float valignOffset = 0;
    if (renderer->drawValign == 1) valignOffset = -totalHeight / 2.0f;
    else if (renderer->drawValign == 2) valignOffset = -totalHeight;

    float cursorY = y + valignOffset - (float)font->ascenderOffset;
    int32_t lineStart = 0;

    for (int32_t lineIdx = 0; lineIdx < lineCount; lineIdx++) {
        int32_t lineEnd = lineStart;
        while (lineEnd < textLen && !TextUtils_isNewlineChar(text[lineEnd])) lineEnd++;
        int32_t lineLen = lineEnd - lineStart;

        float lineWidth = TextUtils_measureLineWidth(font, text + lineStart, lineLen);
        float halignOffset = 0;
        if (renderer->drawHalign == 1) halignOffset = -lineWidth / 2.0f;
        else if (renderer->drawHalign == 2) halignOffset = -lineWidth;

        float cursorX = x + halignOffset;
        int32_t pos = 0;
        uint16_t ch = 0;
        bool hasCh = false;
        if (lineLen > pos) {
            ch = TextUtils_decodeUtf8(text + lineStart, lineLen, &pos);
            hasCh = true;
        }

        while (hasCh) {
            FontGlyph *glyph = TextUtils_findGlyph(font, ch);
            uint16_t nextCh = 0;
            bool hasNext = lineLen > pos;
            if (hasNext) nextCh = TextUtils_decodeUtf8(text + lineStart, lineLen, &pos);

            if (glyph != NULL) {
                if (ch != ' ' && glyph->sourceWidth > 0 && glyph->sourceHeight > 0) {
                    if (!font->isSpriteFont) {
                        if (font->tpagIndex >= 0 && (uint32_t)font->tpagIndex < dw->tpag.count) {
                            TexturePageItem *tpag = &dw->tpag.items[font->tpagIndex];
                            int sx = tpag->sourceX + glyph->sourceX;
                            int sy = tpag->sourceY + glyph->sourceY;
                            float gx = cursorX + (float)glyph->offset * xscale;
                            float gy = cursorY;
                            blitSubRect(sw, (uint32_t)tpag->texturePageId,
                                        sx, sy, glyph->sourceWidth, glyph->sourceHeight,
                                        gx, gy, xscale, yscale, renderer->drawColor, renderer->drawAlpha);
                        }
                    } else if (font->spriteIndex >= 0 && (uint32_t)font->spriteIndex < dw->sprt.count) {
                        Sprite *spr = &dw->sprt.sprites[font->spriteIndex];
                        int32_t glyphIdx = (int32_t)(glyph - font->glyphs);
                        if (glyphIdx >= 0 && glyphIdx < (int32_t)spr->textureCount) {
                            int32_t tIdx = spr->tpagIndices[glyphIdx];
                            if (tIdx >= 0 && (uint32_t)tIdx < dw->tpag.count) {
                                TexturePageItem *tpag = &dw->tpag.items[tIdx];
                                float gx = cursorX + (float)glyph->offset * xscale;
                                float gy = cursorY + ((float)tpag->targetY - (float)font->spriteOriginYAdjust) * yscale;
                                blitSubRect(sw, (uint32_t)tpag->texturePageId,
                                            tpag->sourceX, tpag->sourceY, tpag->sourceWidth, tpag->sourceHeight,
                                            gx, gy, xscale, yscale, renderer->drawColor, renderer->drawAlpha);
                            }
                        }
                    }
                }
                cursorX += (float)glyph->shift * xscale;
                if (hasNext) cursorX += (float)TextUtils_getKerningOffset(glyph, nextCh) * xscale;
            }
            ch = nextCh;
            hasCh = hasNext;
        }

        cursorY += lineStride;
        lineStart = lineEnd;
        if (lineStart < textLen && text[lineStart] == '\r') lineStart++;
        if (lineStart < textLen && text[lineStart] == '\n') lineStart++;
    }
}

static void swDrawTextColor(Renderer *renderer, const char *text, float x, float y, float xscale, float yscale, float angleDeg, int32_t c1, MAYBE_UNUSED int32_t c2, MAYBE_UNUSED int32_t c3, MAYBE_UNUSED int32_t c4, float alpha, float lineSeparation) {
    uint32_t oldColor = renderer->drawColor;
    float oldAlpha = renderer->drawAlpha;
    renderer->drawColor = (uint32_t)c1;
    renderer->drawAlpha = alpha;
    swDrawText(renderer, text, x, y, xscale, yscale, angleDeg, lineSeparation);
    renderer->drawColor = oldColor;
    renderer->drawAlpha = oldAlpha;
}

static void swDrawTextUI(Renderer *renderer, const char *text, float x, float y, float xscale, float yscale, float angleDeg, int32_t c1, int32_t c2, int32_t c3, int32_t c4, float alpha, float lineSeparation) {
    swDrawTextColor(renderer, text, x, y, xscale, yscale, angleDeg, c1, c2, c3, c4, alpha, lineSeparation);
}

// ===[ Screen and Surface Management ]===

static void swFlush(MAYBE_UNUSED Renderer *renderer) {}

static void swClearScreen(Renderer *renderer, uint32_t color, MAYBE_UNUSED float alpha) {
    SWRenderer *sw = (SWRenderer *)renderer;
    uint16_t c565 = bgr_to_rgb565(color);
    int total = sw->targetWidth * sw->targetHeight;
    if (c565 == 0) {
        memset(sw->currentTarget, 0, (size_t)total * sizeof(uint16_t));
        return;
    }
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    uint16x8_t v_c = vdupq_n_u16(c565);
    int i = 0;
    for (; i <= total - 8; i += 8) {
        vst1q_u16(sw->currentTarget + i, v_c);
    }
    for (; i < total; i++) {
        sw->currentTarget[i] = c565;
    }
#else
    uint32_t c32 = ((uint32_t)c565 << 16) | c565;
    uint32_t *dst32 = (uint32_t *)sw->currentTarget;
    int total32 = total / 2;
    for (int i = 0; i < total32; i++) dst32[i] = c32;
    if (total & 1) sw->currentTarget[total - 1] = c565;
#endif
}

static int32_t swCreateSurface(Renderer *renderer, int32_t width, int32_t height) {
    SWRenderer *sw = (SWRenderer *)renderer;
    if (width <= 0 || height <= 0) return -1;

    int32_t slot = -1;
    for (uint32_t i = 0; i < sw->surfaceCount; i++) {
        if (!sw->surfaces[i].exists) {
            slot = (int32_t)i;
            break;
        }
    }
    if (slot < 0) {
        slot = (int32_t)sw->surfaceCount;
        ensureSurfaceCapacity(sw, sw->surfaceCount + 1);
    }

    SWSurface *s = &sw->surfaces[slot];
    s->width = width;
    s->height = height;
    s->pixels = (uint16_t *)safeCalloc((size_t)width * height, sizeof(uint16_t));
    s->exists = true;
    return slot;
}

static bool swSurfaceExists(Renderer *renderer, int32_t surfaceID) {
    SWRenderer *sw = (SWRenderer *)renderer;
    if (surfaceID < 0 || (uint32_t)surfaceID >= sw->surfaceCount) return false;
    return sw->surfaces[surfaceID].exists;
}

static bool swSetRenderTarget(Renderer *renderer, int32_t surfaceID, MAYBE_UNUSED bool implicitApplicationSurface) {
    SWRenderer *sw = (SWRenderer *)renderer;
    if (surfaceID == RENDER_TARGET_HOST_FRAMEBUFFER || surfaceID == renderer->runner->applicationSurfaceId) {
        sw->currentTarget = sw->framebuffer;
        sw->targetWidth = (sw->fbCurrentWidth > 0) ? sw->fbCurrentWidth : SCREEN_WIDTH;
        sw->targetHeight = (sw->fbCurrentHeight > 0) ? sw->fbCurrentHeight : SCREEN_HEIGHT;
        sw->activeSurfaceId = surfaceID;
        sw->clipX0 = 0;
        sw->clipY0 = 0;
        sw->clipX1 = sw->targetWidth;
        sw->clipY1 = sw->targetHeight;
        return true;
    }

    if (surfaceID < 0 || (uint32_t)surfaceID >= sw->surfaceCount || !sw->surfaces[surfaceID].exists) {
        return false;
    }

    SWSurface *s = &sw->surfaces[surfaceID];
    sw->currentTarget = s->pixels;
    sw->targetWidth = s->width;
    sw->targetHeight = s->height;
    sw->activeSurfaceId = surfaceID;
    sw->clipX0 = 0;
    sw->clipY0 = 0;
    sw->clipX1 = s->width;
    sw->clipY1 = s->height;
    return true;
}

static int32_t swEnsureApplicationSurface(Renderer *renderer, int32_t width, int32_t height) {
    (void)renderer;
    (void)width;
    (void)height;
    return RENDER_TARGET_HOST_FRAMEBUFFER;
}

static float swGetSurfaceWidth(Renderer *renderer, int32_t surfaceID) {
    SWRenderer *sw = (SWRenderer *)renderer;
    if (surfaceID == RENDER_TARGET_HOST_FRAMEBUFFER) return (float)SCREEN_WIDTH;
    if (surfaceID < 0 || (uint32_t)surfaceID >= sw->surfaceCount || !sw->surfaces[surfaceID].exists) return 0.0f;
    return (float)sw->surfaces[surfaceID].width;
}

static float swGetSurfaceHeight(Renderer *renderer, int32_t surfaceID) {
    SWRenderer *sw = (SWRenderer *)renderer;
    if (surfaceID == RENDER_TARGET_HOST_FRAMEBUFFER) return (float)SCREEN_HEIGHT;
    if (surfaceID < 0 || (uint32_t)surfaceID >= sw->surfaceCount || !sw->surfaces[surfaceID].exists) return 0.0f;
    return (float)sw->surfaces[surfaceID].height;
}

static void swDrawSurface(Renderer *renderer, int32_t surfaceID, int32_t srcLeft, int32_t srcTop, int32_t srcWidth, int32_t srcHeight, float x, float y, MAYBE_UNUSED float xscale, MAYBE_UNUSED float yscale, MAYBE_UNUSED float angleDeg, MAYBE_UNUSED uint32_t color, float alpha) {
    SWRenderer *sw = (SWRenderer *)renderer;
    if (surfaceID < 0 || (uint32_t)surfaceID >= sw->surfaceCount || !sw->surfaces[surfaceID].exists) return;
    if (alpha <= 0.0f) return;

    SWSurface *s = &sw->surfaces[surfaceID];
    float sx_pt, sy_pt;
    sw_transform_point(sw, x, y, &sx_pt, &sy_pt);
    int dx = (int)roundf(sx_pt);
    int dy = (int)roundf(sy_pt);
    int sx = srcLeft;
    int sy = srcTop;
    int w = srcWidth;
    int h = srcHeight;

    if (dx < sw->clipX0) { int d = sw->clipX0 - dx; sx += d; w -= d; dx = sw->clipX0; }
    if (dy < sw->clipY0) { int d = sw->clipY0 - dy; sy += d; h -= d; dy = sw->clipY0; }
    if (dx + w > sw->clipX1) w = sw->clipX1 - dx;
    if (dy + h > sw->clipY1) h = sw->clipY1 - dy;

    if (w <= 0 || h <= 0) return;

    int srcStride = s->width;
    int dstStride = sw->targetWidth;
    for (int r = 0; r < h; r++) {
        uint16_t *d_ptr = sw->currentTarget + (dy + r) * dstStride + dx;
        const uint16_t *s_ptr = s->pixels + (sy + r) * srcStride + sx;
        memcpy(d_ptr, s_ptr, w * sizeof(uint16_t));
    }
}

static void swDrawSurfaceColor(Renderer *renderer, int32_t surfaceID, int32_t srcLeft, int32_t srcTop, int32_t srcWidth, int32_t srcHeight, float x, float y, float xscale, float yscale, float angleDeg, uint32_t color1, MAYBE_UNUSED uint32_t color2, MAYBE_UNUSED uint32_t color3, MAYBE_UNUSED uint32_t color4, float alpha) {
    swDrawSurface(renderer, surfaceID, srcLeft, srcTop, srcWidth, srcHeight, x, y, xscale, yscale, angleDeg, color1, alpha);
}

static void swDrawSurfaceTiled(Renderer *renderer, int32_t surfaceID, float x, float y, float xscale, float yscale, float roomW, float roomH, uint32_t color, float alpha) {
    (void)renderer;
    (void)surfaceID;
    (void)x;
    (void)y;
    (void)xscale;
    (void)yscale;
    (void)roomW;
    (void)roomH;
    (void)color;
    (void)alpha;
}

static void swSurfaceResize(Renderer *renderer, int32_t surfaceID, int32_t width, int32_t height) {
    SWRenderer *sw = (SWRenderer *)renderer;
    if (surfaceID < 0 || (uint32_t)surfaceID >= sw->surfaceCount || !sw->surfaces[surfaceID].exists) return;
    SWSurface *s = &sw->surfaces[surfaceID];
    free(s->pixels);
    s->width = width;
    s->height = height;
    s->pixels = (uint16_t *)safeCalloc((size_t)width * height, sizeof(uint16_t));
}

static void swSurfaceFree(Renderer *renderer, int32_t surfaceID) {
    SWRenderer *sw = (SWRenderer *)renderer;
    if (surfaceID < 0 || (uint32_t)surfaceID >= sw->surfaceCount || !sw->surfaces[surfaceID].exists) return;
    free(sw->surfaces[surfaceID].pixels);
    sw->surfaces[surfaceID].pixels = NULL;
    sw->surfaces[surfaceID].exists = false;
}

static void swSurfaceCopy(Renderer *renderer, int32_t destSurfaceID, int32_t destX, int32_t destY, int32_t srcSurfaceID, int32_t srcX, int32_t srcY, int32_t srcW, int32_t srcH, MAYBE_UNUSED bool part) {
    SWRenderer *sw = (SWRenderer *)renderer;
    if (destSurfaceID < 0 || (uint32_t)destSurfaceID >= sw->surfaceCount || !sw->surfaces[destSurfaceID].exists) return;
    if (srcSurfaceID < 0 || (uint32_t)srcSurfaceID >= sw->surfaceCount || !sw->surfaces[srcSurfaceID].exists) return;

    SWSurface *dst = &sw->surfaces[destSurfaceID];
    SWSurface *src = &sw->surfaces[srcSurfaceID];

    for (int r = 0; r < srcH; r++) {
        int dy = destY + r;
        int sy = srcY + r;
        if (dy < 0 || dy >= dst->height || sy < 0 || sy >= src->height) continue;
        for (int c = 0; c < srcW; c++) {
            int dx = destX + c;
            int sx = srcX + c;
            if (dx < 0 || dx >= dst->width || sx < 0 || sx >= src->width) continue;
            dst->pixels[dy * dst->width + dx] = src->pixels[sy * src->width + sx];
        }
    }
}

static bool swSurfaceGetPixels(Renderer *renderer, int32_t surfaceID, uint8_t *outRGBA) {
    SWRenderer *sw = (SWRenderer *)renderer;
    if (!outRGBA) return false;
    const uint16_t *srcPixels = NULL;
    int w = 0, h = 0;

    if (surfaceID == RENDER_TARGET_HOST_FRAMEBUFFER) {
        srcPixels = sw->framebuffer;
        w = SCREEN_WIDTH;
        h = SCREEN_HEIGHT;
    } else if (surfaceID >= 0 && (uint32_t)surfaceID < sw->surfaceCount && sw->surfaces[surfaceID].exists) {
        srcPixels = sw->surfaces[surfaceID].pixels;
        w = sw->surfaces[surfaceID].width;
        h = sw->surfaces[surfaceID].height;
    } else {
        return false;
    }

    int total = w * h;
    for (int i = 0; i < total; i++) {
        uint16_t px = srcPixels[i];
        uint8_t r = ((px >> 11) & 0x1F) * 255 / 31;
        uint8_t g = ((px >> 5) & 0x3F) * 255 / 63;
        uint8_t b = (px & 0x1F) * 255 / 31;
        outRGBA[i * 4 + 0] = r;
        outRGBA[i * 4 + 1] = g;
        outRGBA[i * 4 + 2] = b;
        outRGBA[i * 4 + 3] = 255;
    }
    return true;
}

// ===[ Stubs for unused features ]===

static int32_t swCreateSpriteFromSurface(MAYBE_UNUSED Renderer *renderer, MAYBE_UNUSED int32_t surfaceID, MAYBE_UNUSED int32_t x, MAYBE_UNUSED int32_t y, MAYBE_UNUSED int32_t w, MAYBE_UNUSED int32_t h, MAYBE_UNUSED bool removeback, MAYBE_UNUSED bool smooth, MAYBE_UNUSED int32_t xorig, MAYBE_UNUSED int32_t yorig) { return -1; }
static void swDeleteSprite(MAYBE_UNUSED Renderer *renderer, MAYBE_UNUSED int32_t spriteIndex) {}

static uint32_t swFindOrAllocTexturePageSlot(SWRenderer *sw) {
    DataWin *dw = sw->base.dataWin;
    uint32_t startIndex = dw ? dw->txtr.count : 0;
    for (uint32_t i = startIndex; i < sw->pageCount; i++) {
        if (!sw->pages[i].loaded && sw->pages[i].rgb == NULL) {
            return i;
        }
    }
    uint32_t newPageId = sw->pageCount;
    uint32_t newCap = newPageId + 1;
    sw->pages = (SWTexturePage *)safeRealloc(sw->pages, newCap * sizeof(SWTexturePage));
    memset(&sw->pages[newPageId], 0, sizeof(SWTexturePage));
    sw->pageCount = newCap;
    return newPageId;
}

static int32_t swCreateBackgroundFromColor(Renderer *renderer, int32_t w, int32_t h, uint32_t bgrColor) {
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return -1;
    SWRenderer *sw = (SWRenderer *)renderer;
    DataWin *dw = renderer->dataWin;
    if (!dw) return -1;

    uint32_t pageId = swFindOrAllocTexturePageSlot(sw);
    SWTexturePage *p = &sw->pages[pageId];
    p->width = w;
    p->height = h;
    p->rgb = (uint16_t *)safeMalloc((size_t)w * (size_t)h * sizeof(uint16_t));
    p->alpha = (uint8_t *)safeMalloc((size_t)w * (size_t)h * sizeof(uint8_t));

    uint16_t c565 = bgr_to_rgb565(bgrColor);
    int totalPixels = w * h;
    for (int i = 0; i < totalPixels; i++) {
        p->rgb[i] = c565;
        p->alpha[i] = 255;
    }
    p->loaded = true;

    uint32_t tpagIndex = DataWin_allocTpagSlot(dw);
    TexturePageItem *tpag = &dw->tpag.items[tpagIndex];
    tpag->sourceX = 0;
    tpag->sourceY = 0;
    tpag->sourceWidth = (uint16_t)w;
    tpag->sourceHeight = (uint16_t)h;
    tpag->targetX = 0;
    tpag->targetY = 0;
    tpag->targetWidth = (uint16_t)w;
    tpag->targetHeight = (uint16_t)h;
    tpag->boundingWidth = (uint16_t)w;
    tpag->boundingHeight = (uint16_t)h;
    tpag->texturePageId = (int16_t)pageId;

    uint32_t bgIndex = DataWin_allocBackgroundSlot(dw);
    Background *bg = &dw->bgnd.backgrounds[bgIndex];
    bg->present = true;
    bg->transparent = false;
    bg->smooth = false;
    bg->preload = true;
    bg->tpagIndex = (int32_t)tpagIndex;

    logInfo("SWRenderer: Created dynamic background %u (%dx%d, color 0x%06X)\n", bgIndex, w, h, bgrColor);
    return (int32_t)bgIndex;
}

static void swDeleteBackground(Renderer *renderer, int32_t bgIndex) {
    SWRenderer *sw = (SWRenderer *)renderer;
    DataWin *dw = renderer->dataWin;
    if (!dw || bgIndex < 0 || (uint32_t)bgIndex >= dw->bgnd.count) return;
    if ((uint32_t)bgIndex < dw->bgnd.parsedCount) {
        logWarn("SWRenderer: Cannot delete data.win background %d\n", bgIndex);
        return;
    }

    Background *bg = &dw->bgnd.backgrounds[bgIndex];
    if (!bg->present) return;

    int32_t tpagIndex = bg->tpagIndex;
    if (tpagIndex >= 0 && (uint32_t)tpagIndex < dw->tpag.count) {
        TexturePageItem *tpag = &dw->tpag.items[tpagIndex];
        int16_t pageId = tpag->texturePageId;
        if (pageId >= 0 && (uint32_t)pageId < sw->pageCount) {
            SWTexturePage *p = &sw->pages[pageId];
            free(p->rgb);
            p->rgb = NULL;
            free(p->alpha);
            p->alpha = NULL;
            p->width = 0;
            p->height = 0;
            p->loaded = false;
        }
        tpag->texturePageId = -1;
    }

    bg->present = false;
    bg->tpagIndex = -1;
    logInfo("SWRenderer: Deleted background %d\n", bgIndex);
}
static BlendFactors swGpuGetBlendFactors(Renderer *renderer) { return ((SWRenderer *)renderer)->blendFactors; }
static int32_t swGpuGetBlendMode(Renderer *renderer) { return ((SWRenderer *)renderer)->blendMode; }
static void swGpuSetBlendMode(Renderer *renderer, int32_t mode) { ((SWRenderer *)renderer)->blendMode = mode; }
static void swGpuSetBlendModeExt(Renderer *renderer, int32_t sfactor, int32_t dfactor, int32_t sfactor_alpha, int32_t dfactor_alpha) {
    SWRenderer *sw = (SWRenderer *)renderer;
    sw->blendFactors.src = sfactor;
    sw->blendFactors.dst = dfactor;
    sw->blendFactors.srcAlpha = sfactor_alpha;
    sw->blendFactors.dstAlpha = dfactor_alpha;
}
static void swGpuSetBlendEnable(Renderer *renderer, bool enable) { ((SWRenderer *)renderer)->blendEnable = enable; }
static bool swGpuGetBlendEnable(Renderer *renderer) { return ((SWRenderer *)renderer)->blendEnable; }
static void swGpuSetTexFilter(MAYBE_UNUSED Renderer *renderer, MAYBE_UNUSED bool enable) {}
static void swGpuSetAlphaTestEnable(MAYBE_UNUSED Renderer *renderer, MAYBE_UNUSED bool enable) {}
static bool swGpuGetAlphaTestEnable(MAYBE_UNUSED Renderer *renderer) { return false; }
static void swGpuSetAlphaTestRef(MAYBE_UNUSED Renderer *renderer, MAYBE_UNUSED uint8_t ref) {}
static void swGpuSetColorWriteEnable(MAYBE_UNUSED Renderer *renderer, MAYBE_UNUSED bool r, MAYBE_UNUSED bool g, MAYBE_UNUSED bool b, MAYBE_UNUSED bool a) {}
static void swGpuGetColorWriteEnable(MAYBE_UNUSED Renderer *renderer, bool *r, bool *g, bool *b, bool *a) {
    if (r) *r = true;
    if (g) *g = true;
    if (b) *b = true;
    if (a) *a = true;
}
static void swGpuSetFog(MAYBE_UNUSED Renderer *renderer, MAYBE_UNUSED bool enable, MAYBE_UNUSED uint32_t color, MAYBE_UNUSED float start, MAYBE_UNUSED float end) {}
static void swDrawTiledPart(MAYBE_UNUSED Renderer *renderer, MAYBE_UNUSED int32_t tpagIndex, MAYBE_UNUSED int32_t srcX, MAYBE_UNUSED int32_t srcY, MAYBE_UNUSED int32_t srcW, MAYBE_UNUSED int32_t srcH, MAYBE_UNUSED float dstX, MAYBE_UNUSED float dstY, MAYBE_UNUSED float dstW, MAYBE_UNUSED float dstH, MAYBE_UNUSED uint32_t color, MAYBE_UNUSED float alpha) {}

static void swPrimitiveBegin(Renderer *renderer, int32_t primitiveType) {
    SWRenderer *sw = (SWRenderer *)renderer;
    sw->primType = primitiveType;
    sw->primTexture = -1;
    sw->primVertexCount = 0;
}

static void swPrimitiveBeginTexture(Renderer *renderer, int32_t primitiveType, int32_t texture) {
    SWRenderer *sw = (SWRenderer *)renderer;
    sw->primType = primitiveType;
    sw->primTexture = texture;
    sw->primVertexCount = 0;
}

static void swDrawVertex(Renderer *renderer, float x, float y, MAYBE_UNUSED float z, uint32_t color, float alpha, float u, float v) {
    SWRenderer *sw = (SWRenderer *)renderer;
    if (sw->primVertexCount >= sw->primVertexCapacity) {
        int32_t newCap = sw->primVertexCapacity * 2;
        if (newCap < 512) newCap = 512;
        SWVertex *newVerts = (SWVertex *)realloc(sw->primVertices, newCap * sizeof(SWVertex));
        if (!newVerts) return;
        sw->primVertices = newVerts;
        sw->primVertexCapacity = newCap;
    }

    float sx, sy;
    sw_transform_point(sw, x, y, &sx, &sy);

    SWVertex *vert = &sw->primVertices[sw->primVertexCount++];
    vert->x = sx;
    vert->y = sy;
    vert->u = u;
    vert->v = v;
    vert->color = color;
    vert->alpha = alpha;
}

static void swPrimitiveEnd(Renderer *renderer) {
    SWRenderer *sw = (SWRenderer *)renderer;
    int32_t count = sw->primVertexCount;
    if (count <= 0) return;

    SWVertex *v = sw->primVertices;
    int stride = sw->targetWidth;
    bool isTextured = (sw->primTexture > 0);

    switch (sw->primType) {
        case PRIMITIVE_POINTS: {
            for (int32_t i = 0; i < count; i++) {
                int px = (int)roundf(v[i].x);
                int py = (int)roundf(v[i].y);
                if (px >= sw->clipX0 && px < sw->clipX1 && py >= sw->clipY0 && py < sw->clipY1) {
                    uint16_t c565 = bgr_to_rgb565(v[i].color);
                    uint8_t a = (uint8_t)(v[i].alpha * 255.0f);
                    if (a == 0) continue;
                    uint16_t *ptr = sw->currentTarget + py * stride + px;
                    if (a == 255 || !sw->blendEnable) *ptr = c565;
                    else *ptr = blend_pixel_rgb565(*ptr, c565, a);
                }
            }
            break;
        }
        case PRIMITIVE_LINES: {
            for (int32_t i = 0; i + 1 < count; i += 2) {
                sw_draw_line_internal(sw, (int)roundf(v[i].x), (int)roundf(v[i].y),
                                          (int)roundf(v[i+1].x), (int)roundf(v[i+1].y),
                                          v[i].color, v[i].alpha);
            }
            break;
        }
        case PRIMITIVE_LINE_STRIP: {
            for (int32_t i = 0; i + 1 < count; i++) {
                sw_draw_line_internal(sw, (int)roundf(v[i].x), (int)roundf(v[i].y),
                                          (int)roundf(v[i+1].x), (int)roundf(v[i+1].y),
                                          v[i+1].color, v[i+1].alpha);
            }
            break;
        }
        case PRIMITIVE_TRIANGLES: {
            for (int32_t i = 0; i + 2 < count; i += 3) {
                if (isTextured) {
                    sw_rasterize_triangle_textured(sw, &v[i], &v[i+1], &v[i+2]);
                } else {
                    sw_rasterize_triangle(sw, v[i].x, v[i].y, v[i+1].x, v[i+1].y, v[i+2].x, v[i+2].y,
                                          v[i+2].color, v[i+2].alpha);
                }
            }
            break;
        }
        case PRIMITIVE_TRIANGLE_STRIP: {
            for (int32_t i = 0; i + 2 < count; i++) {
                if (isTextured) {
                    sw_rasterize_triangle_textured(sw, &v[i], &v[i+1], &v[i+2]);
                } else {
                    sw_rasterize_triangle(sw, v[i].x, v[i].y, v[i+1].x, v[i+1].y, v[i+2].x, v[i+2].y,
                                          v[i+2].color, v[i+2].alpha);
                }
            }
            break;
        }
        case PRIMITIVE_TRIANGLE_FAN: {
            for (int32_t i = 1; i + 1 < count; i++) {
                if (isTextured) {
                    sw_rasterize_triangle_textured(sw, &v[0], &v[i], &v[i+1]);
                } else {
                    sw_rasterize_triangle(sw, v[0].x, v[0].y, v[i].x, v[i].y, v[i+1].x, v[i+1].y,
                                          v[i+1].color, v[i+1].alpha);
                }
            }
            break;
        }
        default:
            break;
    }

    sw->primVertexCount = 0;
    sw->primTexture = -1;
}

static void swGpuSetShader(MAYBE_UNUSED Renderer *renderer, MAYBE_UNUSED int32_t shaderIndex) {}
static void swGpuResetShader(MAYBE_UNUSED Renderer *renderer) {}
static int32_t swShaderGetUniform(MAYBE_UNUSED Renderer *renderer, MAYBE_UNUSED int32_t shaderIndex, MAYBE_UNUSED char *uniform) { return -1; }
static int32_t swShaderGetSamplerIndex(MAYBE_UNUSED Renderer *renderer, MAYBE_UNUSED int32_t shaderIndex, MAYBE_UNUSED char *uniform) { return -1; }
static void swShaderSetUniformF(MAYBE_UNUSED Renderer *renderer, MAYBE_UNUSED int32_t handle, MAYBE_UNUSED int32_t count, MAYBE_UNUSED float v1, MAYBE_UNUSED float v2, MAYBE_UNUSED float v3, MAYBE_UNUSED float v4) {}
static void swShaderSetUniformFArray(MAYBE_UNUSED Renderer *renderer, MAYBE_UNUSED int32_t handle, MAYBE_UNUSED float *values, MAYBE_UNUSED uint32_t count) {}
static void swShaderSetUniformI(MAYBE_UNUSED Renderer *renderer, MAYBE_UNUSED int32_t handle, MAYBE_UNUSED int32_t count, MAYBE_UNUSED int32_t v1, MAYBE_UNUSED int32_t v2, MAYBE_UNUSED int32_t v3, MAYBE_UNUSED int32_t v4) {}
static bool swShaderIsCompiled(MAYBE_UNUSED Renderer *renderer, MAYBE_UNUSED int32_t shader) { return false; }
static bool swShadersSupported(void) { return false; }

static uint32_t swSpriteGetTexture(MAYBE_UNUSED Renderer *renderer, int32_t tpagIndex) {
    return tpagIndex >= 0 ? (uint32_t)(tpagIndex + 1) : 0;
}
static uint32_t swSurfaceGetTexture(MAYBE_UNUSED Renderer *renderer, int32_t surfaceID) {
    return surfaceID >= 0 ? (uint32_t)(surfaceID + 1000) : 0;
}
static float swTextureGetTexelWidth(Renderer *renderer, uint32_t texID) {
    SWRenderer *sw = (SWRenderer *)renderer;
    DataWin *dw = renderer->dataWin;
    if (texID > 0 && dw && (texID - 1) < dw->tpag.count) {
        int16_t pageId = dw->tpag.items[texID - 1].texturePageId;
        if (pageId >= 0) {
            ensureTextureLoaded(sw, (uint32_t)pageId);
            if ((uint32_t)pageId < sw->pageCount && sw->pages[pageId].width > 0) {
                return 1.0f / (float)sw->pages[pageId].width;
            }
        }
    }
    if (texID > 0 && texID <= sw->pageCount && sw->pages[texID - 1].width > 0) {
        return 1.0f / (float)sw->pages[texID - 1].width;
    }
    return 1.0f / 2048.0f;
}
static float swTextureGetTexelHeight(Renderer *renderer, uint32_t texID) {
    SWRenderer *sw = (SWRenderer *)renderer;
    DataWin *dw = renderer->dataWin;
    if (texID > 0 && dw && (texID - 1) < dw->tpag.count) {
        int16_t pageId = dw->tpag.items[texID - 1].texturePageId;
        if (pageId >= 0) {
            ensureTextureLoaded(sw, (uint32_t)pageId);
            if ((uint32_t)pageId < sw->pageCount && sw->pages[pageId].height > 0) {
                return 1.0f / (float)sw->pages[pageId].height;
            }
        }
    }
    if (texID > 0 && texID <= sw->pageCount && sw->pages[texID - 1].height > 0) {
        return 1.0f / (float)sw->pages[texID - 1].height;
    }
    return 1.0f / 2048.0f;
}
static bool swTextureGetUVs(MAYBE_UNUSED Renderer *renderer, MAYBE_UNUSED uint32_t texID, float *outUVs) {
    if (!outUVs) return false;
    DataWin *dw = renderer->dataWin;
    if (texID > 0 && dw && (texID - 1) < dw->tpag.count) {
        TexturePageItem *tpag = &dw->tpag.items[texID - 1];
        float invW = renderer->vtable->textureGetTexelWidth(renderer, texID);
        float invH = renderer->vtable->textureGetTexelHeight(renderer, texID);
        outUVs[0] = (float)tpag->sourceX * invW;
        outUVs[1] = (float)tpag->sourceY * invH;
        outUVs[2] = (float)(tpag->sourceX + tpag->sourceWidth) * invW;
        outUVs[3] = (float)(tpag->sourceY + tpag->sourceHeight) * invH;
        return true;
    }
    outUVs[0] = 0.0f; outUVs[1] = 0.0f; outUVs[2] = 1.0f; outUVs[3] = 1.0f;
    return true;
}
static void swTextureSetStage(MAYBE_UNUSED Renderer *renderer, MAYBE_UNUSED int32_t slot, MAYBE_UNUSED uint32_t texID) {}
static void swSetMatrix(Renderer *renderer, int32_t matrixType, Matrix4f matrix) {
    if (matrixType >= 0 && matrixType < MATRICES_MAX) renderer->gmlMatrices[matrixType] = matrix;
}

// ===[ Factory Function ]===

Renderer* SWRenderer_create(void) {
    SWRenderer *sw = (SWRenderer *)safeCalloc(1, sizeof(SWRenderer));
    sw->base.vtable = &swVtable;

    swVtable.init = swInit;
    swVtable.destroy = swDestroy;
    swVtable.beginFrame = swBeginFrame;
    swVtable.endFrameInit = swEndFrameInit;
    swVtable.endFrameEnd = swEndFrameEnd;
    swVtable.beginView = swBeginView;
    swVtable.endView = swEndView;
    swVtable.applyProjection = swApplyProjection;
    swVtable.beginGUI = swBeginGUI;
    swVtable.setGuiProjection = swSetGuiProjection;
    swVtable.endGUI = swEndGUI;
    swVtable.drawSprite = swDrawSprite;
    swVtable.drawSpritePart = swDrawSpritePart;
    swVtable.drawSpritePartColor = swDrawSpritePartColor;
    swVtable.drawSpritePos = swDrawSpritePos;
    swVtable.drawRectangle = swDrawRectangle;
    swVtable.drawRectangleColor = swDrawRectangleColor;
    swVtable.drawLine = swDrawLine;
    swVtable.drawTriangle = swDrawTriangle;
    swVtable.drawLineColor = swDrawLineColor;
    swVtable.drawCircleColor = swDrawCircleColor;
    swVtable.drawText = swDrawText;
    swVtable.drawTextColor = swDrawTextColor;
    swVtable.drawTextUI = swDrawTextUI;
    swVtable.drawSpriteTiled = swDrawSpriteTiled;
    swVtable.drawTile = NULL; // Let Renderer_drawTile use default drawSpritePart
    swVtable.flush = swFlush;
    swVtable.clearScreen = swClearScreen;
    swVtable.createSpriteFromSurface = swCreateSpriteFromSurface;
    swVtable.deleteSprite = swDeleteSprite;
    swVtable.createBackgroundFromColor = swCreateBackgroundFromColor;
    swVtable.deleteBackground = swDeleteBackground;
    swVtable.gpuGetBlendFactors = swGpuGetBlendFactors;
    swVtable.gpuGetBlendMode = swGpuGetBlendMode;
    swVtable.gpuSetBlendMode = swGpuSetBlendMode;
    swVtable.gpuSetBlendModeExt = swGpuSetBlendModeExt;
    swVtable.gpuSetBlendEnable = swGpuSetBlendEnable;
    swVtable.gpuGetBlendEnable = swGpuGetBlendEnable;
    swVtable.gpuSetTexFilter = swGpuSetTexFilter;
    swVtable.gpuSetAlphaTestEnable = swGpuSetAlphaTestEnable;
    swVtable.gpuGetAlphaTestEnable = swGpuGetAlphaTestEnable;
    swVtable.gpuSetAlphaTestRef = swGpuSetAlphaTestRef;
    swVtable.gpuSetColorWriteEnable = swGpuSetColorWriteEnable;
    swVtable.gpuGetColorWriteEnable = swGpuGetColorWriteEnable;
    swVtable.gpuSetFog = swGpuSetFog;
    swVtable.primitiveBegin = swPrimitiveBegin;
    swVtable.primitiveBeginTexture = swPrimitiveBeginTexture;
    swVtable.primitiveEnd = swPrimitiveEnd;
    swVtable.drawVertex = swDrawVertex;
    swVtable.createSurface = swCreateSurface;
    swVtable.surfaceExists = swSurfaceExists;
    swVtable.setRenderTarget = swSetRenderTarget;
    swVtable.ensureApplicationSurface = swEnsureApplicationSurface;
    swVtable.getSurfaceWidth = swGetSurfaceWidth;
    swVtable.getSurfaceHeight = swGetSurfaceHeight;
    swVtable.drawSurface = swDrawSurface;
    swVtable.drawSurfaceColor = swDrawSurfaceColor;
    swVtable.drawSurfaceTiled = swDrawSurfaceTiled;
    swVtable.surfaceResize = swSurfaceResize;
    swVtable.surfaceFree = swSurfaceFree;
    swVtable.surfaceCopy = swSurfaceCopy;
    swVtable.surfaceGetPixels = swSurfaceGetPixels;
    swVtable.drawTiledPart = swDrawTiledPart;
    swVtable.gpuSetShader = swGpuSetShader;
    swVtable.gpuResetShader = swGpuResetShader;
    swVtable.shaderGetUniform = swShaderGetUniform;
    swVtable.shaderGetSamplerIndex = swShaderGetSamplerIndex;
    swVtable.shaderSetUniformF = swShaderSetUniformF;
    swVtable.shaderSetUniformFArray = swShaderSetUniformFArray;
    swVtable.shaderSetUniformI = swShaderSetUniformI;
    swVtable.spriteGetTexture = swSpriteGetTexture;
    swVtable.surfaceGetTexture = swSurfaceGetTexture;
    swVtable.textureGetTexelWidth = swTextureGetTexelWidth;
    swVtable.textureGetTexelHeight = swTextureGetTexelHeight;
    swVtable.textureGetUVs = swTextureGetUVs;
    swVtable.textureSetStage = swTextureSetStage;
    swVtable.shaderIsCompiled = swShaderIsCompiled;
    swVtable.shadersSupported = swShadersSupported;
    swVtable.setMatrix = swSetMatrix;

    sw->base.drawColor = 0xFFFFFF;
    sw->base.drawAlpha = 1.0f;
    sw->base.drawFont = -1;
    sw->base.drawHalign = 0;
    sw->base.drawValign = 0;
    sw->base.circlePrecision = 24;
    sw->base.currentShader = -1;
    Matrix4f_identity(&sw->base.gmlMatrices[MATRIX_WORLD]);

    sw->blendEnable = true;
    sw->blendMode = bm_normal;
    sw->blendFactors.src = bm_src_alpha;
    sw->blendFactors.dst = bm_inv_src_alpha;
    sw->blendFactors.srcAlpha = bm_src_alpha;
    sw->blendFactors.dstAlpha = bm_inv_src_alpha;

    return (Renderer *)sw;
}
