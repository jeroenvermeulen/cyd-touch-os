#include "UI.h"

namespace {
// One-pixel frame: tl on the top and left edges, br on the bottom and right.
void drawEdge(TFT_eSPI& g, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t tl, uint16_t br) {
    g.drawFastHLine(x, y, w - 1, tl);
    g.drawFastVLine(x, y, h - 1, tl);
    g.drawFastHLine(x, y + h - 1, w, br);
    g.drawFastVLine(x + w - 1, y, h, br);
}
}  // namespace

void Button::draw(TFT_eSPI& tft, bool pressed, bool enabled) const {
    ui::drawPanel(tft, x, y, w, h, pressed ? ui::Bevel::Sunken : ui::Bevel::Raised);
    const int16_t cx = x + w / 2 + (pressed ? 1 : 0);
    const int16_t cy = y + h / 2 + (pressed ? 1 : 0);
    tft.setTextDatum(MC_DATUM);
    if (enabled) {
        tft.setTextColor(color::FACE_TEXT, color::FACE);
        tft.drawString(label, cx, cy, font);
        return;
    }
    // Greyed out: a white copy one pixel down-right makes the text look etched.
    tft.setTextColor(color::EDGE_LIGHT);
    tft.drawString(label, cx + 1, cy + 1, font);
    tft.setTextColor(color::EDGE_SHADOW);
    tft.drawString(label, cx, cy, font);
}

bool Button::contains(int16_t px, int16_t py) const {
    return px >= x && px < x + w && py >= y && py < y + h;
}

namespace ui {
void drawBevel(TFT_eSPI& g, int16_t x, int16_t y, int16_t w, int16_t h, Bevel style) {
    if (style == Bevel::Raised) {
        drawEdge(g, x, y, w, h, color::EDGE_LIGHT, color::EDGE_DARK);
        drawEdge(g, x + 1, y + 1, w - 2, h - 2, color::EDGE_SOFT, color::EDGE_SHADOW);
    } else {
        drawEdge(g, x, y, w, h, color::EDGE_SHADOW, color::EDGE_LIGHT);
        drawEdge(g, x + 1, y + 1, w - 2, h - 2, color::EDGE_DARK, color::EDGE_SOFT);
    }
}

void drawPanel(TFT_eSPI& g, int16_t x, int16_t y, int16_t w, int16_t h, Bevel style, uint16_t fill) {
    g.fillRect(x + BEVEL, y + BEVEL, w - 2 * BEVEL, h - 2 * BEVEL, fill);
    drawBevel(g, x, y, w, h, style);
}

void drawTitleBar(TFT_eSPI& g, int16_t x, int16_t y, int16_t w, const char* title) {
    g.fillRect(x, y, w, TITLE_H, color::TITLE);
    g.setTextDatum(ML_DATUM);
    g.setTextColor(color::TITLE_TEXT);
    // Drawn twice, one pixel apart, for a bold look.
    g.drawString(title, x + 4, y + TITLE_H / 2, 2);
    g.drawString(title, x + 5, y + TITLE_H / 2, 2);
}

void drawWindow(TFT_eSPI& g, int16_t x, int16_t y, int16_t w, int16_t h, const char* title) {
    drawPanel(g, x, y, w, h);
    drawTitleBar(g, x + BEVEL + 1, y + BEVEL + 1, w - 2 * BEVEL - 2, title);
    g.setTextDatum(TC_DATUM);
    g.setTextColor(color::FACE_TEXT, color::FACE);
}

void drawDialog(TFT_eSPI& tft, const char* title) { drawWindow(tft, DLG_X, DLG_Y, DLG_W, DLG_H, title); }

void drawHeader(TFT_eSPI& tft, const char* title) {
    drawWindow(tft, 0, 0, cfg::SCREEN_W, cfg::SCREEN_H, title);
}

void drawPauseButton(TFT_eSPI& g, int16_t x, int16_t y) {
    drawPanel(g, x, y, PAUSE_W, PAUSE_H);
    const int16_t cx = x + PAUSE_W / 2;
    const int16_t top = y + (PAUSE_H - 8) / 2;
    g.fillRect(cx - 4, top, 3, 8, color::FACE_TEXT);
    g.fillRect(cx + 1, top, 3, 8, color::FACE_TEXT);
}
}  // namespace ui
