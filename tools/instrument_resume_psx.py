"""Instrument a build-local copy of xport's native VBlank loop."""
from pathlib import Path
import sys


def instrument(source):
    # Unique anchors, fail closed on upstream control-flow changes.
    before = '''    present_field = fields != 0u;
    while (fields && !vblank_wait_failed)
'''
    after = '''    if (present_field && !vblank_wait_failed)
        gpu_present();
    native_vblank_polling = 0u;
'''
    if source.count(before) != 1 or source.count(after) != 1:
        raise ValueError('xport VBlank anchors changed; review the instrumentation')
    source = source.replace(before, '''    present_field = fields != 0u;
    const uint64 batch_fields = fields;
    RESUME_BEGIN(batch);
    if (batch_fields >= 4u)
        RESUME_END(RESUME_VBLANK_ELAPSED, batch, elapsed, true);
    while (fields && !vblank_wait_failed)
''')
    source = source.replace(after, '''    RESUME_END(RESUME_BATCH, batch, batch_fields, batch_fields >= 4u);
    if (present_field && !vblank_wait_failed)
    {
        RESUME_BEGIN(gpu_present);
        gpu_present();
        RESUME_END(RESUME_GPU_PRESENT, gpu_present, batch_fields - fields, false);
    }
    native_vblank_polling = 0u;
''')
    return '#include "resume_switch.h"\n' + source


if __name__ == '__main__':
    src, dst = map(Path, sys.argv[1:])
    dst.write_bytes(instrument(src.read_text(encoding='utf-8')).encode('utf-8'))
