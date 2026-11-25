import json
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
for src, dst, symbol in [
    ('replay/raster.comp', 'live_raster.inc', 'shader_source'),
    ('src/platform/switch/race_gpu_scanout.frag', 'live_scanout.inc', 'scanout_source'),
]:
    lines = (root/src).read_text(encoding='utf-8').splitlines(keepends=True)
    body = '\n'.join(json.dumps(line) for line in lines)
    perspective_lines = [lines[0], '#define PRESENT_PERSPECTIVE 1\n', *lines[1:]]
    perspective_body = '\n'.join(json.dumps(line) for line in perspective_lines)
    (out/dst).write_text(f'static const char {symbol}[] =\n{body};\n'
                         f'static const char {symbol}_perspective[] =\n{perspective_body};\n',
                         encoding='utf-8', newline='\n')
