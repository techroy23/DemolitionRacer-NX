# Demolition Racer for Nintendo Switch

A native homebrew port of `SLUS-00969`, the US PlayStation release, for the first-generation Switch OLED. The project builds the reconstructed game as AArch64 code and reads original artwork, tracks, and audio from a local asset pack. No emulator frontend or retail BIOS is used. See [PLAN.MD](PLAN.MD) for parity milestones and what has been verified.

**Status: 0.1.5 is ready for testing.** It tries three display buffers to reduce time spent waiting for one to become available, falling back to two if allocation fails. It also adds collision response timing and missed-submission-gap counters. Original race timing, collision logic, and game artwork are unchanged.

## Prepare your game data

Use your own unmodified US single-track `MODE2/2352` BIN. Python 3.11 or newer is required. Run these from the repository root in PowerShell or Git Bash:

```sh
python -m unittest discover -s tests -v
python tools/prepare_assets.py "DISC/Demolition Racer (USA).bin"
```

The tool checks the exact supported disc SHA-256 and makes `pack/DATA/` without changing `DATA/` or `DISC/`. If `pack/` already exists, pass `--out` with a new local path. Supply the raw disc image, not the loose `.XA` and `.STR` files, which do not retain their XA sector metadata.

## Build

Install the Switch development packages following [devkitPro's Windows setup](https://devkitpro.org/wiki/Getting_Started). The build needs devkitA64, libnx, Switch packaging tools, and Python with Pillow (`python -m pip install Pillow`). The two `game_*.png` files are launcher artwork; the NRO embeds a square Homebrew Menu icon and shows the splash for about five seconds before the original intro. The splash fills the available width, with top and bottom bars so no artwork is cropped or stretched. The original movies and game assets remain on the SD card. Invoke `make` from the devkitPro MSYS shell with its normal SDK environment. Run from the project root:

```sh
git submodule update --init -- .deps/xport
make -C switch
```

Output: `switch/build/DemolitionRacer.nro`. The build does not need the retail files and must not embed them.

## Package and install on an existing homebrew setup

After building the NRO and preparing the assets, run `python tools/package_switch.py` from the repository root. It creates `dist/` with the exact SD card layout. The script refuses to overwrite an existing package; move the old `dist/` aside or pass `--out` with a new directory.

Copy the **contents of `dist/`** to the root of the Switch SD card, merging with its existing `switch/` directory. You should end up with:

```text
sdmc:/switch/DemolitionRacer/
  DemolitionRacer.nro
  settings.yaml
  DATA/
```

Start the NRO from the Homebrew Menu. For initial testing, use the full-memory Homebrew Menu mode. Keep `dist/` private because it contains your extracted game assets.

## Settings and hardware logs

`settings.yaml` is read at launch. These are the packaged defaults:

```yaml
LOG: DEBUG
FPS_OVERLAY: false
UPSCALE: FIT
```

- Hold **+ (Start) + − (Select) + ZL (L2) + ZR (R2)** together to exit to the Homebrew Menu. This does not force a game save.
- `FPS_OVERLAY: true` shows the Switch framebuffer submission rate, not the number of distinct PS1 frames or the panel refresh rate. The default keeps the original picture free of overlays.
- `UPSCALE` uses nearest-neighbor output scaling from a 320×240 reference with the original 4:3 shape. `x2` targets 640×480, `x3` targets 960×720, and `x4` targets 1280×960 when launched docked with 1080p framebuffer support. `x4` is capped at 960×720 on the 720p path. `FIT` keeps the existing fit-to-screen behavior. This does not rerender geometry or add detail to the source pixels.
- On launch the NRO creates `SAVE/`. It moves an existing `DemolitionRacer.sav` there if no save or recovery backup exists there already. Existing saves in `SAVE/` take priority.
- Each launch creates a separate `DemolitionRacer_YYYY-MM-DD_HH-MM-SS.log` next to the NRO, using the console's local time. `DEBUG` adds elapsed timestamps, frame submission timings, audio counters, file opens, and save events. `INFO` keeps major events and errors; `ERROR` keeps errors only. Missing or invalid settings use `INFO`, overlay off, and `FIT`.

If something goes wrong, send the log for that launch and tell us the last screen you saw and what you pressed. For race stutter, run the same race and send the `Race profile` and `Present profile` lines with `LOG: DEBUG`. Race timings include the original two-field wait and can include calls that also submit to the framebuffer; the phase and presentation timings must not be added together. The FPS overlay counts framebuffer submissions, not distinct race iterations. The log is not a system crash dump.

## Source provenance

The game C baseline was matched against [DemolitionRacer-xport](https://github.com/psxports/DemolitionRacer-xport/commit/d7d82178d7ec1fc1614391ceeb6a705b5bb1fde7), which is MIT-licensed. Shared [xport](https://github.com/psxports/xport) code is pinned in the Git submodule, with its own MIT license. This repository's license does not cover the original PlayStation game or its media.
