#include "docked_presenter.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static void assert_rectangle(FramebufferViewport rectangle, int x, int y, int width, int height)
{
    assert(rectangle.x == x);
    assert(rectangle.y == y);
    assert(rectangle.width == width);
    assert(rectangle.height == height);
}

static void test_game_letterboxes_at_exact_x4(void)
{
    DockedPresenterFrameLayout layout;
    const FramebufferSource crop = {0, 0, 320, 240};
    assert(docked_presenter_frame_layout(1920, 1080, 320, 240, crop,
                                         DOCKED_PRESENTER_GAME, &layout));
    assert_rectangle(layout.gl_viewport, 320, 60, 1280, 960);
    assert(layout.source_offset_pixels == 0);
    assert(layout.unpack_row_length_pixels == 320);
    assert(!docked_presenter_frame_layout(1280, 720, 320, 240, crop,
                                          DOCKED_PRESENTER_GAME, &layout));
    assert(!docked_presenter_frame_layout(1919, 1080, 320, 240, crop,
                                          DOCKED_PRESENTER_GAME, &layout));
}

static void test_zero_egl_metadata_requires_registered_1080p_native_buffers(void)
{
    assert(docked_presenter_validate_drawable(1920, 1080, 1920, 1080, true));
    assert(docked_presenter_validate_drawable(0, 0, 1920, 1080, true));
    assert(!docked_presenter_validate_drawable(0, 0, 1920, 1080, false));
    assert(!docked_presenter_validate_drawable(0, 0, 1280, 720, true));
    assert(!docked_presenter_validate_drawable(1280, 720, 1920, 1080, true));
    assert(!docked_presenter_validate_drawable(1920, 0, 1920, 1080, true));
}

static void test_movie_fits_whole_crop_with_bottom_origin(void)
{
    DockedPresenterFrameLayout layout;
    assert(docked_presenter_frame_layout(1920, 1080, 512, 256,
                                         (FramebufferSource){13, 7, 320, 179},
                                         DOCKED_PRESENTER_MOVIE, &layout));
    // The fit is 1920x1074; also exercise asymmetric one-pixel rounding below.
    assert_rectangle(layout.gl_viewport, 0, 3, 1920, 1074);
    assert(docked_presenter_frame_layout(1920, 1080, 512, 256,
                                         (FramebufferSource){13, 7, 320, 177},
                                         DOCKED_PRESENTER_MOVIE, &layout));
    assert_rectangle(layout.gl_viewport, 0, 9, 1920, 1062);
    assert(docked_presenter_frame_layout(1920, 1080, 512, 256,
                                         (FramebufferSource){13, 7, 384, 211},
                                         DOCKED_PRESENTER_MOVIE, &layout));
    assert_rectangle(layout.gl_viewport, 0, 13, 1920, 1055);
    // The project movie fit is not constrained to the 1280x960 game rectangle.
    assert(docked_presenter_frame_layout(1920, 1080, 512, 256,
                                         (FramebufferSource){0, 0, 320, 240},
                                         DOCKED_PRESENTER_MOVIE, &layout));
    assert_rectangle(layout.gl_viewport, 240, 0, 1440, 1080);
}

static void test_splash_fits_its_own_aspect(void)
{
    DockedPresenterFrameLayout layout;
    assert(docked_presenter_frame_layout(1920, 1080, 1280, 720,
                                         (FramebufferSource){0, 0, 1280, 720},
                                         DOCKED_PRESENTER_SPLASH, &layout));
    assert_rectangle(layout.gl_viewport, 0, 0, 1920, 1080);
}

static void test_strided_bgra_crop_needs_no_repacking(void)
{
    // On Switch (little endian) each numerical 0xAARRGGBB pixel is B,G,R,A
    // in memory. The shader restores RGB and ignores this arbitrary alpha.
    const uint32_t source[] = {
        0, 0, 0, 0,
        0, 0x00123456u, 0x78abcdefu, 0,
        0, 0xfedcba98u, 0x44010203u, 0
    };
    const unsigned char expected_first_row[] = {0x56, 0x34, 0x12, 0, 0xef, 0xcd, 0xab, 0x78};
    const unsigned char expected_second_row[] = {0x98, 0xba, 0xdc, 0xfe, 3, 2, 1, 0x44};
    DockedPresenterFrameLayout layout;
    assert(docked_presenter_frame_layout(1920, 1080, 4, 3, (FramebufferSource){1, 1, 2, 2},
                                         DOCKED_PRESENTER_GAME, &layout));
    const uint32_t *first_row = source + layout.source_offset_pixels;
    assert(memcmp(first_row, expected_first_row, sizeof(expected_first_row)) == 0);
    assert(memcmp(first_row + layout.unpack_row_length_pixels, expected_second_row,
                  sizeof(expected_second_row)) == 0);
}

static void test_invalid_or_out_of_bounds_frames_are_rejected(void)
{
    DockedPresenterFrameLayout layout;
    assert(!docked_presenter_frame_layout(1920, 1080, 4, 3, (FramebufferSource){1, 1, 4, 2},
                                          DOCKED_PRESENTER_GAME, &layout));
    assert(!docked_presenter_frame_layout(1920, 1080, 4, 3, (FramebufferSource){0, 1, 4, 3},
                                          DOCKED_PRESENTER_GAME, &layout));
    assert(!docked_presenter_frame_layout(1920, 1080, 4, 3, (FramebufferSource){-1, 0, 1, 1},
                                          DOCKED_PRESENTER_GAME, &layout));
    assert(!docked_presenter_frame_layout(1920, 1080, 4, 3, (FramebufferSource){INT_MAX, 0, 1, 1},
                                          DOCKED_PRESENTER_GAME, &layout));
    assert(!docked_presenter_frame_layout(1920, 1080, 4, 3, (FramebufferSource){0, 0, 0, 1},
                                          DOCKED_PRESENTER_GAME, &layout));
    assert(!docked_presenter_frame_layout(1920, 1080, 1, INT_MAX, (FramebufferSource){0, 0, 1, INT_MAX},
                                          DOCKED_PRESENTER_MOVIE, &layout));
    assert(!docked_presenter_frame_layout(1920, 1080, 4, 3, (FramebufferSource){0, 0, 4, 3},
                                          (DockedPresenterFrameKind)99, &layout));
}

int main(void)
{
    test_game_letterboxes_at_exact_x4();
    test_zero_egl_metadata_requires_registered_1080p_native_buffers();
    test_movie_fits_whole_crop_with_bottom_origin();
    test_splash_fits_its_own_aspect();
    test_strided_bgra_crop_needs_no_repacking();
    test_invalid_or_out_of_bounds_frames_are_rejected();
    puts("Docked presenter geometry and upload layout passed");
    return 0;
}
