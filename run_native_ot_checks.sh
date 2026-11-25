#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
export PATH="$PATH"
mkdir -p _build
export TMPDIR="$(pwd -W)/_build" TEMP="$(pwd -W)/_build" TMP="$(pwd -W)/_build"
flags=(-std=c11 -O2 -pipe -Wall -Wextra -Werror -Ireplay)
gcc "${flags[@]}" tests/test_race_gpu_native_ot.c replay/replay.c -o _build/test_native_ot.exe
./_build/test_native_ot.exe
cross=${DEVKITPRO:-C:/devkitPro}/devkitA64/bin/aarch64-none-elf
for unit in replay/replay.c tests/test_race_gpu_native_ot.c; do
    "$cross-gcc" "${flags[@]}" -D__SWITCH__ -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE \
        -c "$unit" -o "_build/$(basename "$unit" .c).aarch64.o"
done
"$cross-readelf" -h _build/replay.aarch64.o | awk '/Class:|Machine:/'
