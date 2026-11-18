#include "framebuffer_blit.h"
#include <assert.h>

int main(void)
{
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
    return 0;
}
