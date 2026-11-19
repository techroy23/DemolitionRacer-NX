#include "framebuffer_blit.h"
#include <string.h>

int32_t framebuffer_integer_scale(int32_t output_width, int32_t input_width)
{
    return input_width > 0 && output_width > 0 && output_width % input_width == 0 ? output_width / input_width : 0;
}

FramebufferViewport framebuffer_viewport(int32_t width, int32_t height, unsigned upscale)
{
    if (width < 4 || height < 3)
        return (FramebufferViewport){0};
    int32_t scale = width / 4 < height / 3 ? width / 4 : height / 3;
    if (upscale >= 2 && upscale <= 4 && scale > (int32_t)(80 * upscale))
        scale = (int32_t)(80 * upscale);
    return (FramebufferViewport){(width - 4 * scale) / 2, (height - 3 * scale) / 2, 4 * scale, 3 * scale};
}

FramebufferViewport framebuffer_movie_viewport(int32_t width, int32_t height,
                                                int32_t movie_width, int32_t movie_height)
{
    if (width <= 0 || height <= 0 || movie_width <= 0 || movie_height <= 0)
        return (FramebufferViewport){0};
    int32_t active_width = width, active_height = height;
    if ((int64_t)width * movie_height > (int64_t)height * movie_width)
        active_width = (int32_t)((int64_t)height * movie_width / movie_height);
    else
        active_height = (int32_t)((int64_t)width * movie_height / movie_width);
    return (FramebufferViewport){(width - active_width) / 2, (height - active_height) / 2,
                                 active_width, active_height};
}

static uint32_t blend_color(uint32_t first, uint32_t second, uint32_t weight)
{
    const uint32_t inverse = 256u - weight;
    const uint32_t red_blue = ((first & 0xff00ffu) * inverse + (second & 0xff00ffu) * weight + 0x800080u) >> 8;
    const uint32_t green = ((first & 0x00ff00u) * inverse + (second & 0x00ff00u) * weight + 0x008000u) >> 8;
    return (red_blue & 0xff00ffu) | (green & 0x00ff00u);
}

static uint32_t blit_bilinear(uint32_t *destination, size_t stride, int32_t width, int32_t height,
                             const uint32_t *source, int32_t source_stride, FramebufferSource crop,
                             FramebufferViewport viewport)
{
    // Map output pixel centers onto source pixel centers. Clamp to the crop, not VRAM.
    enum { fraction_bits = 24, fraction_unit = 1 << fraction_bits };
    const int64_t horizontal_step = (int64_t)crop.width * fraction_unit / viewport.width;
    const int64_t vertical_step = (int64_t)crop.height * fraction_unit / viewport.height;
    const int64_t horizontal_limit = (int64_t)(crop.width - 1) * fraction_unit;
    const int64_t vertical_limit = (int64_t)(crop.height - 1) * fraction_unit;
    int64_t vertical = (vertical_step - fraction_unit) / 2;
    uint32_t colors = 0;
    for (int32_t y = 0; y < height; ++y)
    {
        uint32_t *row = destination + (size_t)y * stride;
        if (y < viewport.y || y - viewport.y >= viewport.height)
        {
            for (int32_t x = 0; x < width; ++x) row[x] = 0xff000000u;
            continue;
        }
        const int64_t clamped_vertical = vertical < 0 ? 0 : vertical > vertical_limit ? vertical_limit : vertical;
        const int32_t top = (int32_t)(clamped_vertical >> fraction_bits);
        const uint32_t vertical_weight = ((clamped_vertical & (fraction_unit - 1)) + (1 << 15)) >> 16;
        const uint32_t *upper = source + (size_t)(crop.y + top) * source_stride + crop.x;
        const uint32_t *lower = upper + (top < crop.height - 1 ? source_stride : 0);
        int64_t horizontal = (horizontal_step - fraction_unit) / 2;
        for (int32_t x = 0; x < viewport.x; ++x) row[x] = 0xff000000u;
        for (int32_t x = 0; x < viewport.width; ++x, horizontal += horizontal_step)
        {
            const int64_t clamped_horizontal = horizontal < 0 ? 0 : horizontal > horizontal_limit ? horizontal_limit : horizontal;
            const int32_t left = (int32_t)(clamped_horizontal >> fraction_bits);
            const int32_t right = left + (left < crop.width - 1);
            const uint32_t horizontal_weight = ((clamped_horizontal & (fraction_unit - 1)) + (1 << 15)) >> 16;
            const uint32_t color = blend_color(blend_color(upper[left], upper[right], horizontal_weight),
                                               blend_color(lower[left], lower[right], horizontal_weight), vertical_weight);
            colors |= color;
            row[viewport.x + x] = color | 0xff000000u;
        }
        for (int32_t x = viewport.x + viewport.width; x < width; ++x) row[x] = 0xff000000u;
        vertical += vertical_step;
    }
    return colors;
}

static bool framebuffer_blit_viewport(uint32_t *destination, size_t stride, int32_t width, int32_t height,
                                      const uint32_t *source, int32_t source_width, int32_t source_height,
                                      FramebufferSource crop, FramebufferViewport viewport,
                                      FramebufferFilter filter, uint32_t *visible_colors)
{
    if (!destination || !source || width < 4 || height < 3 || stride < (size_t)width ||
        stride > SIZE_MAX / sizeof(*destination) / (size_t)height ||
        source_width <= 0 || source_height <= 0 ||
        (size_t)source_width > SIZE_MAX / sizeof(*source) / (size_t)source_height ||
        crop.x < 0 || crop.y < 0 || crop.width <= 0 || crop.height <= 0 ||
        crop.width > source_width || crop.height > source_height ||
        crop.x > source_width - crop.width || crop.y > source_height - crop.height ||
        viewport.width <= 0 || viewport.height <= 0)
        return false;

    if (filter == FRAMEBUFFER_FILTER_BILINEAR)
    {
        const uint32_t colors = blit_bilinear(destination, stride, width, height, source, source_width, crop, viewport);
        if (visible_colors) *visible_colors = colors;
        return true;
    }

    int32_t active_width = viewport.width, active_height = viewport.height;
    int32_t left = viewport.x, top = viewport.y;
    int32_t integer_scale = framebuffer_integer_scale(active_width, crop.width);
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
            if (integer_scale == 3)
            {
                uint32_t *output = row + left;
                for (int32_t x = 0; x < crop.width; ++x)
                {
                    uint32_t pixel = source_row[x];
                    colors |= pixel;
                    pixel |= opaque_black;
                    output[0] = output[1] = output[2] = pixel;
                    output += 3;
                }
            }
            else
            {
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

bool framebuffer_blit_filtered(uint32_t *destination, size_t stride, int32_t width, int32_t height,
                               const uint32_t *source, int32_t source_width, int32_t source_height,
                               FramebufferSource crop, unsigned upscale, FramebufferFilter filter,
                               uint32_t *visible_colors)
{
    return framebuffer_blit_viewport(destination, stride, width, height, source, source_width, source_height,
                                     crop, framebuffer_viewport(width, height, upscale), filter, visible_colors);
}

bool framebuffer_blit_scaled(uint32_t *destination, size_t stride, int32_t width, int32_t height,
                             const uint32_t *source, int32_t source_width, int32_t source_height,
                             FramebufferSource crop, unsigned upscale, uint32_t *visible_colors)
{
    return framebuffer_blit_filtered(destination, stride, width, height, source, source_width, source_height,
                                     crop, upscale, FRAMEBUFFER_FILTER_NEAREST, visible_colors);
}

bool framebuffer_blit_movie(uint32_t *destination, size_t stride, int32_t width, int32_t height,
                            const uint32_t *source, int32_t source_width, int32_t source_height,
                            FramebufferSource crop, uint32_t *visible_colors)
{
    return framebuffer_blit_viewport(destination, stride, width, height, source, source_width, source_height,
                                     crop, framebuffer_movie_viewport(width, height, crop.width, crop.height),
                                     FRAMEBUFFER_FILTER_NEAREST, visible_colors);
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
