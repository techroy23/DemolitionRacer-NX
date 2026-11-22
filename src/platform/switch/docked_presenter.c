#include "docked_presenter.h"
#include "resume_switch.h"

bool docked_presenter_select_output(bool docked, bool full_application, unsigned upscale,
                                    int32_t *width, int32_t *height)
{
    const bool use_1080p = docked && full_application && upscale == 4;
    *width = use_1080p ? DOCKED_PRESENTER_WIDTH : HANDHELD_PRESENTER_WIDTH;
    *height = use_1080p ? DOCKED_PRESENTER_HEIGHT : HANDHELD_PRESENTER_HEIGHT;
    return full_application;
}

static bool supported_dimensions(int32_t width, int32_t height)
{
    return (width == DOCKED_PRESENTER_WIDTH && height == DOCKED_PRESENTER_HEIGHT) ||
           (width == HANDHELD_PRESENTER_WIDTH && height == HANDHELD_PRESENTER_HEIGHT);
}

bool docked_presenter_validate_drawable(int32_t width, int32_t height,
                                         int32_t egl_width, int32_t egl_height,
                                         uint32_t native_width, uint32_t native_height,
                                         bool native_buffers_registered)
{
    return supported_dimensions(width, height) && native_buffers_registered &&
           native_width == (uint32_t)width && native_height == (uint32_t)height &&
           ((egl_width == 0 && egl_height == 0) || (egl_width == width && egl_height == height));
}

bool docked_presenter_frame_layout(int32_t drawable_width, int32_t drawable_height, unsigned upscale,
                                   int32_t bitmap_width, int32_t bitmap_height,
                                   FramebufferSource crop, DockedPresenterFrameKind kind,
                                   DockedPresenterFrameLayout *layout)
{
    if (!layout || !supported_dimensions(drawable_width, drawable_height) ||
        bitmap_width <= 0 || bitmap_height <= 0 || crop.x < 0 || crop.y < 0 ||
        crop.width <= 0 || crop.height <= 0 || crop.width > bitmap_width || crop.height > bitmap_height ||
        crop.x > bitmap_width - crop.width || crop.y > bitmap_height - crop.height ||
        (size_t)bitmap_width > SIZE_MAX / sizeof(uint32_t) / (size_t)bitmap_height)
        return false;

    FramebufferViewport viewport;
    switch (kind)
    {
    case DOCKED_PRESENTER_GAME:
        viewport = framebuffer_viewport(drawable_width, drawable_height, upscale);
        break;
    case DOCKED_PRESENTER_MOVIE:
    case DOCKED_PRESENTER_SPLASH:
        viewport = framebuffer_movie_viewport(drawable_width, drawable_height, crop.width, crop.height);
        break;
    default:
        return false;
    }
    if (viewport.width <= 0 || viewport.height <= 0)
        return false;

    viewport.y = drawable_height - viewport.y - viewport.height;
    *layout = (DockedPresenterFrameLayout){viewport, (size_t)crop.y * bitmap_width + crop.x, bitmap_width};
    return true;
}

#ifdef __SWITCH__
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <switch/services/applet.h>

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error This presenter uploads little-endian BGRA bytes
#endif

typedef struct {
    GLuint name;
    GLsizei width, height;
} PresenterTexture;

static struct {
    NWindow *window;
    u32 previous_width, previous_height, previous_swap_interval;
    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
    bool egl_initialized;
    bool ready;
    int32_t width, height;
    unsigned upscale;
    AppletOperationMode operation_mode;
    GLuint program;
    GLuint vertex_array;
    GLint max_texture_size;
    PresenterTexture frame, overlay;
    bool submitted_frame;
    DockedPresenterLog logger;
    void *log_user_data;
} presenter;

static void log_message(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void log_message(const char *format, ...)
{
    if (!presenter.logger)
        return;
    char message[512];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    presenter.logger(message, presenter.log_user_data);
}

static void log_egl_failure(const char *operation)
{
    log_message("GPU presenter: %s failed (EGL 0x%04x)", operation, (unsigned)eglGetError());
}

void docked_presenter_close(void)
{
    if (presenter.egl_initialized)
    {
        // Destroying the private context also releases all its GL objects.
        if (presenter.context != EGL_NO_CONTEXT && eglGetCurrentContext() == presenter.context &&
            !eglMakeCurrent(presenter.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT))
            log_egl_failure("release current context");
        if (presenter.context != EGL_NO_CONTEXT && !eglDestroyContext(presenter.display, presenter.context))
            log_egl_failure("eglDestroyContext");
        if (presenter.surface != EGL_NO_SURFACE && !eglDestroySurface(presenter.display, presenter.surface))
            log_egl_failure("eglDestroySurface");
        if (!eglTerminate(presenter.display))
            log_egl_failure("eglTerminate");
        if (!eglReleaseThread())
            log_egl_failure("eglReleaseThread");
    }
    if (presenter.window)
    {
        NWindow *window = presenter.window;
        Result result = nwindowSetDimensions(window, presenter.previous_width, presenter.previous_height);
        if (R_FAILED(result))
            log_message("GPU presenter: could not restore window dimensions (libnx 0x%08x)", (unsigned)result);
        if (!window->slots_configured)
        {
            // libnx disconnect clears format to zero, but ConfigureBuffer only
            // adopts the next buffer's format when the sentinel is UINT32_MAX.
            // In particular the software fallback is BGRA, not Mesa's RGBA.
            window->format = UINT32_MAX;
            window->usage = 0;
            result = nwindowSetSwapInterval(window, presenter.previous_swap_interval);
            if (R_FAILED(result))
                log_message("GPU presenter: could not restore swap interval (libnx 0x%08x)", (unsigned)result);
        }
    }
    // EGL_NO_DISPLAY, EGL_NO_CONTEXT and EGL_NO_SURFACE are null handles.
    memset(&presenter, 0, sizeof(presenter));
}

static GLuint compile_shader(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    if (!shader)
    {
        log_message("GPU presenter: glCreateShader(0x%x) failed (GL 0x%x)", type, glGetError());
        return 0;
    }
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled)
    {
        char details[320] = {0};
        glGetShaderInfoLog(shader, sizeof(details), NULL, details);
        log_message("GPU presenter: shader 0x%x compile failed: %s", type, details);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static bool create_draw_objects(void)
{
    // One oversized triangle. Source row 0 is sampled at the top of the viewport.
    const char *vertex_source =
        "#version 300 es\n"
        "out highp vec2 texture_position;\n"
        "void main() {\n"
        "    vec2 corner = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
        "    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);\n"
        "    texture_position = vec2(corner.x, 1.0 - corner.y);\n"
        "}\n";
    const char *fragment_source =
        "#version 300 es\n"
        "precision highp float;\n"
        "in highp vec2 texture_position;\n"
        "uniform highp sampler2D pixels;\n"
        "out vec4 color;\n"
        "void main() { color = vec4(texture(pixels, texture_position).bgr, 1.0); }\n";

    GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_source);
    if (!vertex_shader)
        return false;
    GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
    if (!fragment_shader)
        return false;
    presenter.program = glCreateProgram();
    if (!presenter.program)
    {
        log_message("GPU presenter: glCreateProgram failed (GL 0x%x)", glGetError());
        return false;
    }
    glAttachShader(presenter.program, vertex_shader);
    glAttachShader(presenter.program, fragment_shader);
    glLinkProgram(presenter.program);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    GLint linked = GL_FALSE;
    glGetProgramiv(presenter.program, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        char details[320] = {0};
        glGetProgramInfoLog(presenter.program, sizeof(details), NULL, details);
        log_message("GPU presenter: shader link failed: %s", details);
        return false;
    }
    GLint sampler_location = glGetUniformLocation(presenter.program, "pixels");
    if (sampler_location < 0)
    {
        log_message("GPU presenter: source texture uniform unavailable");
        return false;
    }
    glUseProgram(presenter.program);
    glUniform1i(sampler_location, 0);
    glActiveTexture(GL_TEXTURE0);
    glGenVertexArrays(1, &presenter.vertex_array);
    glBindVertexArray(presenter.vertex_array);
    glGenTextures(1, &presenter.frame.name);
    glGenTextures(1, &presenter.overlay.name);
    glDisable(GL_DITHER);
    glClearColor(0, 0, 0, 1);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &presenter.max_texture_size);
    GLint max_viewport[2] = {0, 0};
    glGetIntegerv(GL_MAX_VIEWPORT_DIMS, max_viewport);
    GLenum error = glGetError();
    if (error != GL_NO_ERROR || !presenter.vertex_array || !presenter.frame.name || !presenter.overlay.name ||
        presenter.max_texture_size <= 0 || max_viewport[0] < presenter.width ||
        max_viewport[1] < presenter.height)
    {
        log_message("GPU presenter: GL setup failed (GL 0x%x, max texture %d, max viewport %dx%d)",
                    error, presenter.max_texture_size, max_viewport[0], max_viewport[1]);
        return false;
    }
    return true;
}

static bool validate_drawable(bool announce)
{
    EGLint egl_width, egl_height;
    if (!eglQuerySurface(presenter.display, presenter.surface, EGL_WIDTH, &egl_width) ||
        !eglQuerySurface(presenter.display, presenter.surface, EGL_HEIGHT, &egl_height))
    {
        log_egl_failure("eglQuerySurface dimensions");
        return false;
    }
    u32 native_width, native_height;
    Result result = nwindowGetDimensions(presenter.window, &native_width, &native_height);
    if (R_FAILED(result))
    {
        log_message("GPU presenter: drawable nwindowGetDimensions failed (libnx 0x%08x)", (unsigned)result);
        return false;
    }
    // Switch Mesa 20.1.0 allocates all window buffers from nwindowGetDimensions
    // but does not populate _EGLSurface.Width/Height. Do not interpret 0x0 as
    // a renderable viewport or accept nonzero EGL dimensions that disagree.
    const bool buffers_registered = presenter.window->slots_configured != 0;
    const bool valid = docked_presenter_validate_drawable(presenter.width, presenter.height, egl_width, egl_height,
                                                          native_width, native_height, buffers_registered);
    if (announce || !valid)
        log_message("GPU presenter: drawable %s: requested %dx%d, EGL %dx%d, native %ux%u, buffers=%s",
                    valid ? "validated" : "rejected", presenter.width, presenter.height, egl_width, egl_height,
                    native_width, native_height, buffers_registered ? "registered" : "missing");
    return valid;
}

bool docked_presenter_init(NWindow *window, int32_t width, int32_t height, unsigned upscale,
                           DockedPresenterLog logger, void *log_user_data)
{
    if (presenter.ready)
        return (!window || window == presenter.window) && width == presenter.width &&
               height == presenter.height && upscale == presenter.upscale;
    if (presenter.window)
        return false; // A failed runtime presenter must be closed before reinitialization.
    presenter.logger = logger;
    presenter.log_user_data = log_user_data;
    presenter.operation_mode = appletGetOperationMode();
    const AppletType type = appletGetAppletType();
    if (!supported_dimensions(width, height) ||
        (height == DOCKED_PRESENTER_HEIGHT && presenter.operation_mode != AppletOperationMode_Console) ||
        (type != AppletType_Application && type != AppletType_SystemApplication) ||
        eglGetCurrentContext() != EGL_NO_CONTEXT)
    {
        log_message("GPU presenter: incompatible output, applet type or current EGL context");
        goto fail;
    }
    presenter.width = width;
    presenter.height = height;
    presenter.upscale = upscale;
    const bool default_window = !window;
    if (default_window)
        window = nwindowGetDefault();
    if (!window || !nwindowIsValid(window) || window->slots_configured)
    {
        log_message("GPU presenter: %s NWindow invalid or already has registered buffers",
                    default_window ? "default" : "explicit");
        goto fail;
    }
    if (window->transform || window->sticky_transform || window->crop.left || window->crop.top ||
        window->crop.right || window->crop.bottom)
    {
        log_message("GPU presenter: NWindow must be uncropped and untransformed");
        goto fail;
    }
    u32 previous_width, previous_height;
    Result result = nwindowGetDimensions(window, &previous_width, &previous_height);
    if (R_FAILED(result))
    {
        log_message("GPU presenter: nwindowGetDimensions failed (libnx 0x%08x)", (unsigned)result);
        goto fail;
    }
    // Keep the explicit overrides, including zero (use native defaults).
    presenter.previous_width = window->width;
    presenter.previous_height = window->height;
    presenter.previous_swap_interval = window->swap_interval;
    result = nwindowSetDimensions(window, width, height);
    if (R_FAILED(result))
    {
        log_message("GPU presenter: nwindowSetDimensions(%dx%d) failed (libnx 0x%08x)", width, height, (unsigned)result);
        goto fail;
    }
    presenter.window = window;
    window->format = UINT32_MAX;
    window->usage = 0;
    log_message("GPU presenter: %s NWindow %ux%u, requested %dx%d",
                default_window ? "default" : "explicit", previous_width, previous_height, width, height);

    const char *operation = "eglGetDisplay";
    presenter.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (presenter.display == EGL_NO_DISPLAY)
        goto egl_fail;
    EGLint major, minor;
    operation = "eglInitialize";
    if (!eglInitialize(presenter.display, &major, &minor))
        goto egl_fail;
    presenter.egl_initialized = true;
    operation = "eglBindAPI(GLES)";
    if (!eglBindAPI(EGL_OPENGL_ES_API))
        goto egl_fail;

    // No depth/stencil/MSAA needed. Choose from the driver's actual window configs.
    // The ES3 KHR bit also works with Switch Mesa's EGL 1.4 implementation.
    const EGLint config_attributes[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
        EGL_COLOR_BUFFER_TYPE, EGL_RGB_BUFFER, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 0, EGL_STENCIL_SIZE, 0,
        EGL_SAMPLE_BUFFERS, 0, EGL_NONE
    };
    EGLConfig config;
    EGLint count;
    operation = "eglChooseConfig(ES3 RGBA8888 window)";
    if (!eglChooseConfig(presenter.display, config_attributes, &config, 1, &count))
        goto egl_fail;
    if (count < 1)
    {
        log_message("GPU presenter: EGL %d.%d advertises no ES3 RGBA8888 window config", major, minor);
        goto fail;
    }
    EGLint config_id, color_bits, depth_bits, stencil_bits, samples;
    operation = "eglGetConfigAttrib";
    if (!eglGetConfigAttrib(presenter.display, config, EGL_CONFIG_ID, &config_id) ||
        !eglGetConfigAttrib(presenter.display, config, EGL_BUFFER_SIZE, &color_bits) ||
        !eglGetConfigAttrib(presenter.display, config, EGL_DEPTH_SIZE, &depth_bits) ||
        !eglGetConfigAttrib(presenter.display, config, EGL_STENCIL_SIZE, &stencil_bits) ||
        !eglGetConfigAttrib(presenter.display, config, EGL_SAMPLES, &samples))
        goto egl_fail;
    log_message("GPU presenter: EGL %d.%d config %d: color=%d depth=%d stencil=%d samples=%d",
                major, minor, config_id, color_bits, depth_bits, stencil_bits, samples);
    if (color_bits != 32 || depth_bits != 0 || stencil_bits != 0 || samples != 0)
    {
        log_message("GPU presenter: expected RGBA8888 without depth, stencil or multisampling");
        goto fail;
    }

    const EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    operation = "eglCreateContext(ES3)";
    presenter.context = eglCreateContext(presenter.display, config, EGL_NO_CONTEXT, context_attributes);
    if (presenter.context == EGL_NO_CONTEXT)
        goto egl_fail;
    operation = "eglCreateWindowSurface";
    presenter.surface = eglCreateWindowSurface(presenter.display, config, (EGLNativeWindowType)window, NULL);
    if (presenter.surface == EGL_NO_SURFACE)
        goto egl_fail;
    operation = "eglMakeCurrent";
    if (!eglMakeCurrent(presenter.display, presenter.surface, presenter.surface, presenter.context))
        goto egl_fail;
    if (!validate_drawable(true))
        goto fail;
    operation = "eglSwapInterval(1)";
    if (!eglSwapInterval(presenter.display, 1))
        goto egl_fail;
    if (!create_draw_objects())
        goto fail;
    const char *renderer = (const char *)glGetString(GL_RENDERER);
    log_message("GPU presenter: %s", renderer ? renderer : "unknown GLES renderer");
    presenter.ready = true;
    return true;

egl_fail:
    log_egl_failure(operation);
fail:
    docked_presenter_close();
    return false;
}

static void upload_and_draw(PresenterTexture *texture, const uint32_t *pixels, GLsizei width, GLsizei height,
                            GLint row_length, GLint filter, FramebufferViewport viewport)
{
    glBindTexture(GL_TEXTURE_2D, texture->name);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, row_length);
    // GLES core RGBA upload plus shader BGR swizzle avoids a BGRA extension or a CPU repack.
    // Allocate the crop alone so bilinear sampling cannot bleed from outside it.
    if (texture->width != width || texture->height != height)
    {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        texture->width = width;
        texture->height = height;
    }
    else
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glViewport(viewport.x, viewport.y, viewport.width, viewport.height);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

static void draw_fps_overlay(unsigned fps, EGLint drawable_height)
{
    // Reuse the software glyph renderer on a tiny patch, not the full output.
    enum { bitmap_width = 70, bitmap_height = 30, left = 10, top = 10, width = 60, height = 18 };
    uint32_t pixels[bitmap_height * bitmap_width];
    framebuffer_draw_fps(pixels, bitmap_width, bitmap_width, bitmap_height, fps);
    upload_and_draw(&presenter.overlay, pixels + top * bitmap_width + left,
                    width, height, bitmap_width, GL_NEAREST,
                    (FramebufferViewport){left, drawable_height - top - height, width, height});
}

DockedPresenterResult docked_presenter_present(const uint32_t *pixels, int32_t bitmap_width,
                                               int32_t bitmap_height, FramebufferSource crop,
                                               DockedPresenterFrameKind kind,
                                               FramebufferFilter filter, const unsigned *fps)
{
    if (!presenter.ready)
        return DOCKED_PRESENTER_UNAVAILABLE;
    if (appletGetOperationMode() != presenter.operation_mode)
    {
        log_message("GPU presenter: operation mode changed; releasing the old EGL surface");
        return DOCKED_PRESENTER_MODE_CHANGED;
    }
    if (!validate_drawable(false))
        goto unavailable;

    DockedPresenterFrameLayout layout;
    if (!pixels || !docked_presenter_frame_layout(presenter.width, presenter.height, presenter.upscale, bitmap_width,
                                                   bitmap_height, crop, kind, &layout) ||
        crop.width > presenter.max_texture_size || crop.height > presenter.max_texture_size ||
        (filter != FRAMEBUFFER_FILTER_NEAREST && filter != FRAMEBUFFER_FILTER_BILINEAR))
    {
        log_message("GPU presenter: invalid frame bitmap=%dx%d crop=%d,%d %dx%d kind=%d filter=%d",
                    bitmap_width, bitmap_height, crop.x, crop.y, crop.width, crop.height, (int)kind, (int)filter);
        return DOCKED_PRESENTER_INVALID_FRAME;
    }

    // Clear every back buffer to opaque black before drawing inside the active viewport.
    // glClear is not limited by the previous glViewport (scissor is never enabled).
    glClear(GL_COLOR_BUFFER_BIT);
    upload_and_draw(&presenter.frame, pixels + layout.source_offset_pixels, crop.width, crop.height,
                    layout.unpack_row_length_pixels,
                    kind == DOCKED_PRESENTER_GAME && filter == FRAMEBUFFER_FILTER_BILINEAR ? GL_LINEAR : GL_NEAREST,
                    layout.gl_viewport);
    if (fps)
        draw_fps_overlay(*fps, presenter.height);
    GLenum error = glGetError();
    if (error != GL_NO_ERROR)
    {
        log_message("GPU presenter: clear/upload/draw failed (GL 0x%x)", error);
        goto unavailable;
    }
    RESUME_BEGIN(swap);
    const EGLBoolean swap_succeeded = eglSwapBuffers(presenter.display, presenter.surface);
    RESUME_END(RESUME_SWAP, swap, swap_succeeded, !swap_succeeded);
    if (!swap_succeeded)
    {
        log_egl_failure("eglSwapBuffers");
        goto unavailable;
    }
    if (!presenter.submitted_frame)
    {
        const FramebufferViewport viewport = layout.gl_viewport;
        log_message("GPU presenter: first swap accepted; kind=%d crop=%d,%d %dx%d viewport(GL)=%d,%d %dx%d",
                    (int)kind, crop.x, crop.y, crop.width, crop.height,
                    viewport.x, viewport.y, viewport.width, viewport.height);
        presenter.submitted_frame = true;
    }
    return DOCKED_PRESENTER_SUBMITTED;

unavailable:
    presenter.ready = false;
    return DOCKED_PRESENTER_UNAVAILABLE;
}

DockedPresenterResult docked_presenter_splash(const uint32_t *pixels, int32_t width, int32_t height)
{
    return docked_presenter_present(pixels, width, height, (FramebufferSource){0, 0, width, height},
                                    DOCKED_PRESENTER_SPLASH, FRAMEBUFFER_FILTER_NEAREST, NULL);
}
#endif
