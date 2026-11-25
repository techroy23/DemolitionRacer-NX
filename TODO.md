# Switch graphics and performance backlog

Status after `v0.2.5`. Checked items are shipped or verified as described. Unchecked items are follow-up tests or future work. Keep the stock look and timing as defaults. Change one option at a time, keep `DATA/` and saves untouched, and use local test builds for new experiments.

## Resume and dock transitions

- [x] **HOME/sleep:** The owner confirmed the combined active-clock build recovered. The subsequent OLED hot-dock test kept GLES active, rebuilt at 1080p, and returned to about 60 submissions/sec.
- [ ] **Broader lifecycle coverage:** Retest hot undocking, repeated mode changes, HOME and sleep separately, and applet fallback. Measure the short hot-dock hitch and audio overruns. See [release evidence](PLAN.MD#v024-release-evidence).
- [ ] **Next graphics work:** Evaluate automatic software scaling across up to three cores separately. The standalone OLED graphics probe confirmed 16× anisotropy and exact 2×/4× MSAA; neither is integrated into the race renderer. Tessellation remains a prototype, not a release setting.

## Graphics

- [x] **Near-track perspective correction:** On by default in the [eligible solo GPU path](switch/graphics.md#solo-race-renderer). The owner confirmed the close-up skew was gone in the opt-in OLED test build.
- [ ] **Solo GPU renderer coverage:** Compare the exact release NRO against stock on more circuits and cameras. Check dock transitions and HOME/sleep separately and measure pacing and audio overruns. Do not infer visible coverage from metadata or submission counts.
- [x] **Car LOD:** Shipped opt-in `CAR_LOD: EXTENDED`. The original near model and its effects stay visible slightly farther out. The docked OLED test logged both near and far draws.
- [ ] **Car LOD comparison:** Compare `STOCK` and `EXTENDED` with `NEAREST` and stock track distance. Check near-model effects, crowded grids, collisions, and frame time.
- [x] **Track draw distance:** Shipped opt-in `TRACK_DISTANCE: EXTENDED`, limited to already-loaded track scenery. The docked OLED test logged nonzero extra geometry.
- [ ] **Track distance coverage:** Check long straights, lap wraparound, and more circuits. Compare section-cache stats and frame times against `STOCK`. Extending section streaming itself remains future work.
- [x] **Smoothing:** Shipped optimized race-only bilinear filtering with horizontal sample and blended-row reuse on the software path. `NEAREST` remains the default.
- [ ] **Anisotropic texture filtering:** Prototype opt-in `ANISOTROPIC_FILTER: OFF|x2|x4|x8|x16` in `settings.yaml`, defaulting to `OFF`. Evaluate filtering actual race textures during rasterization, not the finished 2D frame. Check renderer support and cost, fall back to stock when unsupported, and report the effective level. Compare road texture clarity at shallow angles and frame pacing in handheld and docked modes. Leave movies, menus, loading art, and HUD unchanged.

## Docked x4 and loading

- [x] **True docked x4:** Shipped GPU scaling from the source-sized image to a centered 1280×960 viewport on a 1920×1080 output. The docked OLED playtest averaged 59.7 race submissions/sec with no failed submissions and exited cleanly. The CPU fallback remains available.
- [x] **RAM preload:** Shipped bounded raw-file caching during the launcher splash and original race loading screen, with independent stream cursors and SD fallback. The docked test cached all three opening STRs and 13 selected race files; race setup opened 10 files from RAM.
- [x] **Return-to-menu loading:** Shipped the original loading disc with a three-turn hold. The docked run completed all 192 target fields.
- [ ] **Isolate preload gains on hardware:** Compare GPU x4 with and without preloading, keeping track, cars, and settings fixed. The combined test reached `INTRO.STR` exhaustion in about 55.1 s but still logged a roughly 166 ms gap around race start. Docked x3 remains a reference (59.7 race submissions/sec, 55.1 s intro, 3.27 s return-to-menu hold). The GPU x4 return-to-menu hold took about 4.46 s in the tested run.
- [x] **Handheld multi-race smoke test:** The Nov 21 run used the pre-release test NRO reporting `0.2.2`. It completed four consecutive races across four tracks at 59.6 display submissions/sec overall, with zero failed submissions. The extended track pass drew extra geometry on all four tracks, race preloading cached 13/13 files each time, and the run saved, completed the 192-field return-to-menu hold, and exited cleanly.
- [ ] **Finish hardware coverage:** Smoke-test the exact `v0.2.3` NRO in handheld mode, explicitly verify intro skip, and test applet memory/display fallback. Keep the diagnostics for output dimensions, backend, cache bytes and hits, and loading timings.

## Parallel work

- [x] **OLED core diagnostics:** The full-memory run logged the available core mask and successful worker creation. Applet-mode verification remains open above.
- [x] **Persistent display worker:** Shipped opt-in `DISPLAY_WORKER` for software bilinear scaling. It splits output rows and finishes before the main thread presents, without adding a queued frame. Docked GPU scaling does not use it.
- [ ] **Isolate the software worker's benefit:** Compare the same optimized bilinear scaler with the worker off and on, using the same track and settings on the software path. Check output equality, average and tail blit time, worker waits, submission gaps, race iterations, audio overruns, and clean exit. Use about 60 submissions/sec and 30 race iterations/sec as the reference. Profile other candidates before threading them; keep physics, AI, SPU/CD callbacks, and the software rasterizer on the main thread.

## Logging settings

- [x] **Disable logging:** The app reads `LOG: false` before opening any session log. Saves and the FPS overlay remain independent.
- [x] **Cumulative log levels:** Added `FATAL`, `WARN`, and `TRACE` while keeping `ERROR`, `INFO`, and `DEBUG` compatible. Host tests check every level and threshold.
- [ ] **Logger hardware check:** Verify `LOG: false` creates no new file on the Switch. Use a short `TRACE` run for bug reproduction and compare with `DEBUG` if timing matters.

API references: [libnx threads](https://switchbrew.github.io/libnx/thread_8h.html) and [Switchbrew kernel services](https://switchbrew.org/wiki/SVC). See [PLAN.MD](PLAN.MD) for fidelity and hardware checks.
