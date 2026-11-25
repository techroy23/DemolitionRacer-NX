# Technical notes

## Source layout

| Path | What it contains |
| --- | --- |
| `src/` | Reconstructed game logic, plus Windows, web, and Switch platform code under `src/platform/`. |
| `src/xport/` | Pinned Git submodule for the PS1 compatibility library. It lives alongside the game source without copying files out of the upstream checkout. |
| `switch/` | Switch build rules and default configuration; compiled artifacts go in `switch/build/`. |
| `tools/`, `tests/` | Disc extraction, artwork preparation, packaging, and host tests. |

The Switch build compiles the game to AArch64 and links the needed compatibility services. It does not execute the retail MIPS instructions or use a retail BIOS. Some files extracted from the disc, including PS1 executables, are read as data. The Switch backend keeps the original 4:3 race view. It prefers EGL/GLES presentation in full-memory handheld and docked launches and falls back to libnx when needed. GPU scaling itself does not change race cadence or move triangle rasterization to the GPU. The separate [solo race renderer](switch/graphics.md#solo-race-renderer) automatically handles eligible races with GPU rasterization and presentation-only perspective correction.

## Build the Switch app

Use a Git checkout so you can initialize the submodule. Install the Switch development packages from [devkitPro](https://devkitpro.org/wiki/Getting_Started), including devkitA64, libnx, and the NRO packaging tools. Install the `switch-mesa` portlib in the devkitPro shell (`pacman -S switch-mesa`) for the EGL/GLES presenter. Install Pillow for the same Python interpreter the build uses (`python -m pip install Pillow`). On Windows, run the build from the devkitPro MSYS shell. Start in the repository root:

```sh
git submodule update --init -- src/xport
make -C switch
```

The output is `switch/build/DemolitionRacer.nro`, with a Homebrew Menu icon and metadata and no RomFS. The build generates the GPU hooks from the pinned xport source without modifying the submodule. Building does not require a retail disc. The two `game_*.png` files provide the port's launcher icon and startup splash; they are separate from the original game assets that the app reads from your SD card. The splash lasts about five seconds before the game starts, and scales to fit without stretching or cropping.

## Disc extraction and packaging

[`tools/prepare_assets.py`](tools/prepare_assets.py) requires Python 3.11 or newer but no extra packages. In CUE mode it expects a single `MODE2/2352` track starting at `00:00:00`, resolves a BIN beside the CUE, and checks its SHA-256 against the supported US dump. It unpacks `DD.DAT` into `DD.IDX` and individual files and preserves full raw sectors for STR movies and XA audio. Loose copies of those files may lack the required sector metadata.

The [default command](README.md#1-make-the-data-folder) writes only `dist/switch/DemolitionRacer/DATA/`. Provide a quoted CUE path and an alternate output parent to check extraction separately:

```sh
python tools/prepare_assets.py "DISC/Demolition Racer (USA).cue" --out "pack/check/switch/DemolitionRacer"
```

For an all-in-one staging folder after a build, pass the raw BIN instead to use the original two-step workflow:

```sh
python tools/prepare_assets.py "DISC/Demolition Racer (USA).bin"
python tools/package_switch.py
```

This creates `pack/DATA/` first, then a fresh `dist/` containing the app, settings, and data. If either destination exists, choose a new location with the relevant tool's `--out`. Keep extracted assets local.

## Settings and diagnostics

[`switch/settings.yaml`](switch/settings.yaml) defaults to `LOG: DEBUG`, `FPS_OVERLAY: false`, and `UPSCALE: FIT`. The port reads `settings.yaml` beside the NRO on launch. If the file is absent, the defaults are `INFO`, overlay off, and `FIT`.

- `FPS_OVERLAY` counts framebuffer submissions, not unique game frames, race updates, or panel refreshes.
- `UPSCALE` accepts `FIT`, `x2`, `x3`, and `x4`. Integer modes scale a 320×240 reference to 640×480, 960×720, or 1280×960. The 4× mode needs a compatible 1080p docked framebuffer and is capped at 3× on the 720p path. Scaling does not rerender geometry.
- `DISPLAY_FILTER: NEAREST` (the default when omitted) keeps sharp pixels. `BILINEAR` smooths the completed race image, including the in-race HUD, without affecting the launcher, boot movies, loading screens, or front end. Relaunch after editing. For an A/B check, run the same race with the same `UPSCALE` and `LOG: DEBUG` in both modes. Compare `Race display` and `Present profile` for submission gaps and audio overruns. The EGL queue timing includes upload, draw, and swap; software blit timings do not measure that GPU work.
- `DISPLAY_WORKER: true` splits the software bilinear blit across the main thread and one worker when an alternate core is available. It does not accelerate EGL or `NEAREST`. On the software path, compare serial and worker performance on the same track with `BILINEAR` enabled for both runs. `Race display worker` reports actual worker usage and wait time.
- `CAR_LOD: EXTENDED` uses the original near-car model and its effects at up to 10% more distance. `TRACK_DISTANCE: EXTENDED` can draw farther into the forward-most already-loaded section on eligible single-player circuits; it does not stream an extra section. Both default to `STOCK`. Test them separately with `NEAREST`, then together if frame pacing holds. `Race geometry` reports car draws and track-cache stats; `Race track extension` shows whether the extra pass actually emitted geometry.
- Saves go in `SAVE/` next to the NRO. A legacy `DemolitionRacer.sav` can migrate if there is no existing save or recovery backup there. The exit chord does not force a save.
- A new `DemolitionRacer_YYYY-MM-DD_HH-MM-SS.log` uses the console's local time. `LOG: false` skips file creation. Each level includes all more severe messages: `FATAL` for failures that stop the app, `ERROR` for failed operations, `WARN` for recoverable issues, `INFO` for milestones, `DEBUG` for troubleshooting, and `TRACE` for per-frame presentation timings. Use `TRACE` for short reproductions: its frequent writes can affect frame pacing.

For race stutter, reproduce on the same track and keep the `Race profile`, `Collision profile`, and `Present profile` lines. The original race loop waits for at least two game VBlank counts per iteration. Its timings can include calls that submit frames, so don't add race and presentation timings together.

For a dock change, check that `Video: GLES` reports the new output size. For HOME or sleep issues, note which action you used and quit with the exit chord to flush the RAM trace. See [video output](switch/graphics.md) and [resume diagnostics](switch/RESUME-DIAGNOSTICS.md).

## Verification

Run `python -m unittest discover -s tests -v` from the project root for host tests. For runtime changes, start with `make -C switch clean` and rebuild before testing on a real Switch. Report a cross-build and a hardware test separately. The [porting plan](PLAN.MD) describes the original-game comparison and handheld and docked checks.

For the Windows renderer regression fixtures, use Git Bash with UCRT64 GCC on `PATH`, Python 3.11+ (`PYTHON` can select an interpreter), and local ANGLE `libEGL.dll` and `libGLESv2.dll`. Set `DEVKITPRO` to the native SDK path (for example, `C:/devkitPro`). Run from the project root:

```sh
bash run_native_ot_checks.sh
bash run_presenter_checks.sh "$ANGLE_DIR"
bash run_live_native_race_gpu_hooks.sh "$ANGLE_DIR"
```

Use a native Windows path for `ANGLE_DIR`. These commands run synthetic fixtures without retail data. The last script also accepts an optional local `DRRCAP1` replay path as its second argument; keep any private capture outside source control and release assets. An untagged replay checks stock VRAM isolation, not where perspective correction is visible.
