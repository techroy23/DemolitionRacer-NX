#include "framebuffer_blit.h"
#include <assert.h>

int main(void)
{
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
