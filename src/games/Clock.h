#pragma once

#include <time.h>

#include "Game.h"
#include "UI.h"

// Analog + digital clock. The time comes from NTP (see net::timeValid()).
class Clock : public Game {
public:
    const char* name() const override { return "Clock"; }
    void drawIcon(TFT_eSPI& tft, int16_t x, int16_t y, int16_t w, int16_t h) override;
    uint32_t iconVersion() const override;  // changes every minute
    void begin(TFT_eSPI& tft) override;
    void update(const TouchEvent& ev) override;
    bool finished() const override { return exit_; }

private:
    // Hand angles in degrees clockwise from 12; negative = not drawn.
    struct Hands {
        float hour = -1, minute = -1, second = -1;
    };

    void drawFace();
    void drawNumerals();
    void drawHands(const Hands& h);
    void eraseChangedHands(const Hands& now);
    void drawDigital(const struct tm& t);
    void drawDate(const struct tm& t);
    int statusCode() const;  // which "no time yet" message applies
    void drawStatus();
    void tick();
    static Hands handsFor(const struct tm& t);

    TFT_eSPI* tft_ = nullptr;
    bool exit_ = false;
    bool valid_ = false;  // time was valid when last drawn
    time_t lastSec_ = 0;
    int lastDay_ = -1;     // tm_yday of the date on screen
    int lastStatus_ = -1;  // statusCode() on screen, -1 = none
    Hands hands_;          // hands currently on screen
};
