# Demolition Racer for Nintendo Switch

A native homebrew port of `SLUS-00969`, the US PlayStation release, for the first-generation Switch OLED. The project builds the reconstructed game as AArch64 code and reads original artwork, tracks, and audio from a local asset pack. No emulator frontend or retail BIOS is used. See [PLAN.MD](PLAN.MD) for parity milestones and what has been verified.

**Status: build verified, not play-tested on Switch.** The first native NRO is ready for an OLED boot test. Menu, loading-screen, racing, audio, and save parity remain unverified.

## Prepare your game data

Use your own unmodified US single-track `MODE2/2352` BIN. Python 3.11 or newer is required. Run these from the repository root in PowerShell or Git Bash:

```sh
python -m unittest discover -s tests -v
python tools/prepare_assets.py "DISC/Demolition Racer (USA).bin"
```

The tool checks the exact supported disc SHA-256 and makes `pack/DATA/` without changing `DATA/` or `DISC/`. If `pack/` already exists, pass `--out` with a new local path. Supply the raw disc image, not the loose `.XA` and `.STR` files, which do not retain their XA sector metadata.

## Build

Install the Switch development packages following [devkitPro's Windows setup](https://devkitpro.org/wiki/Getting_Started). The build needs devkitA64, libnx, Switch packaging tools, and Switch Mesa (EGL/OpenGL ES). Invoke `make` from the devkitPro MSYS shell with its normal SDK environment. Run from the project root:

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

`settings.yaml` is read at launch. The package ships with `LOG: DEBUG`, which records startup, graphics and audio initialization, first presentation, asset opens, and application errors. `INFO` keeps major events and errors; `ERROR` keeps errors only. A missing file or invalid `LOG` falls back to `INFO`. No original in-game options are changed.

If something goes wrong, send `sdmc:/switch/DemolitionRacer/DemolitionRacer.log` and tell us the last screen you saw and what you pressed. Copy it before relaunching if you can. The next launch normally moves it to `DemolitionRacer.previous.log`. Logging starts after the NRO reaches its entry point, and is not a crash dump; a failure before then may require a photo or the system error code.

## Source provenance

The game C baseline was matched against [DemolitionRacer-xport](https://github.com/psxports/DemolitionRacer-xport/commit/d7d82178d7ec1fc1614391ceeb6a705b5bb1fde7), which is MIT-licensed. Shared [xport](https://github.com/psxports/xport) code is pinned in the Git submodule, with its own MIT license. This repository's license does not cover the original PlayStation game or its media.
