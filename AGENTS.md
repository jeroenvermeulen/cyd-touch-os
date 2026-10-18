# AGENTS.md

Notes for AI coding agents working on this repo. Read [README.md](README.md) first for what the project does.

## Project

Arduino/PlatformIO firmware for the **ESP32-2432S028R** ("Cheap Yellow Display"): a small touch-screen "OS" (`cyd-touch-os`) whose menu launches apps: games, tools and the settings screen.

- Display: ILI9341, 320×240 landscape (`tft.setRotation(1)`), driven by `TFT_eSPI` on HSPI. All `TFT_eSPI` settings live in `build_flags` in [platformio.ini](platformio.ini); there is no `User_Setup.h`.
- Touch: XPT2046 resistive panel on its own VSPI bus, read with `XPT2046_Touchscreen`, not with TFT_eSPI's touch support.
- Board: plain ESP32 with **no PSRAM** (about 300 KB of usable heap).

## Build

```sh
pio run                # compile; run this to check every change
pio run -t upload      # flash (needs the board on USB)
pio device monitor     # serial log at 115200
```

There are no unit tests. `pio run` compiling cleanly is the minimum check. Once it compiles, try to flash with `pio run -t upload` so the change can be tried on the device. If the upload fails (for example, no board on USB), say so instead of retrying. You usually can't see the screen, so say so when a change still needs testing on the device.

## Layout

| Path | Purpose |
|---|---|
| [include/Config.h](include/Config.h) | Pins, screen size, touch calibration (`cfg::`), shared RGB565 palette (`color::`) |
| [src/main.cpp](src/main.cpp) | `setup()`/`loop()`, app registry (`apps[]`), switching between menu and apps |
| [src/App.h](src/App.h) | Base class every app implements |
| [src/Input.h](src/Input.h) / [.cpp](src/Input.cpp) | Touch polling, debouncing, mapping raw values to screen coordinates → `TouchEvent` |
| [src/Menu.h](src/Menu.h) / [.cpp](src/Menu.cpp) | Home screen styled as a Windows 3.11 Program Manager: app icons three per row (`drawIcon()` gets a 64×64 box and draws straight onto the white `color::ICON_BG`; either draw just the shapes or fill a mini scene); the title and menu bars are decorative; drag or tap the scrollbar arrows to scroll, a tap launches on release |
| [src/UI.h](src/UI.h) / [.cpp](src/UI.cpp) | Windows 95-style kit: `Button`, bevels, panels, title bars, windows, `ui::drawDialog`, `ui::drawHeader`, `ui::drawPauseButton` |
| [src/Keyboard.h](src/Keyboard.h) / [.cpp](src/Keyboard.cpp) | Full-screen on-screen keyboard (`Keyboard::begin()` + `update()` → `Done`/`Cancelled`), edits a caller-owned `char` buffer |
| [src/Net.h](src/Net.h) / [.cpp](src/Net.cpp) | Wi-Fi: saved credentials (NVS namespace `wifi`), background connect at startup and retries; `net::connected()` for apps that need the network |
| [src/Settings.h](src/Settings.h) / [.cpp](src/Settings.cpp) | Configuration screen (Wi-Fi scan, password entry). Registered in `apps[]` like any app, kept last |
| [src/apps/](src/apps/) | One `.h`/`.cpp` pair per app |

## How apps work

- Subclass `App`: `name()`, optional `drawIcon()`, `begin(tft)`, `update(ev)`, `finished()`. For a live icon, override `iconVersion()`: the menu polls it every loop and redraws that icon when the value changes (Clock returns the current minute).
- `begin()` is called each time the app is opened from the menu. It must reset `exit_`/state as needed and draw the full first screen. Objects are global and live forever, so state carries over between launches unless you reset it.
- `update()` is called every `loop()` (about every 2 ms) with a `TouchEvent`:
  - `tapped`: true once per finger-down; use it for buttons.
  - `touching` + `x`/`y`: finger is currently down; use it for dragging/drawing.
  - `released`: true once when the finger is lifted (after the debounce).
- Return `true` from `finished()` to go back to the menu, which then redraws itself.
- To register an app, add a global instance and append it to `apps[]` in `main.cpp`. The menu scrolls, so there's no fixed limit. `drawIcon()` may be handed a sprite strip instead of the screen, so draw only through its `tft` argument.

## Conventions and constraints

- **Never block.** Don't use `delay()` or long loops inside `update()`. Do timing with `millis()` (see `cpuMoveAt_` in TicTacToe, `lastMs_` in CrossyRoad) and compare with `(int32_t)(millis() - t) >= 0` so it survives wraparound.
- **Redraw only what changed.** Full `fillScreen` calls flicker. Draw the whole screen only on screen changes.
- **Watch memory.** A full-screen 16-bit sprite (150 KB) won't reliably allocate. Existing patterns: CrossyRoad renders through a 320×`TILE` strip sprite; Paint keeps its canvas in a 4-bit sprite. Allocate sprites once, lazily in `begin()`, check `createSprite()`'s return value, and log failures to `Serial`.
- **Persistence:** use `Preferences` (NVS) for small values like high scores (CrossyRoad) and `LittleFS` for files (Paint, mounted with `LittleFS.begin(true)`).
- **Network:** `net::begin()`/`net::loop()` in `main.cpp` own the Wi-Fi connection. Apps should check `net::connected()` and use `WiFi`/`HTTPClient` from there; don't call `WiFi.begin()`/`disconnect()` yourself. Keep requests non-blocking or short, as `update()` must not stall. Wi-Fi takes about 50 KB of heap once it starts, which leaves less for sprites.
- The interface has a uniform 90s (Windows 95) look: silver `color::FACE` with 3D bevels, navy title bars, black text. Build menus, buttons, dialogs, HUDs and side panels with the `UI.h` helpers and the `FACE`/`EDGE_*`/`TITLE` colours; the play area itself can stay dark. Use `ui::drawDialog` for pause/game-over and `ui::drawPauseButton` for the pause button. Put bright game colours (player colours, lives) in a sunken `color::BG` well so they stay readable. Per-game colours can be local `constexpr`s.
- Style: 4-space indent, `camelCase` methods, trailing `_` on members, `constexpr` constants in UPPER_CASE, anonymous namespaces for file-local helpers. Comments explain *why* and stay short.
- Don't change pin numbers, SPI settings or touch calibration unless asked; they're specific to this hardware.

## Docs

When you add or change an app, update the **Apps** section of [README.md](README.md) to match (controls, saving, how to exit).
