# AGENTS.md

Notes for AI coding agents working on this repo. Read [README.md](README.md) first for what the project does.

## Project

Arduino/PlatformIO firmware for the **ESP32-2432S028R** ("Cheap Yellow Display"): a touch-screen menu that launches small games.

- Display: ILI9341, 320×240 landscape (`tft.setRotation(1)`), driven by `TFT_eSPI` on HSPI. All `TFT_eSPI` settings live in `build_flags` in [platformio.ini](platformio.ini); there is no `User_Setup.h`.
- Touch: XPT2046 resistive panel on its own VSPI bus, read with `XPT2046_Touchscreen`, not with TFT_eSPI's touch support.
- Board: plain ESP32 with **no PSRAM** (about 300 KB of usable heap).

## Build

```sh
pio run                # compile; run this to check every change
pio run -t upload      # flash (needs the board on USB)
pio device monitor     # serial log at 115200
```

There are no unit tests. `pio run` compiling cleanly is the minimum check. You usually can't see the screen, so say so when a change still needs testing on the device.

## Layout

| Path | Purpose |
|---|---|
| [include/Config.h](include/Config.h) | Pins, screen size, touch calibration (`cfg::`), shared RGB565 palette (`color::`) |
| [src/main.cpp](src/main.cpp) | `setup()`/`loop()`, game registry (`games[]`), switching between menu and games |
| [src/Game.h](src/Game.h) | Base class every game implements |
| [src/Input.h](src/Input.h) / [.cpp](src/Input.cpp) | Touch polling, debouncing, mapping raw values to screen coordinates → `TouchEvent` |
| [src/Menu.h](src/Menu.h) / [.cpp](src/Menu.cpp) | 2×2 tile menu (max 4 games) |
| [src/UI.h](src/UI.h) / [.cpp](src/UI.cpp) | `Button` and `ui::drawHeader` helpers |
| [src/games/](src/games/) | One `.h`/`.cpp` pair per game |

## How games work

- Subclass `Game`: `name()`, optional `drawIcon()`, `begin(tft)`, `update(ev)`, `finished()`.
- `begin()` is called each time the game is opened from the menu. It must reset `exit_`/state as needed and draw the full first screen. Objects are global and live forever, so state carries over between launches unless you reset it.
- `update()` is called every `loop()` (about every 2 ms) with a `TouchEvent`:
  - `tapped`: true once per finger-down; use it for buttons.
  - `touching` + `x`/`y`: finger is currently down; use it for dragging/drawing.
- Return `true` from `finished()` to go back to the menu, which then redraws itself.
- To register a game, add a global instance and append it to `games[]` in `main.cpp`. The menu has room for 4, and all 4 are used today.

## Conventions and constraints

- **Never block.** Don't use `delay()` or long loops inside `update()`. Do timing with `millis()` (see `cpuMoveAt_` in TicTacToe, `lastMs_` in CrossyRoad) and compare with `(int32_t)(millis() - t) >= 0` so it survives wraparound.
- **Redraw only what changed.** Full `fillScreen` calls flicker. Draw the whole screen only on screen changes.
- **Watch memory.** A full-screen 16-bit sprite (150 KB) won't reliably allocate. Existing patterns: CrossyRoad renders through a 320×`TILE` strip sprite; Paint keeps its canvas in a 4-bit sprite. Allocate sprites once, lazily in `begin()`, check `createSprite()`'s return value, and log failures to `Serial`.
- **Persistence:** use `Preferences` (NVS) for small values like high scores (CrossyRoad) and `LittleFS` for files (Paint, mounted with `LittleFS.begin(true)`).
- Use colours from `color::` and helpers from `UI.h` for a consistent look; per-game colours can be local `constexpr`s.
- Style: 4-space indent, `camelCase` methods, trailing `_` on members, `constexpr` constants in UPPER_CASE, anonymous namespaces for file-local helpers. Comments explain *why* and stay short.
- Don't change pin numbers, SPI settings or touch calibration unless asked; they're specific to this hardware.

## Docs

When you add or change a game, update the **Games** section of [README.md](README.md) to match (controls, saving, how to exit).
