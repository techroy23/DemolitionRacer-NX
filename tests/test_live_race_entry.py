import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class LiveRaceEntryTest(unittest.TestCase):
    def test_automatic_solo_entry_and_stock_fallback(self):
        root = Path(__file__).resolve().parents[1]
        source = (root / 'src/game_race_setup.c').read_text()
        controller = source.split('uint32 sub_80013A9C(void)\n{', 1)[1]
        switch_block = controller.split('#if defined(AP_SWITCH)\n', 1)[1].split('#endif', 1)[0]
        platform = (root / 'src/platform/switch/platform.c').read_text()
        start = platform.index('bool switch_live_race_gpu_permitted(void)\n{')
        end = platform.index('void switch_live_race_gpu_log(', start)
        permission_checks = platform[start:end]
        fixture = '''#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include "diagnostics.h"
typedef unsigned uint32;
static unsigned racer_count, split_screen, demo, replay;
static bool armed, video_initialized, gpu_active, headless, race_display_active;
static DiagnosticSettings game_settings;
/* No marker files present at launch (old policy inputs). */
static const bool live_native_race_gpu_requested = false, live_race_perspective_requested = false;
''' + permission_checks + '''
static unsigned guest_read(unsigned address) {
    switch (address) {
        case 0x80065254u: return racer_count;
        case 0x800593A8u: return split_screen;
        case 0x80064B5Cu: return demo;
        case 0x80065110u: return replay;
        default: assert(0); return 0;
    }
}
#define R32(address) guest_read(address)
static void switch_set_race_display_active(bool active) { race_display_active = active; }
static void switch_live_race_gpu_scope(bool enabled) { armed = enabled; }
static void switch_live_race_gpu_entry_log(bool eligible, unsigned racers,
    unsigned split, unsigned demo_mode, unsigned replay_mode) {
    (void)eligible; (void)racers; (void)split; (void)demo_mode; (void)replay_mode;
}
static void evaluate(unsigned result) {
''' + switch_block + '''}
static void assert_platform_stock(void) {
    assert(!switch_live_race_gpu_permitted());
    assert(!switch_live_race_perspective_permitted());
}
int main(int argc, char **argv) {
    assert(argc == 2);
    video_initialized = gpu_active = true;
    assert_platform_stock();  /* Before entering a race. */
    racer_count = 16; evaluate(0); assert(armed);
    if (!strcmp(argv[1], "gpu")) {
        assert(switch_live_race_gpu_permitted());
        return 0;
    }
    if (!strcmp(argv[1], "perspective")) {
        assert(switch_live_race_perspective_permitted());
        return 0;
    }
    assert(!strcmp(argv[1], "fallback"));
    gpu_active = false; assert_platform_stock(); gpu_active = true;
    video_initialized = false; assert_platform_stock(); video_initialized = true;
    headless = true; assert_platform_stock(); headless = false;
    game_settings.car_lod_extended = true; assert_platform_stock(); game_settings.car_lod_extended = false;
    game_settings.track_distance_extended = true; assert_platform_stock(); game_settings.track_distance_extended = false;
    evaluate(1); assert(!armed);
    demo = 1; evaluate(0); assert(!armed); demo = 0;
    replay = 1; evaluate(0); assert(!armed); replay = 0;
    racer_count = 12; split_screen = 1; evaluate(0); assert(!armed);
    return 0;
}
'''
        compiler = os.environ.get('CC') or shutil.which('gcc') or shutil.which('clang')
        if not compiler:
            self.fail('A host C compiler is required')
        with tempfile.TemporaryDirectory() as temporary:
            fixture_path = Path(temporary) / 'entry.c'
            executable = Path(temporary) / 'entry.exe'
            fixture_path.write_text(fixture)
            subprocess.run([compiler, '-std=c11', '-I', str(root / 'src/platform/switch'),
                            str(fixture_path), '-o', str(executable)], check=True)
            for scenario in ('gpu', 'perspective', 'fallback'):
                with self.subTest(scenario=scenario):
                    subprocess.run([str(executable), scenario], check=True)


if __name__ == '__main__':
    unittest.main()
