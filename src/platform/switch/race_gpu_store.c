#include "race_gpu_store.h"
#include <string.h>

RaceGpuStep race_gpu_store(RaceGpuGather gather, void *context, int x, int y,
                           int width, int height, void *destination, size_t capacity)
{
    const unsigned sx=(unsigned)x & 1023u, sy=(unsigned)y & 511u;
    if (!gather || !destination || width < 1 || width > 16 || height < 1 ||
        height > 16 || (unsigned)height > 512u - sy)
        return RACE_GPU_REJECTED;
    const size_t row_bytes=(size_t)width * 2u;
    const size_t packed_pitch=((size_t)width + 1u) / 2u * 4u;
    uint8_t staging[16u * 16u * 2u];
    if (capacity < row_bytes * (size_t)height)
        return RACE_GPU_REJECTED;
    if (!gather(context, sx, sy, (unsigned)width, (unsigned)height, staging))
        return RACE_GPU_UNCERTAIN;
    for (int row=0; row<height; ++row)
        memcpy((uint8_t *)destination + row * row_bytes, staging + row * packed_pitch, row_bytes);
    return RACE_GPU_COMPLETE;
}
