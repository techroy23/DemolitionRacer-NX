"""Standalone host checks for the Switch track draw limits (set CC if needed)."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature)
    return source[start:source.index('\n}\n', start) + 3]


class TrackDistanceTest(unittest.TestCase):
    def test_track_distance_bounds(self):
        cc = os.getenv('CC', 'cc')
        if not shutil.which(cc) and 'CC' not in os.environ:
            self.skipTest('set CC to run the standalone C test')
        source = (ROOT / 'src/game_race_scene.c').read_text()
        bounds = function(source, 'static sint32 track_forward_section(')
        bounds += function(source, 'static uint32 track_packet_bound(')
        start_source = (ROOT / 'src/game_race_start.c').read_text()
        bounds += function(start_source, 'uint32 track_extra_packets(')
        harness = r'''
#include <assert.h>
#include <stdint.h>
typedef int16_t sint16;
typedef uint8_t uint8;
typedef int32_t sint32;
typedef uint32_t uint32;
static uint8 model[8192];
static uint32 track_packets, free_bytes, units, allocations;
static uint8 r_u8(uint32 a) { assert(a < sizeof(model)); return model[a]; }
static uint32 r_u32(uint32 a) {
    switch (a) {
    case 0x80065184: return 0x80010000;
    case 0x80010000: return 0x80020000;
    case 0x80010008: return free_bytes;
    case 0x80020000: return 0x80030000;
    case 0x80030004: return units;
    }
    return r_u8(a) | (uint32)r_u8(a+1)<<8 | (uint32)r_u8(a+2)<<16 | (uint32)r_u8(a+3)<<24;
}
static uint32 r_u16(uint32 a) { return r_u8(a) | (uint32)r_u8(a+1)<<8; }
static uint32 sub_80042544(uint32 bytes) {
    assert(bytes == 8192 && units >= 513);
    ++allocations;
    return 0x80100000;
}
'''
        cases = r'''
int main(void) {
    assert(track_forward_section(12 * 8, 10) == 1);
    assert(track_forward_section(12 * 10, 10) == 3);
    assert(track_forward_section(-12 * 2, 10) == 2);
    assert(track_forward_section(12, 7) == -1);
    model[44] = 64; model[48] = 128;
    model[128] = 2; model[129] = 0; model[130] = 1; model[131] = 24;
    assert(track_packet_bound(0, 184, 3) == 3);
    assert(track_packet_bound(0, 184, 2) == 0);
    assert(track_packet_bound(0, 183, 3) == 0);
    model[130] = 4;
    assert(track_packet_bound(0, 184, 3) == 0);
    model[130] = 1; model[144] = 40;
    assert(track_packet_bound(0, 184, 3) == 0);
    assert(track_extra_packets(0, 4096) == 0 && allocations == 0);
    free_bytes = 65536; units = 512;
    assert(track_extra_packets(0, 4096) == 0 && allocations == 0);
    units = 513;
    assert(track_extra_packets(2, 4096) == 0 && allocations == 0);
    assert(track_extra_packets(1, 4096) == 0x80101000);
    assert(track_extra_packets(0, 4096) == 0x80100000 && allocations == 1);
    return 0;
}
'''
        # Use the platform's writable scratch directory for native GCC/collect2.
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            test = path / 'track.c'
            exe = path / 'track.exe'
            test.write_text(harness + bounds + cases)
            env = dict(os.environ, TMPDIR=path.as_posix(), TMP=path.as_posix(), TEMP=path.as_posix())
            subprocess.run([cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
                            str(test), '-o', str(exe)], check=True, env=env)
            subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    unittest.main()
