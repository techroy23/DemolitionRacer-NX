#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
angle=${1:?usage: bash run_live_native_race_gpu_hooks.sh C:/path/to/ANGLE}
export PATH="$(cygpath -u "$angle"):$PATH"
mkdir -p _build
export TMPDIR="$(pwd -W)/_build" TEMP="$(pwd -W)/_build" TMP="$(pwd -W)/_build"
python=${PYTHON:-python}
"$python" tools/embed_race_gpu_shaders.py _build
"$python" tools/instrument_live_native_race_gpu.py src/xport/src/psx_gpu.c _build/generated_psx_gpu.c
gcc -std=c11 -O2 -pipe -flto -fwhole-program -fno-builtin-ccos -fno-builtin-csin -fno-builtin-catan \
    '-DWND_TITLE="Replay"' -DWND_WIDTH=320 -DWND_HEIGHT=240 -DFIELD_RATE=60 -DAP_X64 \
    -Itests/presenter_host -Isrc/xport/src -I_build -Ireplay -Isrc/platform/switch -I"${DEVKITPRO:-C:/devkitPro}/portlibs/switch/include" \
    tests/test_live_native_race_gpu_hooks.c src/platform/switch/race_gpu_mesa.c src/platform/switch/race_gpu_store.c replay/replay.c \
    "$angle/libEGL.dll" "$angle/libGLESv2.dll" -o _build/test_live_native_race_gpu_hooks.exe
gcc -std=gnu11 -fms-extensions -fwrapv -DTEST_REAL_SCENE_PRODUCER -DAP_SWITCH -DXPORT_NATIVE -O2 -pipe -flto -fwhole-program -fno-builtin-ccos -fno-builtin-csin -fno-builtin-catan \
    '-DWND_TITLE="Replay"' -DWND_WIDTH=320 -DWND_HEIGHT=240 -DFIELD_RATE=60 -DAP_X64 \
    -Itests/presenter_host -Isrc/xport/src -I_build -Ireplay -Isrc/platform/switch -I"${DEVKITPRO:-C:/devkitPro}/portlibs/switch/include" \
    tests/test_live_native_race_gpu_hooks.c src/platform/switch/race_gpu_mesa.c src/platform/switch/race_gpu_store.c replay/replay.c \
    src/game_race_scene_packets.c src/game_race_scene_subdivide_ft.c src/game_race_scene_subdivide_gt.c src/game_race_scene_clip_ft.c src/game_race_scene_clip_gt.c \
    src/game_race_scene.c src/game_race_controls.c src/draft_agent2.c src/draft_agent3.c src/xport/src/psx.c \
    "$angle/libEGL.dll" "$angle/libGLESv2.dll" -o _build/test_perspective_producer.exe
if [[ ${DRACER_NEAR_FIXTURE:-} != '' ]]; then
    DRACER_ANGLE_BACKEND=${DRACER_ANGLE_BACKEND:-0x320D} ./_build/test_perspective_producer.exe --near "$DRACER_NEAR_FIXTURE"
    exit
fi
if [[ ${DRACER_QUAD_FIXTURE:-} != '' ]]; then
    DRACER_ANGLE_BACKEND=${DRACER_ANGLE_BACKEND:-0x320D} ./_build/test_perspective_producer.exe --quad "$DRACER_QUAD_FIXTURE"
    exit
fi
for backend in 0x320D 0x3450; do
    echo "Generated native hook fixture: ANGLE $backend"
    DRACER_ANGLE_BACKEND=$backend ./_build/test_live_native_race_gpu_hooks.exe
    DRACER_ANGLE_BACKEND=$backend ./_build/test_live_native_race_gpu_hooks.exe --hazards
    DRACER_ANGLE_BACKEND=$backend ./_build/test_live_native_race_gpu_hooks.exe --texture-parity modulated
    DRACER_ANGLE_BACKEND=$backend ./_build/test_live_native_race_gpu_hooks.exe --texture-parity raw
    for mode in 0 1 2 3 4 5 6; do
        DRACER_ANGLE_BACKEND=$backend ./_build/test_perspective_producer.exe --perspective "$mode"
    done
    for diagonal in 0 1; do
        DRACER_ANGLE_BACKEND=$backend ./_build/test_perspective_producer.exe --diagonal "$diagonal"
    done
    for fixture in {0..39}; do
        DRACER_ANGLE_BACKEND=$backend ./_build/test_perspective_producer.exe --quad "$fixture"
    done
    for fixture in {0..23} 25 27 {28..31}; do
        DRACER_ANGLE_BACKEND=$backend ./_build/test_perspective_producer.exe --near "$fixture"
    done
    if [[ $# -gt 1 ]]; then
        DRACER_REPLAY_PERSPECTIVE=1 DRACER_ANGLE_BACKEND=$backend ./_build/test_live_native_race_gpu_hooks.exe "$2"
        DRACER_ANGLE_BACKEND=$backend ./_build/test_live_native_race_gpu_hooks.exe "$2"
        # Check each earlier END through the existing one-shot handoff.
        for prefix in {1..11}; do
            DRACER_ANGLE_BACKEND=$backend ./_build/test_live_native_race_gpu_hooks.exe "$2" "$prefix"
        done
    fi
done
