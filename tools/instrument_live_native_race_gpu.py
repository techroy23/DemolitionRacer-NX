"""Install live GPU hooks on a generated copy of the exact pinned xport renderer."""
import hashlib
from pathlib import Path
import sys

src, dst = map(Path, sys.argv[1:])
raw = src.read_bytes()
if hashlib.sha256(raw.replace(b'\r\n', b'\n')).hexdigest() != '328e2c03f1d2e03c3af731468f55380752969010bdc4871a7c5920e9dc2868bd':
    raise SystemExit('Unexpected pinned psx_gpu.c; audit the native hooks before rebasing')
s = raw.decode().replace('\r\n', '\n')


def replace(anchor, text):
    global s
    if s.count(anchor) != 1:
        raise SystemExit(f'Expected one native GPU hook: {anchor}')
    s = s.replace(anchor, text)


def before(signature, body):
    anchor = signature + '\n{\n'
    replace(anchor, anchor + '    ' + body + '\n')


replace('#include "xport_trace.h"', '''#include "xport_trace.h"
#include <stdbool.h>
static void live_race_handoff(const char *reason);
static void live_race_require_stock_raster(void);
static bool live_race_draw_ot(uint32 *ordering_table);
static bool live_race_load(const PSX_RECT *, const uint16 *);
static bool live_race_store(const PSX_RECT *, uint16 *);
static bool live_race_background(uint32);
static bool live_race_present(void);''')
replace('    if (psyq_queue_bound)\n    {\n        uint32 rectangle_address;\n        uint32 pixel_address;',
        '    if (live_race_load(rectangle, source)) return 0;\n    if (psyq_queue_bound)\n    {\n        uint32 rectangle_address;\n        uint32 pixel_address;')
replace('    gpu_vram_invalidate();\n    for (row = 0; row < rectangle->h; ++row)',
        '    if (live_race_store(rectangle, destination)) return 0;\n    gpu_vram_invalidate();\n    for (row = 0; row < rectangle->h; ++row)')
replace('        for (y = raster_clip_y0; y < raster_clip_y1; y++)',
        '        if (live_race_background(color)) return;\n        for (y = raster_clip_y0; y < raster_clip_y1; y++)')
replace('    completed = gpu_draw_linked_dma(ot);', '    completed = live_race_draw_ot(ot) || gpu_draw_linked_dma(ot);')
replace('    gpu_convert_display();\n    return xport_present', '    if (live_race_present()) return 1;\n    gpu_convert_display();\n    return xport_present')
for signature in (
    'void gpu_set_raster(sint32 enabled)',
    'void gpu_bind_linked_submit(GpuLinkedSubmit submit, void *context)',
    'void gpu_bind_linked_pause(GpuLinkedPause pause)',
    'void gpu_set_transfer_observer(GpuTransferObserver observer, void *context)',
    'sint32 gpu_load_rect(uint32 source, sint32 x, sint32 y, sint32 width, sint32 height)',
    'sint32 MoveImage(PSX_RECT *rectangle, sint32 x, sint32 y)',
    'sint32 ClearImage(PSX_RECT *rectangle, uint8 red, uint8 green, uint8 blue)',
    'static sint32 gpu_port_gp0(uint32 value)',
    'sint32 ResetGraph(sint32 mode)',
    'sint32 gpu_render_list_replay(const GpuRenderList *list)',
    'sint32 gpu_render_list_begin(GpuRenderList *list, const DRAWENV *environment)',
    'void gpu_packet(void *packet)', 'void DrawPrim(void *primitive)',
    'sint32 gpu_execute_linked_dma(uint32 physical_address)',
    'void gpu_resume_linked_draw(uint32 continuation)',
    'void gpu_init_empty(void)', 'sint32 gpu_load_vram(const char *path)',
    'sint32 gpu_save_frame(const char *path)', 'void gpu_reset_graph_state(void)',
    'void gpu_clear_menu_surfaces(void)',
    'sint32 gpu_import_raster(const GpuRasterState *state, const void *vram, size_t bytes)',
):
    before(signature, f'live_race_handoff("{signature.split("(")[0].split()[-1]}");')
replace('    linked_trace_operation = operation.active ? &operation : NULL;\n    completed = live_race_draw_ot',
        '    if (operation.active) live_race_handoff("traced OT");\n    linked_trace_operation = operation.active ? &operation : NULL;\n    completed = live_race_draw_ot')
s += '\n#include "live_native_race_gpu.inc"\n'
dst.write_text(s, encoding='utf-8', newline='\n')
