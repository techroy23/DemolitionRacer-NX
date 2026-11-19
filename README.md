# Demolition Racer for Nintendo Switch

This is an unofficial homebrew port of the US PlayStation game for the first-generation Nintendo Switch OLED. It aims to keep the original menus, loading screens, and racing intact.

**The game is not included.** This repository contains reconstructed port code, not the original game's source code, disc image, or retail game assets. You must supply your own disc dump. The port runs on the Switch without a PlayStation emulator or BIOS.

## 1. Make the `DATA` folder

If you don't already have the project files, open the [latest release page](https://github.com/techroy23/DemolitionRacer-NX/releases/latest), download **Source code (zip)** under **Assets**, and extract it. The ZIP includes the Python script and `settings.yaml`. You don't need to build anything.

Install [Python 3.11 or newer](https://www.python.org/downloads/). You don't need any extra Python packages or Switch build tools for this step.

In the folder that contains this README, create a `DISC` folder and put both files from your own unmodified US disc dump (`SLUS-00969`) inside it:

```text
DISC/
  Demolition Racer (USA).bin
  Demolition Racer (USA).cue
```

Open PowerShell or a terminal in the folder that contains this README, not inside `DISC`, and run:

```sh
python tools/prepare_assets.py
```

The script makes `dist/switch/DemolitionRacer/DATA/`. It won't change your disc files or overwrite an existing `DATA` folder. If it says the disc is unsupported, check that you have the unmodified US release; renaming a different release won't make it work.

## 2. Download the Switch app

[Download `DemolitionRacer.nro`](https://github.com/techroy23/DemolitionRacer-NX/releases/latest/download/DemolitionRacer.nro). This is the file you open on your Switch. It won't run without the `DATA` folder you made in step 1 from your own disc dump.

Prefer to compile the app yourself? See the [build guide](technical.md#build-the-switch-app).

## 3. Copy it to your Switch

Your Switch must already be able to open the Homebrew Menu.

1. Put `DemolitionRacer.nro` and a copy of [settings.yaml](switch/settings.yaml) beside the generated `DATA` folder in `dist/switch/DemolitionRacer/`. Keep your existing settings if you're updating.
2. Copy `dist/switch/DemolitionRacer/` into `switch/` on your SD card. The app should land at `switch/DemolitionRacer/DemolitionRacer.nro`, with `DATA/` right beside it.
3. Open **Demolition Racer** from the Homebrew Menu. To quit, hold **+**, **−**, **ZL**, and **ZR** together.

If something breaks, share the dated `.log` file next to the NRO and say what was on screen when it happened. Keep your disc dump and generated `DATA` files private.

## More information

The Switch Homebrew Menu icon and launcher splash artwork were sourced from [Demolition Racer on SteamGridDB](https://www.steamgriddb.com/game/5270215). These are separate from the original in-game loading screen, which comes from your disc dump.

- [Project goals, status, and credits](project.md)
- [Build, settings, and technical notes](technical.md)
- [Install instructions for AI assistants](AI.md)
- [Porting plan and parity checks](PLAN.MD)
