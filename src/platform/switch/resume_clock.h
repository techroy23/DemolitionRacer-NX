#ifndef DRACER_RESUME_CLOCK_H
#define DRACER_RESUME_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint64_t origin, offset, last;
    bool active;
} ResumeClock;

static inline void resume_clock_init(ResumeClock *clock, bool enabled, uint64_t raw,
    bool valid, uint64_t active)
{
    const bool use_active = enabled && valid;
    *clock = (ResumeClock){use_active ? active : raw, 0, 0, use_active};
}

static inline uint64_t resume_clock_sample(ResumeClock *clock, uint64_t raw,
    bool valid, uint64_t active)
{
    if (clock->active && !valid)
    {
        /* Permanent fallback, with no uncertain raw gap injected at the switch. */
        clock->active = false;
        clock->origin = raw;
        clock->offset = clock->last;
    }
    const uint64_t sample = clock->active ? active : raw;
    if (sample >= clock->origin)
    {
        const uint64_t next = clock->offset + (sample - clock->origin);
        if (next > clock->last)
            clock->last = next;
    }
    return clock->last;
}

#endif
