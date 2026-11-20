"""Exercise the real preload worker against temporary raw files on the host."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include <stdio.h>
#include <pthread.h>

static pthread_mutex_t read_gate_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t read_gate_changed = PTHREAD_COND_INITIALIZER;
static int pause_read;
static int reads_before_pause;
static int read_entered;
static int release_read;

static size_t gated_fread(void *buffer, size_t size, size_t count, FILE *stream)
{
    pthread_mutex_lock(&read_gate_mutex);
    if (pause_read && reads_before_pause-- == 0) {
        read_entered = 1;
        pthread_cond_signal(&read_gate_changed);
        while (!release_read)
            pthread_cond_wait(&read_gate_changed, &read_gate_mutex);
    }
    pthread_mutex_unlock(&read_gate_mutex);
    return fread(buffer, size, count, stream);
}

#define fread gated_fread
#include "asset_preload.c"
#undef fread
#include <assert.h>

#ifdef _WIN32
/* UCRT has no cookie FILE: use the production stream callbacks directly. */
typedef AssetPreloadCursor TestStream;
#define open_ram acquire_asset_preload_cursor
#define close_ram close_asset_preload_cursor
static int read_byte(TestStream *stream)
{
    char byte;
    return read_asset_preload_cursor(stream, &byte, 1) ? (unsigned char)byte : EOF;
}
static int64_t seek_ram(TestStream *stream, long offset, int origin)
{
    return seek_asset_preload_cursor(stream, offset, origin);
}
#else
typedef FILE TestStream;
#define open_ram asset_preload_try_open
#define close_ram fclose
#define read_byte fgetc
static int64_t seek_ram(TestStream *stream, long offset, int origin)
{
    return fseek(stream, offset, origin) == 0 ? ftell(stream) : -1;
}
#endif

static void exercise_paths_budget_and_streams(void)
{
    char copied_path[] = "raw.bin";
    const char *paths[] = {"missing.bin", "oversize.bin", copied_path, "raw.bin", "short.bin"};
    AssetPreloadStats statistics;
    assert(!asset_preload_start(paths, 5, 0));
    assert(asset_preload_start(paths, 5, 5));
    memcpy(copied_path, "bad.bin", sizeof(copied_path));
    assert(!asset_preload_start(paths, 5, 5));
    assert(!open_ram("raw.bin", "rb")); /* unpublished until join */
    assert(asset_preload_join(&statistics));
    assert(statistics.requested_count == 5);
    assert(statistics.cached_count == 1 && statistics.cached_bytes == 5);
    assert(statistics.failed_count == 1 && statistics.budget_skipped_count == 2);
    assert(statistics.duplicate_count == 1 && statistics.cancelled_count == 0);
    assert(!open_ram("raw.bin", "r+b") && !open_ram("raw.bin", "wb"));
    assert(!open_ram("RAW.bin", "rb") && !open_ram("short.bin", "rb"));
    FILE *fallback = fopen("short.bin", "rb");
    assert(fallback && fgetc(fallback) == 'x' && fclose(fallback) == 0);

    TestStream *first = open_ram("raw.bin", "rb");
    TestStream *second = open_ram("raw.bin", "r");
    assert(first && second);
    assert(read_byte(first) == 0 && read_byte(first) == 13);
    assert(read_byte(second) == 0 && read_byte(second) == 13);
    assert(seek_ram(first, 1, SEEK_CUR) == 3 && read_byte(first) == 255);
    assert(seek_ram(second, -3, SEEK_CUR) == -1);
    assert(seek_ram(second, -1, SEEK_END) == 4 && read_byte(second) == 7);
    assert(read_byte(second) == EOF);
    assert(seek_ram(second, 1, SEEK_END) == 6 && read_byte(second) == EOF);
    assert(seek_ram(second, 0, SEEK_SET) == 0 && read_byte(second) == 0);
    assert(asset_preload_clear() && !open_ram("raw.bin", "rb"));

    const char *next_paths[] = {"short.bin"};
    assert(asset_preload_start(next_paths, 1, 5) && asset_preload_join(&statistics));
    assert(statistics.cached_count == 0 && statistics.budget_skipped_count == 1);
    assert(seek_ram(first, 0, SEEK_SET) == 0 && read_byte(first) == 0);
    assert(close_ram(first) == 0 && close_ram(second) == 0 && asset_preload_clear());
    assert(asset_preload_start(next_paths, 1, 5) && asset_preload_join(&statistics));
    assert(statistics.cached_count == 1 && statistics.cached_bytes == 1);
    assert(asset_preload_clear());
}

static void exercise_cancellation_during_read(void)
{
    const char *paths[] = {"short.bin", "oversize.bin", "raw.bin"};
    AssetPreloadStats statistics;
    pause_read = 1;
    reads_before_pause = 1;
    assert(asset_preload_start(paths, 3, 256 * 1024));
    pthread_mutex_lock(&read_gate_mutex);
    while (!read_entered)
        pthread_cond_wait(&read_gate_changed, &read_gate_mutex);
    asset_preload_cancel();
    release_read = 1;
    pthread_cond_signal(&read_gate_changed);
    pthread_mutex_unlock(&read_gate_mutex);
    assert(asset_preload_join(&statistics));
    assert(statistics.cancelled_count == 2 && statistics.cached_bytes == 1);
    assert(!open_ram("oversize.bin", "rb") && !open_ram("raw.bin", "rb"));
    TestStream *completed = open_ram("short.bin", "rb");
    assert(completed && read_byte(completed) == 'x' && close_ram(completed) == 0);
    assert(asset_preload_clear() && asset_preload_clear());
}

int main(void)
{
    exercise_paths_budget_and_streams();
    exercise_cancellation_during_read();
    return 0;
}
'''


class AssetPreloadTest(unittest.TestCase):
    def test_preload_worker_and_ram_streams(self):
        compiler = shlex.split(os.environ.get("CC", "cc"), posix=sys.platform != "win32")
        compiler_path = shutil.which(compiler[0])
        if compiler_path is None:
            self.skipTest("set CC to a host C compiler (GCC with winpthreads on Windows)")
        scratch = Path.home() / "AppData/Local/hermes/cache/scratch" if os.name == "nt" else None
        if scratch is not None and not scratch.is_dir():
            scratch = None
        with tempfile.TemporaryDirectory(prefix="asset-preload-", dir=scratch) as directory:
            work = Path(directory)
            harness = work / "harness.c"
            harness.write_text(HARNESS, encoding="utf-8")
            (work / "raw.bin").write_bytes(bytes([0, 13, 10, 255, 7]))
            (work / "short.bin").write_bytes(b"x")
            (work / "oversize.bin").write_bytes(b"z" * (192 * 1024))
            executable = work / "asset_preload_test.exe"
            environment = {**os.environ, **dict.fromkeys(("TMPDIR", "TEMP", "TMP"), work.as_posix())}
            if os.name == "nt":
                compiler_directory = str(Path(compiler_path).resolve().parent)
                environment["PATH"] = compiler_directory + os.pathsep + environment["PATH"]
            result = subprocess.run(
                [*compiler, "-D_GNU_SOURCE", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-pthread",
                 "-I", str(ROOT / "src/platform/switch"), str(harness), "-o", str(executable)],
                cwd=work, env=environment, capture_output=True, text=True, timeout=30,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(executable)], cwd=work, env=environment,
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
