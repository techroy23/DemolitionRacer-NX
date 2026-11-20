#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* fopencookie on devkitA64 newlib and GNU hosts */
#endif
#include "asset_preload.h"

#include <errno.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifdef __SWITCH__
#include <switch.h>
static Thread preload_thread;
static Mutex preload_mutex;
#else
#include <pthread.h>
static pthread_t preload_thread;
static pthread_mutex_t preload_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

#define ASSET_PRELOAD_READ_CHUNK_BYTES (64u * 1024u)
#define ASSET_PRELOAD_WORKER_STACK_BYTES (64u * 1024u)
#define ASSET_PRELOAD_WORKER_PRIORITY 0x30 /* below the usual game-thread priority */

typedef struct AssetPreloadPayload
{
    _Atomic size_t reference_count;
    size_t length;
    unsigned char bytes[];
} AssetPreloadPayload;

typedef struct AssetPreloadEntry
{
    char path[ASSET_PRELOAD_MAX_PATH_BYTES];
    AssetPreloadPayload *payload;
} AssetPreloadEntry;

typedef struct AssetPreloadCursor
{
    AssetPreloadPayload *payload;
    int64_t position;
} AssetPreloadCursor;

typedef enum AssetPreloadPhase
{
    ASSET_PRELOAD_IDLE,
    ASSET_PRELOAD_RUNNING,
    ASSET_PRELOAD_PUBLISHED
} AssetPreloadPhase;

/* The worker exclusively owns entries/stats while running. A successful join
 * publishes them under the mutex. Open streams own separate payload references.
 */
static AssetPreloadPhase preload_phase;
static AssetPreloadEntry preload_entries[ASSET_PRELOAD_MAX_PATHS];
static AssetPreloadStats preload_stats;
static atomic_bool preload_cancel_requested;
static _Atomic size_t preload_resident_bytes;

static void lock_asset_preload(void)
{
#ifdef __SWITCH__
    mutexLock(&preload_mutex);
#else
    pthread_mutex_lock(&preload_mutex);
#endif
}

static void unlock_asset_preload(void)
{
#ifdef __SWITCH__
    mutexUnlock(&preload_mutex);
#else
    pthread_mutex_unlock(&preload_mutex);
#endif
}

static uint64_t asset_preload_clock(void)
{
#ifdef __SWITCH__
    return armGetSystemTick();
#else
    struct timespec timestamp;
    if (clock_gettime(CLOCK_MONOTONIC, &timestamp) != 0)
        return 0;
    return (uint64_t)timestamp.tv_sec * UINT64_C(1000000000) + (uint64_t)timestamp.tv_nsec;
#endif
}

static uint64_t asset_preload_elapsed_ns(uint64_t started)
{
    uint64_t elapsed = asset_preload_clock() - started;
#ifdef __SWITCH__
    return armTicksToNs(elapsed);
#else
    return elapsed;
#endif
}

static void release_asset_preload_payload(AssetPreloadPayload *payload)
{
    if (atomic_fetch_sub(&payload->reference_count, 1) == 1)
    {
        size_t released_bytes = payload->length;
        free(payload);
        atomic_fetch_sub(&preload_resident_bytes, released_bytes);
    }
}

static void load_asset_preload_entry(AssetPreloadEntry *entry)
{
    FILE *source = fopen(entry->path, "rb");
    struct stat file_status;
    if (!source || fstat(fileno(source), &file_status) != 0 ||
        !S_ISREG(file_status.st_mode) || file_status.st_size <= 0)
    {
        if (source)
            fclose(source);
        ++preload_stats.failed_count;
        return;
    }

    uintmax_t file_length = (uintmax_t)file_status.st_size;
    size_t resident_bytes = atomic_load(&preload_resident_bytes);
    if (file_length > LONG_MAX || file_length > SIZE_MAX - sizeof(AssetPreloadPayload) ||
        resident_bytes > preload_stats.budget_bytes ||
        file_length > preload_stats.budget_bytes - resident_bytes)
    {
        fclose(source);
        ++preload_stats.budget_skipped_count;
        return;
    }

    size_t payload_length = (size_t)file_length;
    AssetPreloadPayload *payload = malloc(sizeof(*payload) + payload_length);
    if (!payload)
    {
        fclose(source);
        ++preload_stats.failed_count;
        return;
    }
    payload->length = payload_length;
    atomic_init(&payload->reference_count, 1);
    /* Only this worker allocates payloads. Concurrent closes can only lower
     * resident bytes between the budget check and this reservation.
     */
    atomic_fetch_add(&preload_resident_bytes, payload_length);

    size_t received_bytes = 0;
    while (received_bytes < payload_length && !atomic_load(&preload_cancel_requested))
    {
        size_t chunk_length = payload_length - received_bytes;
        if (chunk_length > ASSET_PRELOAD_READ_CHUNK_BYTES)
            chunk_length = ASSET_PRELOAD_READ_CHUNK_BYTES;
        size_t chunk_received = fread(payload->bytes + received_bytes, 1, chunk_length, source);
        received_bytes += chunk_received;
        if (chunk_received != chunk_length)
            break;
    }
    bool read_failed = ferror(source) != 0;
    if (fclose(source) != 0)
        read_failed = true;
    if (received_bytes != payload_length || read_failed)
    {
        release_asset_preload_payload(payload);
        if (atomic_load(&preload_cancel_requested))
            ++preload_stats.cancelled_count;
        else
            ++preload_stats.failed_count;
        return;
    }

    entry->payload = payload;
    ++preload_stats.cached_count;
    preload_stats.cached_bytes += payload_length;
}

static void run_asset_preload_batch(void)
{
    uint64_t started = asset_preload_clock();
    for (size_t entry_index = 0; entry_index < preload_stats.requested_count; ++entry_index)
    {
        if (atomic_load(&preload_cancel_requested))
        {
            preload_stats.cancelled_count += preload_stats.requested_count - entry_index;
            break;
        }
        AssetPreloadEntry *entry = &preload_entries[entry_index];
        bool duplicate_path = false;
        for (size_t prior_index = 0; prior_index < entry_index; ++prior_index)
        {
            if (strcmp(entry->path, preload_entries[prior_index].path) == 0)
            {
                duplicate_path = true;
                break;
            }
        }
        if (duplicate_path)
            ++preload_stats.duplicate_count;
        else
            load_asset_preload_entry(entry);
    }
    preload_stats.worker_elapsed_ns = asset_preload_elapsed_ns(started);
}

#ifdef __SWITCH__
static void asset_preload_worker(void *unused)
{
    (void)unused;
    run_asset_preload_batch();
}

static bool start_asset_preload_worker(void)
{
    if (R_FAILED(threadCreate(&preload_thread, asset_preload_worker, NULL, NULL,
                              ASSET_PRELOAD_WORKER_STACK_BYTES, ASSET_PRELOAD_WORKER_PRIORITY, -2)))
        return false;
    if (R_SUCCEEDED(threadStart(&preload_thread)))
        return true;
    threadClose(&preload_thread);
    return false;
}

static bool join_asset_preload_worker(void)
{
    return R_SUCCEEDED(threadWaitForExit(&preload_thread)) && R_SUCCEEDED(threadClose(&preload_thread));
}
#else
static void *asset_preload_worker(void *unused)
{
    (void)unused;
    run_asset_preload_batch();
    return NULL;
}

static bool start_asset_preload_worker(void)
{
    return pthread_create(&preload_thread, NULL, asset_preload_worker, NULL) == 0;
}

static bool join_asset_preload_worker(void)
{
    return pthread_join(preload_thread, NULL) == 0;
}
#endif

bool asset_preload_start(const char *const *paths, size_t path_count, size_t byte_budget)
{
    if (!paths || !path_count || path_count > ASSET_PRELOAD_MAX_PATHS || !byte_budget)
        return false;

    lock_asset_preload();
    if (preload_phase != ASSET_PRELOAD_IDLE)
    {
        unlock_asset_preload();
        return false;
    }
    for (size_t path_index = 0; path_index < path_count; ++path_index)
    {
        const char *path = paths[path_index];
        size_t path_length = path ? strnlen(path, ASSET_PRELOAD_MAX_PATH_BYTES) : 0;
        if (!path_length || path_length == ASSET_PRELOAD_MAX_PATH_BYTES)
        {
            unlock_asset_preload();
            return false;
        }
        memcpy(preload_entries[path_index].path, path, path_length + 1);
    }
    preload_stats = (AssetPreloadStats){ .requested_count = path_count, .budget_bytes = byte_budget };
    atomic_store(&preload_cancel_requested, false);
    bool started = start_asset_preload_worker();
    if (started)
        preload_phase = ASSET_PRELOAD_RUNNING;
    else
        preload_stats = (AssetPreloadStats){0};
    unlock_asset_preload();
    return started;
}

void asset_preload_cancel(void)
{
    atomic_store(&preload_cancel_requested, true);
}

static bool finish_running_asset_preload(void)
{
    /* Only the main thread transitions phase. Lookups do not wait for SD. */
    if (preload_phase != ASSET_PRELOAD_RUNNING)
        return true;
    uint64_t wait_started = asset_preload_clock();
    if (!join_asset_preload_worker())
        return false;
    preload_stats.join_wait_ns = asset_preload_elapsed_ns(wait_started);
    return true;
}

bool asset_preload_join(AssetPreloadStats *stats)
{
    if (preload_phase == ASSET_PRELOAD_RUNNING)
    {
        if (!finish_running_asset_preload())
            return false;
        lock_asset_preload();
        preload_phase = ASSET_PRELOAD_PUBLISHED;
        unlock_asset_preload();
    }
    if (stats)
        *stats = preload_stats;
    return true;
}

bool asset_preload_clear(void)
{
    asset_preload_cancel();
    if (!finish_running_asset_preload())
        return false;

    lock_asset_preload();
    for (size_t entry_index = 0; entry_index < preload_stats.requested_count; ++entry_index)
    {
        AssetPreloadEntry *entry = &preload_entries[entry_index];
        if (entry->payload)
        {
            release_asset_preload_payload(entry->payload);
            entry->payload = NULL;
        }
    }
    preload_stats = (AssetPreloadStats){0};
    preload_phase = ASSET_PRELOAD_IDLE;
    unlock_asset_preload();
    return true;
}

static AssetPreloadCursor *acquire_asset_preload_cursor(const char *path, const char *mode)
{
    if (!path || !mode || (strcmp(mode, "rb") != 0 && strcmp(mode, "r") != 0))
        return NULL;

    AssetPreloadPayload *payload = NULL;
    lock_asset_preload();
    if (preload_phase == ASSET_PRELOAD_PUBLISHED)
    {
        for (size_t entry_index = 0; entry_index < preload_stats.requested_count; ++entry_index)
        {
            AssetPreloadEntry *entry = &preload_entries[entry_index];
            if (entry->payload && strcmp(path, entry->path) == 0)
            {
                payload = entry->payload;
                atomic_fetch_add(&payload->reference_count, 1);
                break;
            }
        }
    }
    unlock_asset_preload();
    if (!payload)
        return NULL;

    AssetPreloadCursor *cursor = malloc(sizeof(*cursor));
    if (!cursor)
    {
        release_asset_preload_payload(payload);
        return NULL;
    }
    *cursor = (AssetPreloadCursor){ .payload = payload, .position = 0 };
    return cursor;
}

static size_t read_asset_preload_cursor(AssetPreloadCursor *cursor, char *destination, size_t requested_bytes)
{
    if ((uintmax_t)cursor->position >= cursor->payload->length)
        return 0;
    size_t position = (size_t)cursor->position;
    size_t available_bytes = cursor->payload->length - position;
    size_t copied_bytes = requested_bytes < available_bytes ? requested_bytes : available_bytes;
    memcpy(destination, cursor->payload->bytes + position, copied_bytes);
    cursor->position += (int64_t)copied_bytes;
    return copied_bytes;
}

static int64_t seek_asset_preload_cursor(AssetPreloadCursor *cursor, int64_t offset, int origin)
{
    int64_t base_position;
    switch (origin)
    {
        case SEEK_SET: base_position = 0; break;
        case SEEK_CUR: base_position = cursor->position; break;
        case SEEK_END: base_position = (int64_t)cursor->payload->length; break;
        default: errno = EINVAL; return -1;
    }
    if (offset < -base_position)
    {
        errno = EINVAL;
        return -1;
    }
    if (offset > INT64_MAX - base_position)
    {
        errno = EOVERFLOW;
        return -1;
    }
    return cursor->position = base_position + offset;
}

static int close_asset_preload_cursor(void *cookie)
{
    AssetPreloadCursor *cursor = cookie;
    release_asset_preload_payload(cursor->payload);
    free(cursor);
    return 0;
}

/* UCRT has no cookie FILE; its host harness exercises the cursor callbacks. */
#if !defined(_WIN32) || defined(__SWITCH__)
#if defined(__SWITCH__) && defined(__LARGE64_FILES)
typedef _off64_t AssetPreloadCookieOffset;
#elif defined(__SWITCH__)
typedef off_t AssetPreloadCookieOffset;
#else
typedef off64_t AssetPreloadCookieOffset;
#endif

static ssize_t read_asset_preload_cookie(void *cookie, char *destination, size_t requested_bytes)
{
    return (ssize_t)read_asset_preload_cursor(cookie, destination, requested_bytes);
}

static int seek_asset_preload_cookie(void *cookie, AssetPreloadCookieOffset *offset, int origin)
{
    int64_t position = seek_asset_preload_cursor(cookie, *offset, origin);
    if (position < 0)
        return -1;
    *offset = (AssetPreloadCookieOffset)position;
    return 0;
}

FILE *asset_preload_try_open(const char *path, const char *mode)
{
    int previous_errno = errno;
    AssetPreloadCursor *cursor = acquire_asset_preload_cursor(path, mode);
    FILE *stream = NULL;
    if (cursor)
    {
        cookie_io_functions_t callbacks = {
            .read = read_asset_preload_cookie,
            .seek = seek_asset_preload_cookie,
            .close = close_asset_preload_cursor,
        };
        stream = fopencookie(cursor, "rb", callbacks);
        if (!stream)
            close_asset_preload_cursor(cursor);
    }
    errno = previous_errno;
    return stream;
}
#endif
