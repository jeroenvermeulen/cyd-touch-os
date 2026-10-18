#include "Input.h"

#include <Arduino.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>

#include "Config.h"

namespace {
SPIClass touchSpi(VSPI);
XPT2046_Touchscreen touch(cfg::TOUCH_CS, cfg::TOUCH_IRQ);

// The finger must be off the screen this long before a new tap counts,
// which filters out the flicker the resistive panel produces.
constexpr uint32_t RELEASE_MS = 80;
}  // namespace

void Input::begin() {
    touchSpi.begin(cfg::TOUCH_SCLK, cfg::TOUCH_MISO, cfg::TOUCH_MOSI, cfg::TOUCH_CS);
    touch.begin(touchSpi);
    touch.setRotation(1);
}

TouchEvent Input::poll() {
    TouchEvent ev;
    uint32_t now = millis();

    if (touch.tirqTouched() && touch.touched()) {
        lastContactMs_ = now;
        TS_Point p = touch.getPoint();
        ev.touching = true;
        ev.x = constrain(map(p.x, cfg::TOUCH_X_MIN, cfg::TOUCH_X_MAX, 0, cfg::SCREEN_W - 1), 0,
                         cfg::SCREEN_W - 1);
        ev.y = constrain(map(p.y, cfg::TOUCH_Y_MIN, cfg::TOUCH_Y_MAX, 0, cfg::SCREEN_H - 1), 0,
                         cfg::SCREEN_H - 1);
        if (!down_) {
            down_ = true;
            ev.tapped = true;
            Serial.printf("touch raw=(%d,%d) z=%d -> screen=(%d,%d)\n", p.x, p.y, p.z, ev.x, ev.y);
        }
    } else if (down_ && now - lastContactMs_ > RELEASE_MS) {
        down_ = false;
    }
    return ev;
}
