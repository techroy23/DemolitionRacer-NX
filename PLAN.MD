# Demolition Racer: native Switch OLED port

## Goal and rules

Build `DemolitionRacer.nro` for the first-generation Nintendo Switch OLED, targeting an existing homebrew environment. The US PlayStation disc, `SLUS-00969`, is the behavior and presentation reference. The game runs as statically linked AArch64 C code; no PS1 CPU interpreter, emulator frontend, JIT, or retail BIOS ships in the NRO. The game-specific PSX compatibility layer can keep original fixed addresses, packet formats, GPU/SPU behavior, CD stream semantics, and callbacks without executing the original MIPS code. Load the original executable files only as local data where the reconstructed game initializes its memory image; dispatch reachable executable entry points to compiled C.

Preserve original UI artwork, layout, fonts, typography, language, camera, physics, menus, loading art, fades, sound, and FMV behavior. Preserve original aspect and field/frame cadence; do not stretch to widescreen, redraw screens, substitute content, or add in-game features. Black bars and platform-level input, storage, and lifecycle handling are allowed. If an original behavior is not established, log it as unknown and capture evidence before changing it.

## Starting point

Optional graphics and parallel processing experiments are tracked in [TODO.md](TODO.md), separate from the fidelity milestones.

- The attached 116 game C translation units match the public DemolitionRacer-xport baseline `d7d82178d7ec1fc1614391ceeb6a705b5bb1fde7` apart from possible line endings.[7]
- The original Windows and web projects depend on shared xport sources. Pin xport at `5b304d489388d9e3ef2fb6e899f682920103d10b` in `src/xport` with its license intact.[8]
- `DISC/Demolition Racer (USA).cue` identifies one `MODE2/2352` track. Supported `.bin` SHA-256: `7227af2b19221d10f9d8707402909f8f166668bb5868b1b0ce59c95d472349bc`. The loose `.XA` and `.STR` copies in `DATA/` lack the original raw sector envelope expected by `game_data.c`. Use the BIN as media source.
- `DD.DAT` has a 20,480-byte catalog with 383 named entries on this disc. `game_data.c` expects `DD.IDX` and individual archive files, not `DD.DAT` directly. The prep tool retains raw Mode 2 sectors for STR/XA and extracts the archive entries as-is. `_ZYXWVUT.ZYX` is not currently a port input; prove any future need from reads or traces.
- `game_link_stubs.c` contains aborting functions at `0x8001F860` and `0x8001FBE4`. Their reachability remains unverified; a successful link does not clear them. The Windows/web baselines are not the fidelity oracle; in particular the web path intentionally skips STR playback.
- The local devkitA64 GCC 16.1.0 executable, libnx rules, and `elf2nro` are installed. Use devkitPro's Switch development toolchain and libnx, not the Sony SDK.[4][5][6]

## Milestones and exit criteria

| Milestone | Work | Exit criteria |
| --- | --- | --- |
| M0. Source and media | Pin dependencies, record the disc revision, keep asset prep deterministic, inspect native entry and indirect calls | License and revisions documented; asset prep works on this BIN; raw STR/XA headers retained; no assets staged |
| M1. Native build | Cross-compile original C and compatibility services, add Switch graphics, audio, controller, clock, SD, and lifecycle backends; link NRO and stage a private `dist/` with debug settings | ELF verifies AArch64; NRO header and launch metadata valid; no retail files embedded; no runtime CPU core; representative test launch on an OLED still required after a local cross-build |
| M2. Boot and menus | Compare logos, intro and skip behavior, title attract cycle, options, controls, selection, records and two-player entry | Same menu order, button edges, artwork, positions, colors, text, fades, and audio cues; missing data fails visibly outside the game UI |
| M3. Load and first race | Follow actual asset requests into loading transition, pre-race camera, countdown, one full race, results, replay/restart and return | Matching load artwork and transition states, HUD, tracks, cars, audio, input and seeded gameplay traces; differences documented with frames and state deltas |
| M4. Full parity | Cover every mode/track/car/state present in PS1 reference, local two-player, persistence, credits and all movie paths; fix reachable stubs from evidence | No unexplained stub or fallback hit in coverage runs; original scoring, unlocks, AI, damage and results; save survives reload; controls work on handheld and paired controllers |
| M5. OLED release | Verify memory and frame pacing on actual OLED, SD throughput, dock/undock, suspend/resume and clean exit; produce reproducible package | Clean build and install; handheld and docked captures, test matrix, SHA-256 for NRO, no included retail data and no material unreviewed regressions |

## How to verify fidelity

- Race loading: the handheld log showed only 6 ms between opening `PRES/CD.TIM` and opening the first track asset. The Switch now gives the existing track-selected `FRONTEND/LOADING/*.TIM` artwork and original disc callback the same 192-field hold as boot, before race setup can reuse VRAM. In the OLED handheld test, `USS.TIM` and `MOTOR.TIM` completed the hold in 3.243 s and 3.281 s respectively, each with 193 nonblack submissions, and both races started. The log distinguishes `Race loading` from `Boot loading`.

- Startup pacing follow-up: the OLED handheld log confirmed that `INTRO.STR` exhausted its source at frame 1649 and `TATOU.STR` opened 4 ms later. The black interval before `TATOU.STR` opened fell from 17.957 s to 1.658 s, without RAM preloading. The three-turn hold completed all 192 fields in 3.246 s, with 193 nonblack submissions. The run reached the menu, loaded the save, and exited cleanly with zero failed video submissions and three audio overruns around menu entry. This was a boot-to-menu smoke test, not a race or docked test.

- Boot transition: `PRES/CD.TIM` and `PRES/LEGAL.TIM` are present in the current asset pack. Their archive-relative sectors start at 14493 and 14498, or disc LBAs 14765 and 14770. The US executable's boot descriptor plays `INTRO.STR`, `TATOU.STR`, then `PIT.STR`. The Switch path services the original loading callback before the front end clears VRAM and fits decoded STR frames separately from the 4:3 game view. The OLED follow-up showed only five framebuffer submissions between the last sampled movie frame and the first logged menu frame; four were nonblack but the log did not identify their content. The initial visibility fix gave the existing disc callback 64 VBlank fields (one nominal rotation); v0.2.1 extends that hold to three rotations and logs the window separately. This is a Switch presentation minimum, not a claim about the original disc's loading time. Verify the art and animation on an OLED.

1. Capture the US disc in DuckStation without enhancements: native resolution, original geometry and timings, and no texture filtering/replacements or widescreen. Prefer PS1 hardware confirmation for disputed frames. Use fixed starting options and recorded input where possible. DuckStation is a comparison tool, not a dependency of the port.[3]
2. Use jPSXdec on the original BIN to cross-check STR frame count, dimensions, XA channel IDs, duration, audio sync, and selected color/frame samples. Use PSXPrev to inspect supported textures/models as a second check; an unrecognized proprietary format does not prove an asset is absent.[1][2]
3. Align reference and Switch captures by game event and field/frame, then compare the unscaled active picture. Check CLUT, texture affine behavior, primitive ordering, quantization, dither, blend, crop, and bars. Require matching stable menu and loading landmarks; document any approved timing or display-conversion tolerance instead of claiming pixel identity from screenshots alone.
4. Record build revision, toolchain, hardware mode, disc digest, controller sequence, exact checkpoint, reference versus actual, verdict, and open fix. Keep copyrighted captures and runtime dumps under ignored `evidence/local/`.

## Development workflow

- Keep `DATA/`, `DISC/`, `pack/`, `dist/`, and generated artifacts untracked. Only commit source, tests, metadata, and dependency pins. Check `git diff --cached --name-only` before each commit. Do not publish copyrighted game content.
- Keep hardware services separate from game logic. Preserve 32-bit guest pointer math, signed-width semantics, packet layout, fixed-point movement, and input/timing contracts when adapting to AArch64. Audit reachable calls and overlays before calling the port complete.
- Make small, reviewable commits and run host parser/asset tests plus a clean Switch build after changes. Cross-compiling is not a substitute for an OLED runtime test.
- M0: seven asset parser tests pass. The supplied BIN produced 383 archive entries, executable initialization data, and nine byte-for-byte verified STR/XA streams.
- M1 (local build): clean devkitA64 build of 116 game and 13 compatibility-layer C files plus the Switch platform backend. The AArch64 ELF has no undefined symbols. The NRO contains Homebrew Menu metadata and no RomFS or bundled game assets.
- OLED evidence: 0.1.1 reached FMV and menu audio but the screen stayed black. Its graphics success message was not proof of a swap. 0.1.2 submits the game's existing pixels directly through libnx, logs frame contents and cadence, and enables one controller for now.
- OLED 0.1.3 baseline: sampled menu submission FPS had median 60, versus 42 during the selected race interval. The race loop waits for at least two guest VBlank counts per iteration. Keep that timing and distinguish submissions from game iterations. The 0.1.4 build adds phase profiling and a pixel-identical 3× scaler path. Run the same track with `LOG: DEBUG` and compare `Race profile`, `Collision profile`, and `Present profile`. The 0.1.4 OLED log measured roughly 20–30 race iterations per second and average scene time of 3.4–7.4 ms. Display dequeue averaged up to 4.3 ms in the busiest logged race window, but the existing phase timings did not isolate collisions. Build 0.1.5 tries triple buffering and adds collision response and submission gap diagnostics. Keep the original two-field pacing gate; target steadier 60 Hz delivery, not 60 unique race updates. Full parity remains unverified.

## Next work: resume reliability and optional race graphics

Keep these changes on separate local test paths. Fix HOME and sleep resume first, and test that build before combining it with CPU scheduling or graphics changes. The original rendering and game timing remain the reference with enhancements off.

Use the GPU for presentation whenever it is available. First extend the existing GLES presenter to handheld and test it independently of the resume change. This moves scaling and presentation off the CPU; it does not move the PS1 triangle rasterizer. Keep libnx software presentation as the fallback, with automatic CPU work sharing only when it can help. Keep game logic, physics, and audio on the CPU.

For additional GPU race graphics work, test candidate changes in standalone probes first. Compare against stock, checking palette lookups, transparency, draw order, and HUD behavior before promoting them. Treat `x16` anisotropy, MSAA sample counts, and `x32` tessellation as requested limits, not promises of hardware support or performance. Leave these additional enhancements off by default until the stock path and resume tests pass.

| Workstream | Work | OLED acceptance |
| --- | --- | --- |
| HOME and sleep resume | Trace focus loss, wake, and focus regain separately. Record the last completed audio, VBlank, worker, and presentation stages. Preserve the current race, save state, and original timing while resuming platform services. | HOME then return; power-button sleep and wake; repeat each in menus and races. Check input, picture, audio, and clean exit on handheld and docked paths. Include movie and loading transitions. |
| Automatic three-core work sharing | Replace the `DISPLAY_WORKER` toggle with automatic scheduling across up to three available application cores. Keep old settings files readable. Assign independent jobs only and fall back when cores are unavailable. Idle workers when there is no useful job. | Show actual work per core and compare frame pacing and audio against the current build. Do not move physics or other shared game state merely to raise core usage. |
| Anisotropic filtering and AA | Prototype race texture filtering up to `x16` and a separate anti-aliasing option, both off by default. Measure hardware limits and log effective settings. Filter true texture samples and distinguish geometry AA from a post-process filter. | Compare road detail at shallow angles and polygon edges in fixed captures. Preserve menus, HUD, movies, and loading art. Benchmark each option independently. |
| Tessellation | Prototype adjustable subdivision of eligible original race surfaces up to requested `x32`, with a stock/default-off path. Define the factor precisely and cap generated geometry and memory. Do not alter collision, track topology, or original game assets. | Show a visible benefit, no cracks or missing geometry, and acceptable pacing on original circuits. Log requested and effective factors; never label a lower effective factor as `x32`. |

Evidence from `DemolitionRacer_2026-11-22_19-15-56.log`: the last normal video sample was near 327 s. By the exit chord near 585 s, the race log showed 611 additional audio overruns and zero failed video submissions. The app still processed the exit chord. The log does not identify whether HOME or sleep triggered the freeze, or where rendering stopped.

## v0.2.4 release evidence

- The owner reported that HOME/sleep no longer froze the combined test build. The program-active clock is now the default on supported firmware; the raw-clock control remains available for diagnosis.
- In the separate hot-dock OLED run (`DemolitionRacer_2026-11-22_20-53-43.log`), GLES switched from 1280×720 to 1920×1080 during a race at `UPSCALE: x4`. After the transition, frames 18,120 through 26,520 averaged 59.4 submissions/sec. The transition caused a brief hitch and 39 additional audio overruns, but the game recovered without switching to software. The session submitted 28,963 frames, with zero failures and a clean exit. These counts do not measure unique game frames or scanout.
- This run did not cover hot undocking. Repeat dock/undock and HOME/sleep in each mode, including menus and loading. Automatic three-core fallback scaling and GPU race geometry remain separate work.

## v0.2.5 release evidence

- The owner reported that the close-up texture skew was gone in the tested OLED run with both experimental markers enabled. The near test NRO logged 6,263 borrowed GPU textures out of 6,265 race submissions, with zero failed video submissions. These are submission counts, not measured refresh, unique frames, or corrected pixels.
- The v0.2.5 default-on rebuild uses the same renderer with normal launcher metadata and artwork; it no longer checks either marker. That OLED run did not exercise the exact rebuilt NRO, other circuits, or lifecycle transitions with the solo GPU renderer. Stock rendering remains the fidelity reference.

## Sources

[1] https://github.com/m35/jpsxdec

[2] https://github.com/rickomax/psxprev

[3] https://github.com/stenzek/duckstation

[4] https://github.com/switchbrew/libnx

[5] https://github.com/switchbrew/switch-examples

[6] https://devkitpro.org/wiki/Getting_Started

[7] https://github.com/psxports/DemolitionRacer-xport

[8] https://github.com/psxports/xport
