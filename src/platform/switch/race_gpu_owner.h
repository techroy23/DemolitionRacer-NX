#ifndef DRACER_RACE_GPU_OWNER_H
#define DRACER_RACE_GPU_OWNER_H
#include <stdbool.h>

/* Single render thread; initialize to zero. Do not reset after BLOCKED unless
 * the caller has explicitly restarted from a known-good whole-game boundary. */
typedef enum { RACE_OWNER_STOCK, RACE_OWNER_GPU, RACE_OWNER_STOCK_LATCHED, RACE_OWNER_BLOCKED } RaceOwnerState;
typedef struct { RaceOwnerState state; } RaceGpuOwner;
typedef enum {
    RACE_GPU_COMPLETE,   /* Accepted in order, including any synchronous read result. */
    RACE_GPU_REJECTED,   /* Guaranteed no operation effects, including visible state/output. */
    RACE_GPU_UNCERTAIN   /* Partial submission, lost context or otherwise untrusted state. */
} RaceGpuStep;
typedef struct {
    bool (*seed)(void *context);    /* CPU -> GPU; CPU unmodified on failure. */
    bool (*restore)(void *context); /* Drain exact accepted prefix, stage and publish CPU state only on success. */
    void (*stock)(void *context, const void *operation);
    RaceGpuStep (*gpu)(void *context, const void *operation);
} RaceGpuOps;

/* Callbacks and owner must be valid and stable; callbacks must not reenter. An
 * operation may carry its native results separately from these routing results.
 * boundary means no partial DMA/FIFO/env update and no pending callback; failed
 * execute means the caller MUST stop, not return fake native success. */
bool race_gpu_enter(RaceGpuOwner *, const RaceGpuOps *, void *, bool eligible, bool boundary);
bool race_gpu_execute(RaceGpuOwner *, const RaceGpuOps *, void *, const void *operation, bool boundary);
bool race_gpu_leave(RaceGpuOwner *, const RaceGpuOps *, void *, bool boundary);
#endif
