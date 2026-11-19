#include "framebuffer_blit.h"
#include <assert.h>

static void test_bilinear_bands(void)
{
    enum { width = 11, height = 9, stride = 14 };
    const uint32_t marker = 0xdeadbeefu;
    const uint32_t source[] = {
        0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
        0xffffffff, 0x11ff0000, 0x2200ff00, 0xffffffff,
        0xffffffff, 0x330000ff, 0x44000000, 0xffffffff,
        0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
    };
    const FramebufferSource crop = {1, 1, 2, 2};
    uint32_t serial[stride * height], bands[stride * height], colors;
    for (int i = 0; i < stride * height; ++i)
        serial[i] = bands[i] = marker;
    assert(framebuffer_blit_filtered(serial, stride, width, height, source, 4, 4, crop,
                                     0, FRAMEBUFFER_FILTER_BILINEAR, &colors));
    assert(serial[0] == 0xff000000u && serial[stride + 1] == 0xffff0000u);
    assert(serial[3 * stride + 1] == 0xffaa0055u);
    assert(serial[6 * stride + 1] == 0xff0000ffu && serial[8 * stride + 1] == 0xff000000u);
    assert(serial[1 * stride + 9] == 0xff000000u && serial[1 * stride + 8] == 0xff00ff00u);

    // Out of order, including a split between active rows and a bar-only band.
    const int splits[][2] = {{3, 7}, {0, 1}, {7, 9}, {1, 3}};
    uint32_t combined = 0;
    for (unsigned b = 0; b < sizeof(splits) / sizeof(*splits); ++b)
    {
        const int first = splits[b][0], end = splits[b][1];
        for (int i = 0; i < stride * height; ++i) bands[i] = marker;
        uint32_t band_colors = marker, expected = 0;
        assert(framebuffer_blit_bilinear_band(bands, stride, width, height, source, 4, 4, crop,
                                              0, first, end, &band_colors));
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < stride; ++x)
            {
                const bool in_band = y >= first && y < end && x < width;
                assert(bands[y * stride + x] == (in_band ? serial[y * stride + x] : marker));
                if (in_band) expected |= serial[y * stride + x] & 0x00ffffffu;
            }
        assert(band_colors == expected);
        combined |= band_colors;
    }
    assert(combined == colors);
    for (unsigned b = 0; b < sizeof(splits) / sizeof(*splits); ++b)
        assert(framebuffer_blit_bilinear_band(bands, stride, width, height, source, 4, 4, crop,
                                              0, splits[b][0], splits[b][1], NULL));
    for (int i = 0; i < stride * height; ++i) assert(bands[i] == serial[i]);
}

static void test_bilinear_band_bounds(void)
{
    enum { width = 4, height = 3, stride = 5 };
    const uint32_t source = 0x12010203u;
    const FramebufferSource crop = {0, 0, 1, 1};
    uint32_t output[stride * height] = {0};
    uint32_t colors = 123;
    assert(!framebuffer_blit_bilinear_band(output, stride, width, height, &source, 1, 1, crop,
                                           0, -1, 2, &colors));
    assert(!framebuffer_blit_bilinear_band(output, stride, width, height, &source, 1, 1, crop,
                                           0, 2, 1, &colors));
    assert(!framebuffer_blit_bilinear_band(output, stride, width, height, &source, 1, 1, crop,
                                           0, 0, height + 1, &colors));
    assert(!framebuffer_blit_bilinear_band(output, SIZE_MAX, width, height, &source, 1, 1, crop,
                                           0, 0, height, &colors));
    assert(!framebuffer_blit_bilinear_band(output, width - 1, width, height, &source, 1, 1, crop,
                                           0, 0, height, &colors));
    assert(!framebuffer_blit_bilinear_band(NULL, stride, width, height, &source, 1, 1, crop,
                                           0, 0, height, &colors));
    assert(!framebuffer_blit_bilinear_band(output, stride, width, height, NULL, 1, 1, crop,
                                           0, 0, height, &colors));
    assert(!framebuffer_blit_bilinear_band(output, stride, width, height, &source, 1, 1,
                                           (FramebufferSource){1, 0, 1, 1}, 0, 0, height, &colors));
    assert(colors == 123);
    assert(framebuffer_blit_bilinear_band(output, stride, width, height, &source, 1, 1, crop,
                                          0, height, height, &colors));
    assert(colors == 0);
    for (int i = 0; i < stride * height; ++i) assert(output[i] == 0);
}

static void test_bilinear_wide_band(void)
{
    enum { width = 1283, height = 961, stride = 1285 };
    static uint32_t output[stride * height];
    const uint32_t source[] = {0x11220000u, 0x33004400u, 0xaa449900u, 0xdcff1100u};
    // 4x caps the active width at 1280. The gradient crosses the scratch tile
    // boundary and must reach the clamped right sample before the side bars.
    uint32_t colors;
    assert(framebuffer_blit_bilinear_band(output, stride, width, height, source, 4, 1,
                                          (FramebufferSource){0, 0, 4, 1}, 4, 480, 481, &colors));
    const uint32_t *row = output + 480 * stride;
    assert(colors == 0x00ffff00u);
    assert(row[0] == 0xff000000u && row[1] == 0xff220000u);
    assert(row[640] == 0xff226f00u && row[641] == 0xff226f00u);
    assert(row[1024] == 0xffc73a00u && row[1025] == 0xffc73900u);
    assert(row[1280] == 0xffff1100u && row[1281] == 0xff000000u && row[1282] == 0xff000000u);
    assert(row[1283] == 0 && row[1284] == 0 && row[-1] == 0 && row[stride] == 0);
}

int main(void)
{
    test_bilinear_bands();
    test_bilinear_band_bounds();
    test_bilinear_wide_band();
    FramebufferViewport movie = framebuffer_movie_viewport(1280, 720, 320, 208);
    assert(movie.x == 86 && movie.y == 0 && movie.width == 1107 && movie.height == 720);
    movie = framebuffer_movie_viewport(1280, 720, 320, 240);
    assert(movie.x == 160 && movie.y == 0 && movie.width == 960 && movie.height == 720);
    assert(framebuffer_movie_viewport(1280, 720, 320, 0).width == 0);
    uint32_t movie_output[8 * 6];
    const uint32_t movie_source[] = {0x00112233, 0x00445566};
    assert(framebuffer_blit_movie(movie_output, 8, 8, 6, movie_source, 2, 1,
                                  (FramebufferSource){0, 0, 2, 1}, NULL));
    assert(movie_output[0] == 0xff000000u && movie_output[5 * 8 + 7] == 0xff000000u);
    assert(movie_output[8] == 0xff112233u && movie_output[4 * 8 + 7] == 0xff445566u);
    const uint32_t original[] = {0x00000000, 0x00ff0000, 0x0000ff00, 0x000000ff};
    uint32_t output[14 * 6] = {0};
    uint32_t visible_colors = 0;
    assert(framebuffer_blit_4_3(output, 14, 12, 6, original, 2, 2, (FramebufferSource){0, 0, 2, 2}, &visible_colors));
    assert(visible_colors == 0x00ffffff);
    assert(output[0] == 0xff000000 && output[1] == 0xff000000 && output[10] == 0xff000000);
    assert(output[2] == 0xff000000 && output[9] == 0xffff0000);
    assert(output[5 * 14 + 2] == 0xff00ff00 && output[5 * 14 + 9] == 0xff0000ff);
    assert(output[12] == 0 && output[13] == 0);
    assert(framebuffer_blit_4_3(output, 14, 12, 6, original, 2, 2, (FramebufferSource){1, 0, 1, 2}, &visible_colors));
    assert(output[2] == 0xffff0000 && output[5 * 14 + 2] == 0xff0000ff);
    assert(!framebuffer_blit_4_3(output, 11, 12, 6, original, 2, 2, (FramebufferSource){0, 0, 2, 2}, NULL));
    assert(!framebuffer_blit_4_3(output, 14, 12, 6, original, 2, 2, (FramebufferSource){1, 0, 2, 2}, NULL));
    const uint32_t black[] = {0xff000000};
    assert(framebuffer_blit_4_3(output, 14, 12, 6, black, 1, 1, (FramebufferSource){0, 0, 1, 1}, &visible_colors));
    assert(visible_colors == 0);
    FramebufferViewport viewport = framebuffer_viewport(1280, 720, 2);
    assert(viewport.x == 320 && viewport.y == 120 && viewport.width == 640 && viewport.height == 480);
    viewport = framebuffer_viewport(1280, 720, 4);
    assert(viewport.width == 960 && viewport.height == 720);
    viewport = framebuffer_viewport(1920, 1080, 4);
    assert(viewport.width == 1280 && viewport.height == 960);
    assert(!framebuffer_viewport(0, 0, 2).width);
    framebuffer_draw_fps(output, 14, 12, 6, 60); // Too small: must leave the buffer untouched.
    assert(output[12] == 0 && output[13] == 0);
    uint32_t overlay[120 * 40] = {0};
    framebuffer_draw_fps(overlay, 120, 120, 40, 60);
    assert(overlay[10 * 120 + 10] == 0xff000000);
    unsigned white_pixels = 0;
    for (unsigned i = 0; i < 120 * 40; ++i)
        white_pixels += overlay[i] == 0xffffffff;
    assert(white_pixels > 0);
    uint32_t tripled[12 * 9] = {0};
    const uint32_t pattern[4 * 3] = {1,2,3,4, 5,6,7,8, 9,10,11,12};
    assert(framebuffer_blit_scaled(tripled, 12, 12, 9, pattern, 4, 3,
                                   (FramebufferSource){0, 0, 4, 3}, 0, &visible_colors));
    assert(framebuffer_integer_scale(12, 4) == 3 && framebuffer_integer_scale(13, 4) == 0);
    assert(visible_colors == 15);
    for (unsigned y = 0; y < 9; ++y)
        for (unsigned x = 0; x < 12; ++x)
            assert(tripled[y * 12 + x] == (0xff000000u | pattern[(y / 3) * 4 + x / 3]));
    const uint32_t corners[] = {
        0x00ffffff, 0x00ff0000, 0x0000ff00,
        0x00ffffff, 0x000000ff, 0x00000000
    };
    uint32_t filtered[10 * 6] = {0};
    const FramebufferSource square = {1, 0, 2, 2};
    assert(framebuffer_blit_filtered(filtered, 10, 10, 6, corners, 3, 2, square,
                                     0, FRAMEBUFFER_FILTER_BILINEAR, &visible_colors));
    assert(filtered[0] == 0xff000000 && filtered[1] == 0xffff0000);
    assert(filtered[8] == 0xff00ff00 && filtered[9] == 0xff000000);
    assert(filtered[1 + 2 * 10] == 0xffaa0055 && filtered[1 + 5 * 10] == 0xff0000ff);
    assert(filtered[5 * 10 + 8] == 0xff000000 && visible_colors == 0x00ffffff);
    assert(framebuffer_blit_filtered(output, 14, 12, 6, black, 1, 1,
                                     (FramebufferSource){0, 0, 1, 1}, 0,
                                     FRAMEBUFFER_FILTER_BILINEAR, &visible_colors));
    assert(visible_colors == 0 && output[2] == 0xff000000);
    assert(!framebuffer_blit_filtered(output, 14, 12, 6, corners, 3, 2,
                                      (FramebufferSource){2, 0, 2, 2}, 0,
                                      FRAMEBUFFER_FILTER_BILINEAR, NULL));
    assert(framebuffer_blit_filtered(tripled, 12, 12, 9, pattern, 4, 3,
                                     (FramebufferSource){0, 0, 4, 3}, 0,
                                     FRAMEBUFFER_FILTER_NEAREST, NULL));
    for (unsigned y = 0; y < 9; ++y)
        for (unsigned x = 0; x < 12; ++x)
            assert(tripled[y * 12 + x] == (0xff000000u | pattern[(y / 3) * 4 + x / 3]));
    return 0;
}
