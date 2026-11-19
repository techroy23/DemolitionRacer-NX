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

enum { fraction_bits = 24, fraction_unit = 1 << fraction_bits };

typedef struct {
    int32_t index;
    uint32_t weight;
} BilinearSample;

// Map output pixel centers onto source pixel centers. Clamp to the crop, not VRAM.
static BilinearSample bilinear_sample(int64_t position, int64_t limit)
{
    const int64_t clamped = position < 0 ? 0 : position > limit ? limit : position;
    return (BilinearSample){(int32_t)(clamped >> fraction_bits),
                            ((clamped & (fraction_unit - 1)) + (1 << 15)) >> 16};
}

static void blend_horizontal(uint32_t *cache, const uint32_t *source, int32_t last,
                             const BilinearSample *samples, int32_t count)
{
    for (int32_t x = 0; x < count; ++x)
    {
        const int32_t left = samples[x].index;
        const int32_t right = left + (left < last);
        cache[x] = blend_color(source[left], source[right], samples[x].weight);
    }
}

static uint32_t blit_bilinear_active(uint32_t *destination, size_t stride, const uint32_t *source,
                                    int32_t source_stride, FramebufferSource crop, FramebufferViewport viewport,
                                    int32_t first_row, int32_t end_row)
{
    // 16 KiB of fixed scratch per call (one x map and two RGB rows). Wider viewports
    // use horizontal tiles to bound both serial and worker stacks without allocating.
    enum { tile_width = 1024 };
    BilinearSample samples[tile_width];
    uint32_t cache[2][tile_width];
    const int64_t horizontal_step = (int64_t)crop.width * fraction_unit / viewport.width;
    const int64_t vertical_step = (int64_t)crop.height * fraction_unit / viewport.height;
    const int64_t horizontal_limit = (int64_t)(crop.width - 1) * fraction_unit;
    const int64_t vertical_limit = (int64_t)(crop.height - 1) * fraction_unit;
    const int64_t first_vertical = (vertical_step - fraction_unit) / 2 +
                                   (first_row - viewport.y) * vertical_step;
    int64_t horizontal = (horizontal_step - fraction_unit) / 2;
    const uint32_t *crop_start = source + (size_t)crop.y * source_stride + crop.x;
    uint32_t colors = 0;
    for (int32_t start = 0; start < viewport.width;)
    {
        const int32_t remaining = viewport.width - start;
        const int32_t count = remaining < tile_width ? remaining : tile_width;
        for (int32_t x = 0; x < count; ++x, horizontal += horizontal_step)
            samples[x] = bilinear_sample(horizontal, horizontal_limit);

        // Adjacent source rows have opposite parity. Keep each in its parity slot
        // so a previous lower row can become the next upper row without a copy.
        int32_t cached_row[2] = {-1, -1};
        int64_t vertical = first_vertical;
        for (int32_t y = first_row; y < end_row; ++y, vertical += vertical_step)
        {
            const BilinearSample sample = bilinear_sample(vertical, vertical_limit);
            const int32_t top = sample.index;
            const int32_t bottom = top + (top < crop.height - 1);
            const int upper = top & 1, lower = bottom & 1;
            if (cached_row[upper] != top)
            {
                blend_horizontal(cache[upper], crop_start + (size_t)top * source_stride,
                                 crop.width - 1, samples, count);
                cached_row[upper] = top;
            }
            if (cached_row[lower] != bottom)
            {
                blend_horizontal(cache[lower], crop_start + (size_t)bottom * source_stride,
                                 crop.width - 1, samples, count);
                cached_row[lower] = bottom;
            }
            uint32_t *row = destination + (size_t)y * stride + viewport.x + start;
            for (int32_t x = 0; x < count; ++x)
            {
                const uint32_t color = blend_color(cache[upper][x], cache[lower][x], sample.weight);
                colors |= color;
                row[x] = color | 0xff000000u;
            }
        }
        start += count;
    }
    return colors;
}

static bool valid_blit(const uint32_t *destination, size_t stride, int32_t width, int32_t height,
                       const uint32_t *source, int32_t source_width, int32_t source_height,
                       FramebufferSource crop, FramebufferViewport viewport)
{
    return destination && source && width >= 4 && height >= 3 && stride >= (size_t)width &&
           stride <= SIZE_MAX / sizeof(*destination) / (size_t)height &&
           source_width > 0 && source_height > 0 &&
           (size_t)source_width <= SIZE_MAX / sizeof(*source) / (size_t)source_height &&
           crop.x >= 0 && crop.y >= 0 && crop.width > 0 && crop.height > 0 &&
           crop.width <= source_width && crop.height <= source_height &&
           crop.x <= source_width - crop.width && crop.y <= source_height - crop.height &&
           viewport.width > 0 && viewport.height > 0;
}

bool framebuffer_blit_bilinear_band(uint32_t *destination, size_t stride, int32_t width, int32_t height,
                                    const uint32_t *source, int32_t source_width, int32_t source_height,
                                    FramebufferSource crop, unsigned upscale, int32_t first_row,
                                    int32_t end_row, uint32_t *visible_colors)
{
    const FramebufferViewport viewport = framebuffer_viewport(width, height, upscale);
    if (first_row < 0 || end_row < first_row || end_row > height ||
        !valid_blit(destination, stride, width, height, source, source_width, source_height, crop, viewport))
        return false;

    const int32_t viewport_end = viewport.y + viewport.height;
    const int32_t first_active = first_row > viewport.y ? first_row : viewport.y;
    const int32_t end_active = end_row < viewport_end ? end_row : viewport_end;
    for (int32_t y = first_row; y < end_row; ++y)
    {
        uint32_t *row = destination + (size_t)y * stride;
        if (y < first_active || y >= end_active)
        {
            for (int32_t x = 0; x < width; ++x) row[x] = 0xff000000u;
        }
        else
        {
            for (int32_t x = 0; x < viewport.x; ++x) row[x] = 0xff000000u;
            for (int32_t x = viewport.x + viewport.width; x < width; ++x) row[x] = 0xff000000u;
        }
    }
    const uint32_t colors = first_active < end_active
        ? blit_bilinear_active(destination, stride, source, source_width, crop, viewport, first_active, end_active)
        : 0;
    if (visible_colors) *visible_colors = colors;
    return true;
}

static bool framebuffer_blit_nearest(uint32_t *destination, size_t stride, int32_t width, int32_t height,
                                     const uint32_t *source, int32_t source_width, int32_t source_height,
                                     FramebufferSource crop, FramebufferViewport viewport, uint32_t *visible_colors)
{
    if (!valid_blit(destination, stride, width, height, source, source_width, source_height, crop, viewport))
        return false;

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
    if (filter == FRAMEBUFFER_FILTER_BILINEAR)
        return framebuffer_blit_bilinear_band(destination, stride, width, height, source, source_width, source_height,
                                              crop, upscale, 0, height, visible_colors);
    return framebuffer_blit_nearest(destination, stride, width, height, source, source_width, source_height,
                                    crop, framebuffer_viewport(width, height, upscale), visible_colors);
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
    return framebuffer_blit_nearest(destination, stride, width, height, source, source_width, source_height,
                                    crop, framebuffer_movie_viewport(width, height, crop.width, crop.height),
                                    visible_colors);
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
