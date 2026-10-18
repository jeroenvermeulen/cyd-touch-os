# ESP32 Touch Games

A mini game computer for the **ESP32-2432S028R** ("Cheap Yellow Display"): it boots into a menu, and you tap a game to start it.

> **Disclaimer:** This code is fully AI-generated. Review it before you rely on it.

## Games

- **Tic-Tac-Toe**: 1 player vs CPU (Easy / Hard, where Hard is unbeatable) or 2 players on one screen. It keeps score and alternates who goes first.
- **Crossy Road**: hop across roads, rivers (ride the logs!) and railway tracks. Tap the middle of the screen to hop forward, the left/right edges to hop sideways and the bottom to hop back. The screen scrolls slowly on its own, so don't dawdle. The high score is saved in flash. Tap the pause button (top right) to get back to the menu.
- **Paint**: finger painting with 16 colours (white is the eraser) and 4 brush sizes; tap the dot button to change the size. **File** opens your pictures: pick one of 6 slots, then **Save**, **Load** or **Delete** it, or tap **New** to clear the canvas. Pictures are stored in the ESP32's flash (LittleFS), so they survive power-off. The picture stays on the canvas when you go back to the menu, until you power off.
- **Snake**: eat the apples to grow; don't hit the walls or yourself. Tap above or below the head to turn while moving sideways, and left or right of it while moving up or down (you can queue two quick turns). Every apple makes the snake a little faster. The high score is saved in flash. Tap the pause button (top right) to get back to the menu.

## Build & flash

Requires [PlatformIO](https://platformio.org/).

```sh
pio run -t upload      # build and flash
pio device monitor     # serial log (shows raw touch coordinates)
```

## Troubleshooting

- **Colours inverted**: uncomment `-DTFT_INVERSION_ON=1` in `platformio.ini`. Some boards need this, such as the 2-USB variant.
- **Taps land in the wrong spot**: watch the serial monitor while tapping the corners, then adjust `TOUCH_X_MIN/MAX` and `TOUCH_Y_MIN/MAX` in `include/Config.h`.

## Adding a game

1. Create `src/games/MyGame.h/.cpp` with a class that derives from `Game` (see `src/Game.h`).
2. Add an instance to the `games[]` list in `src/main.cpp`.

The menu shows up to 4 games in a 2×2 grid. Set `finished()` to return `true` to go back to the menu.
