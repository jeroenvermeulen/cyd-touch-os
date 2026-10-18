#pragma once

#include <TFT_eSPI.h>

#include "Config.h"

struct Button {
    int16_t x, y, w, h;
    const char* label;
    uint16_t fill;
    uint16_t textColor;
    uint8_t font;

    Button(int16_t x, int16_t y, int16_t w, int16_t h, const char* label,
           uint16_t fill = color::PANEL, uint16_t textColor = color::TEXT, uint8_t font = 2)
        : x(x), y(y), w(w), h(h), label(label), fill(fill), textColor(textColor), font(font) {}

    void draw(TFT_eSPI& tft) const;
    bool contains(int16_t px, int16_t py) const;
};

namespace ui {
// Clear the screen and draw a centred title bar.
void drawHeader(TFT_eSPI& tft, const char* title);
}
