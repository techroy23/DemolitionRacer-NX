#ifndef GAME_RACE_SCENE_PERSPECTIVE_H
#define GAME_RACE_SCENE_PERSPECTIVE_H

#include "race_perspective.h"

/* RTPT: SZ1..SZ3 match SXY0..SXY2. RTPS: only the new SZ3
 * matches the new SXY2. Neither AVSZ nor subdivision LOD Z is a divisor.
 * Snapshot before another GTE command can overwrite FLAG or shift the FIFO. */
static inline bool scene_read_projective_depths(uint32 *depths, unsigned first, unsigned count)
{
    const uint32 flags = xport_gte_read_control(31u);
    const uint32 h = (uint16)xport_gte_read_control(26u);
    for (unsigned i = 0; i < count; ++i)
        depths[i] = xport_gte_read_data(first + i);
    /* Conservative projection gate: MAC/IR overflow, SZ saturation, divide
     * overflow, and SXY saturation. IR0 depth-cue saturation is unrelated. */
    if ((flags & 0x7fffe000u) || !h) return false;
    for (unsigned i = 0; i < count; ++i)
        if (depths[i] >= 65535u || 2u * depths[i] <= h) return false;
    return true;
}

/* Inspect only the finished, linked stock packet (never speculative stores).
 * Partial outcodes do not change the projection: lowerer uses original XY
 * and clips only stock scanline bounds. A common outcode stays gated. */
static inline void scene_record_projected_face(uint32 packet, unsigned stride, unsigned arity,
    const uint32 *depths, bool rtpt_ok, bool rtps_ok, bool allow_partial, RacePerspectiveSource source)
{
    if (!rtpt_ok || !rtps_ok) {
        switch_race_perspective_fallback(arity, source, !rtpt_ok ? RACE_PERSPECTIVE_RTPT : RACE_PERSPECTIVE_RTPS);
        return;
    }
    const sint32 width = (sint32)r_u32(0x8006511Cu), height = (sint32)r_u32(0x80065120u);
    unsigned any = 0u, common = 15u;
    for (unsigned i = 0; i < arity; ++i) {
        const uint32 xy = r_u32(packet + 8u + i * stride);
        const sint32 x = (sint16)xy, y = (sint16)(xy >> 16);
        const unsigned out = (x < 0) | ((x >= width) << 1) | ((y < 0) << 2) | ((y >= height) << 3);
        any |= out;
        common &= out;
    }
    if (width <= 0 || height <= 0 || common || (any && !allow_partial)) {
        switch_race_perspective_fallback(arity, source, RACE_PERSPECTIVE_CLIP);
        return;
    }
    switch_race_perspective_record_source(packet, psx_addr(packet, 4u), depths, source, any != 0u);
}

#endif
