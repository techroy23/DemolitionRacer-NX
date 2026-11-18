#ifndef DRACER_FRAMEBUFFER_BLIT_H
#define DRACER_FRAMEBUFFER_BLIT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int32_t x, y, width, height;
} FramebufferSource;

typedef FramebufferSource FramebufferViewport;
FramebufferViewport framebuffer_viewport(int32_t width, int32_t height, unsigned upscale);
void framebuffer_draw_fps(uint32_t *destination, size_t stride, int32_t width, int32_t height, unsigned fps);
bool framebuffer_blit_scaled(uint32_t *destination, size_t stride, int32_t width, int32_t height,
                             const uint32_t *source, int32_t source_width, int32_t source_height,
                             FramebufferSource crop, unsigned upscale, uint32_t *visible_colors);

bool framebuffer_blit_4_3(uint32_t *destination, size_t stride, int32_t width, int32_t height,
                          const uint32_t *source, int32_t source_width, int32_t source_height,
                          FramebufferSource crop, uint32_t *visible_colors);

#endif
