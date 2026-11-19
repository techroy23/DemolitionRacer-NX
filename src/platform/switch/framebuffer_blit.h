#ifndef DRACER_FRAMEBUFFER_BLIT_H
#define DRACER_FRAMEBUFFER_BLIT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int32_t x, y, width, height;
} FramebufferSource;

typedef FramebufferSource FramebufferViewport;
typedef enum {
    FRAMEBUFFER_FILTER_NEAREST,
    FRAMEBUFFER_FILTER_BILINEAR
} FramebufferFilter;

bool framebuffer_blit_filtered(uint32_t *destination, size_t stride, int32_t width, int32_t height,
                               const uint32_t *source, int32_t source_width, int32_t source_height,
                               FramebufferSource crop, unsigned upscale, FramebufferFilter filter,
                               uint32_t *visible_colors);
// Writes only [first_row, end_row) within the full framebuffer, excluding stride padding.
// Disjoint bands may run concurrently with a shared read-only source and separate,
// optional visible_colors outputs (RGB OR for the band; zero for an empty band).
// Source, destination and visible_colors must not alias. Invalid inputs write nothing.
bool framebuffer_blit_bilinear_band(uint32_t *destination, size_t stride, int32_t width, int32_t height,
                                    const uint32_t *source, int32_t source_width, int32_t source_height,
                                    FramebufferSource crop, unsigned upscale, int32_t first_row,
                                    int32_t end_row, uint32_t *visible_colors);
int32_t framebuffer_integer_scale(int32_t output_width, int32_t input_width);
FramebufferViewport framebuffer_viewport(int32_t width, int32_t height, unsigned upscale);
FramebufferViewport framebuffer_movie_viewport(int32_t width, int32_t height,
                                                int32_t movie_width, int32_t movie_height);
bool framebuffer_blit_movie(uint32_t *destination, size_t stride, int32_t width, int32_t height,
                            const uint32_t *source, int32_t source_width, int32_t source_height,
                            FramebufferSource crop, uint32_t *visible_colors);
void framebuffer_draw_fps(uint32_t *destination, size_t stride, int32_t width, int32_t height, unsigned fps);
bool framebuffer_blit_scaled(uint32_t *destination, size_t stride, int32_t width, int32_t height,
                             const uint32_t *source, int32_t source_width, int32_t source_height,
                             FramebufferSource crop, unsigned upscale, uint32_t *visible_colors);

bool framebuffer_blit_4_3(uint32_t *destination, size_t stride, int32_t width, int32_t height,
                          const uint32_t *source, int32_t source_width, int32_t source_height,
                          FramebufferSource crop, uint32_t *visible_colors);

#endif
