#include "Menu.h"

#include <Arduino.h>

#include "Config.h"

namespace {
constexpr int16_t BAR_X = cfg::SCREEN_W - 8;
constexpr int16_t BAR_W = 3;
}  // namespace

void Menu::tileRect(int i, int16_t& x, int16_t& y) const {
    const int16_t left = (cfg::SCREEN_W - (COLS * TILE_W + (COLS - 1) * GAP)) / 2;
    x = left + (i % COLS) * (TILE_W + GAP);
    y = MARGIN + (i / COLS) * (TILE_H + GAP);
}

int Menu::tileAt(int16_t sx, int16_t sy) const {
    const int16_t cy = sy + scroll_;
    for (int i = 0; i < count_; i++) {
        int16_t x, y;
        tileRect(i, x, y);
        if (sx >= x && sx < x + TILE_W && cy >= y && cy < y + TILE_H) return i;
    }
    return -1;
}

int16_t Menu::maxScroll() const {
    const int rows = (count_ + COLS - 1) / COLS;
    const int16_t contentH = 2 * MARGIN + rows * TILE_H + (rows - 1) * GAP;
    return max<int16_t>(0, contentH - cfg::SCREEN_H);
}

void Menu::draw(TFT_eSPI& tft) {
    tft_ = &tft;
    if (!strip_) {
        strip_ = new TFT_eSprite(&tft);
        strip_->setColorDepth(16);
        if (!strip_->createSprite(cfg::SCREEN_W, STRIP_H)) Serial.println("Menu: sprite alloc failed");
    }
    pressed_ = dragging_ = false;
    scroll_ = constrain(scroll_, 0, maxScroll());
    render();
}

int Menu::update(const TouchEvent& ev) {
    if (ev.tapped) {
        pressed_ = true;
        dragging_ = false;
        pressX_ = ev.x;
        pressY_ = ev.y;
        pressScroll_ = scroll_;
    }
    if (!pressed_) return -1;

    if (ev.touching) {
        if (!dragging_ && abs(ev.y - pressY_) >= DRAG_PX && maxScroll() > 0) dragging_ = true;
        if (dragging_) {
            const int16_t s = constrain(pressScroll_ - (ev.y - pressY_), 0, maxScroll());
            if (s != scroll_) {
                scroll_ = s;
                render();
            }
        }
    }

    // Launch on release, so that a drag never starts a game.
    if (ev.released) {
        pressed_ = false;
        if (!dragging_) return tileAt(pressX_, pressY_);
    }
    return -1;
}

// The screen is drawn as horizontal strips through one small sprite so that
// scrolling doesn't flicker. Without the sprite it falls back to drawing
// straight to the screen.
void Menu::render() {
    if (!strip_ || !strip_->created()) {
        tft_->fillScreen(color::BG);
        for (int i = 0; i < count_; i++) drawTile(*tft_, i, -scroll_);
        drawScrollbar(*tft_, 0);
        return;
    }

    for (int16_t y0 = 0; y0 < cfg::SCREEN_H; y0 += STRIP_H) {
        strip_->fillSprite(color::BG);
        for (int i = 0; i < count_; i++) {
            int16_t x, y;
            tileRect(i, x, y);
            const int16_t sy = y - scroll_;
            if (sy >= y0 + STRIP_H || sy + TILE_H <= y0) continue;
            drawTile(*strip_, i, -scroll_ - y0);
        }
        drawScrollbar(*strip_, -y0);
        strip_->pushSprite(0, y0);
    }
}

// oy shifts content coordinates into the target's coordinates.
void Menu::drawTile(TFT_eSPI& g, int i, int16_t oy) {
    int16_t x, y;
    tileRect(i, x, y);
    y += oy;
    g.fillRoundRect(x, y, TILE_W, TILE_H, 10, color::PANEL);
    g.drawRoundRect(x, y, TILE_W, TILE_H, 10, color::ACCENT);
    games_[i]->drawIcon(g, x + 4, y + 4, TILE_W - 8, TILE_H - 26);
    g.setTextDatum(BC_DATUM);
    g.setTextColor(color::TEXT, color::PANEL);
    g.drawString(games_[i]->name(), x + TILE_W / 2, y + TILE_H - 4, 2);
}

void Menu::drawScrollbar(TFT_eSPI& g, int16_t oy) {
    const int16_t range = maxScroll();
    if (range == 0) return;
    const int16_t trackH = cfg::SCREEN_H - 2 * MARGIN;
    const int16_t thumbH = (int32_t)trackH * cfg::SCREEN_H / (cfg::SCREEN_H + range);
    const int16_t thumbY = MARGIN + (int32_t)(trackH - thumbH) * scroll_ / range;
    g.fillRoundRect(BAR_X, MARGIN + oy, BAR_W, trackH, 1, color::PANEL);
    g.fillRoundRect(BAR_X, thumbY + oy, BAR_W, thumbH, 1, color::BORDER);
}
