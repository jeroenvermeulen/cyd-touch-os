#include "Menu.h"

#include <Arduino.h>

#include "Config.h"
#include "UI.h"

namespace {
constexpr int16_t BAR_W = 16;  // scrollbar width; also the arrow boxes' and thumb's size
constexpr int16_t BAR_X = cfg::SCREEN_W - 4 - BAR_W;  // inside the right frame (Menu::FRAME)

const char* const MENU_ITEMS[] = {"File", "Options", "Window", "Help"};

// A raised square button with a small black triangle pointing up or down.
void drawArrowBox(TFT_eSPI& g, int16_t x, int16_t y, int16_t s, bool up) {
    ui::drawPanel(g, x, y, s, s);
    const int16_t cx = x + s / 2, cy = y + s / 2;
    if (up) g.fillTriangle(cx, cy - 3, cx - 4, cy + 2, cx + 4, cy + 2, color::FACE_TEXT);
    else g.fillTriangle(cx, cy + 3, cx - 4, cy - 2, cx + 4, cy - 2, color::FACE_TEXT);
}
}  // namespace

void Menu::iconRect(int i, int16_t& x, int16_t& y) const {
    // Share the width out between the columns, leaving room for the
    // scrollbar only when it's shown.
    const int16_t areaW = maxScroll() > 0 ? BAR_X - CLIENT_X : CLIENT_W;
    const int16_t cellW = areaW / COLS;
    x = CLIENT_X + (i % COLS) * cellW + (cellW - ICON_W) / 2;
    y = CLIENT_Y + MARGIN + (i / COLS) * CELL_H;
}

int Menu::iconAt(int16_t sx, int16_t sy) const {
    if (sy < CLIENT_Y || sy >= CLIENT_Y + CLIENT_H) return -1;
    const int16_t cy = sy + scroll_;
    for (int i = 0; i < count_; i++) {
        int16_t x, y;
        iconRect(i, x, y);
        if (sx >= x && sx < x + ICON_W && cy >= y && cy < y + ICON_H + LABEL_GAP + LABEL_H) return i;
    }
    return -1;
}

int16_t Menu::maxScroll() const {
    const int rows = (count_ + COLS - 1) / COLS;
    const int16_t contentH = 2 * MARGIN + rows * CELL_H - (CELL_H - ICON_H - LABEL_GAP - LABEL_H);
    return max<int16_t>(0, contentH - CLIENT_H);
}

void Menu::scrollBy(int16_t dy) {
    const int16_t s = constrain(scroll_ + dy, 0, maxScroll());
    if (s == scroll_) return;
    scroll_ = s;
    render();
}

void Menu::draw(TFT_eSPI& tft) {
    tft_ = &tft;
    if (!strip_) {
        strip_ = new TFT_eSprite(&tft);
        strip_->setColorDepth(16);
        if (!strip_->createSprite(cfg::SCREEN_W, STRIP_H)) Serial.println("Menu: sprite alloc failed");
    }
    if (!iconVersions_) iconVersions_ = new uint32_t[count_];
    for (int i = 0; i < count_; i++) iconVersions_[i] = apps_[i]->iconVersion();
    pressed_ = dragging_ = false;
    selected_ = -1;
    scroll_ = constrain(scroll_, 0, maxScroll());
    render();
}

int Menu::update(const TouchEvent& ev) {
    for (int i = 0; i < count_; i++) {
        const uint32_t v = apps_[i]->iconVersion();
        if (v == iconVersions_[i]) continue;
        iconVersions_[i] = v;
        refreshIcon(i);
    }

    if (ev.tapped) {
        // The scrollbar arrows scroll by one row of icons.
        if (maxScroll() > 0 && ev.x >= BAR_X && ev.x < BAR_X + BAR_W) {
            if (ev.y >= CLIENT_Y && ev.y < CLIENT_Y + BAR_W) {
                scrollBy(-CELL_H);
                return -1;
            }
            if (ev.y >= CLIENT_Y + CLIENT_H - BAR_W && ev.y < CLIENT_Y + CLIENT_H) {
                scrollBy(CELL_H);
                return -1;
            }
        }
        pressed_ = true;
        dragging_ = false;
        pressX_ = ev.x;
        pressY_ = ev.y;
        pressScroll_ = scroll_;
        selected_ = iconAt(ev.x, ev.y);
        if (selected_ >= 0) render();
    }
    if (!pressed_) return -1;

    if (ev.touching) {
        if (!dragging_ && abs(ev.y - pressY_) >= DRAG_PX && maxScroll() > 0) {
            dragging_ = true;
            selected_ = -1;
            render();
        }
        if (dragging_) scrollBy(pressScroll_ - (ev.y - pressY_) - scroll_);
    }

    // Launch on release, so that a drag never starts an app.
    if (ev.released) {
        pressed_ = false;
        selected_ = -1;
        if (!dragging_) return iconAt(pressX_, pressY_);
    }
    return -1;
}

// The screen is drawn as horizontal strips through one small sprite so that
// scrolling doesn't flicker. Without the sprite it falls back to drawing
// straight to the screen (always the whole screen). The chrome goes on last, so it covers icons
// scrolled up under the menu bar.
void Menu::renderRows(int16_t top, int16_t bottom) {
    if (!strip_ || !strip_->created()) {
        tft_->fillRect(CLIENT_X, CLIENT_Y, CLIENT_W, CLIENT_H, color::ICON_BG);
        for (int i = 0; i < count_; i++) drawIcon(*tft_, i, -scroll_);
        drawScrollbar(*tft_, 0);
        drawChrome(*tft_, 0);
        return;
    }

    for (int16_t y0 = top / STRIP_H * STRIP_H; y0 < bottom; y0 += STRIP_H) {
        strip_->fillSprite(color::ICON_BG);
        for (int i = 0; i < count_; i++) {
            int16_t x, y;
            iconRect(i, x, y);
            const int16_t sy = y - scroll_;
            if (sy >= y0 + STRIP_H || sy + ICON_H + LABEL_GAP + LABEL_H <= y0) continue;
            drawIcon(*strip_, i, -scroll_ - y0);
        }
        drawScrollbar(*strip_, -y0);
        drawChrome(*strip_, -y0);
        strip_->pushSprite(0, y0);
    }
}

// Redraws only the strips under icon i's picture, if it's scrolled into view.
void Menu::refreshIcon(int i) {
    int16_t x, y;
    iconRect(i, x, y);
    const int16_t top = max<int16_t>(y - scroll_, CLIENT_Y);
    const int16_t bottom = min<int16_t>(y - scroll_ + ICON_H, CLIENT_Y + CLIENT_H);
    if (top < bottom) renderRows(top, bottom);
}

// oy shifts screen coordinates into the target's coordinates.
// The picture straight on the white window with the name underneath; the
// one under the finger gets its name highlighted in navy, as in Program Manager.
void Menu::drawIcon(TFT_eSPI& g, int i, int16_t oy) {
    int16_t x, y;
    iconRect(i, x, y);
    y += oy;
    const bool sel = i == selected_;

    apps_[i]->drawIcon(g, x, y, ICON_W, ICON_H);

    const char* name = apps_[i]->name();
    const int16_t cx = x + ICON_W / 2;
    const int16_t ly = y + ICON_H + LABEL_GAP;
    if (sel) {
        const int16_t tw = g.textWidth(name, 2);
        g.fillRect(cx - tw / 2 - 2, ly, tw + 4, LABEL_H, color::TITLE);
    }
    g.setTextDatum(TC_DATUM);
    g.setTextColor(sel ? color::TITLE_TEXT : color::FACE_TEXT);
    g.drawString(name, cx, ly, 2);
}

// Windows 3.1 style: arrow boxes at both ends and a square thumb.
void Menu::drawScrollbar(TFT_eSPI& g, int16_t oy) {
    const int16_t range = maxScroll();
    if (range == 0) return;
    const int16_t top = CLIENT_Y + oy;
    const int16_t trackY = top + BAR_W;
    const int16_t trackH = CLIENT_H - 2 * BAR_W;
    g.fillRect(BAR_X, top, BAR_W, CLIENT_H, color::EDGE_SOFT);
    g.drawFastVLine(BAR_X, top, CLIENT_H, color::EDGE_DARK);
    drawArrowBox(g, BAR_X, top, BAR_W, true);
    drawArrowBox(g, BAR_X, top + CLIENT_H - BAR_W, BAR_W, false);
    ui::drawPanel(g, BAR_X, trackY + (int32_t)(trackH - BAR_W) * scroll_ / range, BAR_W, BAR_W);
}

// Frame, title bar and menu bar. Only the scrollbar arrows respond to touch;
// the window buttons and menus are there for the look.
void Menu::drawChrome(TFT_eSPI& g, int16_t oy) {
    constexpr int16_t W = cfg::SCREEN_W, H = cfg::SCREEN_H;

    // Thick grey sizing border between two black lines.
    g.drawRect(0, oy, W, H, color::EDGE_DARK);
    for (int16_t i = 1; i < FRAME - 1; i++) g.drawRect(i, i + oy, W - 2 * i, H - 2 * i, color::FACE);
    g.drawRect(FRAME - 1, FRAME - 1 + oy, W - 2 * (FRAME - 1), H - 2 * (FRAME - 1), color::EDGE_DARK);

    // Title bar: control-menu box, centred title, minimize and maximize.
    const int16_t ty = FRAME + oy;
    const int16_t inner = W - 2 * FRAME;
    g.fillRect(FRAME, ty, inner, BAR_H, color::TITLE);
    g.fillRect(FRAME, ty, BAR_H, BAR_H, color::FACE);
    g.fillRect(FRAME + 4, ty + 7, BAR_H - 7, 3, color::EDGE_SHADOW);  // the bar's shadow
    g.fillRect(FRAME + 3, ty + 6, BAR_H - 7, 3, color::EDGE_LIGHT);
    g.drawRect(FRAME + 3, ty + 6, BAR_H - 7, 3, color::EDGE_DARK);
    g.drawFastVLine(FRAME + BAR_H, ty, BAR_H, color::EDGE_DARK);
    const int16_t bx = W - FRAME - 2 * BAR_H;
    g.drawFastVLine(bx - 1, ty, BAR_H, color::EDGE_DARK);
    drawArrowBox(g, bx, ty, BAR_H, false);
    drawArrowBox(g, bx + BAR_H, ty, BAR_H, true);
    g.setTextDatum(MC_DATUM);
    g.setTextColor(color::TITLE_TEXT);
    // Drawn twice, one pixel apart, for a bold look.
    g.drawString("Program Manager", W / 2, ty + BAR_H / 2, 2);
    g.drawString("Program Manager", W / 2 + 1, ty + BAR_H / 2, 2);
    g.drawFastHLine(FRAME, ty + BAR_H, inner, color::EDGE_DARK);

    // Menu bar, with the first letter of each item underlined.
    const int16_t my = ty + BAR_H + 1;
    g.fillRect(FRAME, my, inner, BAR_H, color::EDGE_LIGHT);
    g.setTextDatum(ML_DATUM);
    g.setTextColor(color::FACE_TEXT);
    int16_t mx = FRAME + 8;
    for (const char* item : MENU_ITEMS) {
        g.drawString(item, mx, my + BAR_H / 2, 2);
        const char first[2] = {item[0], 0};
        g.drawFastHLine(mx, my + BAR_H - 3, g.textWidth(first, 2) - 1, color::FACE_TEXT);
        mx += g.textWidth(item, 2) + 14;
    }
    g.drawFastHLine(FRAME, my + BAR_H, inner, color::EDGE_DARK);
}
