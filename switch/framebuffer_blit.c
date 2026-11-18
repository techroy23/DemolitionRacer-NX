#include "framebuffer_blit.h"
#include <string.h>

FramebufferViewport framebuffer_viewport(int32_t width, int32_t height, unsigned upscale)
{
    if (width < 4 || height < 3)
        return (FramebufferViewport){0};
    int32_t scale = width / 4 < height / 3 ? width / 4 : height / 3;
    if (upscale >= 2 && upscale <= 4 && scale > (int32_t)(80 * upscale))
        scale = (int32_t)(80 * upscale);
    return (FramebufferViewport){(width - 4 * scale) / 2, (height - 3 * scale) / 2, 4 * scale, 3 * scale};
}

bool framebuffer_blit_scaled(uint32_t *destination, size_t stride, int32_t width, int32_t height,
                          const uint32_t *source, int32_t source_width, int32_t source_height,
                          FramebufferSource crop, unsigned upscale, uint32_t *visible_colors)
{
    if (!destination || !source || width < 4 || height < 3 || stride < (size_t)width ||
        stride > SIZE_MAX / sizeof(*destination) / (size_t)height ||
        source_width <= 0 || source_height <= 0 ||
        (size_t)source_width > SIZE_MAX / sizeof(*source) / (size_t)source_height ||
        crop.x < 0 || crop.y < 0 || crop.width <= 0 || crop.height <= 0 ||
        crop.width > source_width || crop.height > source_height ||
        crop.x > source_width - crop.width || crop.y > source_height - crop.height)
        return false;

    FramebufferViewport viewport = framebuffer_viewport(width, height, upscale);
    int32_t active_width = viewport.width, active_height = viewport.height;
    int32_t left = viewport.x, top = viewport.y;
    const uint32_t opaque_black = 0xff000000u;
    uint32_t colors = 0;
    int32_t source_y = 0, vertical_remainder = 0, previous_source_y = -1;
    for (int32_t y = 0; y < height; ++y)
    {
        uint32_t *row = destination + (size_t)y * stride;
        if (y < top || y - top >= active_height)
        {
            for (int32_t x = 0; x < width; ++x)
                row[x] = opaque_black;
            continue;
        }
        if (previous_source_y == source_y)
            memcpy(row, row - stride, (size_t)width * sizeof(*row));
        else
        {
            const uint32_t *source_row = source + (size_t)(crop.y + source_y) * source_width + crop.x;
            for (int32_t x = 0; x < left; ++x)
                row[x] = opaque_black;
            uint64_t horizontal_remainder = 0;
            int32_t source_x = 0;
            const int32_t step_x = crop.width / active_width;
            const int32_t remainder_x = crop.width % active_width;
            for (int32_t x = 0; x < active_width; ++x)
            {
                uint32_t pixel = source_row[source_x];
                colors |= pixel;
                row[left + x] = pixel | opaque_black;  // Already BGRA in little-endian memory.
                source_x += step_x;
                horizontal_remainder += remainder_x;
                if (horizontal_remainder >= (uint32_t)active_width)
                {
                    horizontal_remainder -= active_width;
                    ++source_x;
                }
            }
            for (int32_t x = left + active_width; x < width; ++x)
                row[x] = opaque_black;
        }
        previous_source_y = source_y;
        // The arithmetic below is bounded by the nonnegative crop and active dimensions.
        uint64_t next = (uint64_t)vertical_remainder + crop.height;
        source_y += (int32_t)(next / active_height);
        vertical_remainder = (int32_t)(next % active_height);
    }
    if (visible_colors)
        *visible_colors = colors & 0x00ffffffu;
    return true;
}

bool framebuffer_blit_4_3(uint32_t *destination, size_t stride, int32_t width, int32_t height,
                          const uint32_t *source, int32_t source_width, int32_t source_height,
                          FramebufferSource crop, uint32_t *visible_colors)
{
    return framebuffer_blit_scaled(destination, stride, width, height, source, source_width, source_height,
                                   crop, 0, visible_colors);
}

void framebuffer_draw_fps(uint32_t *destination, size_t stride, int32_t width, int32_t height, unsigned fps)
{
    static const uint8_t glyphs[][5] = {
        {7,5,5,5,7}, {2,6,2,2,7}, {7,1,7,4,7}, {7,1,7,1,7}, {5,5,7,1,1},
        {7,4,7,1,7}, {7,4,7,5,7}, {7,1,1,1,1}, {7,5,7,5,7}, {7,5,7,1,7},
        {7,4,6,4,4}, {6,5,6,4,4}, {7,4,7,1,7}, {0,0,0,0,0}
    };
    if (!destination || width < 70 || height < 30 || stride < (size_t)width)
        return;
    if (fps > 999) fps = 999;
    unsigned label[] = {10, 11, 12, 13, fps >= 100 ? fps / 100 : 13,
                        fps >= 10 ? fps / 10 % 10 : 13, fps % 10};
    for (unsigned y = 10; y < 28; ++y)
        for (unsigned x = 10; x < 70; ++x)
            destination[y * stride + x] = 0xff000000u;
    for (unsigned character = 0; character < sizeof(label) / sizeof(*label); ++character)
        for (unsigned y = 0; y < 10; ++y)
            for (unsigned x = 0; x < 6; ++x)
                if (glyphs[label[character]][y / 2] & (4u >> (x / 2)))
                    destination[(y + 14) * stride + x + 12 + character * 8] = 0xffffffffu;
}
