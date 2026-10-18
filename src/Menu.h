#pragma once

#include <TFT_eSPI.h>

#include "Game.h"
#include "Input.h"

class Menu {
public:
    Menu(Game* const* games, int count) : games_(games), count_(count) {}

    void draw(TFT_eSPI& tft);

    // Returns the index of the tapped game, or -1.
    int update(const TouchEvent& ev) const;

private:
    static constexpr int COLS = 2;
    static constexpr int MAX_TILES = 4;
    static constexpr int16_t TILE_W = 135;
    static constexpr int16_t TILE_H = 80;
    static constexpr int16_t GAP = 10;
    static constexpr int16_t TOP = 55;

    void tileRect(int i, int16_t& x, int16_t& y) const;

    Game* const* games_;
    int count_;
};
