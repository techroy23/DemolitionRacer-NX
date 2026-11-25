#ifndef RACE_PERSPECTIVE_H
#define RACE_PERSPECTIVE_H
#include <stdbool.h>
#include <stdint.h>
void switch_race_perspective_begin_arena(uint32_t guest_base, uint32_t size);
void switch_race_perspective_track_scope(bool enabled);
void switch_race_perspective_seal(uint32_t guest_ot_head);
bool switch_race_perspective_collecting(void);
typedef enum { RACE_PERSPECTIVE_DIRECT, RACE_PERSPECTIVE_NEAR_LEAF, RACE_PERSPECTIVE_NEAR_PARENT,
    RACE_PERSPECTIVE_SOURCES } RacePerspectiveSource;
void switch_race_perspective_record_source(uint32_t guest_address, const uint32_t *packet,
    const uint32_t *depths, RacePerspectiveSource source, bool partial);
typedef enum { RACE_PERSPECTIVE_RTPT, RACE_PERSPECTIVE_RTPS, RACE_PERSPECTIVE_CLIP,
    RACE_PERSPECTIVE_SUBDIVIDE, RACE_PERSPECTIVE_PRODUCER_REASONS } RacePerspectiveFallback;
void switch_race_perspective_fallback(unsigned arity, RacePerspectiveSource source, RacePerspectiveFallback reason);
#endif
