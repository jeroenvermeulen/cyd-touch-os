# CYD Touch OS

A tiny touch-screen OS for the **ESP32-2432S028R** ("Cheap Yellow Display"): it boots into a Program Manager-style menu, and you tap an app (a game, a tool or the settings) to start it.

> **Disclaimer:** This code is fully AI-generated. Review it before you rely on it.

## Apps

- **Tic-Tac-Toe**: 1 player vs CPU (Easy / Hard, where Hard is unbeatable) or 2 players on one screen. It keeps score and alternates who goes first.
- **Crossy Road**: hop across roads, rivers (ride the logs!) and railway tracks. Tap the middle of the screen to hop forward, the left/right edges to hop sideways and the bottom to hop back. The screen scrolls slowly on its own, so don't dawdle. The high score is saved in flash. Tap the pause button (top right) to get back to the menu.
- **Paint**: finger painting with 16 colours (white is the eraser) and 4 brush sizes; tap the dot button to change the size. **File** opens your pictures: pick one of 6 slots, then **Save**, **Load** or **Delete** it, or tap **New** to clear the canvas. Pictures are stored in the ESP32's flash (LittleFS), so they survive power-off. The picture stays on the canvas when you go back to the menu, until you power off.
- **Snake**: eat the apples to grow; don't hit the walls or yourself. Tap above or below the head to turn while moving sideways, and left or right of it while moving up or down (you can queue two quick turns). Every apple makes the snake a little faster. The high score is saved in flash. Tap the pause button (top right) to get back to the menu.
- **Tetris**: the classic falling blocks. The two buttons on the left move the piece (hold them to keep moving), and on the right the top button rotates it and the bottom one drops it faster while you hold it. A piece that has landed can still be slid or rotated for a moment before it locks. A faint outline shows where the piece will land, and the box on the right shows the next one. Every 10 lines the level goes up and the pieces fall faster. The high score is saved in flash. Tap the pause button (top right) to get back to the menu.
- **Pac-Man**: the arcade maze with all four ghosts, each chasing in its own way, and their scatter/chase waves. To steer, swipe anywhere on the screen or hold the D-pad on the right. Pac-Man remembers the last direction and turns as soon as he can, so you can steer a little before a corner. Power pellets make the ghosts edible for a while (200, 400, 800, then 1600 points). You get an extra life at 10,000 points, and every cleared maze makes the next level faster. The high score is saved in flash. Tap the pause button (top right) to get back to the menu.
- **Minesweeper**: clear the 14×12 field without setting off any of its 25 mines. Each number tells how many mines touch that cell. Tap a cell to dig it, or hold it for a moment to plant or remove a flag. The **Dig** and **Flag** buttons on the left swap the two, so in Flag mode a tap flags and a hold digs. Your first dig is always safe and opens up an area. Tapping a number that already has all its flags opens the rest of its neighbours. The counter at the top left shows the mines still unflagged and the one below the face shows the time. Tap the face to start a new game. The best winning time is saved in flash. Tap the pause button (top left) to get back to the menu.
- **Clock**: an analog clock face with the digital time (24-hour) and the date beside it. The time is fetched from the internet (NTP) once Wi-Fi connects, so set up Wi-Fi in **Settings** first; until then the clock says what it is waiting for. The time zone is `TIMEZONE` in `include/Config.h` (a POSIX TZ string, set to Europe/Amsterdam with summer time). Tap **< Back** to go back to the menu. Once the time is known, the Clock icon in the menu shows it too, updated every minute.
- **Weather**: the current weather and a 4-day forecast for Retie, Belgium, from [Open-Meteo](https://open-meteo.com/) (free, no API key). The left shows the conditions and temperature, the right shows the "feels like" temperature, humidity, wind and when the reading was taken. Along the bottom, each day shows its high and low (the low in grey) and the chance of rain. It needs Wi-Fi (set it up in **Settings**). The weather is downloaded when you open the app and every 15 minutes while it stays open. If a download fails, it tries again after a minute. Tap **Refresh** to download right away, or **< Back** to go back to the menu. The menu icon shows the last weather that was downloaded. The location is `WEATHER_PLACE`/`WEATHER_LAT`/`WEATHER_LON` in `include/Config.h`.

## Screensaver

After 2 minutes without a touch, Flying Windows from Windows 3.1 takes over: Windows logos fly towards you out of a black sky. This happens on the menu and in the games with a pause button (Crossy Road, Snake, Tetris, Pac-Man and Minesweeper). A game is paused first. A game that is in the middle of a death or line-clear animation waits for it to finish. The other apps never start it. Tap anywhere to stop the screensaver. You go back to the menu, or to the game with its **Resume** / **Menu** dialog open (or its game-over dialog, if the game had ended). The delay is `SCREENSAVER_MS` in `include/Config.h`.

## Settings

The **Settings** icon (last in the menu) sets up Wi-Fi. It shows the saved network, whether it is connected and its IP address. **Choose Network** scans for networks; the list shows signal strength and a lock for secured ones, and the saved network is in bold. Use the arrow buttons to page through the list and **Rescan** to scan again. Tap a network to join it. For a secured network, type the password on the on-screen keyboard: **?123** switches to symbols, **Shift** is caps lock, and holding **Bksp** keeps deleting. **OK** stays grey until the password is long enough. The network and password are saved in flash, and the board connects in the background at every startup, trying again every 30 seconds while the network can't be reached. Once connected it also sets the clock over the internet (NTP) and keeps it in sync. **Forget** removes the saved network and switches Wi-Fi off. Hidden networks (no name broadcast) aren't supported yet. Tap **< Main Menu** to go back.

## Build & flash

Requires [PlatformIO](https://platformio.org/).

```sh
pio run -t upload      # build and flash
pio device monitor     # serial log (shows raw touch coordinates)
```

## Troubleshooting

- **Colours inverted**: uncomment `-DTFT_INVERSION_ON=1` in `platformio.ini`. Some boards need this, such as the 2-USB variant.
- **Taps land in the wrong spot**: watch the serial monitor while tapping the corners, then adjust `TOUCH_X_MIN/MAX` and `TOUCH_Y_MIN/MAX` in `include/Config.h`.

## Adding an app

1. Create `src/apps/MyApp.h/.cpp` with a class that derives from `App` (see `src/App.h`).
2. Add an instance to the `apps[]` list in `src/main.cpp`.

The menu looks like a maximized Windows 3.11 Program Manager: each app is an icon (its `drawIcon()` picture, 64×64 px with no box behind it, with the name underneath), three per row. When they don't all fit, drag up or down or tap the scrollbar arrows to scroll. Set `finished()` to return `true` to go back to the menu.
