#include <switch.h>
#include <inttypes.h>
#include "resume_switch.h"
#include "resume_clock.h"

/* Hooks are dispatched synchronously by the main-thread applet pump.
   The display worker does not write this trace. */
static ResumeTrace trace;
static AppletHookCookie hook_cookie;
static uint64_t raw_base, last_pump, last_probe, last_active_at, last_active_ns, audio_drops;
static bool has_active_api, probe_pending, active_valid;
#ifdef DRACER_RESUME_CLOCK
static ResumeClock clock;
#endif

uint64_t resume_switch_now(void)
{
    return armTicksToNs(armGetSystemTick() - raw_base) / 1000;
}

void resume_switch_end(ResumeStage stage, uint64_t begin, uint64_t value, bool force)
{
    resume_trace_span(&trace, stage, begin, resume_switch_now(), value, force);
}

static uint64_t applet_snapshot(void)
{
    return ((uint64_t)appletGetFocusState() << 16) |
        ((uint64_t)appletGetOperationMode() << 8) | appletGetPerformanceMode();
}

static void lifecycle(AppletHookType type, void *ignored)
{
    (void)ignored;
    switch (type)
    {
    case AppletHookType_OnFocusState:
    case AppletHookType_OnOperationMode:
    case AppletHookType_OnPerformanceMode:
    case AppletHookType_OnExitRequest:
    case AppletHookType_OnResume:
    case AppletHookType_RequestToDisplay:
        break;
    default:
        return;
    }
    /* libnx updates these caches before invoking the corresponding hook. */
    const uint64_t snapshot = ((uint64_t)type << 24) | applet_snapshot();
    const uint64_t now = resume_switch_now();
    resume_trace_span(&trace, RESUME_HOOK, now, now, snapshot, true);
    probe_pending = true;
}

void resume_switch_probe(bool force)
{
    const uint64_t now = resume_switch_now();
    const bool gap = now - last_pump >= RESUME_LONG_US;
    if (gap)
        resume_trace_span(&trace, RESUME_PUMP_GAP, last_pump, now, 0, true);
    last_pump = now;
    const bool interesting = force || gap || probe_pending;
    if (!interesting && now - last_probe < 1000000)
        return;
    probe_pending = false;
    last_probe = now;
    if (!has_active_api)
        return;
    u64 ns = 0;
    const Result rc = appletGetProgramTotalActiveTime(&ns);
    const uint64_t end = resume_switch_now();
    resume_trace_span(&trace, RESUME_ACTIVE_ERROR, now, end, rc, R_FAILED(rc));
    if (R_SUCCEEDED(rc))
    {
        if (interesting)
        {
            if (active_valid)
                resume_trace_span(&trace, RESUME_ACTIVE_PREVIOUS, last_active_at, last_active_at, last_active_ns, true);
            resume_trace_span(&trace, RESUME_ACTIVE, now, end, ns, true);
        }
        last_active_at = end;
        last_active_ns = ns;
    }
    active_valid = R_SUCCEEDED(rc);
}

void resume_switch_start(void)
{
    raw_base = armGetSystemTick();
    has_active_api = hosversionAtLeast(6, 0, 0);
    appletHook(&hook_cookie, lifecycle, NULL);
    RESUME_BEGIN(initial);
    RESUME_END(RESUME_INITIAL_STATE, initial, applet_snapshot(), true);
    RESUME_BEGIN(restart);
    const Result rc = appletSetRestartMessageEnabled(true);
    RESUME_END(RESUME_RESTART, restart, rc, true);
    resume_switch_probe(true);
#ifdef DRACER_RESUME_CLOCK
    resume_clock_init(&clock, has_active_api, 0, active_valid, last_active_ns / 1000);
#endif
}

#ifdef DRACER_RESUME_CLOCK
uint64_t resume_switch_clock(uint64_t raw_us)
{
    u64 ns = 0;
    Result rc = 0;
    if (clock.active)
    {
        RESUME_BEGIN(sample);
        rc = appletGetProgramTotalActiveTime(&ns);
        RESUME_END(RESUME_CLOCK_SAMPLE, sample, rc, R_FAILED(rc));
    }
    return resume_clock_sample(&clock, raw_us, R_SUCCEEDED(rc), ns / 1000);
}
#endif

void resume_switch_audio_drop(void)
{
    ++audio_drops;
}

void resume_switch_stop(FILE *log)
{
    appletUnhook(&hook_cookie);
    if (!log)
        return;
    static const char *const names[] = {
        [RESUME_HOOK] = "hook", [RESUME_INITIAL_STATE] = "initial-state",
        [RESUME_PUMP] = "pump", [RESUME_PUMP_GAP] = "pump-gap",
        [RESUME_RESTART] = "restart-enable", [RESUME_ACTIVE] = "active-ns",
        [RESUME_ACTIVE_PREVIOUS] = "previous-active-ns",
        [RESUME_ACTIVE_ERROR] = "active-read", [RESUME_CLOCK_SAMPLE] = "clock-read",
        [RESUME_VBLANK_ELAPSED] = "vblank-elapsed-us", [RESUME_BATCH] = "vblank-batch",
        [RESUME_GPU_PRESENT] = "gpu-present",
        [RESUME_DEQUEUE] = "fb-dequeue", [RESUME_BLIT] = "fb-blit", [RESUME_QUEUE] = "fb-queue",
        [RESUME_SWAP] = "egl-swap", [RESUME_AUDIO_RELEASE] = "audio-release",
        [RESUME_AUDIO_APPEND] = "audio-append", [RESUME_AUDIO_MIX] = "audio-mix", [RESUME_CHORD] = "exit-chord"
    };
#ifdef DRACER_RESUME_CLOCK
    const char *mode = clock.active ? "clock-active" : "clock-raw-fallback";
#else
    const char *mode = "trace-only";
#endif
    fprintf(log, "[resume] mode=%s hos-active-api=%d active-probe-valid=%d events=%" PRIu64
        " overwritten=%" PRIu64 " full-ring-drops=%" PRIu64 "\n", mode, has_active_api, active_valid,
        trace.total, trace.total > RESUME_CAPACITY ? trace.total - RESUME_CAPACITY : 0, audio_drops);
    for (size_t i = 0; i < RESUME_CAPACITY; ++i)
    {
        const ResumeEvent *e = resume_trace_get(&trace, i);
        if (!e)
            break;
        fprintf(log, "[resume] %s start-us=%" PRIu64 " end-us=%" PRIu64 " value=%" PRIu64 "\n",
            names[e->stage], e->begin, e->end, e->value);
    }
    fflush(log);
}
