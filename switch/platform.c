#include <switch.h>
#include <errno.h>
#include <setjmp.h>
#include <string.h>
#include <unistd.h>
#include "diagnostics.h"

#define XPORT_NO_PSYQ_CRITICAL_SECTION
#include "psx_spu.h"
#include "vid.h"
#include "xport_trace.h"

#define GAME_DIR "sdmc:/switch/DemolitionRacer"
#define SETTINGS_PATH GAME_DIR "/settings.yaml"
#define LOG_PATH GAME_DIR "/DemolitionRacer.log"
#define PREVIOUS_LOG_PATH GAME_DIR "/DemolitionRacer.previous.log"
#define SAVE_FILE "DemolitionRacer.sav"
#define SAVE_TEMP SAVE_FILE ".tmp"
#define SAVE_BACKUP SAVE_FILE ".bak"

static sint32 headless, quitting, exit_code;
static bool game_running, video_initialized, shutting_down;
static jmp_buf game_exit;
static XportCleanup cleanup_handler;
static FILE *diagnostic_log;
static DiagnosticLevel diagnostic_threshold = DIAGNOSTICS_INFO;
static unsigned debug_lines;

static void switch_log(DiagnosticLevel level, const char *format, ...) DIAGNOSTICS_FORMAT(2, 3);

static void switch_log(DiagnosticLevel level, const char *format, ...)
{
    if (!diagnostic_log || level > diagnostic_threshold)
        return;
    if (level == DIAGNOSTICS_DEBUG && debug_lines >= 4096)
        return;
    va_list arguments;
    va_start(arguments, format);
    bool written = diagnostics_vwrite(diagnostic_log, diagnostic_threshold, level, format, arguments);
    va_end(arguments);
    if (written && level == DIAGNOSTICS_DEBUG && ++debug_lines == 4096)
        written = diagnostics_write(diagnostic_log, diagnostic_threshold, DIAGNOSTICS_INFO,
                                    "Debug event limit reached; INFO and ERROR remain enabled");
    if (level == DIAGNOSTICS_ERROR)
        fsync(fileno(diagnostic_log));
    if (!written)
    {
        fprintf(stderr, "Could not write %s\n", LOG_PATH);
        fclose(diagnostic_log);
        diagnostic_log = NULL;
    }
}

static void start_diagnostics(void)
{
    FILE *settings = fopen(SETTINGS_PATH, "rb");
    int settings_error = settings ? 0 : errno;
    DiagnosticLevel requested = settings ? diagnostics_read_settings(settings) : DIAGNOSTICS_INFO;
    if (settings)
        fclose(settings);
    diagnostic_threshold = requested == DIAGNOSTICS_INVALID ? DIAGNOSTICS_INFO : requested;

    const char *log_mode = "w";
    FILE *previous_run = fopen(LOG_PATH, "rb");
    if (previous_run)
    {
        fclose(previous_run);
        if ((remove(PREVIOUS_LOG_PATH) != 0 && errno != ENOENT) || rename(LOG_PATH, PREVIOUS_LOG_PATH) != 0)
            log_mode = "a";
    }
    else if (errno != ENOENT)
        log_mode = "a";
    diagnostic_log = fopen(LOG_PATH, log_mode);
    if (!diagnostic_log)
        fprintf(stderr, "Could not open %s (errno=%d)\n", LOG_PATH, errno);
    switch_log(DIAGNOSTICS_INFO, "Demolition Racer Switch " DRACER_VERSION " starting; LOG=%s; applet type=%d",
               diagnostics_level_name(diagnostic_threshold), (int)appletGetAppletType());
    if (requested == DIAGNOSTICS_INVALID)
        switch_log(DIAGNOSTICS_ERROR, "Invalid LOG in settings.yaml; using INFO");
    else if (settings_error && settings_error != ENOENT)
        switch_log(DIAGNOSTICS_ERROR, "Could not read settings.yaml (errno=%d); using INFO", settings_error);
}

static void close_diagnostics(void)
{
    if (diagnostic_log)
    {
        fclose(diagnostic_log);
        diagnostic_log = NULL;
    }
}

int xport_hd = 1;

static bool clock_started;
static u64 clock_start;
static u64 clock_frequency;

static void pump_applet(void)
{
    if (!quitting && !appletMainLoop())
    {
        quitting = 1;
        switch_log(DIAGNOSTICS_INFO, "Homebrew exit requested");
    }
    // Native game loops don't all check the result of xport_poll().
    if (quitting && game_running)
        longjmp(game_exit, 1);
}

uint64 xport_timer_get(void)
{
    u64 ticks = armGetSystemTick();
    if (!clock_started)
    {
        clock_start = ticks;
        clock_frequency = armGetSystemTickFreq();
        clock_started = true;
    }
    ticks -= clock_start;
    return ticks / clock_frequency * 1000000u + ticks % clock_frequency * 1000000u / clock_frequency;
}

void xport_timer_wait_frame(uint64 target_field)
{
    while (!quitting && xport_frame() < target_field)
    {
        pump_applet();
        svcSleepThread(1000000);
    }
}

sint32 xport_render_wait_until(uint64 deadline)
{
    // EGL owns the swap interval. Like Windows, just test the deadline.
    return xport_timer_get() < deadline;
}

void xport_present_complete(void)
{
    static bool reported_first_frame;
    if (!reported_first_frame)
    {
        reported_first_frame = true;
        switch_log(DIAGNOSTICS_DEBUG, "First frame presented");
    }
    // The renderer already swapped; don't wait for a second VBlank here.
    pump_applet();
}

sint32 xport_poll(void)
{
    pump_applet();
    if (!quitting)
        psx_root_counter_poll();
    return !quitting;
}

enum { PAD_COUNT = 2, VIBRATION_COUNT = 2 };

typedef struct {
    PadState input;
    uint8 axes[4];
    HidNpadIdType id;
    u32 style;
    HidVibrationDeviceHandle vibration[VIBRATION_COUNT];
    bool vibration_valid[VIBRATION_COUNT];
    bool vibrating;
    bool analog;
} Controller;

static Controller pads[PAD_COUNT];
static bool input_ready, input_overridden;
static uint32 override_buttons;

static void stop_rumble(Controller *pad)
{
    const HidVibrationValue stop = {0.0f, 160.0f, 0.0f, 320.0f};
    if (!pad->vibrating)
        return;
    for (unsigned i = 0; i < VIBRATION_COUNT; ++i)
        if (pad->vibration_valid[i])
            hidSendVibrationValue(pad->vibration[i], &stop);
    pad->vibrating = false;
}

static void update_vibration(Controller *pad, HidNpadIdType id, u32 style)
{
    if (id == pad->id && style == pad->style)
        return;
    stop_rumble(pad);
    pad->id = id;
    pad->style = style;
    memset(pad->vibration_valid, 0, sizeof(pad->vibration_valid));
    if (style && R_SUCCEEDED(hidInitializeVibrationDevices(pad->vibration, VIBRATION_COUNT, id, style)))
    {
        for (unsigned i = 0; i < VIBRATION_COUNT; ++i)
        {
            HidVibrationDeviceInfo info;
            pad->vibration_valid[i] = R_SUCCEEDED(hidGetVibrationDeviceInfo(pad->vibration[i], &info)) &&
                                       info.type == HidVibrationDeviceType_LinearResonantActuator;
        }
    }
}

sint32 xport_input_init(void)
{
    for (unsigned i = 0; i < PAD_COUNT; ++i)
        stop_rumble(&pads[i]);
    memset(pads, 0, sizeof(pads));
    if (!input_ready)
        padConfigureInput(PAD_COUNT, HidNpadStyleSet_NpadFullCtrl);
    padInitializeDefault(&pads[0].input);
    padInitialize(&pads[1].input, HidNpadIdType_No2);
    for (unsigned i = 0; i < PAD_COUNT; ++i)
        memset(pads[i].axes, 128, sizeof(pads[i].axes));
    input_ready = true;
    input_overridden = false;
    override_buttons = 0;
    switch_log(DIAGNOSTICS_DEBUG, "HID initialized for %u player slots", (unsigned)PAD_COUNT);
    return 1;
}

static uint8 stick_axis(sint32 value)
{
    if (value <= -32768)
        return 0;
    if (value >= 32767)
        return 255;
    return (uint8)((value + 32768) / 256);
}

static uint32 read_pad(sint32 controller)
{
    static const struct { u64 source; uint32 target; } mapping[] = {
        {HidNpadButton_Up, PADLup}, {HidNpadButton_Right, PADLright},
        {HidNpadButton_Down, PADLdown}, {HidNpadButton_Left, PADLleft},
        // Map by face-button position, not by the printed letter.
        {HidNpadButton_X, PADRup}, {HidNpadButton_A, PADRright},
        {HidNpadButton_B, PADRdown}, {HidNpadButton_Y, PADRleft},
        {HidNpadButton_L, PADL1}, {HidNpadButton_R, PADR1},
        {HidNpadButton_ZL, PADL2}, {HidNpadButton_ZR, PADR2},
        {HidNpadButton_Plus, PADstart}, {HidNpadButton_Minus, PADselect},
        {HidNpadButton_StickR, PADj}
    };
    Controller *pad = &pads[controller];
    HidNpadIdType id = controller ? HidNpadIdType_No2 : HidNpadIdType_No1;
    uint32 buttons = 0;
    padUpdate(&pad->input);
    pad->analog = padIsConnected(&pad->input);
    if (!pad->analog)
    {
        memset(pad->axes, 128, sizeof(pad->axes));
        update_vibration(pad, id, 0);
        return 0;
    }
    if (padIsHandheld(&pad->input))
        id = HidNpadIdType_Handheld;
    update_vibration(pad, id, hidGetNpadStyleSet(id) & HidNpadStyleSet_NpadFullCtrl);
    HidAnalogStickState left = padGetStickPos(&pad->input, 0);
    HidAnalogStickState right = padGetStickPos(&pad->input, 1);
    pad->axes[0] = stick_axis(right.x);
    pad->axes[1] = stick_axis(-(sint32)right.y);
    pad->axes[2] = stick_axis(left.x);
    pad->axes[3] = stick_axis(-(sint32)left.y);
    u64 held = padGetButtons(&pad->input);
    for (unsigned i = 0; i < sizeof(mapping) / sizeof(mapping[0]); ++i)
        if (held & mapping[i].source)
            buttons |= mapping[i].target;
#if XPORT_PAD_HACK
    if (buttons & (PADLleft | PADLright))
        pad->axes[2] = (buttons & PADLleft) ? ((buttons & PADLright) ? 128 : 0) : 255;
    if (buttons & (PADLup | PADLdown))
        pad->axes[3] = (buttons & PADLup) ? ((buttons & PADLdown) ? 128 : 0) : 255;
    if (left.x < -13107) buttons |= PADLleft;
    if (left.x >  13107) buttons |= PADLright;
    if (left.y < -13107) buttons |= PADLdown;
    if (left.y >  13107) buttons |= PADLup;
#endif
    return buttons;
}

uint32 xport_input_read(sint32 controller)
{
    if (!input_ready || controller < 0 || controller >= PAD_COUNT || quitting)
        return 0;
    if (controller == 0 && input_overridden)
        return override_buttons;
    return read_pad(controller);
}

sint32 xport_input_analog(sint32 controller, uint8 axes[4])
{
    if (!axes)
        return 0;
    memset(axes, 128, 4);
    if (quitting || controller < 0 || controller >= PAD_COUNT || (controller == 0 && input_overridden) || !pads[controller].analog)
        return 0;
    memcpy(axes, pads[controller].axes, 4);
    return 1;
}

sint32 xport_input_rumble_supported(sint32 controller)
{
    if (quitting || controller < 0 || controller >= PAD_COUNT || (controller == 0 && input_overridden))
        return 0;
    return pads[controller].vibration_valid[0] || pads[controller].vibration_valid[1];
}

void xport_input_rumble(sint32 controller, uint16 low, uint16 high)
{
    if (controller < 0 || controller >= PAD_COUNT)
        return;
    Controller *pad = &pads[controller];
    if (!xport_input_rumble_supported(controller) || (!low && !high))
    {
        stop_rumble(pad);
        return;
    }
    const HidVibrationValue value = {low / 65535.0f, 160.0f, high / 65535.0f, 320.0f};
    for (unsigned i = 0; i < VIBRATION_COUNT; ++i)
        if (pad->vibration_valid[i] && R_SUCCEEDED(hidSendVibrationValue(pad->vibration[i], &value)))
            pad->vibrating = true;
}

void xport_input_override(sint32 enabled, uint32 buttons)
{
    if (enabled)
        stop_rumble(&pads[0]);
    input_overridden = enabled != 0;
    override_buttons = buttons;
}

void xport_set_function_key_handler(XportFunctionKeyHandler handler)
{
    // This backend has no keyboard/function-key input source.
    (void)handler;
}

sint32 xport_window_init(void)
{
    if (headless || video_initialized)
        return 1;
    video_initialized = vid_init(NULL) != 0;
    if (!video_initialized)
        xport_message_error("VID", "Switch EGL/OpenGL initialization failed");
    else
        switch_log(DIAGNOSTICS_INFO, "EGL/GLES renderer initialized");
    return video_initialized;
}

sint32 xport_present(const uint32 *pixels, sint32 bitmap_width, sint32 bitmap_height,
                     sint32 source_x, sint32 source_y, sint32 source_width, sint32 source_height, const char *title)
{
    const VID_RECT src = {source_x, source_y, source_width, source_height};
    (void)title;
    if (headless)
        return 1;
    sint32 presented = vid_blit(pixels, bitmap_width, bitmap_height, src);
    static bool reported_first_blit;
    if (presented && !reported_first_blit)
    {
        reported_first_blit = true;
        switch_log(DIAGNOSTICS_DEBUG, "First framebuffer blit presented (%dx%d)",
                   (int)source_width, (int)source_height);
    }
    return presented;
}

sint32 xport_is_headless(void) { return headless; }
void xport_set_headless(sint32 enabled) { headless = enabled != 0; }
sint32 xport_isquit(void) { return quitting; }
RUN_MODE xport_run_mode(void) { return RUN_MODE_GAME; }
void xport_set_exit_code(sint32 code) { exit_code = code; }

volatile uint32 g_xport_audio_submitted_buffers;
volatile uint32 g_xport_audio_nonzero_buffers;
volatile uint32 g_xport_audio_peak;
volatile uint32 g_xport_audio_backend_active;
volatile uint32 g_xport_audio_output_muted;
volatile uint32 g_xport_audio_callback_overruns;

enum {
    AUDIO_OUTPUT_RATE = 48000,
    AUDIO_BUFFERS = 4,
    AUDIO_PAGE = 0x1000,
    AUDIO_OUTPUT_FRAMES = AUDIO_PAGE / (2 * sizeof(sint16)),
    AUDIO_INPUT_FRAMES = 896
};
static _Alignas(AUDIO_PAGE) sint16 audio_pcm[AUDIO_BUFFERS][AUDIO_OUTPUT_FRAMES * 2];
static AudioOutBuffer audio_buffers[AUDIO_BUFFERS];
static bool audio_pending[AUDIO_BUFFERS];
static sint16 audio_input[AUDIO_INPUT_FRAMES * 2];
static bool audio_running, audout_open;

sint32 xport_audio_init(void)
{
    if (audio_running)
        return 1;
    g_xport_audio_submitted_buffers = 0;
    g_xport_audio_nonzero_buffers = 0;
    g_xport_audio_peak = 0;
    g_xport_audio_callback_overruns = 0;
    if (!headless)
    {
        if (R_FAILED(audoutInitialize()))
            goto fail;
        audout_open = true;
        if (audoutGetSampleRate() != AUDIO_OUTPUT_RATE || audoutGetChannelCount() != 2 ||
            audoutGetPcmFormat() != PcmFormat_Int16 || R_FAILED(audoutStartAudioOut()))
            goto fail;
        memset(audio_pending, 0, sizeof(audio_pending));
        for (unsigned i = 0; i < AUDIO_BUFFERS; ++i)
            audio_buffers[i] = (AudioOutBuffer){.buffer = audio_pcm[i], .buffer_size = AUDIO_PAGE};
        g_xport_audio_backend_active = 1;
    }
    audio_running = true;
    if (audout_open)
        switch_log(DIAGNOSTICS_INFO, "Switch audout initialized at %u Hz", (unsigned)AUDIO_OUTPUT_RATE);
    return 1;

fail:
    xport_audio_shutdown();
    xport_message_error("Audio", "Cannot start Switch audout (48 kHz stereo PCM16)");
    return 0;
}

// SPU access and mixing both run on the game thread. Audout only reads
// submitted PCM pages; it never calls into the SPU or takes these locks.
void xport_audio_lock(void) {}
void xport_audio_unlock(void) {}

void xport_audio_shutdown(void)
{
    audio_running = false;
    g_xport_audio_backend_active = 0;
    if (audout_open)
    {
        audoutStopAudioOut();
        audoutExit();
        audout_open = false;
    }
}

sint32 xport_audio_is_running(void) { return audio_running; }

static uint32 samples_for_field(uint32 before, uint32 rate, uint32 sample_rate)
{
    if (rate == FIELD_RATE)
        return xport_field_sample_count(before, sample_rate);
    return (uint32)((((uint64)before + 1) * sample_rate) / rate - (uint64)before * sample_rate / rate);
}

static void resample_stereo(sint16 *dst, uint32 output_frames, const sint16 *src, uint32 input_frames)
{
    for (uint32 i = 0; i < output_frames; ++i)
    {
        uint32 position = i * input_frames;
        uint32 frame = position / output_frames;
        uint32 fraction = position % output_frames;
        uint32 next = frame + (frame + 1 < input_frames);
        for (unsigned channel = 0; channel < 2; ++channel)
        {
            sint32 a = src[frame * 2 + channel];
            sint32 b = src[next * 2 + channel];
            dst[i * 2 + channel] = (sint16)(a + (b - a) * (sint32)fraction / (sint32)output_frames);
        }
    }
}

static void submit_audio(uint32 input_frames, uint32 output_frames, uint32 peak)
{
    for (unsigned i = 0; i < AUDIO_BUFFERS; ++i)
    {
        AudioOutBuffer *released = NULL;
        u32 count = 0;
        if (R_FAILED(audoutGetReleasedAudioOutBuffer(&released, &count)))
            goto fail;
        if (!count || !released)
            break;
        for (unsigned j = 0; j < AUDIO_BUFFERS; ++j)
            if (released == &audio_buffers[j])
                audio_pending[j] = false;
    }
    for (unsigned i = 0; i < AUDIO_BUFFERS; ++i)
    {
        if (audio_pending[i])
            continue;
        AudioOutBuffer *buffer = &audio_buffers[i];
        buffer->data_size = output_frames * 2 * sizeof(sint16);
        if (g_xport_audio_output_muted)
            memset(buffer->buffer, 0, buffer->data_size);
        else
            resample_stereo(buffer->buffer, output_frames, audio_input, input_frames);
        armDCacheFlush(buffer->buffer, buffer->buffer_size);
        if (R_FAILED(audoutAppendAudioOutBuffer(buffer)))
            goto fail;
        audio_pending[i] = true;
        ++g_xport_audio_submitted_buffers;
        if (peak)
            ++g_xport_audio_nonzero_buffers;
        return;
    }
    // Do not stall the game or stop the guest SPU when output backs up.
    ++g_xport_audio_callback_overruns;
    return;

fail:
    ++g_xport_audio_callback_overruns;
    xport_audio_shutdown();
    xport_message_error("Audio", "Switch audout buffer transfer failed");
}

void xport_audio_vblank(uint32 before, uint32 rate)
{
    spu_dispatch_pending_irq();
    if (!audio_running || !rate)
        return;
    uint32 input_total = samples_for_field(before, rate, SPU_SAMPLE_RATE);
    uint32 output_total = samples_for_field(before, rate, AUDIO_OUTPUT_RATE);
    uint32 mixed = 0, produced = 0;
    while (mixed < input_total)
    {
        uint32 count = input_total - mixed;
        if (count > AUDIO_INPUT_FRAMES)
            count = AUDIO_INPUT_FRAMES;
        spu_render_vblank(audio_input, count);
        uint32 peak = 0;
        for (uint32 i = 0; i < count * 2; ++i)
        {
            sint32 value = audio_input[i];
            uint32 magnitude = (uint32)(value < 0 ? -value : value);
            if (magnitude > peak)
                peak = magnitude;
        }
        if (peak > g_xport_audio_peak)
            g_xport_audio_peak = peak;
        mixed += count;
        uint32 end = (uint32)((uint64)mixed * output_total / input_total);
        uint32 output_frames = end - produced;
        if (audout_open && output_frames && output_frames <= AUDIO_OUTPUT_FRAMES)
            submit_audio(count, output_frames, peak);
        produced = end;
    }
    spu_dispatch_pending_irq();
}

FILE *xport_fopen(const char *path, const char *mode)
{
    if (!path || !mode)
    {
        errno = EINVAL;
        return NULL;
    }
    FILE *file = fopen(path, mode);
    int open_error = errno;
    if (file)
        switch_log(DIAGNOSTICS_DEBUG, "Opened %s (%s)", path, mode);
    else
        switch_log(DIAGNOSTICS_DEBUG, "Could not open %s (%s, errno=%d)", path, mode, open_error);
    errno = open_error;
    return file;
}

static long stream_length(FILE *file)
{
    return fseek(file, 0, SEEK_END) == 0 ? ftell(file) : -1;
}

sint32 xport_file_size(const char *path, size_t *size)
{
    if (!size)
        return 0;
    *size = 0;
    FILE *file = xport_fopen(path, "rb");
    if (!file)
        return 0;
    long length = stream_length(file);
    if (fclose(file) != 0 || length < 0)
        return 0;
    *size = (size_t)length;
    return 1;
}

sint32 xport_file_read(const char *path, void *data, size_t capacity, size_t *size)
{
    if (size)
        *size = 0;
    FILE *file = data ? xport_fopen(path, "rb") : NULL;
    if (!file)
        return 0;
    size_t received = fread(data, 1, capacity, file);
    int failed = ferror(file);
    if (size)
        *size = received;
    return fclose(file) == 0 && !failed;
}

sint32 xport_save(const void *data, sint32 size)
{
    if (!data || size <= 0)
        return -1;
    FILE *file = fopen(SAVE_TEMP, "wb");
    if (!file)
        return -1;
    bool written = fwrite(data, 1, (size_t)size, file) == (size_t)size &&
                   fflush(file) == 0 && fsync(fileno(file)) == 0;
    if (fclose(file) != 0 || !written)
        goto fail;

    // SD rename need not overwrite an existing file. Keep the last good
    // save recoverable if the process stops between the two renames.
    file = fopen(SAVE_FILE, "rb");
    if (file)
    {
        fclose(file);
        if (remove(SAVE_BACKUP) != 0 && errno != ENOENT)
            goto fail;
        if (rename(SAVE_FILE, SAVE_BACKUP) != 0)
            goto fail;
    }
    else if (errno != ENOENT)
        goto fail;
    if (rename(SAVE_TEMP, SAVE_FILE) != 0)
        goto fail;
    remove(SAVE_BACKUP);
    return 1;

fail:
    remove(SAVE_TEMP);
    return -1;
}

sint32 xport_load(void *data, sint32 capacity)
{
    if (!data || capacity <= 0)
        return -1;
    FILE *file = fopen(SAVE_FILE, "rb");
    if (!file && errno == ENOENT)
        file = fopen(SAVE_BACKUP, "rb");
    if (!file)
        return errno == ENOENT ? 0 : -1;
    long length = stream_length(file);
    bool read = length > 0 && length <= capacity && fseek(file, 0, SEEK_SET) == 0 &&
                fread(data, 1, (size_t)length, file) == (size_t)length;
    return fclose(file) == 0 && read ? (sint32)length : -1;
}

sint32 xport_memory_readable(const void *address, size_t size)
{
    uintptr_t begin = (uintptr_t)address;
    if (!begin || !size || size > UINTPTR_MAX - begin)
        return 0;
    uintptr_t end = begin + size;
    while (begin < end)
    {
        MemoryInfo region;
        u32 page;
        if (R_FAILED(svcQueryMemory(&region, &page, begin)) || !(region.perm & Perm_R) ||
            region.addr > begin || region.size > UINTPTR_MAX - region.addr || region.addr + region.size <= begin)
            return 0;
        begin = region.addr + region.size;
    }
    return 1;
}

void xport_message_error(const char *title, const char *message)
{
    const char *error_title = title ? title : "Demolition Racer";
    const char *error_message = message ? message : "Unknown error";
    fprintf(stderr, "%s: %s\n", error_title, error_message);
    switch_log(DIAGNOSTICS_ERROR, "%s: %s", error_title, error_message);
}

void xport_set_cleanup(void (*cleanup)(void)) { cleanup_handler = cleanup; }
XportCleanup xport_get_cleanup(void) { return cleanup_handler; }

void xport_shutdown(void)
{
    if (shutting_down)
        return;
    shutting_down = true;
    for (unsigned i = 0; i < PAD_COUNT; ++i)
        stop_rumble(&pads[i]);
    XportCleanup cleanup = cleanup_handler;
    cleanup_handler = NULL;
    if (cleanup)
        cleanup();
    xport_audio_shutdown();
    if (video_initialized)
    {
        video_initialized = false;
        vid_free();
    }
    shutting_down = false;
}

int main(void)
{
    start_diagnostics();
    // libnx's CRT mounts sdmc:. Use a fixed cwd for both xport and direct stdio.
    if (chdir(GAME_DIR) != 0)
    {
        xport_message_error("Demolition Racer", "Cannot enter " GAME_DIR);
        close_diagnostics();
        return 2;
    }
    const bool exit_locked = R_SUCCEEDED(appletLockExit());
    switch_log(DIAGNOSTICS_DEBUG, "Applet exit lock: %s", exit_locked ? "acquired" : "unavailable");
    xport_memory_init(DRAM, sizeof(DRAM), (uint8 *)SCRATCHPAD, sizeof(SCRATCHPAD), NULL, 0u);
#if defined(XPORT_NATIVE_VM) && XPORT_NATIVE_VM
    xport_memory_bind_mmio(0x1F801C00, 0x200, spu_mmio_read, spu_mmio_write, NULL);
#endif
    xport_timer_get();
    xport_input_init();
    if (setjmp(game_exit) == 0)
    {
        game_running = true;
        pump_applet();
        switch_log(DIAGNOSTICS_INFO, "Entering reconstructed PS1 game code");
        xport_main();
    }
    game_running = false;
    xport_shutdown();
    if (exit_locked)
        appletUnlockExit();
    switch_log(DIAGNOSTICS_INFO, "Shutdown complete (exit code=%d)", (int)exit_code);
    close_diagnostics();
    return exit_code;
}
