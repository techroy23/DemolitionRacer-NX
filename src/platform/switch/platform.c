#include <switch.h>
#include <errno.h>
#include <fcntl.h>
#include <setjmp.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "diagnostics.h"
#include "framebuffer_blit.h"
#include "asset_preload.h"
#include "docked_presenter.h"

#define XPORT_NO_PSYQ_CRITICAL_SECTION
#include "psx_spu.h"
#include "xport_trace.h"

#define GAME_DIR "sdmc:/switch/DemolitionRacer"
#define SETTINGS_PATH GAME_DIR "/settings.yaml"
#define SAVE_DIRECTORY "SAVE"
#define LEGACY_SAVE "DemolitionRacer.sav"
#define SAVE_FILE SAVE_DIRECTORY "/" LEGACY_SAVE
#define SAVE_TEMP SAVE_FILE ".tmp"
#define SAVE_BACKUP SAVE_FILE ".bak"

static sint32 headless, quitting, exit_code;
static bool game_running, video_initialized, shutting_down;
static jmp_buf game_exit;
static XportCleanup cleanup_handler;
static int display_width = 1280, display_height = 720;
static Framebuffer switch_framebuffer;
static bool docked_gpu_active, docked_gpu_attempted;
static const char *asset_preload_phase;
static unsigned asset_preload_hits;
static uint64 present_attempts, presented_frames, failed_frames, nonblack_frames;
static bool race_display_active;
static uint64 race_display_start, race_display_start_frames, race_display_start_failures;
static uint64 race_blit_samples, race_blit_total_us, race_blit_peak_us;
static uint32 race_start_audio_overruns;
static uint32 race_near_models, race_far_models, track_samples, track_requested_peak;
static uint32 track_occupied_peak, track_loads, track_misses;
static uint64 track_time, track_peak;
static uint64 track_extension_attempts, track_extension_hits, track_extension_objects, track_extension_bytes;
static uint64 worker_frames, worker_wait_total, worker_wait_peak;
static FILE *diagnostic_log;
static DiagnosticLevel diagnostic_threshold = DIAGNOSTICS_INFO;
static unsigned debug_lines;
static DiagnosticSettings game_settings;
static char diagnostic_path[160];
static PadState exit_pad;
static bool exit_pad_ready;
static uint64 collision_response_total, collision_response_peak;
static unsigned collision_response_count;

static void switch_log(DiagnosticLevel level, const char *format, ...) DIAGNOSTICS_FORMAT(2, 3);

static void switch_log(DiagnosticLevel level, const char *format, ...)
{
    if (!diagnostic_log || level > diagnostic_threshold)
        return;
    if (level == DIAGNOSTICS_DEBUG && debug_lines >= 4096)
        return;
    va_list arguments;
    va_start(arguments, format);
    bool written = fprintf(diagnostic_log, "[%10llu ms] ", (unsigned long long)(xport_timer_get() / 1000)) >= 0 &&
                   diagnostics_vwrite(diagnostic_log, diagnostic_threshold, level, format, arguments);
    va_end(arguments);
    if (written && level == DIAGNOSTICS_DEBUG && ++debug_lines == 4096)
        written = diagnostics_write(diagnostic_log, diagnostic_threshold, DIAGNOSTICS_INFO,
                                    "Debug event limit reached; INFO and ERROR remain enabled");
    if (level == DIAGNOSTICS_ERROR)
        fsync(fileno(diagnostic_log));
    if (!written)
    {
        fprintf(stderr, "Could not write %s\n", diagnostic_path);
        fclose(diagnostic_log);
        diagnostic_log = NULL;
    }
}

static const char *display_filter_name(FramebufferFilter filter)
{
    return filter == FRAMEBUFFER_FILTER_BILINEAR ? "BILINEAR" : "NEAREST";
}

uint32 switch_car_lod_limit(uint32 stock_limit)
{
    if (!race_display_active || !game_settings.car_lod_extended || stock_limit > INT32_MAX)
        return stock_limit;
    // The renderer compares squared distance: a 10% radius increase uses 1.21.
    const uint64 extended_limit = (uint64)stock_limit * 121u / 100u;
    return extended_limit > INT32_MAX ? INT32_MAX : (uint32)extended_limit;
}

void switch_record_car_lod(int near_model)
{
    if (!race_display_active) return;
    if (near_model) ++race_near_models;
    else ++race_far_models;
}

sint32 switch_extra_track_section(void)
{
    return race_display_active && game_settings.track_distance_extended ? 1 : 0;
}

void switch_record_track_stream(uint32 requested, uint32 occupied, uint32 loaded, uint32 misses, uint64 duration_us)
{
    if (!race_display_active) return;
    ++track_samples;
    if (requested > track_requested_peak) track_requested_peak = requested;
    if (occupied > track_occupied_peak) track_occupied_peak = occupied;
    track_loads += loaded;
    track_misses += misses;
    track_time += duration_us;
    if (duration_us > track_peak) track_peak = duration_us;
}

typedef struct {
    uint32_t *destination;
    const uint32_t *source;
    size_t stride;
    int32_t width, height, source_width, source_height;
    FramebufferSource crop;
    unsigned upscale;
} DisplayJob;

void switch_record_track_extension(uint32 submitted_objects, uint32 packet_bytes)
{
    ++track_extension_attempts;
    if (submitted_objects) ++track_extension_hits;
    track_extension_objects += submitted_objects;
    track_extension_bytes += packet_bytes;
}

static Thread display_worker_thread;
static Mutex display_worker_mutex;
static CondVar display_worker_request, display_worker_completion;
static bool display_worker_started, display_worker_stopping, display_job_pending;
static DisplayJob display_worker_job;
static bool display_worker_copied;
static uint32_t display_worker_colors;
static uint32_t display_worker_core;

static void display_worker_main(void *context)
{
    (void)context;
    mutexLock(&display_worker_mutex);
    while (!display_worker_stopping)
    {
        while (!display_job_pending && !display_worker_stopping)
            condvarWait(&display_worker_request, &display_worker_mutex);
        if (display_worker_stopping) break;
        const DisplayJob job = display_worker_job;
        mutexUnlock(&display_worker_mutex);
        uint32_t colors = 0;
        const bool copied = framebuffer_blit_bilinear_band(job.destination, job.stride, job.width, job.height,
            job.source, job.source_width, job.source_height, job.crop, job.upscale, job.height / 2, job.height, &colors);
        const uint32_t core = svcGetCurrentProcessorNumber();
        mutexLock(&display_worker_mutex);
        display_worker_copied = copied;
        display_worker_colors = colors;
        display_worker_core = core;
        display_job_pending = false;
        condvarWakeOne(&display_worker_completion);
    }
    mutexUnlock(&display_worker_mutex);
}

static void start_display_worker(void)
{
    u64 core_mask = 0;
    const Result mask_result = svcGetInfo(&core_mask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0);
    const uint32_t main_core = svcGetCurrentProcessorNumber();
    switch_log(DIAGNOSTICS_INFO, "Display cores: main=%u allowed=0x%llx mask-result=0x%08x",
               (unsigned)main_core, (unsigned long long)core_mask, (unsigned)mask_result);
    if (!game_settings.display_worker || game_settings.display_filter != FRAMEBUFFER_FILTER_BILINEAR)
        return;
    const u64 candidates = R_SUCCEEDED(mask_result) && main_core < 64 ? core_mask & ~(UINT64_C(1) << main_core) : 0;
    if (!candidates)
    {
        switch_log(DIAGNOSTICS_INFO, "Display worker: serial fallback; no alternate core available");
        return;
    }
    // Prefer the application's lower-numbered cores rather than core 3.
    const int selected_core = __builtin_ctzll(candidates);
    mutexInit(&display_worker_mutex);
    condvarInit(&display_worker_request);
    condvarInit(&display_worker_completion);
    Result result = threadCreate(&display_worker_thread, display_worker_main, NULL, NULL, 0x10000, 0x2c, selected_core);
    if (R_SUCCEEDED(result))
    {
        result = threadStart(&display_worker_thread);
        if (R_FAILED(result)) threadClose(&display_worker_thread);
    }
    display_worker_started = R_SUCCEEDED(result);
    switch_log(DIAGNOSTICS_INFO, "Display worker: %s; requested-core=%d result=0x%08x",
               display_worker_started ? "ready" : "serial fallback", selected_core, (unsigned)result);
}

static void stop_display_worker(void)
{
    if (!display_worker_started) return;
    mutexLock(&display_worker_mutex);
    display_worker_stopping = true;
    condvarWakeOne(&display_worker_request);
    mutexUnlock(&display_worker_mutex);
    const Result result = threadWaitForExit(&display_worker_thread);
    const Result close_result = R_SUCCEEDED(result) ? threadClose(&display_worker_thread) : result;
    switch_log(R_SUCCEEDED(close_result) ? DIAGNOSTICS_INFO : DIAGNOSTICS_ERROR,
               "Display worker: stop; join=0x%08x close=0x%08x", (unsigned)result, (unsigned)close_result);
    display_worker_started = false;
}

static bool blit_with_display_worker(DisplayJob job, uint32_t *visible_colors)
{
    mutexLock(&display_worker_mutex);
    display_worker_job = job;
    display_job_pending = true;
    condvarWakeOne(&display_worker_request);
    mutexUnlock(&display_worker_mutex);

    uint32_t main_colors = 0;
    const bool copied = framebuffer_blit_bilinear_band(job.destination, job.stride, job.width, job.height,
        job.source, job.source_width, job.source_height, job.crop, job.upscale, 0, job.height / 2, &main_colors);
    const uint64 wait_start = xport_timer_get();
    mutexLock(&display_worker_mutex);
    while (display_job_pending)
        condvarWait(&display_worker_completion, &display_worker_mutex);
    const bool worker_copied = display_worker_copied;
    *visible_colors = main_colors | display_worker_colors;
    mutexUnlock(&display_worker_mutex);
    const uint64 wait_time = xport_timer_get() - wait_start;
    if (!copied || !worker_copied)
        return framebuffer_blit_filtered(job.destination, job.stride, job.width, job.height, job.source,
            job.source_width, job.source_height, job.crop, job.upscale, FRAMEBUFFER_FILTER_BILINEAR, visible_colors);
    ++worker_frames;
    worker_wait_total += wait_time;
    if (wait_time > worker_wait_peak) worker_wait_peak = wait_time;
    return true;
}

void switch_set_race_display_active(bool active)
{
    if (race_display_active == active)
        return;
    race_display_active = active;
    if (active)
    {
        race_display_start = xport_timer_get();
        race_display_start_frames = presented_frames;
        race_display_start_failures = failed_frames;
        race_blit_samples = race_blit_total_us = race_blit_peak_us = 0;
        worker_frames = worker_wait_total = worker_wait_peak = 0;
        race_near_models = race_far_models = track_samples = track_requested_peak = 0;
        track_occupied_peak = track_loads = track_misses = 0;
        track_time = track_peak = 0;
        track_extension_attempts = track_extension_hits = track_extension_objects = track_extension_bytes = 0;
        race_start_audio_overruns = g_xport_audio_callback_overruns;
        switch_log(DIAGNOSTICS_INFO, "Race display: begin; filter=%s output=%dx%d upscale=%u",
                   display_filter_name(game_settings.display_filter), (int)display_width, (int)display_height,
                   game_settings.upscale);
    }
    else
    {
        switch_log(DIAGNOSTICS_INFO, "Race display: end; filter=%s elapsed-ms=%llu submitted=%llu failed=%llu blit-samples=%llu blit-avg-us=%llu blit-max-us=%llu audio-overruns=%u",
                   display_filter_name(game_settings.display_filter),
                   (unsigned long long)((xport_timer_get() - race_display_start) / 1000u),
                   (unsigned long long)(presented_frames - race_display_start_frames),
                   (unsigned long long)(failed_frames - race_display_start_failures),
                   (unsigned long long)race_blit_samples,
                   (unsigned long long)(race_blit_samples ? race_blit_total_us / race_blit_samples : 0),
                   (unsigned long long)race_blit_peak_us,
                   (unsigned)(g_xport_audio_callback_overruns - race_start_audio_overruns));
        switch_log(DIAGNOSTICS_INFO, "Race geometry: car-lod=%s near-draws=%u far-draws=%u track-distance=%s track-samples=%u wanted-peak=%u occupied-peak=%u section-loads=%u misses=%u stream-avg-us=%llu stream-max-us=%llu",
                   game_settings.car_lod_extended ? "EXTENDED" : "STOCK", (unsigned)race_near_models, (unsigned)race_far_models,
                   game_settings.track_distance_extended ? "EXTENDED" : "STOCK", (unsigned)track_samples,
                   (unsigned)track_requested_peak, (unsigned)track_occupied_peak, (unsigned)track_loads, (unsigned)track_misses,
                   (unsigned long long)(track_samples ? track_time / track_samples : 0), (unsigned long long)track_peak);
        if (game_settings.track_distance_extended)
            switch_log(DIAGNOSTICS_INFO, "Race track extension: attempts=%llu with-extra-geometry=%llu submitted-objects=%llu packet-bytes=%llu",
                       (unsigned long long)track_extension_attempts, (unsigned long long)track_extension_hits,
                       (unsigned long long)track_extension_objects, (unsigned long long)track_extension_bytes);
        if (display_worker_started)
            switch_log(DIAGNOSTICS_INFO, "Race display worker: frames=%llu main-core=%u worker-core=%u wait-avg-us=%llu wait-max-us=%llu",
                       (unsigned long long)worker_frames, (unsigned)svcGetCurrentProcessorNumber(), (unsigned)display_worker_core,
                       (unsigned long long)(worker_frames ? worker_wait_total / worker_frames : 0), (unsigned long long)worker_wait_peak);
    }
}

static void start_diagnostics(void)
{
    FILE *settings = fopen(SETTINGS_PATH, "rb");
    int settings_error = settings ? 0 : errno;
    bool valid = diagnostics_parse_settings(settings, &game_settings);
    if (settings)
        fclose(settings);
    diagnostic_threshold = game_settings.level;

    u64 timestamp = 0;
    TimeCalendarTime calendar;
    TimeCalendarAdditionalInfo calendar_info;
    char suffix[48];
    bool clock_available = R_SUCCEEDED(timeGetCurrentTime(TimeType_Default, &timestamp)) &&
                           R_SUCCEEDED(timeToCalendarTimeWithMyRule(timestamp, &calendar, &calendar_info));
    if (clock_available)
        snprintf(suffix, sizeof(suffix), "%04u-%02u-%02u_%02u-%02u-%02u", (unsigned)calendar.year,
                 (unsigned)calendar.month, (unsigned)calendar.day, (unsigned)calendar.hour,
                 (unsigned)calendar.minute, (unsigned)calendar.second);
    else
        snprintf(suffix, sizeof(suffix), "clock-unavailable_%llu", (unsigned long long)armGetSystemTick());
    for (unsigned attempt = 0; attempt < 100; ++attempt)
    {
        if (attempt == 0)
            snprintf(diagnostic_path, sizeof(diagnostic_path), GAME_DIR "/DemolitionRacer_%s.log", suffix);
        else
            snprintf(diagnostic_path, sizeof(diagnostic_path), GAME_DIR "/DemolitionRacer_%s_%02u.log", suffix, attempt);
        // Keep past sessions even when two launches have the same wall-clock second.
        struct stat existing;
        if (stat(diagnostic_path, &existing) == 0)
            continue;
        if (errno != ENOENT)
            break;
        int descriptor = open(diagnostic_path, O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (descriptor < 0 && errno == EEXIST)
            continue;
        if (descriptor >= 0)
        {
            diagnostic_log = fdopen(descriptor, "w");
            if (!diagnostic_log) close(descriptor);
        }
        break;
    }
    if (!diagnostic_log)
        fprintf(stderr, "Could not open %s (errno=%d)\n", diagnostic_path, errno);
    switch_log(DIAGNOSTICS_INFO, "Demolition Racer Switch " DRACER_VERSION " starting; LOG=%s FPS_OVERLAY=%s UPSCALE=%u (0=FIT) applet=%d docked=%d",
               diagnostics_level_name(diagnostic_threshold), game_settings.fps_overlay ? "true" : "false",
               game_settings.upscale, (int)appletGetAppletType(), appletGetOperationMode() == AppletOperationMode_Console);
    if (!valid)
        switch_log(DIAGNOSTICS_ERROR, "Invalid settings.yaml; using LOG=INFO FPS_OVERLAY=false UPSCALE=FIT DISPLAY_FILTER=NEAREST");
    else if (settings_error && settings_error != ENOENT)
        switch_log(DIAGNOSTICS_ERROR, "Could not read settings.yaml (errno=%d); using INFO", settings_error);
    switch_log(DIAGNOSTICS_INFO, "Graphics experiment: DISPLAY_FILTER=%s DISPLAY_WORKER=%s CAR_LOD=%s TRACK_DISTANCE=%s; scope=race",
               display_filter_name(game_settings.display_filter), game_settings.display_worker ? "true" : "false",
               game_settings.car_lod_extended ? "EXTENDED" : "STOCK", game_settings.track_distance_extended ? "EXTENDED" : "STOCK");
}

void switch_record_collision_response(uint64 duration_microseconds)
{
    if (diagnostic_threshold != DIAGNOSTICS_DEBUG || !diagnostic_log)
        return;
    ++collision_response_count;
    collision_response_total += duration_microseconds;
    if (duration_microseconds > collision_response_peak)
        collision_response_peak = duration_microseconds;
}

static void start_asset_preload(const char *phase, const char *const *paths, size_t count, size_t budget)
{
    AppletType applet_type = appletGetAppletType();
    if (applet_type != AppletType_Application && applet_type != AppletType_SystemApplication)
    {
        switch_log(DIAGNOSTICS_INFO, "Asset preload %s: skipped in applet mode", phase);
        return;
    }
    if (!asset_preload_start(paths, count, budget))
    {
        switch_log(DIAGNOSTICS_INFO, "Asset preload %s: unavailable; using SD", phase);
        return;
    }
    asset_preload_phase = phase;
    asset_preload_hits = 0;
}

void switch_join_asset_preload(void)
{
    if (!asset_preload_phase)
        return;
    AssetPreloadStats statistics;
    asset_preload_cancel();
    if (!asset_preload_join(&statistics))
    {
        switch_log(DIAGNOSTICS_ERROR, "Asset preload %s: join failed; using SD", asset_preload_phase);
        return;
    }
    switch_log(DIAGNOSTICS_INFO, "Asset preload %s: cached=%zu/%zu bytes=%zu budget=%zu failed=%zu cancelled=%zu budget-skipped=%zu worker-us=%llu join-us=%llu",
               asset_preload_phase, statistics.cached_count, statistics.requested_count,
               statistics.cached_bytes, statistics.budget_bytes, statistics.failed_count,
               statistics.cancelled_count, statistics.budget_skipped_count,
               (unsigned long long)(statistics.worker_elapsed_ns / 1000),
               (unsigned long long)(statistics.join_wait_ns / 1000));
}

void switch_clear_asset_preload(void)
{
    if (!asset_preload_phase)
        return;
    if (!asset_preload_clear())
    {
        switch_log(DIAGNOSTICS_ERROR, "Asset preload %s: cleanup failed", asset_preload_phase);
        return;
    }
    switch_log(DIAGNOSTICS_INFO, "Asset preload %s: cleared; RAM opens=%u", asset_preload_phase, asset_preload_hits);
    asset_preload_phase = NULL;
}

void switch_start_race_preload(unsigned level, unsigned circuit, unsigned sky)
{
    if (level < 1 || level > 29 || circuit < 1 || circuit > 29)
        return;
    char stream_path[48], level_path[48], circuit_path[48], sky_path[48], left_path[48], right_path[48];
    snprintf(stream_path, sizeof(stream_path), "DATA/LEVEL%u/STREAM.VRM", level);
    snprintf(level_path, sizeof(level_path), "DATA/LEVEL%u/LEVEL.DAT", level);
    snprintf(circuit_path, sizeof(circuit_path), "DATA/LEVEL%u/CIR%u.DAT", level, circuit);
    snprintf(sky_path, sizeof(sky_path), "DATA/LEVEL%u/SKY%u.DAT", level, sky);
    snprintf(left_path, sizeof(left_path), "DATA/LEVEL%u/LEFT.TRK", level);
    snprintf(right_path, sizeof(right_path), "DATA/LEVEL%u/RIGHT.TRK", level);
    const char *const paths[] = {stream_path, level_path, circuit_path, sky_path, left_path, right_path,
        "DATA/STATIC/STATIC.VRM", "DATA/STATIC/STATIC.DAT", "DATA/CARS/LOGOS.DAT", "DATA/PRES/INFO.TIM",
        "DATA/ANIMS/PIPEIN.GRR", "DATA/ANIMS/PIPEOUT.GRR", "DATA/ANIMS/SAIL.GRR"};
    start_asset_preload("race", paths, sizeof(paths) / sizeof(paths[0]), 8u * 1024u * 1024u);
}

void switch_hold_loading_screen(const char *screen_name)
{
    // Three turns at the original callback's 64 angle units per field.
    enum { loading_hold_fields = 3 * (4096 / 64) };
    const uint64 start_time = xport_timer_get();
    const uint64 start_frames = presented_frames;
    const uint64 start_nonblack_frames = nonblack_frames;
    switch_log(DIAGNOSTICS_INFO, "%s: begin; target-fields=%d", screen_name, loading_hold_fields);

    // Synchronize to the current field before counting the visible hold.
    const sint32 initial_sync = VSync(0);
    const sint32 hold_result = initial_sync < 0 ? initial_sync : VSync(loading_hold_fields);
    const uint64 submitted = presented_frames - start_frames;
    const uint64 nonblack = nonblack_frames - start_nonblack_frames;
    const DiagnosticLevel level = hold_result < 0 || !nonblack ? DIAGNOSTICS_ERROR : DIAGNOSTICS_INFO;
    switch_log(level, "%s: end; elapsed-ms=%llu submitted=%llu nonblack=%llu vsync-result=%d", screen_name,
               (unsigned long long)((xport_timer_get() - start_time) / 1000u),
               (unsigned long long)submitted, (unsigned long long)nonblack, (int)hold_result);
}

void switch_log_movie_source_end(const char *path, uint32 frame_number)
{
    switch_log(DIAGNOSTICS_INFO, "STR source exhausted: %s at frame %u", path, (unsigned)frame_number);
}

void switch_record_race_profile(uint64 update_us, uint64 scene_us, uint64 wait_us)
{
    if (diagnostic_threshold != DIAGNOSTICS_DEBUG || !diagnostic_log)
        return;
    static uint64 update_total, scene_total, wait_total, peak, window_start;
    static unsigned frames;
    uint64 now = xport_timer_get();
    if (!frames)
        window_start = now - (update_us + scene_us + wait_us);
    update_total += update_us;
    scene_total += scene_us;
    wait_total += wait_us;
    uint64 measured_us = update_us + scene_us + wait_us;
    if (measured_us > peak)
        peak = measured_us;
    ++frames;
    if (now - window_start >= 2000000)
    {
        switch_log(DIAGNOSTICS_DEBUG, "Race profile iterations=%u window-us=%llu update-avg-us=%llu scene-avg-us=%llu pacing-avg-us=%llu measured-peak-us=%llu",
                   frames, (unsigned long long)(now - window_start), (unsigned long long)(update_total / frames),
                   (unsigned long long)(scene_total / frames), (unsigned long long)(wait_total / frames), (unsigned long long)peak);
        switch_log(DIAGNOSTICS_DEBUG, "Collision profile contacts=%u response-total-us=%llu response-max-us=%llu",
                   collision_response_count, (unsigned long long)collision_response_total,
                   (unsigned long long)collision_response_peak);
        collision_response_count = 0;
        collision_response_total = collision_response_peak = 0;
        frames = 0;
        update_total = scene_total = wait_total = peak = 0;
    }
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
    static uint64 next_exit_poll;
    if (!quitting && exit_pad_ready && xport_timer_get() >= next_exit_poll)
    {
        next_exit_poll = xport_timer_get() + 2000;
        const u64 exit_buttons = HidNpadButton_Plus | HidNpadButton_Minus | HidNpadButton_ZL | HidNpadButton_ZR;
        padUpdate(&exit_pad);
        if ((padGetButtons(&exit_pad) & exit_buttons) == exit_buttons)
        {
            quitting = 1;
            switch_log(DIAGNOSTICS_INFO, "Exit chord pressed: Plus + Minus + ZL + ZR");
        }
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
    // The PS1 field clock owns pacing. Only test the requested deadline here.
    return xport_timer_get() < deadline;
}

sint32 xport_poll(void)
{
    pump_applet();
    if (!quitting)
        psx_root_counter_poll();
    return !quitting;
}

enum { PAD_COUNT = 1, VIBRATION_COUNT = 2 };

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
    padInitializeDefault(&exit_pad);
    exit_pad_ready = true;
    for (unsigned i = 0; i < PAD_COUNT; ++i)
        memset(pads[i].axes, 128, sizeof(pads[i].axes));
    input_ready = true;
    input_overridden = false;
    override_buttons = 0;
    switch_log(DIAGNOSTICS_DEBUG, "HID initialized for player 1 only");
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
    HidNpadIdType id = HidNpadIdType_No1;
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

static void log_docked_presenter(const char *message, void *user_data)
{
    (void)user_data;
    switch_log(DIAGNOSTICS_INFO, "Docked GPU: %s", message);
}

static void fall_back_to_software_display(void)
{
    docked_presenter_close();
    docked_gpu_active = false;
    video_initialized = false;
    switch_log(DIAGNOSTICS_INFO, "Docked GPU unavailable; trying software x4");
}

sint32 xport_window_init(void)
{
    if (headless || video_initialized)
        return 1;
    NWindow *window = nwindowGetDefault();
    if (!window || !nwindowIsValid(window))
    {
        xport_message_error("Video", "Switch default display window is invalid");
        return 0;
    }
    u32 window_width = 0, window_height = 0;
    Result result = nwindowGetDimensions(window, &window_width, &window_height);
    switch_log(DIAGNOSTICS_DEBUG, "Video native window before setup: %ux%u, result=0x%08x",
               (unsigned)window_width, (unsigned)window_height, (unsigned)result);
    // Only x4 needs the extra docked pixels; retain the established 720p path otherwise.
    if (game_settings.upscale == 4 && appletGetOperationMode() == AppletOperationMode_Console)
    {
        display_width = 1920;
        display_height = 1080;
        if (!docked_gpu_attempted)
        {
            docked_gpu_attempted = true;
            docked_gpu_active = docked_presenter_init(window, log_docked_presenter, NULL);
            if (docked_gpu_active)
            {
                video_initialized = true;
                switch_log(DIAGNOSTICS_INFO, "Video: docked GPU 1920x1080, active 1280x960 at 320,60");
                return 1;
            }
            fall_back_to_software_display();
        }
    }
    unsigned buffer_count = 3;
    result = framebufferCreate(&switch_framebuffer, window, display_width, display_height,
                               PIXEL_FORMAT_BGRA_8888, buffer_count);
    if (R_FAILED(result))
    {
        switch_log(DIAGNOSTICS_INFO, "Video triple buffer unavailable (0x%08x); retrying with two", (unsigned)result);
        buffer_count = 2;
        result = framebufferCreate(&switch_framebuffer, window, display_width, display_height,
                                   PIXEL_FORMAT_BGRA_8888, buffer_count);
    }
    if (R_FAILED(result) && display_height == 1080)
    {
        switch_log(DIAGNOSTICS_INFO, "Video 1080p setup declined (0x%08x); falling back to 720p", (unsigned)result);
        display_width = 1280;
        display_height = 720;
        result = framebufferCreate(&switch_framebuffer, window, display_width, display_height, PIXEL_FORMAT_BGRA_8888, 2);
    }
    if (R_FAILED(result))
    {
        switch_log(DIAGNOSTICS_ERROR, "Video framebufferCreate failed: 0x%08x", (unsigned)result);
        return 0;
    }
    result = framebufferMakeLinear(&switch_framebuffer);
    if (R_FAILED(result))
    {
        framebufferClose(&switch_framebuffer);
        switch_log(DIAGNOSTICS_ERROR, "Video framebufferMakeLinear failed: 0x%08x", (unsigned)result);
        return 0;
    }
    video_initialized = true;
    FramebufferViewport viewport = framebuffer_viewport(display_width, display_height, game_settings.upscale);
    switch_log(DIAGNOSTICS_INFO, "Video: libnx framebuffer %dx%d BGRA8888, active %dx%d at %d,%d, stride=%u bytes, buffers=%u",
               display_width, display_height, (int)viewport.width, (int)viewport.height,
               (int)viewport.x, (int)viewport.y, (unsigned)switch_framebuffer.stride, buffer_count);
    return video_initialized;
}

sint32 xport_present(const uint32 *pixels, sint32 bitmap_width, sint32 bitmap_height,
                     sint32 source_x, sint32 source_y, sint32 source_width, sint32 source_height, const char *title)
{
    extern sint32 dr_movie_display_active(void);
    (void)title;
    if (headless)
        return 1;
    ++present_attempts;
    if (!video_initialized && !xport_window_init())
    {
        ++failed_frames;
        return 0;
    }
    static uint64 window_start, frame_start_count, max_submit_us, last_submit_us;
    static uint64 dequeue_total, scale_total, queue_total, previous_submission_time, longest_submission_gap;
    static unsigned profiled_submissions, gaps_over_25ms, gaps_over_34ms;
    static unsigned submission_fps;
    uint64 submit_start_us = xport_timer_get();
    const FramebufferSource crop = {source_x, source_y, source_width, source_height};
    const bool movie = dr_movie_display_active() != 0;
    const FramebufferFilter filter = !movie && race_display_active ? game_settings.display_filter : FRAMEBUFFER_FILTER_NEAREST;
    u32 stride_bytes = 0;
    uint64 dequeue_done = submit_start_us, scale_done = submit_start_us, blit_us = 0;
    uint32_t visible_colors = 0;
    bool copied = false;
    if (docked_gpu_active)
    {
        DockedPresenterFrameLayout layout;
        const DockedPresenterFrameKind kind = movie ? DOCKED_PRESENTER_MOVIE : DOCKED_PRESENTER_GAME;
        if (pixels && docked_presenter_frame_layout(display_width, display_height, bitmap_width, bitmap_height, crop, kind, &layout))
        {
            for (int row = 0; row < crop.height; ++row)
            {
                const uint32_t *source_row = pixels + layout.source_offset_pixels + (size_t)row * bitmap_width;
                for (int column = 0; column < crop.width; ++column)
                    visible_colors |= source_row[column] & 0xffffffu;
            }
        }
        scale_done = xport_timer_get();
        DockedPresenterResult result = docked_presenter_present(pixels, bitmap_width, bitmap_height, crop, kind,
            filter, game_settings.fps_overlay ? &submission_fps : NULL);
        if (result == DOCKED_PRESENTER_UNAVAILABLE)
        {
            fall_back_to_software_display();
            if (!xport_window_init())
            {
                ++failed_frames;
                return 0;
            }
        }
        else if (result == DOCKED_PRESENTER_INVALID_FRAME)
        {
            ++failed_frames;
            return 0;
        }
        else
            copied = true;
    }
    if (!copied)
    {
        visible_colors = 0;
        uint32_t *destination = framebufferBegin(&switch_framebuffer, &stride_bytes);
        dequeue_done = xport_timer_get();
        if (stride_bytes % sizeof(*destination) == 0)
        {
            size_t stride = stride_bytes / sizeof(*destination);
            if (filter == FRAMEBUFFER_FILTER_BILINEAR && display_worker_started && destination)
                copied = blit_with_display_worker((DisplayJob){destination, pixels, stride, display_width, display_height,
                    bitmap_width, bitmap_height, crop, game_settings.upscale}, &visible_colors);
            else if (movie)
                copied = framebuffer_blit_movie(destination, stride, display_width, display_height,
                                                pixels, bitmap_width, bitmap_height, crop, &visible_colors);
            else
                copied = framebuffer_blit_filtered(destination, stride, display_width, display_height,
                                                   pixels, bitmap_width, bitmap_height, crop,
                                                   game_settings.upscale, filter, &visible_colors);
        }
        blit_us = copied && race_display_active && !movie ? xport_timer_get() - dequeue_done : 0;
        if (copied && game_settings.fps_overlay)
            framebuffer_draw_fps(destination, stride_bytes / sizeof(*destination),
                                 display_width, display_height, submission_fps);
        scale_done = xport_timer_get();
        if (destination)
            framebufferEnd(&switch_framebuffer);
        if (!copied)
        {
            ++failed_frames;
            if (failed_frames <= 8 || failed_frames % 180 == 0)
                switch_log(DIAGNOSTICS_ERROR, "Video copy failed: attempt=%llu source=%dx%d crop=%d,%d %dx%d buffer=%s stride=%u",
                           (unsigned long long)present_attempts, (int)bitmap_width, (int)bitmap_height,
                           (int)source_x, (int)source_y, (int)source_width, (int)source_height,
                           destination ? "available" : "null", (unsigned)stride_bytes);
            return 0;
        }
    }
    const bool measure_race_blit = !docked_gpu_active && race_display_active && !movie;
    static FramebufferSource previous_crop;
    static sint32 previous_width, previous_height;
    static bool previous_nonblack;
    bool nonblack = visible_colors != 0;
    ++presented_frames;
    uint64 submitted_us = xport_timer_get();
    if (measure_race_blit)
    {
        ++race_blit_samples;
        race_blit_total_us += blit_us;
        if (blit_us > race_blit_peak_us) race_blit_peak_us = blit_us;
        if (race_blit_samples == 1 || race_blit_samples % 120 == 0)
        {
            const FramebufferViewport viewport = framebuffer_viewport(display_width, display_height, game_settings.upscale);
            switch_log(DIAGNOSTICS_DEBUG, "Race filter sample: filter=%s frames=%llu source=%dx%d crop=%d,%d %dx%d viewport=%d,%d %dx%d stride-bytes=%u blit-us=%llu avg-us=%llu max-us=%llu audio-overruns=%u",
                       display_filter_name(filter), (unsigned long long)race_blit_samples, (int)bitmap_width, (int)bitmap_height,
                       (int)crop.x, (int)crop.y, (int)crop.width, (int)crop.height,
                       (int)viewport.x, (int)viewport.y, (int)viewport.width, (int)viewport.height, (unsigned)stride_bytes,
                       (unsigned long long)blit_us, (unsigned long long)(race_blit_total_us / race_blit_samples),
                       (unsigned long long)race_blit_peak_us,
                       (unsigned)(g_xport_audio_callback_overruns - race_start_audio_overruns));
        }
    }
    if (previous_submission_time)
    {
        uint64 gap = submitted_us - previous_submission_time;
        if (gap > longest_submission_gap) longest_submission_gap = gap;
        if (gap > 25000) ++gaps_over_25ms;
        if (gap > 34000) ++gaps_over_34ms;
    }
    previous_submission_time = submitted_us;
    dequeue_total += dequeue_done - submit_start_us;
    scale_total += scale_done - dequeue_done;
    queue_total += submitted_us - scale_done;
    ++profiled_submissions;
    if (profiled_submissions == 120)
    {
        switch_log(DIAGNOSTICS_DEBUG, "Present profile frames=%u dequeue-avg-us=%llu scale-avg-us=%llu queue-avg-us=%llu max-gap-us=%llu gaps-over-25ms=%u gaps-over-34ms=%u backend=%s",
                   profiled_submissions, (unsigned long long)(dequeue_total / profiled_submissions),
                   (unsigned long long)(scale_total / profiled_submissions), (unsigned long long)(queue_total / profiled_submissions),
                   (unsigned long long)longest_submission_gap, gaps_over_25ms, gaps_over_34ms,
                   docked_gpu_active ? "EGL (queue includes upload/draw/swap)" : "libnx");
        profiled_submissions = 0;
        dequeue_total = scale_total = queue_total = 0;
        longest_submission_gap = 0;
        gaps_over_25ms = gaps_over_34ms = 0;
    }
    last_submit_us = submitted_us - submit_start_us;
    if (last_submit_us > max_submit_us)
        max_submit_us = last_submit_us;
    if (!window_start)
    {
        window_start = submitted_us;
        frame_start_count = presented_frames;
    }
    uint64 window_elapsed = submitted_us - window_start;
    if (window_elapsed >= 1000000)
    {
        submission_fps = (unsigned)((1000000 * (presented_frames - frame_start_count) + window_elapsed / 2) / window_elapsed);
        window_start = submitted_us;
        frame_start_count = presented_frames;
    }
    if (nonblack)
        ++nonblack_frames;
    if (presented_frames <= 8 || presented_frames % 120 == 0 ||
        bitmap_width != previous_width || bitmap_height != previous_height ||
        source_x != previous_crop.x || source_y != previous_crop.y ||
        source_width != previous_crop.width || source_height != previous_crop.height ||
        nonblack != previous_nonblack)
        switch_log(DIAGNOSTICS_DEBUG,
                   "Video frame=%llu source=%dx%d crop=%d,%d %dx%d rgb-or=%06x nonblack=%llu failures=%llu output-fps=%u submit-us=%llu max-submit-us=%llu audio-buffers=%u audio-overruns=%u",
                   (unsigned long long)presented_frames,
                   (int)bitmap_width, (int)bitmap_height, (int)source_x, (int)source_y,
                   (int)source_width, (int)source_height, (unsigned)visible_colors,
                   (unsigned long long)nonblack_frames, (unsigned long long)failed_frames,
                   submission_fps, (unsigned long long)last_submit_us, (unsigned long long)max_submit_us,
                   (unsigned)g_xport_audio_submitted_buffers, (unsigned)g_xport_audio_callback_overruns);
    previous_width = bitmap_width;
    previous_height = bitmap_height;
    previous_crop = crop;
    previous_nonblack = nonblack;
    return 1;
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
    FILE *file = asset_preload_try_open(path, mode);
    const bool from_memory = file != NULL;
    if (from_memory)
        ++asset_preload_hits;
    else
        file = fopen(path, mode);
    int open_error = errno;
    if (file)
        switch_log(DIAGNOSTICS_DEBUG, "Opened %s (%s)%s", path, mode, from_memory ? " [RAM preload]" : "");
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

static void prepare_save_directory(void)
{
    if (mkdir(SAVE_DIRECTORY, 0777) != 0)
    {
        int directory_error = errno;
        struct stat directory;
        if (directory_error != EEXIST || stat(SAVE_DIRECTORY, &directory) != 0 || !S_ISDIR(directory.st_mode))
        {
            switch_log(DIAGNOSTICS_ERROR, "Save directory unavailable: %s (errno=%d)", SAVE_DIRECTORY, directory_error);
            return;
        }
    }
    switch_log(DIAGNOSTICS_DEBUG, "Save directory ready: %s", SAVE_DIRECTORY);
    struct stat existing;
    if (stat(SAVE_FILE, &existing) == 0 || errno != ENOENT ||
        stat(SAVE_BACKUP, &existing) == 0 || errno != ENOENT)
        return;
    if (rename(LEGACY_SAVE, SAVE_FILE) == 0)
        switch_log(DIAGNOSTICS_INFO, "Moved existing save into %s", SAVE_FILE);
    else if (errno != ENOENT)
        switch_log(DIAGNOSTICS_ERROR, "Legacy save migration failed (errno=%d); leaving the original in place", errno);
    else if (rename(LEGACY_SAVE ".bak", SAVE_BACKUP) == 0)
        switch_log(DIAGNOSTICS_INFO, "Moved existing save backup into %s", SAVE_BACKUP);
}

sint32 xport_save(const void *data, sint32 size)
{
    if (!data || size <= 0)
        return -1;
    FILE *file = fopen(SAVE_TEMP, "wb");
    if (!file)
    {
        switch_log(DIAGNOSTICS_ERROR, "Could not open save staging file %s (errno=%d)", SAVE_TEMP, errno);
        return -1;
    }
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
    switch_log(DIAGNOSTICS_DEBUG, "Saved %d bytes to %s", (int)size, SAVE_FILE);
    return 1;

fail:
    switch_log(DIAGNOSTICS_ERROR, "Save write or replacement failed for %s (errno=%d)", SAVE_FILE, errno);
    remove(SAVE_TEMP);
    return -1;
}

sint32 xport_load(void *data, sint32 capacity)
{
    if (!data || capacity <= 0)
        return -1;
    const char *paths[] = {SAVE_FILE, SAVE_BACKUP, LEGACY_SAVE, LEGACY_SAVE ".bak"};
    FILE *file = NULL;
    const char *path = SAVE_FILE;
    for (unsigned index = 0; index < sizeof(paths) / sizeof(*paths); ++index)
    {
        path = paths[index];
        file = fopen(path, "rb");
        if (file || errno != ENOENT)
            break;
    }
    if (!file)
    {
        int load_error = errno;
        switch_log(load_error == ENOENT ? DIAGNOSTICS_DEBUG : DIAGNOSTICS_ERROR,
                   "Save load: %s (errno=%d)", load_error == ENOENT ? "no save found" : path, load_error);
        return load_error == ENOENT ? 0 : -1;
    }
    long length = stream_length(file);
    bool read = length > 0 && length <= capacity && fseek(file, 0, SEEK_SET) == 0 &&
                fread(data, 1, (size_t)length, file) == (size_t)length;
    bool loaded = fclose(file) == 0 && read;
    switch_log(loaded ? DIAGNOSTICS_DEBUG : DIAGNOSTICS_ERROR, "Save load: %s size=%ld capacity=%d status=%s",
               path, length, (int)capacity, loaded ? "ok" : "failed");
    return loaded ? (sint32)length : -1;
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
    switch_clear_asset_preload();
    if (video_initialized)
    {
        video_initialized = false;
        if (docked_gpu_active)
        {
            docked_presenter_close();
            docked_gpu_active = false;
        }
        else
            framebufferClose(&switch_framebuffer);
    }
    shutting_down = false;
}

static void show_startup_splash(void)
{
    extern const uint32_t dracer_splash_pixels[1280 * 720];
    if (!xport_window_init())
        return;
    if (docked_gpu_active)
    {
        if (docked_presenter_splash(dracer_splash_pixels, 1280, 720) == DOCKED_PRESENTER_SUBMITTED)
            goto splash_submitted;
        fall_back_to_software_display();
        if (!xport_window_init())
            return;
    }
    u32 stride_bytes = 0;
    uint32_t *destination = framebufferBegin(&switch_framebuffer, &stride_bytes);
    if (!destination)
    {
        switch_log(DIAGNOSTICS_ERROR, "Could not dequeue the launcher splash framebuffer");
        return;
    }
    bool copied = stride_bytes % sizeof(*destination) == 0 && stride_bytes >= (size_t)display_width * sizeof(*destination);
    if (copied)
    {
        size_t stride_pixels = stride_bytes / sizeof(*destination);
        for (int row = 0; row < display_height; ++row)
        {
            uint32_t *output_row = destination + (size_t)row * stride_pixels;
            const uint32_t *input_row = dracer_splash_pixels + (row * 720 / display_height) * 1280;
            if (display_width == 1280)
                memcpy(output_row, input_row, 1280 * sizeof(*destination));
            else
                for (int column = 0; column < display_width; ++column)
                    output_row[column] = input_row[column * 1280 / display_width];
        }
    }
    framebufferEnd(&switch_framebuffer);
    if (!copied)
    {
        switch_log(DIAGNOSTICS_ERROR, "Could not copy launcher splash pixels (stride=%u)", (unsigned)stride_bytes);
        return;
    }
splash_submitted:
    switch_log(DIAGNOSTICS_INFO, "Launcher splash submitted for 5000 ms");
    const uint64 splash_deadline = xport_timer_get() + 5000000;
    while (xport_timer_get() < splash_deadline)
    {
        pump_applet();
        svcSleepThread(10000000);
    }
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
    prepare_save_directory();
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
        start_display_worker();
        pump_applet();
        const char *const startup_movies[] = {"DATA/INTRO.STR", "DATA/TATOU.STR", "DATA/PIT.STR"};
        start_asset_preload("boot", startup_movies, sizeof(startup_movies) / sizeof(startup_movies[0]), 32u * 1024u * 1024u);
        show_startup_splash();
        switch_join_asset_preload();
        switch_log(DIAGNOSTICS_INFO, "Entering reconstructed PS1 game code");
        xport_main();
    }
    game_running = false;
    switch_set_race_display_active(false);
    stop_display_worker();
    switch_log(DIAGNOSTICS_INFO, "Video totals: requested=%llu submitted=%llu nonblack=%llu failed=%llu; audio buffers=%u overruns=%u",
               (unsigned long long)present_attempts, (unsigned long long)presented_frames,
               (unsigned long long)nonblack_frames, (unsigned long long)failed_frames,
               (unsigned)g_xport_audio_submitted_buffers, (unsigned)g_xport_audio_callback_overruns);
    xport_shutdown();
    if (exit_locked)
        appletUnlockExit();
    switch_log(DIAGNOSTICS_INFO, "Shutdown complete (exit code=%d)", (int)exit_code);
    close_diagnostics();
    return exit_code;
}
