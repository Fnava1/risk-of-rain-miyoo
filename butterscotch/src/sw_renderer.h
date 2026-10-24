#ifndef _BS_SW_RENDERER_H_
#define _BS_SW_RENDERER_H_

#include "renderer.h"

#ifdef __cplusplus
extern "C" {
#endif

Renderer* SWRenderer_create(void);
void SWRenderer_clearFrameBuffer(Renderer* renderer, uint32_t color);
uint16_t* SWRenderer_getFramebuffer(Renderer* renderer);
int SWRenderer_getTargetWidth(Renderer* renderer);
int SWRenderer_getTargetHeight(Renderer* renderer);
void SWRenderer_flipBackbuffer(Renderer* renderer);

#ifdef __cplusplus
}
#endif

#endif // _BS_SW_RENDERER_H_
