#pragma once

#include <TFT_eSPI.h>

#include "Game.h"
#include "Input.h"

// Grid of game tiles, two per row. Drag up/down to scroll when there are
// more than fit on the screen; tap a tile to launch it.
class Menu {
public:
    Menu(Game* const* games, int count) : games_(games), count_(count) {}

    void draw(TFT_eSPI& tft);

    // Returns the index of the tapped game, or -1.
    int update(const TouchEvent& ev);

private:
    static constexpr int COLS = 2;
    static constexpr int16_t TILE_W = 135;
    static constexpr int16_t TILE_H = 80;
    static constexpr int16_t GAP = 10;
    static constexpr int16_t MARGIN = 10;    // above the first and below the last row
    static constexpr int16_t STRIP_H = 20;   // rows per sprite strip when rendering
    static constexpr int16_t DRAG_PX = 10;   // movement that turns a touch into a scroll

    // Tile position in content coordinates (before scrolling).
    void tileRect(int i, int16_t& x, int16_t& y) const;
    int tileAt(int16_t x, int16_t y) const;  // screen coords -> tile index or -1
    int16_t maxScroll() const;

    void render();
    void drawTile(TFT_eSPI& g, int i, int16_t oy);
    void drawScrollbar(TFT_eSPI& g, int16_t oy);

    Game* const* games_;
    int count_;
    TFT_eSPI* tft_ = nullptr;
    TFT_eSprite* strip_ = nullptr;

    int16_t scroll_ = 0;  // pixels scrolled down; kept between visits
    bool pressed_ = false;
    bool dragging_ = false;
    int16_t pressX_ = 0, pressY_ = 0;
    int16_t pressScroll_ = 0;
};
