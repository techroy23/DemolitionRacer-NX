#ifndef DRACER_RESUME_DIAG_H
#define DRACER_RESUME_DIAG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { RESUME_CAPACITY = 128, RESUME_LONG_US = 50000 };
typedef enum {
    RESUME_HOOK, RESUME_INITIAL_STATE, RESUME_PUMP, RESUME_PUMP_GAP, RESUME_RESTART, RESUME_ACTIVE,
    RESUME_ACTIVE_PREVIOUS,
    RESUME_ACTIVE_ERROR, RESUME_CLOCK_SAMPLE, RESUME_VBLANK_ELAPSED, RESUME_BATCH,
    RESUME_GPU_PRESENT, RESUME_DEQUEUE, RESUME_BLIT, RESUME_QUEUE, RESUME_SWAP,
    RESUME_AUDIO_RELEASE, RESUME_AUDIO_APPEND, RESUME_AUDIO_MIX, RESUME_CHORD
} ResumeStage;

typedef struct {
    uint64_t begin, end, value;
    ResumeStage stage;
} ResumeEvent;

typedef struct {
    ResumeEvent events[RESUME_CAPACITY];
    uint64_t total;
} ResumeTrace;

static inline void resume_trace_span(ResumeTrace *trace, ResumeStage stage,
    uint64_t begin, uint64_t end, uint64_t value, bool force)
{
    if (force || (end >= begin && end - begin >= RESUME_LONG_US))
        trace->events[trace->total++ % RESUME_CAPACITY] = (ResumeEvent){begin, end, value, stage};
}

static inline const ResumeEvent *resume_trace_get(const ResumeTrace *trace, size_t index)
{
    uint64_t count = trace->total < RESUME_CAPACITY ? trace->total : RESUME_CAPACITY;
    return index < count ? &trace->events[(trace->total - count + index) % RESUME_CAPACITY] : NULL;
}

#endif
