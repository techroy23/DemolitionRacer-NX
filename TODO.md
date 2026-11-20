# Switch graphics and performance backlog

Local experiments are implemented; OLED checks remain open. Keep the stock look and timing as defaults. Change one option at a time, keep `DATA/` and saves untouched, and use local test builds until the OLED results are reviewed.

## Graphics

- [ ] **Car LOD:** With `NEAREST` and stock draw distance, keep the original near car model visible slightly farther out. Trace the near-model effects separately. Count near/far draws and compare overhead on a crowded grid and during collisions.
- [ ] **Track draw distance:** Test the bounded pass into already-loaded scenery on long straights and around lap wraparound. Check `Race track extension` for nonzero geometry and compare section-cache stats and frame times. Extending section streaming itself remains future work.
- [ ] **Smoothing:** Keep `NEAREST` as the baseline. The OLED bilinear run averaged 10.9 ms per blit and 38.1 submissions/sec, so the current scalar filter is not a candidate for the default. Reuse horizontally blended rows and measure before adding threads.

## Docked x4 and loading

- [ ] **Keep true docked x4.** Upload the completed game image at source size and scale it on the GPU into a centered 1280×960 viewport on the 1920×1080 output. Keep the existing CPU path for fallback. Preserve original crop, movie aspect, filtering scope, and all game timing. Confirm the first frame is visible on the TV; successful EGL setup alone is not proof.
- [ ] **Preload original assets during existing loading windows.** Read the three opening STR files into bounded RAM during the launcher splash. Use the track selection to stage race assets during the original rotating-disc screen. Preserve independent stream cursors and fall back to SD for misses or memory limits. Do not move parsing, decompression, or guest state changes to the file worker. Keep the original three turns.
- [ ] **Measure independently on hardware.** Use docked x3 as the reference (59.7 race submissions/sec, 55.1 s to `INTRO.STR` exhaustion, 3.27 s to return to the menu). Test GPU x4 without preloading first, then compare startup and first-race frame gaps with preloading enabled. Log output dimensions, backend, cache bytes/hits, fallback reasons, and load timings. Also verify handheld, intro skip, repeat races, applet memory fallback, and clean exit.

## Parallel work

- [ ] **Check the available cores on OLED:** Log the process core mask, observed main and worker core IDs, launch mode, and worker creation result. Test full-memory launch and applet mode separately. If another core is unavailable, keep the serial path.
- [ ] **Prototype one persistent display worker:** If the optimized scaler still costs enough to justify it, give the worker an immutable frame source and a disjoint band of output rows. Keep framebuffer dequeue, submit, and lifecycle on the main thread. Join the job before presenting or reusing either buffer. Stop and join the worker on exit. Do not add a frame of queueing to the first test.
- [ ] **Measure before moving anything else:** Compare serial and worker paths on the same track and settings. Check pixel equality for the same input, average and tail blit time, worker wait time, submission gaps, race iterations, audio overruns, and clean exit. Aim for the earlier nearest baseline of about 60 submissions/sec and 30 race iterations/sec. Leave physics, AI, SPU/CD callbacks, and the software rasterizer on the main thread for the first test; these touch shared state or depend on ordering. Profile other candidates before threading them.

API references: [libnx threads](https://switchbrew.github.io/libnx/thread_8h.html) and [Switchbrew kernel services](https://switchbrew.org/wiki/SVC). See [PLAN.MD](PLAN.MD) for fidelity and hardware checks.
