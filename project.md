# Project overview

This is an unofficial native homebrew port of the US PlayStation release of *Demolition Racer* (`SLUS-00969`) for the first-generation Nintendo Switch OLED. The goal is to reproduce the original game, not remake it. Menus, loading screens, artwork, sounds, physics, and race timing should match the PlayStation release wherever possible. Switch-specific controls, storage, and diagnostics live outside the original game UI.

## Status

- Version `0.1.5` completed a race, saved, and exited cleanly in a handheld OLED test. That run does not establish parity across all tracks or modes, or validate docked performance.
- The current Switch target is one player. Two-player support is outside the current scope.
- Compare presentation and behavior against the US retail release before treating a difference as an improvement. See the [porting plan](PLAN.MD) for the test matrix and references.

## Source and credits

- `src/` is the locally maintained application source, based on the reconstructed [DemolitionRacer-xport baseline](https://github.com/psxports/DemolitionRacer-xport/commit/d7d82178d7ec1fc1614391ceeb6a705b5bb1fde7). It is not the original developer's source code. The upstream MIT notice is retained in [LICENSE](LICENSE).
- [`third_party/xport/`](third_party/xport) is the pinned [xport](https://github.com/psxports/xport) compatibility library. It has its own MIT notice. It is a dependency, not the game itself.
- The software license does not grant rights to the retail game or its media. Each player supplies their own disc dump. Keep `DISC/`, `DATA/`, `pack/`, and `dist/` out of Git, and don't attach retail files to issues or releases.

See [README.md](README.md) to install the port or [technical.md](technical.md) to build and troubleshoot it.
