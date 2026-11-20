// Standalone docked visual smoke test. Link with docked_presenter.c and framebuffer_blit.c.
// A advances, + exits. Use a 1080p TV with overscan disabled; don't init the libnx console.
#include <switch.h>
#include <stdio.h>

#include "docked_presenter.h"

enum { bitmap_width = 336, bitmap_height = 256, crop_left = 8, crop_top = 8 };
static uint32_t pixels[bitmap_height * bitmap_width];

static void log_message(const char *message, void *user_data)
{
    FILE *log_file = user_data;
    fprintf(log_file, "%s\n", message);
    fflush(log_file);
}

static void make_pattern(int width, int height)
{
    static const uint32_t corners[] = {0x00ff0000u, 0x4000ff00u, 0x800000ffu, 0xffffff00u};
    for (int y = 0; y < bitmap_height; ++y)
        for (int x = 0; x < bitmap_width; ++x)
        {
            int crop_x = x - crop_left, crop_y = y - crop_top;
            uint32_t color = 0xffff00ffu; // Guard pixels must not be visible or bleed in.
            if (crop_x >= 0 && crop_x < width && crop_y >= 0 && crop_y < height)
            {
                color = corners[(crop_y >= height / 2) * 2 + (crop_x >= width / 2)];
                if (crop_x == 0 || crop_x == width - 1 || crop_y == 0 || crop_y == height - 1)
                    color = 0x00ffffffu; // Visible opaque white even when input alpha is zero.
            }
            pixels[y * bitmap_width + x] = color;
        }
}

int main(void)
{
    FILE *log_file = fopen("sdmc:/dracer-docked-presenter-probe.log", "w");
    if (!log_file)
        return 1;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    // Exercise the NULL/default path followed by explicit borrowed-window init.
    static const char *const descriptions[] = {
        "Default window: nearest 1280x960 game; white border 4 pixels wide; FPS label at top left outside game.",
        "Default window: bilinear same geometry; softened internal edges, no magenta outside crop.",
        "Default window: movie 1920x1080; whole 320x180 crop; all edges visible.",
        "Explicit window: splash 1417x1080 fitted to original 336x256 aspect; magenta guard included."
    };
    int exit_status = 0;
    for (size_t step = 0; step < sizeof(descriptions) / sizeof(*descriptions); ++step)
    {
        if (step == 3)
            docked_presenter_close();
        if ((step == 0 || step == 3) &&
            !docked_presenter_init(step == 0 ? NULL : nwindowGetDefault(), log_message, log_file))
        {
            exit_status = 2;
            break;
        }
        log_message(descriptions[step], log_file);
        log_message("Expected corners: red/green above blue/yellow. A advances; + exits.", log_file);
        const int height = step == 2 ? 180 : 240;
        make_pattern(320, height);
        const unsigned fps = 30;
        for (;;)
        {
            if (!appletMainLoop())
                goto done;
            padUpdate(&pad);
            const u64 buttons = padGetButtonsDown(&pad);
            if (buttons & HidNpadButton_Plus)
                goto done;
            if (buttons & HidNpadButton_A)
                break;
            const DockedPresenterResult result = step == 3 ? docked_presenter_splash(pixels, bitmap_width, bitmap_height) :
                docked_presenter_present(pixels, bitmap_width, bitmap_height,
                                         (FramebufferSource){crop_left, crop_top, 320, height},
                                         step == 2 ? DOCKED_PRESENTER_MOVIE : DOCKED_PRESENTER_GAME,
                                         step == 1 ? FRAMEBUFFER_FILTER_BILINEAR : FRAMEBUFFER_FILTER_NEAREST,
                                         step == 0 ? &fps : NULL);
            if (result != DOCKED_PRESENTER_SUBMITTED)
            {
                fprintf(log_file, "Frame rejected (status %d)\n", (int)result);
                exit_status = 3;
                goto done;
            }
        }
    }
done:
    docked_presenter_close();
    fclose(log_file);
    return exit_status;
}
