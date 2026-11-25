#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
angle=${1:?usage: bash run_presenter_checks.sh C:/path/to/ANGLE}
export PATH="$(cygpath -u "$angle"):$PATH"
mkdir -p _build/presenter
export TMPDIR="$(pwd -W)/_build/presenter" TEMP="$(pwd -W)/_build/presenter" TMP="$(pwd -W)/_build/presenter"
source=src/platform/switch
sdk=${DEVKITPRO:-C:/devkitPro}
"${PYTHON:-python}" tools/embed_race_gpu_shaders.py _build/presenter
gcc -std=c11 -Wall -Wextra -Werror -O2 -pipe -I"$source" -Ireplay -I_build/presenter \
    -I"$sdk/portlibs/switch/include" -c "$source/race_gpu_mesa.c" -o _build/presenter/race_gpu_mesa.host.o
gcc -std=c11 -Wall -Wextra -Werror -O2 -pipe -D__SWITCH__ -Itests/presenter_host -I"$source" \
    -I"$sdk/portlibs/switch/include" -Ireplay \
    tests/test_docked_presenter_texture.c "$source/framebuffer_blit.c" _build/presenter/race_gpu_mesa.host.o \
    "$source/race_gpu_store.c" replay/replay.c \
    "$angle/libEGL.dll" "$angle/libGLESv2.dll" -o _build/presenter/test_docked_presenter_texture.exe
for backend in 0x320D 0x3450; do
    printf 'Presenter ANGLE backend=%s\n' "$backend"
    DRACER_ANGLE_BACKEND=$backend ./_build/presenter/test_docked_presenter_texture.exe
done
gcc -O2 -pipe -flto -ffunction-sections -fdata-sections -D__SWITCH__ -Isrc -Isrc/xport/src \
    '-DWND_TITLE="Fixture"' -DWND_WIDTH=320 -DWND_HEIGHT=240 -DFIELD_RATE=60 \
    -fno-builtin-ccos -fno-builtin-csin -fno-builtin-catan \
    tests/test_race_store_failure.c src/game_race_frame_render.c -Wl,--gc-sections \
    -o _build/presenter/test_race_store_failure.exe
./_build/presenter/test_race_store_failure.exe
