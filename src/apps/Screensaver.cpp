#include "apps/Screensaver.h"

#include <Arduino.h>

#include "Config.h"

namespace {
constexpr float Z_FAR = 1000, Z_NEAR = 40;
constexpr long SPREAD = 700;      // world x/y range, +-
constexpr float FOCAL = 100;      // screen px per world unit at z = FOCAL
constexpr float LOGO = 40;        // world size of a logo
constexpr float SPEED = 7;        // z units per frame
constexpr uint32_t FRAME_MS = 25;
constexpr int16_t CX = cfg::SCREEN_W / 2, CY = cfg::SCREEN_H / 2;

constexpr uint16_t SKY = 0x0000;
constexpr uint16_t RED = 0xF800, GREEN = 0x07E0, BLUE = 0x001F, YELLOW = 0xFFE0;

// The four-pane flag, with black gaps once it's big enough to show them.
// Every pixel of the s*s box is painted, so it never needs erasing first.
void drawLogo(TFT_eSPI& g, int16_t x, int16_t y, int16_t s, uint16_t bg) {
    const int16_t gap = s >= 8 ? max(1, s / 10) : 0;
    const int16_t a = (s - gap) / 2, b = s - gap - a;
    g.fillRect(x, y, a, a, RED);
    g.fillRect(x + a + gap, y, b, a, GREEN);
    g.fillRect(x, y + a + gap, a, b, BLUE);
    g.fillRect(x + a + gap, y + a + gap, b, b, YELLOW);
    if (gap) {
        g.fillRect(x + a, y, gap, s, bg);
        g.fillRect(x, y + a, s, gap, bg);
    }
}

// Clears the part of box (x, y, s) that isn't covered by box (nx, ny, ns).
void eraseOutside(TFT_eSPI& g, int16_t x, int16_t y, int16_t s, int16_t nx, int16_t ny, int16_t ns) {
    const int16_t x1 = x + s, y1 = y + s;
    const int16_t ix0 = max(x, nx), iy0 = max(y, ny);
    const int16_t ix1 = min<int16_t>(x1, nx + ns), iy1 = min<int16_t>(y1, ny + ns);
    if (ns == 0 || ix0 >= ix1 || iy0 >= iy1) {
        g.fillRect(x, y, s, s, SKY);
        return;
    }
    g.fillRect(x, y, s, iy0 - y, SKY);           // above
    g.fillRect(x, iy1, s, y1 - iy1, SKY);        // below
    g.fillRect(x, iy0, ix0 - x, iy1 - iy0, SKY);  // left
    g.fillRect(ix1, iy0, x1 - ix1, iy1 - iy0, SKY);  // right
}
}  // namespace

void Screensaver::spawn(Logo& l, float z) {
    l.x = random(-SPREAD, SPREAD);
    l.y = random(-SPREAD, SPREAD);
    l.z = z;
}

void Screensaver::begin(TFT_eSPI& tft) {
    tft_ = &tft;
    exit_ = false;
    tft.fillScreen(SKY);
    for (int i = 0; i < COUNT; i++) {
        spawn(logos_[i], Z_NEAR + (Z_FAR - Z_NEAR) * (i + 1) / COUNT);
        logos_[i].size = 0;
    }
    nextFrameMs_ = millis();
}

void Screensaver::update(const TouchEvent& ev) {
    if (ev.tapped) {
        exit_ = true;
        return;
    }
    if ((int32_t)(millis() - nextFrameMs_) < 0) return;
    nextFrameMs_ = millis() + FRAME_MS;
    frame();
}

void Screensaver::frame() {
    TFT_eSPI& g = *tft_;

    // Farthest first, so nearer logos are drawn over the ones behind them.
    for (int i = 1; i < COUNT; i++) {
        const Logo l = logos_[i];
        int j = i;
        for (; j > 0 && logos_[j - 1].z < l.z; j--) logos_[j] = logos_[j - 1];
        logos_[j] = l;
    }

    g.startWrite();
    for (Logo& l : logos_) {
        l.z -= SPEED;
        int16_t s = 0, sx = 0, sy = 0;
        if (l.z >= Z_NEAR) {
            const float k = FOCAL / l.z;
            s = max<int16_t>(2, LOGO * k);
            sx = CX + l.x * k - s / 2;
            sy = CY + l.y * k - s / 2;
            if (sx >= cfg::SCREEN_W || sy >= cfg::SCREEN_H || sx + s <= 0 || sy + s <= 0) s = 0;
        }
        if (l.size) eraseOutside(g, l.sx, l.sy, l.size, sx, sy, s);
        if (s) {
            drawLogo(g, sx, sy, s, SKY);
        } else {
            spawn(l, Z_FAR);  // flew past or off the edge; drawn from the next frame
        }
        l.sx = sx;
        l.sy = sy;
        l.size = s;
    }
    g.endWrite();
}
