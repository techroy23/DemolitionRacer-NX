# Switch graphics and performance backlog

Parked experiments. Keep the stock look and timing as defaults. Change one option at a time, keep `DATA/` and saves untouched, and use local test builds until the OLED results are reviewed.

## Graphics

- [ ] **Car LOD:** With `NEAREST` and stock draw distance, keep the original near car model visible slightly farther out. Trace the near-model effects separately. Count near/far draws and compare overhead on a crowded grid and during collisions.
- [ ] **Track draw distance:** Measure section-cache occupancy, loads, allocation failures, and scene time first. Check visibility bounds, slot and packet capacity, and lap wraparound before trying a small extension. Test pop-in on long straights and timing in dense scenes.
- [ ] **Smoothing:** Keep `NEAREST` as the baseline. The OLED bilinear run averaged 10.9 ms per blit and 38.1 submissions/sec, so the current scalar filter is not a candidate for the default. Reuse horizontally blended rows and measure before adding threads.

## Parallel work

- [ ] **Check the available cores on OLED:** Log the process core mask, observed main and worker core IDs, launch mode, and worker creation result. Test full-memory launch and applet mode separately. If another core is unavailable, keep the serial path.
- [ ] **Prototype one persistent display worker:** If the optimized scaler still costs enough to justify it, give the worker an immutable frame source and a disjoint band of output rows. Keep framebuffer dequeue, submit, and lifecycle on the main thread. Join the job before presenting or reusing either buffer. Stop and join the worker on exit. Do not add a frame of queueing to the first test.
- [ ] **Measure before moving anything else:** Compare serial and worker paths on the same track and settings. Check pixel equality for the same input, average and tail blit time, worker wait time, submission gaps, race iterations, audio overruns, and clean exit. Aim for the earlier nearest baseline of about 60 submissions/sec and 30 race iterations/sec. Leave physics, AI, SPU/CD callbacks, and the software rasterizer on the main thread for the first test; these touch shared state or depend on ordering. Profile other candidates before threading them.

API references: [libnx threads](https://switchbrew.github.io/libnx/thread_8h.html) and [Switchbrew kernel services](https://switchbrew.org/wiki/SVC). See [PLAN.MD](PLAN.MD) for fidelity and hardware checks.
