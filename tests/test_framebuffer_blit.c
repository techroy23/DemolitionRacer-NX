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
    return 0;
}
