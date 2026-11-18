# Installation for AI assistants

1. Work from the repository root. Check for Python 3.11 or newer with `python --version`. If Python or other tooling must be installed, ask before installing it. Do not install a skill file without explicit approval.
2. Check for the user's own `DISC/Demolition Racer (USA).cue` and the `.bin` it names. If the files are missing, stop and ask the user to supply them. Do not fetch or share a retail disc image, BIOS, or extracted game data.
3. Run `python tools/prepare_assets.py`. Verify that `dist/switch/DemolitionRacer/DATA/DD.IDX` exists. If `DATA/` already exists, do not delete it to rerun extraction. Use the existing data or pass `--out` with a separate app folder.
4. Use the user's `DemolitionRacer.nro`, download the prebuilt NRO from the [GitHub release](https://github.com/techroy23/DemolitionRacer-NX/releases/latest), or follow the [build instructions](technical.md#build-the-switch-app). Place the NRO and a copy of [switch/settings.yaml](switch/settings.yaml) beside `DATA/`. Preserve any existing `settings.yaml`, `SAVE/`, and logs.
5. Verify the app folder contains the NRO, settings, and `DATA/`. Follow the [SD card instructions](README.md#3-copy-it-to-your-switch) once the user identifies the correct card. Do not claim that the port ran on the Switch unless it was actually launched there.
