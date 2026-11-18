# Technical notes

## Source layout

| Path | What it contains |
| --- | --- |
| `src/` | Reconstructed game logic, plus Windows, web, and Switch platform code under `src/platform/`. |
| `third_party/xport/` | Pinned compatibility library providing PS1-style services used by the game code. |
| `switch/` | Switch build rules and default configuration; compiled artifacts go in `switch/build/`. |
| `tools/`, `tests/` | Disc extraction, artwork preparation, packaging, and host tests. |

The Switch build compiles the game to AArch64 and links the needed compatibility services. It does not execute the retail MIPS instructions or use a retail BIOS. Some files extracted from the disc, including PS1 executables, are read as data. The Switch backend submits the game's pixels through libnx and keeps the original 4:3 view. It does not change the game's race timing to match display refresh.

## Build the Switch app

Use a Git checkout so you can initialize the submodule. Install the Switch development packages from [devkitPro](https://devkitpro.org/wiki/Getting_Started), including devkitA64, libnx, and the NRO packaging tools. Install Pillow for the same Python interpreter the build uses (`python -m pip install Pillow`). On Windows, run the build from the devkitPro MSYS shell. Start in the repository root:

```sh
git submodule update --init -- third_party/xport
make -C switch
```

The output is `switch/build/DemolitionRacer.nro`. Building does not require a retail disc. The two `game_*.png` files provide the port's launcher icon and startup splash; they are separate from the original game assets that the app reads from your SD card. The splash lasts about five seconds before the game starts, and scales to fit without stretching or cropping.

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
- Saves go in `SAVE/` next to the NRO. A legacy `DemolitionRacer.sav` can migrate if there is no existing save or recovery backup there. The exit chord does not force a save.
- A new `DemolitionRacer_YYYY-MM-DD_HH-MM-SS.log` uses the console's local time. `DEBUG` includes file access, elapsed time, framebuffer, audio, and save diagnostics. Use `INFO` for major events or `ERROR` for errors only.

For race stutter, reproduce on the same track and keep the `Race profile`, `Collision profile`, and `Present profile` lines. The original race loop waits for at least two game VBlank counts per iteration. Its timings can include calls that submit frames, so don't add race and presentation timings together.

## Verification

Run `python -m unittest discover -s tests -v` from the project root for host tests. For runtime changes, start with `make -C switch clean` and rebuild before testing on a real Switch. Report a cross-build and a hardware test separately. The [porting plan](PLAN.MD) describes the original-game comparison and handheld and docked checks.
