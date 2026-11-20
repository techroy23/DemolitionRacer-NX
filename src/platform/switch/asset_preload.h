#ifndef SWITCH_ASSET_PRELOAD_H
#define SWITCH_ASSET_PRELOAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define ASSET_PRELOAD_MAX_PATHS 64
#define ASSET_PRELOAD_MAX_PATH_BYTES 1024 /* including the terminating NUL */

typedef struct AssetPreloadStats
{
    size_t requested_count;
    size_t cached_count;
    size_t cached_bytes;
    size_t budget_bytes;
    size_t budget_skipped_count;
    size_t duplicate_count;
    size_t failed_count;
    size_t cancelled_count;
    uint64_t worker_elapsed_ns;
    uint64_t join_wait_ns;
} AssetPreloadStats;

/* Lifecycle calls belong to the main thread; only one phase may exist at a time.
 * Pass explicit paths as they will appear at xport_fopen (exact string match).
 * Start copies all paths before returning; the underlying files must be immutable
 * for the phase. Zero count/budget, invalid paths or an active phase fail start.
 * The budget caps raw payload bytes, including bytes pinned by older streams, not
 * fixed metadata, FILE/cursor allocations or the worker stack. Load in list order.
 */
bool asset_preload_start(const char *const *paths, size_t path_count, size_t byte_budget);

/* Cooperative: interrupts between bounded read chunks, not inside a filesystem
 * call. Completed files remain eligible to publish; partial files never do.
 */
void asset_preload_cancel(void);

/* Wait for and stop the worker, then publish completed entries for try_open.
 * stats may be NULL. True means joined, not that all files loaded successfully.
 * Can be called again for the same phase. With no phase, returns zero stats.
 */
bool asset_preload_join(AssetPreloadStats *stats);

/* Cancel/join any worker and unpublish the cache. Streams already open remain
 * valid until fclose, even across a later batch. False leaves the phase intact.
 */
bool asset_preload_clear(void);

/* Cache-only read lookup for "r"/"rb". Returns an independent FILE or NULL;
 * on NULL xport_fopen should call the original fopen(path, mode). On a hit,
 * use ordinary stdio read/seek/close; the FILE has no SD file descriptor.
 */
FILE *asset_preload_try_open(const char *path, const char *mode);

#endif
