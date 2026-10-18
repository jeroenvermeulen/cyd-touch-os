#pragma once

#include <TFT_eSPI.h>

#include "Config.h"
#include "Game.h"
#include "Input.h"

// The home screen, styled as a maximized Windows 3.11 Program Manager: one
// icon per game in a white window. Drag up/down (or tap the scrollbar
// arrows) to scroll when the icons don't all fit; tap an icon to launch it.
class Menu {
public:
    Menu(Game* const* games, int count) : games_(games), count_(count) {}

    void draw(TFT_eSPI& tft);

    // Returns the index of the tapped game, or -1.
    int update(const TouchEvent& ev);

private:
    static constexpr int COLS = 3;
    static constexpr int16_t ICON_W = 64;   // square box handed to Game::drawIcon()
    static constexpr int16_t ICON_H = 64;
    static constexpr int16_t LABEL_GAP = 3;
    static constexpr int16_t LABEL_H = 16;
    static constexpr int16_t CELL_H = ICON_H + LABEL_GAP + LABEL_H + 10;
    static constexpr int16_t MARGIN = 8;  // above the first and below the last row

    // Window chrome: frame, title bar, menu bar, then the white client area.
    static constexpr int16_t FRAME = 4;
    static constexpr int16_t BAR_H = 18;  // title and menu bar height
    static constexpr int16_t CLIENT_X = FRAME;
    static constexpr int16_t CLIENT_Y = FRAME + BAR_H + 1 + BAR_H + 1;
    static constexpr int16_t CLIENT_W = cfg::SCREEN_W - 2 * FRAME;
    static constexpr int16_t CLIENT_H = cfg::SCREEN_H - FRAME - CLIENT_Y;
    static constexpr int16_t STRIP_H = 20;    // rows per sprite strip when rendering
    static constexpr int16_t DRAG_PX = 10;    // movement that turns a touch into a scroll

    // Icon picture position on screen before scrolling.
    void iconRect(int i, int16_t& x, int16_t& y) const;
    int iconAt(int16_t x, int16_t y) const;  // screen coords -> icon index or -1
    int16_t maxScroll() const;

    void render() { renderRows(0, cfg::SCREEN_H); }
    void renderRows(int16_t top, int16_t bottom);  // screen rows [top, bottom)
    void refreshIcon(int i);
    void drawIcon(TFT_eSPI& g, int i, int16_t oy);
    void drawScrollbar(TFT_eSPI& g, int16_t oy);
    void drawChrome(TFT_eSPI& g, int16_t oy);
    void scrollBy(int16_t dy);

    Game* const* games_;
    int count_;
    TFT_eSPI* tft_ = nullptr;
    TFT_eSprite* strip_ = nullptr;
    uint32_t* iconVersions_ = nullptr;  // last Game::iconVersion() drawn, per game

    int16_t scroll_ = 0;  // pixels scrolled down; kept between visits
    int selected_ = -1;   // icon highlighted under the finger
    bool pressed_ = false;
    bool dragging_ = false;
    int16_t pressX_ = 0, pressY_ = 0;
    int16_t pressScroll_ = 0;
};
