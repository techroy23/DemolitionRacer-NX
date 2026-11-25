#ifndef DRACER_RACE_GPU_MESA_H
#define DRACER_RACE_GPU_MESA_H
#include "replay.h"
#include "race_gpu_store.h"

/* Candidate backend. Caller owns EGL and must close before destroying its
 * current context generation. Calls use the same context and draw thread. */
typedef struct {
    uint64_t seed_bytes, jobs, dispatches, dispatch_submit_ns;
    uint64_t tile_dispatches, serial_dispatches, tile_workgroups, hazard_splits, capacity_splits;
    unsigned peak_batch_jobs, peak_tiles;
    uint64_t store_reads, store_bytes, store_submit_ns, store_wait_ns, store_restore_ns;
    uint64_t scanouts, scanout_submit_ns, boundary_bytes;
} RaceGpuMesaCounters;
Gpu *race_gpu_mesa_open_current(void);
Gpu *race_gpu_mesa_open_current_with_perspective(bool perspective);
bool race_gpu_mesa_seed(Gpu *, const void *bytes);
bool race_gpu_mesa_submit(Gpu *, const Job *, unsigned, const uint32_t *, size_t, const TileSchedule *);
/* Submit one successfully preflighted native OT. Nothing remains queued across calls. */
bool race_gpu_mesa_submit_ot(Gpu *, const ReplayNativeOt *);
bool race_gpu_mesa_gather(void *, unsigned x, unsigned y, unsigned w, unsigned h, uint8_t *padded);
unsigned race_gpu_mesa_scanout(Gpu *, int x, int y, int w, int h, int crop_x, int crop_y,
                               int crop_w, int crop_h, int display_dx, int display_dy);
/* Exceptional, once per seed: returns private staging, never publishes VRAM.
 * Backend is drained and cannot accept further operations after this call. */
const uint32_t *race_gpu_mesa_stage_boundary(Gpu *);
RaceGpuMesaCounters race_gpu_mesa_counters(const Gpu *);
void race_gpu_mesa_close(Gpu *);
#endif
