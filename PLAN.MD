# Demolition Racer: native Switch OLED port

## Goal and rules

Build `DemolitionRacer.nro` for the first-generation Nintendo Switch OLED, targeting an existing homebrew environment. The US PlayStation disc, `SLUS-00969`, is the behavior and presentation reference. The game runs as statically linked AArch64 C code; no PS1 CPU interpreter, emulator frontend, JIT, or retail BIOS ships in the NRO. The game-specific PSX compatibility layer can keep original fixed addresses, packet formats, GPU/SPU behavior, CD stream semantics, and callbacks without executing the original MIPS code. Load the original executable files only as local data where the reconstructed game initializes its memory image; dispatch reachable executable entry points to compiled C.

Preserve original UI artwork, layout, fonts, typography, language, camera, physics, menus, loading art, fades, sound, and FMV behavior. Preserve original aspect and field/frame cadence; do not stretch to widescreen, redraw screens, substitute content, or add in-game features. Black bars and platform-level input, storage, and lifecycle handling are allowed. If an original behavior is not established, log it as unknown and capture evidence before changing it.

## Starting point

- The attached 116 game C translation units match the public DemolitionRacer-xport baseline `d7d82178d7ec1fc1614391ceeb6a705b5bb1fde7` apart from possible line endings.[7]
- The original Windows and web projects depend on shared xport sources. Pin xport at `5b304d489388d9e3ef2fb6e899f682920103d10b` in `.deps/xport` with its license intact.[8]
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
- Next gate: retest 0.1.2 on the OLED and capture cold boot through the first single-player race. Visual, gameplay, and timing parity are not yet verified.

## Sources

[1] https://github.com/m35/jpsxdec

[2] https://github.com/rickomax/psxprev

[3] https://github.com/stenzek/duckstation

[4] https://github.com/switchbrew/libnx

[5] https://github.com/switchbrew/switch-examples

[6] https://devkitpro.org/wiki/Getting_Started

[7] https://github.com/psxports/DemolitionRacer-xport

[8] https://github.com/psxports/xport
