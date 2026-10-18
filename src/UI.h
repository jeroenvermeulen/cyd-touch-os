#pragma once

#include <TFT_eSPI.h>

#include "Config.h"

// A grey bevelled push button in the Windows 95 style.
struct Button {
    int16_t x, y, w, h;
    const char* label;
    uint8_t font;

    Button(int16_t x, int16_t y, int16_t w, int16_t h, const char* label, uint8_t font = 2)
        : x(x), y(y), w(w), h(h), label(label), font(font) {}

    // pressed draws it pushed in; a disabled button gets embossed grey text.
    void draw(TFT_eSPI& tft, bool pressed = false, bool enabled = true) const;
    bool contains(int16_t px, int16_t py) const;
};

namespace ui {
enum class Bevel : uint8_t { Raised, Sunken };

constexpr int16_t BEVEL = 2;     // width of a 3D edge
constexpr int16_t TITLE_H = 18;  // title bar height
constexpr int16_t PAUSE_W = 22;
constexpr int16_t PAUSE_H = 16;

// Overlay dialog used by the games' pause and game-over screens.
constexpr int16_t DLG_X = 60, DLG_Y = 45, DLG_W = 200, DLG_H = 145;

// 3D edge just inside (x, y, w, h); the face is left alone.
void drawBevel(TFT_eSPI& g, int16_t x, int16_t y, int16_t w, int16_t h, Bevel style = Bevel::Raised);

// Filled rectangle with a 3D edge.
void drawPanel(TFT_eSPI& g, int16_t x, int16_t y, int16_t w, int16_t h, Bevel style = Bevel::Raised,
               uint16_t fill = color::FACE);

// Navy bar with a bold white title on the left, TITLE_H high.
void drawTitleBar(TFT_eSPI& g, int16_t x, int16_t y, int16_t w, const char* title);

// Raised window with a title bar. Leaves TC_DATUM and black-on-FACE text
// set, ready for the body text.
void drawWindow(TFT_eSPI& g, int16_t x, int16_t y, int16_t w, int16_t h, const char* title);

// The standard pause/game-over dialog (DLG_* above).
void drawDialog(TFT_eSPI& tft, const char* title);

// Fill the screen with one window; content starts below y = contentTop().
void drawHeader(TFT_eSPI& tft, const char* title);
constexpr int16_t contentTop() { return BEVEL + 1 + TITLE_H; }

// Small pause button, PAUSE_W x PAUSE_H.
void drawPauseButton(TFT_eSPI& g, int16_t x, int16_t y);
}  // namespace ui
