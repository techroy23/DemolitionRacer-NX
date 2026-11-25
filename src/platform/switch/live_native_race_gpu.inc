/* Included only in the instrumented Switch copy of pinned psx_gpu.c. */
#include "race_gpu_mesa.h"
#include "race_perspective.h"
#include "docked_presenter.h"

extern bool switch_live_race_gpu_permitted(void);
extern bool switch_live_race_perspective_permitted(void);
extern void switch_live_race_gpu_log(const char *);
extern _Noreturn void switch_abort_game(const char *);
extern DockedPresenterResult switch_present_race_texture(const DockedPresenterBorrowedTexture *, int32_t, int32_t, FramebufferSource);

static Gpu *live_race_gpu;
static uint64_t live_race_generation;
static bool live_race_armed, live_race_latched;
static ReplayNativeOt live_race_ot;
static uint32_t live_race_upload[MAX_WORDS];

enum { PERSPECTIVE_PACKET_CAPACITY=8192, PERSPECTIVE_PACKET_PROBES=16 };
typedef struct {
    uint32 address;
    uint32 words[13];
    uint32 depths[4];
    RacePerspectiveSource source;
} RacePerspectivePacket;
static RacePerspectivePacket perspective_packets[PERSPECTIVE_PACKET_CAPACITY];
static uint32 perspective_arena_base, perspective_arena_end, perspective_ot_head;
static bool perspective_track_scope;
typedef struct {
    uint64 recorded, matched, enhanced, equal, missing, changed, format, invalid;
    uint64 producer[RACE_PERSPECTIVE_PRODUCER_REASONS];
} RacePerspectiveCoverage;
static RacePerspectiveCoverage perspective_coverage[2];
/* Producer provenance counts packets (not raster jobs or visible texels). */
typedef struct {
    uint64 recorded, matched, partial, changed, format, invalid;
    uint64 producer[RACE_PERSPECTIVE_PRODUCER_REASONS];
} RacePerspectiveProducerCoverage;
static RacePerspectiveProducerCoverage perspective_producer[RACE_PERSPECTIVE_SOURCES][2];
static uint64 perspective_bounds, perspective_capacity, perspective_duplicate;

void switch_race_perspective_fallback(unsigned arity, RacePerspectiveSource source, RacePerspectiveFallback reason)
{
    if (perspective_track_scope && (arity==3u || arity==4u) &&
        (unsigned)reason<RACE_PERSPECTIVE_PRODUCER_REASONS && (unsigned)source<RACE_PERSPECTIVE_SOURCES) {
        ++perspective_coverage[arity-3u].producer[reason];
        ++perspective_producer[source][arity-3u].producer[reason];
    }
}

void switch_race_perspective_begin_arena(uint32 guest_base, uint32 size)
{
    perspective_track_scope=false;
    perspective_arena_end=0;
    perspective_ot_head=REPLAY_OT_END;
    if (!live_race_armed || live_race_latched || !switch_live_race_perspective_permitted()) return;
    memset(perspective_packets,0,sizeof perspective_packets);
    const uint32 base=guest_base&0x1fffffffu;
    if (base && !(base&3u) && size>=8u && base<sizeof(DRAM) && size<=sizeof(DRAM)-base) {
        perspective_arena_base=base;
        perspective_arena_end=base+size;
    } else ++perspective_bounds;
}

void switch_race_perspective_track_scope(bool enabled)
{
    perspective_track_scope=enabled && perspective_arena_end!=0;
}

void switch_race_perspective_seal(uint32 guest_ot_head)
{
    perspective_track_scope=false;
    perspective_ot_head=guest_ot_head&0x1fffffffu;
}

bool switch_race_perspective_collecting(void) { return perspective_track_scope; }

static RacePerspectivePacket *perspective_packet_slot(uint32 address)
{
    const uint32 first=((address>>2u)*2654435761u)>>(32u-13u);
    for (unsigned probe=0;probe<PERSPECTIVE_PACKET_PROBES;++probe) {
        RacePerspectivePacket *entry=perspective_packets+((first+probe)&(PERSPECTIVE_PACKET_CAPACITY-1u));
        if (!entry->address || entry->address==address) return entry;
    }
    return NULL;
}

void switch_race_perspective_record_source(uint32 guest_address, const uint32 *packet,
    const uint32 *depths, RacePerspectiveSource source, bool partial)
{
    if (!perspective_track_scope || (unsigned)source>=RACE_PERSPECTIVE_SOURCES) return;
    const uint32 address=guest_address&0x1fffffffu;
    if ((address&3u) || address<perspective_arena_base || address>=perspective_arena_end ||
        perspective_arena_end-address<8u || packet!=(const uint32 *)(DRAM+address)) { ++perspective_bounds; return; }
    const unsigned count=(packet[0]>>24)+1u, opcode=packet[1]>>24;
    const unsigned arity=replay_perspective_arity(opcode,count-1u);
    if (!arity) {
        ++perspective_coverage[(opcode&8u)!=0].format;
        ++perspective_producer[source][(opcode&8u)!=0].format;
        return;
    }
    if (perspective_arena_end-address<count*4u) { ++perspective_bounds; return; }
    RacePerspectiveCoverage *coverage=perspective_coverage+(arity-3u);
    for (unsigned i=0;i<arity;++i)
        if (!depths[i] || depths[i]>=65535u) {
            ++coverage->invalid;
            ++perspective_producer[source][arity-3u].invalid;
            return;
        }
    RacePerspectivePacket *entry=perspective_packet_slot(address);
    if (!entry || entry->address) {
        // A repeated producer address invalidates this arena, even with identical words.
        if (entry) { perspective_track_scope=false; perspective_arena_end=0; ++perspective_duplicate; }
        else ++perspective_capacity;
        return;
    }
    entry->address=address;
    memcpy(entry->words,packet,count*4u);
    memcpy(entry->depths,depths,arity*sizeof *depths);
    entry->source=source;
    ++coverage->recorded;
    ++perspective_producer[source][arity-3u].recorded;
    perspective_producer[source][arity-3u].partial+=partial;
}

static const uint32_t *live_race_verified_packet_depths(uint32_t address,const uint32_t *packet,size_t body_words)
{
    if (!perspective_arena_end || address<perspective_arena_base || address>=perspective_arena_end) return NULL;
    const unsigned arity=replay_perspective_arity(packet[1]>>24,body_words);
    if (!arity) return NULL;
    RacePerspectiveCoverage *coverage=perspective_coverage+(arity-3u);
    const RacePerspectivePacket *entry=perspective_packet_slot(address);
    if (!entry || entry->address!=address) { ++coverage->missing; return NULL; }
    if (perspective_arena_end-address<(body_words+1u)*4u ||
        (entry->words[0]>>24)!=body_words || memcmp(entry->words,packet,(body_words+1u)*4u)) {
        ++coverage->changed;
        ++perspective_producer[entry->source][arity-3u].changed;
        return NULL;
    }
    ++coverage->matched;
    ++perspective_producer[entry->source][arity-3u].matched;
    return entry->depths;
}

static void live_race_require_stock_raster(void)
{
    if (live_race_gpu) switch_abort_game("Stock raster reached while live race GPU owns VRAM");
}

void switch_live_race_gpu_shutdown(void)
{
    perspective_arena_end=0;
    perspective_track_scope=false;
    if (live_race_gpu) {
        if (switch_live_race_perspective_permitted() || perspective_coverage[0].recorded || perspective_coverage[1].recorded) {
            char coverage[512];
            for (unsigned i=0;i<2;++i) {
                const RacePerspectiveCoverage *c=perspective_coverage+i;
                snprintf(coverage,sizeof coverage,"Race perspective arity=%u: recorded=%llu matched=%llu enhanced_jobs=%llu equal_jobs=%llu missing=%llu changed=%llu format=%llu invalid=%llu rtpt=%llu rtps=%llu clip=%llu subdivide=%llu",i+3u,
                    (unsigned long long)c->recorded,(unsigned long long)c->matched,(unsigned long long)c->enhanced,
                    (unsigned long long)c->equal,(unsigned long long)c->missing,(unsigned long long)c->changed,
                    (unsigned long long)c->format,(unsigned long long)c->invalid,
                    (unsigned long long)c->producer[RACE_PERSPECTIVE_RTPT],(unsigned long long)c->producer[RACE_PERSPECTIVE_RTPS],
                    (unsigned long long)c->producer[RACE_PERSPECTIVE_CLIP],(unsigned long long)c->producer[RACE_PERSPECTIVE_SUBDIVIDE]);
                switch_live_race_gpu_log(coverage);
            }
            for (unsigned source=RACE_PERSPECTIVE_NEAR_LEAF;source<RACE_PERSPECTIVE_SOURCES;++source)
                for (unsigned i=0;i<2;++i) {
                    const RacePerspectiveProducerCoverage *c=&perspective_producer[source][i];
                    snprintf(coverage,sizeof coverage,
                        "Race perspective %s arity=%u: recorded_packets=%llu matched_packets=%llu partial_packets=%llu changed=%llu format=%llu invalid=%llu rtpt=%llu rtps=%llu clip=%llu near_helper_entries=%llu",
                        source==RACE_PERSPECTIVE_NEAR_LEAF?"near-leaf":"near-parent",i+3u,
                        (unsigned long long)c->recorded,(unsigned long long)c->matched,(unsigned long long)c->partial,
                        (unsigned long long)c->changed,(unsigned long long)c->format,(unsigned long long)c->invalid,
                        (unsigned long long)c->producer[RACE_PERSPECTIVE_RTPT],(unsigned long long)c->producer[RACE_PERSPECTIVE_RTPS],
                        (unsigned long long)c->producer[RACE_PERSPECTIVE_CLIP],(unsigned long long)c->producer[RACE_PERSPECTIVE_SUBDIVIDE]);
                    switch_live_race_gpu_log(coverage);
                }
            snprintf(coverage,sizeof coverage,"Race perspective sidecar: bounds=%llu probe_full=%llu duplicate=%llu",
                (unsigned long long)perspective_bounds,(unsigned long long)perspective_capacity,(unsigned long long)perspective_duplicate);
            switch_live_race_gpu_log(coverage);
        }
        const RaceGpuMesaCounters stats = race_gpu_mesa_counters(live_race_gpu);
        char line[512];
        snprintf(line, sizeof(line), "Live race GPU: seed_bytes=%llu jobs=%llu dispatches=%llu tile_dispatches=%llu serial_dispatches=%llu tile_workgroups=%llu hazard_splits=%llu capacity_splits=%llu peak_batch_jobs=%u peak_tiles=%u submit_us=%llu reads=%llu read_bytes=%llu scanouts=%llu boundary_bytes=%llu",
                (unsigned long long)stats.seed_bytes, (unsigned long long)stats.jobs,
                (unsigned long long)stats.dispatches, (unsigned long long)stats.tile_dispatches,
                (unsigned long long)stats.serial_dispatches, (unsigned long long)stats.tile_workgroups,
                (unsigned long long)stats.hazard_splits, (unsigned long long)stats.capacity_splits,
                stats.peak_batch_jobs, stats.peak_tiles, (unsigned long long)(stats.dispatch_submit_ns / 1000),
                (unsigned long long)stats.store_reads,
                (unsigned long long)stats.store_bytes, (unsigned long long)stats.scanouts,
                (unsigned long long)stats.boundary_bytes);
        switch_live_race_gpu_log(line);
        race_gpu_mesa_close(live_race_gpu);
        live_race_gpu = NULL;
    }
    memset(perspective_coverage,0,sizeof perspective_coverage);
    memset(perspective_producer,0,sizeof perspective_producer);
    perspective_bounds=perspective_capacity=perspective_duplicate=0;
}

static void live_race_handoff(const char *reason)
{
    if (!live_race_gpu) return;
    /* Registers are kept on CPU. Only the exact staged data plane is stale. */
    const uint32_t *stage = race_gpu_mesa_stage_boundary(live_race_gpu);
    if (!stage) switch_abort_game("Live race GPU could not restore authoritative VRAM");
    memcpy(VRAM, stage, sizeof(VRAM));
    gpu_vram_invalidate();
    char handoff_message[160];
    snprintf(handoff_message, sizeof(handoff_message),
             "Live race GPU: restored VRAM before %s; stock latched", reason);
    switch_live_race_gpu_log(handoff_message);
    live_race_latched = true;
    switch_live_race_gpu_shutdown();
}

void switch_live_race_gpu_before_stock_present(void)
{
    live_race_handoff("stock presenter");
}

void switch_live_race_gpu_scope(bool enabled)
{
    if (!enabled) {
        perspective_arena_end=0;
        perspective_track_scope=false;
        live_race_handoff("leaving single-player race");
    }
    live_race_armed = enabled;
}

static bool live_race_compatible(void)
{
    return !transfer_observer && !linked_submit && !linked_pause && !linked_trace_operation &&
           !xport_device_trace_enabled() && !render_list_capture && !native_linked_cursor &&
           !native_paused_packet && !gpu_port.count && !gpu_port.transfer && raster_enabled() &&
           !(display_state & 4u) && !(native_scanout.mode & 0x3cu) &&
           display_x == 0 && (display_y == 8 || display_y == 264) &&
           display_width == 320 && display_height == 240 && !raster_surface_x && !raster_surface_y;
}

static void live_race_submit(const Job *jobs, unsigned count, const uint32_t *words, size_t word_count)
{
    if (count && !race_gpu_mesa_submit(live_race_gpu, jobs, count, words, word_count, NULL))
        switch_abort_game("Live race GPU submission failed; no stock retry");
}

static bool live_race_draw_ot(uint32 *ordering_table)
{
    if (!live_race_armed || live_race_latched) return false;
    if (!live_race_compatible()) {
        live_race_handoff("unsupported draw state");
        return false;
    }
    /* Scope the first version to the canonical guest-backed track tables.
       Registered mappings have stock priority, so never silently ignore one. */
    uint32_t head;
    if (!gpu_guest_address(ordering_table, &head) || packet_range_count) {
        live_race_handoff("noncanonical OT backing");
        return false;
    }
    const ReplayDrawState initial = {
        raster_clip_x0, raster_clip_y0, raster_clip_x1, raster_clip_y1,
        raster_offset_x, raster_offset_y, active_tpage, texture_window,
        gpu_port.mask, (active_draw_flags >> 9) & 1u
    };
    const ReplayOtRange range = {.base=0, .words=(const uint32_t *)DRAM, .word_count=sizeof(DRAM)/4};
    if (!live_race_gpu && !switch_live_race_gpu_permitted()) return false;
    ReplayOtError error;
    const bool perspective=switch_live_race_perspective_permitted();
    const bool consumes_perspective_arena=perspective_arena_end && head==perspective_ot_head;
    const uint64 matched_before=perspective_coverage[0].matched+perspective_coverage[1].matched;
    ReplayOtStatus status = replay_prepare_native_ot_with_depths(&live_race_ot, initial, &range, 1, head,
        REPLAY_OT_MAX_TAGS, &error, perspective && consumes_perspective_arena ? live_race_verified_packet_depths : NULL);
    if (consumes_perspective_arena) {
        perspective_arena_end=0;
        perspective_track_scope=false;
    }
    if (status != REPLAY_OT_OK) {
        fprintf(stderr, "Live race GPU: OT preflight rejected (%d) link=%06x word=%zu\n", status, error.link, (size_t)error.word);
        switch_abort_game("Live race GPU rejected whole OT before submission");
    }
    if (!live_race_gpu) {
        live_race_generation = docked_presenter_context_generation();
        if (!live_race_generation) return false;
        live_race_gpu = race_gpu_mesa_open_current_with_perspective(perspective);
        if (!live_race_gpu || !race_gpu_mesa_seed(live_race_gpu, VRAM)) {
            live_race_latched = true;
            switch_live_race_gpu_shutdown();
            return false;
        }
        switch_live_race_gpu_log(perspective ? "Live race GPU: single-player ownership active (ordered tiles, presentation perspective)" :
                                      "Live race GPU: single-player ownership active (ordered tiles)");
    }
    if (!race_gpu_mesa_submit_ot(live_race_gpu, &live_race_ot))
        switch_abort_game("Live race GPU OT submission failed; no stock retry");
    if (perspective && perspective_coverage[0].matched+perspective_coverage[1].matched!=matched_before)
        for (unsigned job=0;job<live_race_ot.plan.count;++job) {
            const uint32 *w=live_race_ot.plan.jobs[job].w;
            if (!w[PERSPECTIVE_ARITY]) continue;
            RacePerspectiveCoverage *c=perspective_coverage+w[PERSPECTIVE_ARITY]-3u;
            if (w[PERSPECTIVE_VERTEX+2]) ++c->enhanced;
            else ++c->equal;
        }
    /* All render state changes are committed on CPU, never by running stock raster.
       E1 flags are distinct from the page modified by polygon tpage writeback. */
    const ReplayDrawState final = live_race_ot.plan.state;
    if (live_race_ot.last_e1) active_draw_flags = (uint16_t)(live_race_ot.last_e1 & 0x600u);
    active_tpage = (uint16_t)final.tp;
    texture_window = final.window;
    gpu_port.mask = final.mask;
    raster_clip_x0 = final.x0; raster_clip_y0 = final.y0;
    raster_clip_x1 = final.x1; raster_clip_y1 = final.y1;
    raster_offset_x = final.dx; raster_offset_y = final.dy;
    return true;
}

static bool live_race_load(const PSX_RECT *rectangle, const uint16_t *source)
{
    if (!live_race_gpu) return false;
    if (!rectangle->w || !rectangle->h) return true;
    const unsigned x = gpu_wrap_x(rectangle->x), y = gpu_wrap_y(rectangle->y);
    const unsigned w = rectangle->w, h = rectangle->h;
    if (w > 1024 - x || h > 512 - y) {
        live_race_handoff("wrapping native upload");
        return false;
    }
    const size_t pixels = (size_t)w * h;
    memcpy(live_race_upload, source, pixels * 2);
    if (pixels & 1) ((uint16_t *)live_race_upload)[pixels] = 0;
    Job job = {{0}};
    job.w[OP] = UPLOAD;
    job.w[LX] = job.w[X0] = x; job.w[LY] = job.w[Y0] = y;
    job.w[HX] = x + w; job.w[HY] = y + h; job.w[WIDTH] = w;
    live_race_submit(&job, 1, live_race_upload, (pixels + 1) / 2);
    return true;
}

static bool live_race_store(const PSX_RECT *rectangle, uint16_t *destination)
{
    if (!live_race_gpu) return false;
    if (!rectangle->w || !rectangle->h) return true;
    /* GPU fast path only for the explicitly bounded 512-byte race mask buffer. */
    if (destination != (uint16_t *)psx_addr(0x800C7488u, 512u)) {
        live_race_handoff("non-race StoreImage consumer");
        return false;
    }
    switch (race_gpu_store(race_gpu_mesa_gather, live_race_gpu, rectangle->x, rectangle->y,
                           rectangle->w, rectangle->h, destination, 512u)) {
        case RACE_GPU_COMPLETE: return true;
        case RACE_GPU_REJECTED:
            live_race_handoff("unsupported StoreImage rectangle");
            return false;
        default: switch_abort_game("Live race GPU StoreImage failed before destination inspection");
    }
}

static bool live_race_background(uint32 color)
{
    if (!live_race_gpu) return false;
    int x0 = raster_clip_x0 + raster_surface_x, x1 = raster_clip_x1 + raster_surface_x;
    int y0 = raster_clip_y0 + raster_surface_y, y1 = raster_clip_y1 + raster_surface_y;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > 1024) x1 = 1024;
    if (y1 > 512) y1 = 512;
    if (x0 < x1 && y0 < y1) {
        Job job = {{0}};
        job.w[OP] = REPLAY_RECT; job.w[LX] = x0; job.w[HX] = x1; job.w[LY] = y0; job.w[HY] = y1;
        job.w[COLOR] = ((color >> 16) & 255u) | (color & 0xff00u) | ((color & 255u) << 16);
        job.w[FLAGS] = (gpu_port.mask & 1 ? SET_MASK : 0) | (gpu_port.mask & 2 ? TEST_MASK : 0);
        live_race_submit(&job, 1, NULL, 0);
    }
    return true;
}

static bool live_race_present(void)
{
    if (!live_race_gpu) return false;
    if (!live_race_compatible()) {
        live_race_handoff("unsupported display state");
        return false;
    }
    if (docked_presenter_context_generation() != live_race_generation)
        switch_abort_game("Live race GPU presenter context changed during ownership");
    const unsigned name = race_gpu_mesa_scanout(live_race_gpu, display_x, display_y,
                         display_width, display_height, 0, 0, display_width, display_height,
                         display_offset_x, display_offset_y);
    if (!name) switch_abort_game("Live race GPU scanout failed");
    const DockedPresenterBorrowedTexture texture = {name, live_race_generation, display_width, display_height};
    const FramebufferSource crop = {0, 0, display_width, display_height};
    const DockedPresenterResult result = switch_present_race_texture(&texture, display_width, display_height, crop);
    if (result == DOCKED_PRESENTER_SUBMITTED) return true;
    if (result == DOCKED_PRESENTER_MODE_CHANGED) {
        live_race_handoff("presenter mode change");
        return false;
    }
    switch_abort_game("Live race GPU presenter failed; no stock retry");
}
