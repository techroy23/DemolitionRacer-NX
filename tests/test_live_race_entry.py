import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class LiveRaceEntryTest(unittest.TestCase):
    def test_real_controller_gate_with_stock_racer_counts(self):
        root = Path(__file__).resolve().parents[1]
        source = (root / 'src/game_race_setup.c').read_text()
        controller = source.split('uint32 sub_80013A9C(void)\n{', 1)[1]
        switch_block = controller.split('#if defined(AP_SWITCH)\n', 1)[1].split('#endif', 1)[0]
        fixture = '''#include <assert.h>
#include <stdbool.h>
typedef unsigned uint32;
static unsigned racer_count, split_screen, demo, replay;
static bool armed;
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
static void switch_set_race_display_active(bool active) { (void)active; }
static void switch_live_race_gpu_scope(bool enabled) { armed = enabled; }
static void switch_live_race_gpu_entry_log(bool eligible, unsigned racers,
    unsigned split, unsigned demo_mode, unsigned replay_mode) {
    (void)eligible; (void)racers; (void)split; (void)demo_mode; (void)replay_mode;
}
static void evaluate(unsigned result) {
''' + switch_block + '''}
int main(void) {
    racer_count = 16; evaluate(0); assert(armed);
    evaluate(1); assert(!armed);
    demo = 1; evaluate(0); assert(!armed); demo = 0;
    replay = 1; evaluate(0); assert(!armed); replay = 0;
    racer_count = 12; split_screen = 1; evaluate(0); assert(!armed);
    return 0;
}
'''
        compiler = os.environ.get('CC') or shutil.which('gcc') or shutil.which('clang')
        self.assertTrue(compiler, 'A host C compiler is required')
        with tempfile.TemporaryDirectory() as temporary:
            fixture_path = Path(temporary) / 'entry.c'
            executable = Path(temporary) / 'entry.exe'
            fixture_path.write_text(fixture)
            subprocess.run([compiler, '-std=c11', str(fixture_path), '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == '__main__':
    unittest.main()
