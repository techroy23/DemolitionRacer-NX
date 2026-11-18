# Demolition Racer for Nintendo Switch

This is an unofficial homebrew port of the US PlayStation game for the first-generation Nintendo Switch OLED. It aims to keep the original menus, loading screens, and racing intact.

**The game is not included.** This repository contains reconstructed port code, not the original game's source code, disc image, or retail game assets. You must supply your own disc dump. The port runs on the Switch without a PlayStation emulator or BIOS.

## 1. Make the `DATA` folder

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

## 2. Copy it to your Switch

You need a Switch that can already launch the Homebrew Menu and a copy of `DemolitionRacer.nro`, the port's Switch app. If you need to build the app, see the [build instructions](technical.md#build-the-switch-app).

1. Put `DemolitionRacer.nro` and a copy of [settings.yaml](switch/settings.yaml) beside the generated `DATA` folder in `dist/switch/DemolitionRacer/`. Keep your existing settings if you're updating.
2. Copy `dist/switch/DemolitionRacer/` into `switch/` on your SD card. The app should land at `switch/DemolitionRacer/DemolitionRacer.nro`, with `DATA/` right beside it.
3. Open **Demolition Racer** from the Homebrew Menu. To quit, hold **+**, **−**, **ZL**, and **ZR** together.

If something breaks, share the dated `.log` file next to the NRO and say what was on screen when it happened. Keep your disc dump and generated `DATA` files private.

## More information

- [Project goals, status, and credits](project.md)
- [Build, settings, and technical notes](technical.md)
- [Install instructions for AI assistants](ai.md)
- [Porting plan and parity checks](PLAN.MD)
