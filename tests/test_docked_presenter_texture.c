#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include "docked_presenter.h"
#include "race_gpu_mesa.h"

static unsigned uploads, swaps;
static bool inject_swap_failure;
static EGLDisplay test_display(EGLNativeDisplayType ignored)
{
    (void)ignored;
    const EGLAttrib attrs[] = {0x3203, strtol(getenv("DRACER_ANGLE_BACKEND"), NULL, 0), EGL_NONE};
    return eglGetPlatformDisplay(0x3202, EGL_DEFAULT_DISPLAY, attrs);
}
static EGLBoolean test_choose(EGLDisplay d, const EGLint *attrs, EGLConfig *cfg, EGLint size, EGLint *count)
{
    EGLint pbuffer_attrs[32];
    unsigned i = 0;
    for (; attrs[i] != EGL_NONE; i += 2) {
        pbuffer_attrs[i] = attrs[i];
        pbuffer_attrs[i + 1] = attrs[i] == EGL_SURFACE_TYPE ? EGL_PBUFFER_BIT : attrs[i + 1];
    }
    pbuffer_attrs[i] = EGL_NONE;
    return eglChooseConfig(d, pbuffer_attrs, cfg, size, count);
}
static EGLBoolean test_config_attribute(EGLDisplay d, EGLConfig cfg, EGLint name, EGLint *value)
{
    // ANGLE's pbuffer can have unused depth/stencil; emulate the Switch window config.
    if (name == EGL_DEPTH_SIZE || name == EGL_STENCIL_SIZE) { *value = 0; return EGL_TRUE; }
    return eglGetConfigAttrib(d, cfg, name, value);
}
static EGLContext test_create_context(EGLDisplay d, EGLConfig config, EGLContext shared, const EGLint *attrs)
{
    assert(attrs[0] == EGL_CONTEXT_CLIENT_VERSION && attrs[1] == 3);
    const EGLint compute_attrs[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 1, EGL_NONE};
    return eglCreateContext(d, config, shared, compute_attrs);
}
static EGLSurface test_surface(EGLDisplay d, EGLConfig cfg, EGLNativeWindowType native, const EGLint *unused)
{
    (void)unused;
    NWindow *window = (NWindow *)native;
    const EGLint attrs[] = {EGL_WIDTH, (EGLint)window->width, EGL_HEIGHT, (EGLint)window->height, EGL_NONE};
    window->slots_configured = 1;
    return eglCreatePbufferSurface(d, cfg, attrs);
}
static EGLBoolean test_swap(EGLDisplay d, EGLSurface s)
{
    ++swaps;
    return !inject_swap_failure && eglSwapBuffers(d, s);
}
#define eglGetDisplay test_display
#define eglChooseConfig test_choose
#define eglGetConfigAttrib test_config_attribute
#define eglCreateContext test_create_context
#define eglCreateWindowSurface test_surface
#define eglSwapBuffers test_swap
#define glTexImage2D(...) (++uploads, glTexImage2D(__VA_ARGS__))
#define glTexSubImage2D(...) (++uploads, glTexSubImage2D(__VA_ARGS__))
#define glReadPixels(...) forbidden_presenter_readback(__VA_ARGS__)
#include "../src/platform/switch/docked_presenter.c"
#undef glTexImage2D
#undef glTexSubImage2D
#undef glReadPixels

static void check_pixel(int x, int y, unsigned r, unsigned g, unsigned b)
{
    unsigned char pixel[4];
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    assert(glGetError() == GL_NO_ERROR);
    if (pixel[0] != r || pixel[1] != g || pixel[2] != b || pixel[3] != 255) {
        fprintf(stderr, "pixel %d,%d: %u,%u,%u,%u expected %u,%u,%u,255\n",
                x,y,pixel[0],pixel[1],pixel[2],pixel[3],r,g,b);
        abort();
    }
}

static void fixture_log(const char *message, void *unused) { (void)unused; fprintf(stderr, "%s\n", message); }

static void exercise_race_scanout(FramebufferSource crop, FramebufferViewport v)
{
    // The presenter's context is the only context: actual Mesa backend code,
    // actual scanout shader, actual presenter draw. Seed synthetic PS1 data.
    static uint16_t packed_vram[1024 * 512];
    packed_vram[8 * 1024 + 4] = 0x801f;
    packed_vram[9 * 1024 + 4] = 0x7c00;
    packed_vram[264 * 1024 + 4] = 0x03e0;
    packed_vram[265 * 1024 + 4] = 0x7fff;
    const uint64_t generation = docked_presenter_context_generation();
    Gpu *gpu = race_gpu_mesa_open_current();
    assert(gpu && race_gpu_mesa_seed(gpu, packed_vram));
    const unsigned before = uploads;
    for (int bank = 0; bank < 2; ++bank) {
        const GLuint scanout = race_gpu_mesa_scanout(gpu, 4, 8 + bank * 256, 320, 240,
            crop.x, crop.y, crop.width, crop.height, crop.x, crop.y);
        assert(scanout);
        const DockedPresenterBorrowedTexture frame = {scanout, generation, crop.width, crop.height};
        assert(docked_presenter_present_texture(&frame,320,240,crop,FRAMEBUFFER_FILTER_NEAREST,NULL) == DOCKED_PRESENTER_SUBMITTED);
        check_pixel(v.x + v.width / 4, v.y + v.height * 3 / 4, bank ? 0 : 255, bank ? 255 : 0, 0);
        check_pixel(v.x + v.width / 4, v.y + v.height / 4, bank ? 255 : 0, bank ? 255 : 0, 255);
    }
    assert(uploads == before && race_gpu_mesa_counters(gpu).scanouts == 2);
    race_gpu_mesa_close(gpu);
}

static void run_case(int width, int height)
{
    assert(docked_presenter_init(NULL, width, height, 0, fixture_log, NULL));
    DockedPresenterFrameLayout layout;
    const FramebufferSource crop = {13, 17, 2, 2};
    assert(docked_presenter_frame_layout(width, height, 0, 320, 240, crop, DOCKED_PRESENTER_GAME, &layout));
    const FramebufferViewport v = layout.gl_viewport;
    assert(v.width == height * 4 / 3 && v.height == height && v.x == (width - v.width)/2 && v.y == 0);
    GLuint texture, fbo;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 2, 2);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glEnable(GL_SCISSOR_TEST);
    const GLfloat colors[][4] = {{0,0,1,0}, {0,1,0,0}, {1,0,0,0}, {1,1,1,0}};
    for (int i = 0; i < 4; ++i) {
        glScissor(i % 2, i / 2, 1, 1);
        glClearBufferfv(GL_COLOR, 0, colors[i]);
    }
    glEnable(GL_RASTERIZER_DISCARD);
    glColorMask(0,0,0,0);
    DockedPresenterBorrowedTexture frame = {texture, docked_presenter_context_generation(), 2, 2};
    const unsigned uploads_before = uploads, swaps_before = swaps;
    assert(docked_presenter_present_texture(&frame, 320, 240, crop, FRAMEBUFFER_FILTER_NEAREST, NULL) == DOCKED_PRESENTER_SUBMITTED);
    assert(uploads == uploads_before && swaps == swaps_before + 1);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    check_pixel(v.x+v.width/4, height*3/4, 255,0,0);
    check_pixel(v.x+v.width*3/4, height*3/4, 0,255,0);
    check_pixel(v.x+v.width/4, height/4, 0,0,255);
    check_pixel(v.x+v.width*3/4, height/4, 255,255,255);
    check_pixel(0, height/2, 0,0,0);
    const unsigned fps = 42;
    assert(docked_presenter_present_texture(&frame, 320, 240, crop, FRAMEBUFFER_FILTER_BILINEAR, &fps) == DOCKED_PRESENTER_SUBMITTED);
    assert(uploads == uploads_before + 1 && swaps == swaps_before + 2); /* Overlay only. */
    check_pixel(10, height-11, 0,0,0);
    check_pixel(12, height-15, 255,255,255);
    unsigned char center[4];
    glReadPixels(width/2, height/2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, center);
    assert(center[0] > 120 && center[0] < 136 && center[1] > 120 && center[1] < 136 && center[2] > 120 && center[2] < 136);
    GLint filter;
    glBindTexture(GL_TEXTURE_2D, texture);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &filter);
    assert(filter == GL_NEAREST && glIsTexture(texture));
    DockedPresenterBorrowedTexture stale = frame;
    --stale.context_generation;
    assert(docked_presenter_present_texture(&stale,320,240,crop,FRAMEBUFFER_FILTER_NEAREST,NULL) == DOCKED_PRESENTER_INVALID_FRAME);
    assert(docked_presenter_present_texture(&frame,320,240,(FramebufferSource){319,0,2,2},FRAMEBUFFER_FILTER_NEAREST,NULL) == DOCKED_PRESENTER_INVALID_FRAME);
    ++stale.width;
    stale.context_generation = frame.context_generation;
    assert(docked_presenter_present_texture(&stale,320,240,crop,FRAMEBUFFER_FILTER_NEAREST,NULL) == DOCKED_PRESENTER_INVALID_FRAME);
    test_mode = AppletOperationMode_Handheld;
    assert(docked_presenter_present_texture(&frame,320,240,crop,FRAMEBUFFER_FILTER_NEAREST,NULL) == DOCKED_PRESENTER_MODE_CHANGED);
    test_mode = AppletOperationMode_Console;
    assert(eglGetCurrentContext() != EGL_NO_CONTEXT && uploads == uploads_before + 1 && swaps == swaps_before + 2);
    const EGLContext current = eglGetCurrentContext();
    const EGLDisplay display = eglGetCurrentDisplay();
    const EGLSurface surface = eglGetCurrentSurface(EGL_DRAW);
    assert(eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT));
    assert(!docked_presenter_context_generation());
    assert(docked_presenter_present_texture(&frame,320,240,crop,FRAMEBUFFER_FILTER_NEAREST,NULL) == DOCKED_PRESENTER_UNAVAILABLE);
    assert(eglMakeCurrent(display, surface, surface, current));
    assert(docked_presenter_context_generation() == frame.context_generation && swaps == swaps_before + 2);
    exercise_race_scanout(crop, v);
    // Stock CPU pixels still use the actual crop offset and row pitch.
    static uint32_t bitmap[320 * 240];
    bitmap[crop.y * 320 + crop.x] = 0xffff0000u;
    bitmap[(crop.y + 1) * 320 + crop.x] = 0xff0000ffu;
    assert(docked_presenter_present(bitmap,320,240,crop,DOCKED_PRESENTER_GAME,FRAMEBUFFER_FILTER_NEAREST,NULL) == DOCKED_PRESENTER_SUBMITTED);
    check_pixel(v.x + v.width / 4, v.y + v.height * 3 / 4, 255,0,0);
    check_pixel(v.x + v.width / 4, v.y + v.height / 4, 0,0,255);
    inject_swap_failure = true;
    assert(docked_presenter_present_texture(&frame,320,240,crop,FRAMEBUFFER_FILTER_NEAREST,NULL) == DOCKED_PRESENTER_UNAVAILABLE);
    assert(eglGetCurrentContext() == current); // The owner, not fallback, must act next.
    inject_swap_failure = false;
    glDeleteTextures(1, &texture);
    glDeleteFramebuffers(1, &fbo);
    docked_presenter_close();
    test_window.slots_configured = 0;
    assert(!docked_presenter_context_generation());
}
int main(void)
{
    run_case(1920,1080);
    run_case(1280,720);
    puts("Production docked presenter: borrowed texture viewport, orientation, filtering, overlay and rejection passed; actual scanout at rows 8 and 264");
}
