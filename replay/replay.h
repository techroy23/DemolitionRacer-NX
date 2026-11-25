#ifndef ORDERED_REPLAY_H
#define ORDERED_REPLAY_H
#include <stddef.h>
#include <stdint.h>
#define VRAM_BYTES (1024u*512u*2u)
/* The captured main race OT lowers to 2,684 jobs. Storage is static, and
 * overflow rejects the whole OT before any GPU submission. */
#define MAX_JOBS 4096u
#define MAX_WORDS (VRAM_BYTES/4u+8192u)
/* One 256-byte slot per job; see raster.comp. All coordinates physical. */
typedef struct { uint32_t w[64]; } Job;
enum { REPLAY_RECT=1, TRI, FILL, UPLOAD, COPY, COPY_ROW };
enum { OP, LX, LY, HX, HY, FLAGS, TP, CL, WIN, SRC, X0, Y0, COLOR, WIDTH,
       EDGE0=16, EDGE1=21, EDGE2=26, ATTR=32, PERSPECTIVE_VERTEX=47, PERSPECTIVE_ARITY=56 };
enum { TEX=1, RAW=2, SEMI=4, DITHER=8, SET_MASK=16, TEST_MASK=32 };
typedef struct { int x0,y0,x1,y1,dx,dy; unsigned tp,window,mask,dither; } ReplayDrawState;
typedef struct { ReplayDrawState state; Job jobs[MAX_JOBS]; unsigned count; } Plan;
typedef struct { unsigned left, top, right, bottom; } PackedWordRectangle;
PackedWordRectangle destination_words(const Job *job);
int texture_reads_intersect(const Job *reader, PackedWordRectangle destination);
#define MAX_BATCH_JOBS 32u
#define TILE_WORD_WIDTH 16u
#define TILE_HEIGHT 2u
#define TILE_COLUMNS (512u/TILE_WORD_WIDTH)
#define TILE_COUNT (TILE_COLUMNS*(512u/TILE_HEIGHT))
typedef struct {
    uint32_t masks[TILE_COUNT];
    uint32_t entries[2u*TILE_COUNT]; /* tile index, ordered job bit mask */
    unsigned count;
} TileSchedule;
/* Transactional: output count is zero on error; initial state is not mutated. */
int replay_prepare(Plan *out, ReplayDrawState initial, const uint32_t *words, size_t n, size_t *bad);
/* Preflight a bounded tagged packet from xport native DrawPrim/DrawOTag.
 * The scoped native plan does not support lines, fill, or wrap transfers.
 * Input for UPLOAD submission is packet+1; this does not publish state. */
int replay_prepare_native_packet(Plan *out, ReplayDrawState initial,
                                 const uint32_t *packet, size_t packet_words, size_t *bad);

#define REPLAY_OT_END 0x00ffffffu
#define REPLAY_OT_MAX_TAGS 65536u
#define REPLAY_OT_MAX_RANGES 256u
typedef struct {
    uint32_t base; /* Canonical low-24-bit byte address. */
    const uint32_t *words;
    size_t word_count;
} ReplayOtRange;
typedef enum {
    REPLAY_OT_OK, REPLAY_OT_BAD_INPUT, REPLAY_OT_BAD_LINK, REPLAY_OT_CYCLE,
    REPLAY_OT_TAG_LIMIT, REPLAY_OT_WORD_LIMIT, REPLAY_OT_JOB_LIMIT,
    REPLAY_OT_TRUNCATED, REPLAY_OT_UNSUPPORTED
} ReplayOtStatus;
typedef struct {
    uint32_t link;
    size_t word; /* Zero-based command offset in the tag body, or zero for a tag error. */
} ReplayOtError;
typedef struct {
    Plan plan;
    uint32_t last_e1; /* Zero when no native E1 word was staged. */
    size_t word_count;
    uint32_t words[MAX_WORDS]; /* Owned command storage for job SRC indices. */
    uint8_t visited[(REPLAY_OT_END+1u)/32u]; /* Private scratch: one bit per aligned link. */
} ReplayNativeOt;
/* Prepare only, synchronously, before any native walker or GPU effect. Use static/heap
 * storage for out. Ranges must be sorted, nonoverlapping in canonical link space,
 * word aligned, and proved readable by the caller. Keep inputs immutable during
 * the call and disjoint from out/error; no source bytes are borrowed after return.
 * A tag's entire body must be inside one range. Only explicit ranges are resolved;
 * no raw host pointers, address guessing, deferred markers or observer effects.
 * initial uses physical VRAM coordinates, exclusive clip maxima, effective signed
 * offsets, full native tpage, separate dither and mask. Raster must be enabled.
 * On any error counts are zero and out->plan.state is initial. On success submit
 * only out's jobs/words; publish the returned state only after successful submission. */
ReplayOtStatus replay_prepare_native_ot(ReplayNativeOt *out, ReplayDrawState initial,
                                        const ReplayOtRange *ranges, size_t range_count,
                                        uint32_t head, unsigned tag_limit, ReplayOtError *error);
typedef const uint32_t *(*ReplayPacketDepths)(uint32_t address, const uint32_t *packet, size_t body_words);
/* Only standalone opaque FT3/GT3/FT4/GT4 packets have a depth sidecar. */
static inline unsigned replay_perspective_arity(unsigned opcode, size_t body_words) {
    switch (opcode & ~1u) {
    case 0x24: return body_words==7 ? 3 : 0;
    case 0x34: return body_words==9 ? 3 : 0;
    case 0x2c: return body_words==9 ? 4 : 0;
    case 0x3c: return body_words==12 ? 4 : 0;
    default: return 0;
    }
}
ReplayOtStatus replay_prepare_native_ot_with_depths(ReplayNativeOt *out, ReplayDrawState initial,
    const ReplayOtRange *ranges, size_t range_count, uint32_t head, unsigned tag_limit,
    ReplayOtError *error, ReplayPacketDepths packet_depths);
extern const ReplayDrawState reset_state;
typedef struct Gpu Gpu;
Gpu *gpu_open(void);
int gpu_replay(Gpu *, const Job *, unsigned job_count, const uint32_t *words,
               size_t word_count, const TileSchedule *);
const uint32_t *gpu_read(Gpu *); /* Known-good full staging, END only. */
#define STORE_FBO_WIDTH 8u
#define STORE_FBO_HEIGHT 16u
#define STORE_MAX_PIXELS_PER_ROW (2u * STORE_FBO_WIDTH)
#define STORE_FBO_CAPACITY (4u * STORE_FBO_WIDTH * STORE_FBO_HEIGHT)
typedef struct {
    uint64_t draw_submit_ns, read_pixels_ns, restore_ns;
} StoreFboTiming;
uint64_t replay_now_ns(void);
int gpu_read_store_fbo(Gpu *, unsigned x, unsigned y, unsigned width, unsigned height,
                       void *destination, StoreFboTiming *);
int gpu_seed(Gpu *, const void *);
void gpu_close(Gpu *);
#endif
