#include "UI.h"

void Button::draw(TFT_eSPI& tft) const {
    tft.fillRoundRect(x, y, w, h, 8, fill);
    tft.drawRoundRect(x, y, w, h, 8, color::BORDER);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(textColor, fill);
    tft.drawString(label, x + w / 2, y + h / 2, font);
}

bool Button::contains(int16_t px, int16_t py) const {
    return px >= x && px < x + w && py >= y && py < y + h;
}

namespace ui {
void drawHeader(TFT_eSPI& tft, const char* title) {
    tft.fillScreen(color::BG);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(color::ACCENT, color::BG);
    tft.drawString(title, cfg::SCREEN_W / 2, 10, 4);
    tft.drawFastHLine(20, 40, cfg::SCREEN_W - 40, color::BORDER);
}
}  // namespace ui
