# HOME / sleep diagnostics

The production build still uses the program-active clock by default in v0.2.5. For a stock resume comparison, leave both [experimental graphics markers](graphics.md#experimental-solo-race-renderer) absent. Test resume with the experimental renderer in a separate run. In the devkitPro SDK shell (tested with libnx 4.12.0):

```sh
make -C switch RESUME_DIAG=trace
make -C switch RESUME_DIAG=clock
```

The NROs are in `switch/build-resume-trace/` and `switch/build/`. `RESUME_DIAG=off` builds without instrumentation in `switch/build/resume-off/`. The instrumented build transforms only a generated copy of `psx.c`; it does not modify the xport submodule. Field rate, fractional remainder, catch-up policy, dispatch order and presentation placement are unchanged. Trace-only keeps the raw timer. Clock mode uses libnx's program-active clock on firmware 6.0.0+ after a successful initial read. It holds the last value on a backwards sample; on read failure it falls back to raw time without counting the unmeasured interval. Active time may include unfocused time when the app was not suspended.

## Hardware test

1. Start with the regular release. If comparing builds, back up the installed NRO and replace only that file. Leave `DATA/`, `SAVE/`, the game settings, and launch context unchanged. Use `LOG: DEBUG` for both runs.
2. In a repeatable race, note launch mode and handheld/docked state. Try HOME for 10 seconds and return. In a separate launch, sleep for 10 seconds and wake. Note screen, audio, input, and elapsed wall time until motion or exit. When ready, hold **Plus + Minus + ZL + ZR** and allow shutdown; send that launch's `DemolitionRacer_YYYY-MM-DD_HH-MM-SS*.log` from the game directory. Use the trace-only build separately if a raw-clock comparison is needed.
3. If the process never reaches a normal exit path, the RAM trace may not be flushed; send the partial log and describe which inputs still worked. Ring records aren't crash-persistent and older records can be overwritten. An accepted queue/swap is not evidence of scanout.

## Trace interpretation

`[resume]` rows use raw microseconds since probe startup for `start-us`/`end-us`; the surrounding logger has its own time origin. Rows are in insertion order (a completed outer span can follow nested spans). Ordinary stages are kept only when they last at least 50 ms, with forced records for lifecycle, errors, and VBlank batches starting with at least four fields. The ring is dumped before shutdown services, not during a frame or in a hook.

- `initial-state` and `hook` values encode hook type (bits 24+ for `hook`), focus (bits 16–23: 1 focused, 2 out of focus, 3 background), operation mode (bits 8–15), and performance mode (bits 0–7). Hook IDs from libnx 4.12.0: 0 focus, 1 operation, 2 performance, 3 exit, 4 resume, 7 request to display. `restart-enable` holds the result of explicitly enabling resume messages. Transitions are dated when messages are consumed, not when they occurred; libnx consumes at most one message per `appletMainLoop` call, and a true result doesn't imply focus.
- `pump-gap` is time between pump observations; it does not identify which operation caused the gap. Compare `active-ns` against `previous-active-ns` and their raw timestamps. These are cumulative libnx active nanoseconds, not guest time.
- `vblank-elapsed-us` is the actual scheduler input; `vblank-batch` is the number of fields planned and the batch's processing interval. `gpu-present` is timed separately after the batch. Contrast long batches with long `fb-dequeue`, `fb-blit`, `fb-queue`, `egl-swap`, `audio-mix`, `audio-release`, or `audio-append` spans. Audio IPC rows carry raw libnx results; the header reports full-ring drops separately.

A large trace-only batch after resume is evidence for catch-up debt, not proof that debt explains the entire visible stall. A short batch followed by a long queue/swap points elsewhere. No hardware result is implied by host tests or successful linking.
