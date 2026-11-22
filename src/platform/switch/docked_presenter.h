#ifndef DRACER_DOCKED_PRESENTER_H
#define DRACER_DOCKED_PRESENTER_H

#include "framebuffer_blit.h"

#define DOCKED_PRESENTER_WIDTH 1920
#define DOCKED_PRESENTER_HEIGHT 1080
#define HANDHELD_PRESENTER_WIDTH 1280
#define HANDHELD_PRESENTER_HEIGHT 720

typedef enum {
    DOCKED_PRESENTER_GAME,
    DOCKED_PRESENTER_MOVIE,
    DOCKED_PRESENTER_SPLASH
} DockedPresenterFrameKind;

typedef struct {
    FramebufferViewport gl_viewport;  // Bottom-left origin, in drawable pixels.
    size_t source_offset_pixels;      // First pixel of the top-down crop.
    int32_t unpack_row_length_pixels; // Source pitch, not the crop width.
} DockedPresenterFrameLayout;

// Select the software fallback dimensions too. Returns whether to try GLES.
bool docked_presenter_select_output(bool docked, bool full_application, unsigned upscale,
                                    int32_t *width, int32_t *height);

// EGL may report 0x0 on devkitPro Switch Mesa. Accept it only with a
// successfully created window surface and registered native buffers matching the requested output.
bool docked_presenter_validate_drawable(int32_t width, int32_t height,
                                         int32_t egl_width, int32_t egl_height,
                                         uint32_t native_width, uint32_t native_height,
                                         bool native_buffers_registered);

// Portable geometry and upload layout. Accepts only the supported 720p and 1080p drawables.
bool docked_presenter_frame_layout(int32_t drawable_width, int32_t drawable_height, unsigned upscale,
                                   int32_t bitmap_width, int32_t bitmap_height,
                                   FramebufferSource crop, DockedPresenterFrameKind kind,
                                   DockedPresenterFrameLayout *layout);

#ifdef __SWITCH__
#include <switch/display/native_window.h>

typedef enum {
    DOCKED_PRESENTER_SUBMITTED,      // EGL accepted the swap; not proof of scanout.
    DOCKED_PRESENTER_INVALID_FRAME,  // Caller supplied an invalid frame; context remains usable.
    DOCKED_PRESENTER_MODE_CHANGED,   // Recreate the presenter for the current mode.
    DOCKED_PRESENTER_UNAVAILABLE     // Close and switch to the existing software presenter.
} DockedPresenterResult;

typedef void (*DockedPresenterLog)(const char *message, void *user_data);

// Singleton; use on one presentation thread. For full applications; choose 720p or docked 1080p.
// NULL selects nwindowGetDefault(); otherwise borrow a valid NWindow through close.
// Require an exclusive, uncropped, untransformed window without registered
// buffers and no other EGL user. The synchronous optional logger must not reenter the presenter.
// On init failure any partially opened presenter is closed before returning.
bool docked_presenter_init(NWindow *window, int32_t width, int32_t height, unsigned upscale,
                           DockedPresenterLog logger, void *log_user_data);

// Bitmap is immutable, top-down, tightly row-packed 0xAARRGGBB (BGRA bytes),
// valid through this call. Alpha is ignored. Pass BILINEAR only for a race
// that requests it; movies always use nearest. Non-NULL fps enables the overlay.
// On UNAVAILABLE, close before (re)creating the software framebuffer, then
// retry the same frame there. Do not retry EGL init each frame after a failure.
DockedPresenterResult docked_presenter_present(const uint32_t *pixels, int32_t bitmap_width,
                                               int32_t bitmap_height, FramebufferSource crop,
                                               DockedPresenterFrameKind kind,
                                               FramebufferFilter filter, const unsigned *fps);

// Fits a whole splash using its own aspect ratio; the caller owns splash timing.
DockedPresenterResult docked_presenter_splash(const uint32_t *pixels, int32_t width, int32_t height);

// Idempotent. Releases EGL before restoring the borrowed window's dimensions.
void docked_presenter_close(void);
#endif

#endif
