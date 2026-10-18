#include "Menu.h"

#include "Config.h"
#include "UI.h"

void Menu::tileRect(int i, int16_t& x, int16_t& y) const {
    const int16_t left = (cfg::SCREEN_W - (COLS * TILE_W + (COLS - 1) * GAP)) / 2;
    x = left + (i % COLS) * (TILE_W + GAP);
    y = TOP + (i / COLS) * (TILE_H + GAP);
}

void Menu::draw(TFT_eSPI& tft) {
    ui::drawHeader(tft, "Game Menu");

    for (int i = 0; i < count_ && i < MAX_TILES; i++) {
        int16_t x, y;
        tileRect(i, x, y);
        tft.fillRoundRect(x, y, TILE_W, TILE_H, 10, color::PANEL);
        tft.drawRoundRect(x, y, TILE_W, TILE_H, 10, color::ACCENT);
        games_[i]->drawIcon(tft, x + 4, y + 4, TILE_W - 8, TILE_H - 26);
        tft.setTextDatum(BC_DATUM);
        tft.setTextColor(color::TEXT, color::PANEL);
        tft.drawString(games_[i]->name(), x + TILE_W / 2, y + TILE_H - 4, 2);
    }
}

int Menu::update(const TouchEvent& ev) const {
    if (!ev.tapped) return -1;
    for (int i = 0; i < count_ && i < MAX_TILES; i++) {
        int16_t x, y;
        tileRect(i, x, y);
        if (ev.x >= x && ev.x < x + TILE_W && ev.y >= y && ev.y < y + TILE_H) return i;
    }
    return -1;
}
