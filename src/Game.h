#pragma once

#include <TFT_eSPI.h>

#include "Input.h"

// Base class for every game shown in the main menu.
//
// To add a game: subclass this, then add an instance to the `games` list in main.cpp.
class Game {
public:
    virtual ~Game() = default;

    // Name shown on the menu tile.
    virtual const char* name() const = 0;

    // Draw a small preview picture for the menu icon (optional). There's no
    // box behind it, so it lands on color::ICON_BG unless it fills its own
    // background (a mini scene, like CrossyRoad and PacMan).
    virtual void drawIcon(TFT_eSPI& tft, int16_t x, int16_t y, int16_t w, int16_t h) {}

    // For a live icon (like Clock's): the menu polls this and redraws the
    // icon whenever the value changes. Keep it cheap.
    virtual uint32_t iconVersion() const { return 0; }

    // Called when the game is launched from the menu; should draw its first screen.
    virtual void begin(TFT_eSPI& tft) = 0;

    // Called continuously while the game is active.
    virtual void update(const TouchEvent& ev) = 0;

    // Return true to go back to the main menu.
    virtual bool finished() const = 0;
};
