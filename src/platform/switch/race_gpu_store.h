#ifndef DRACER_RACE_GPU_STORE_H
#define DRACER_RACE_GPU_STORE_H
#include <stddef.h>
#include <stdint.h>
#include "race_gpu_owner.h"

/* Gather must flush prior accepted writes in order, read an RGBA8UI FBO with
 * rows of ceil(width/2)*4 bytes, and return true only after read and restore
 * succeed. Output points to temporary storage, not game memory. */
typedef bool (*RaceGpuGather)(void *, unsigned x, unsigned y, unsigned width,
                             unsigned height, uint8_t *output);
RaceGpuStep race_gpu_store(RaceGpuGather gather, void *context, int x, int y,
                           int width, int height, void *destination, size_t capacity);
#endif
