#pragma once

#include "App.h"

// Windows 3.1's "Flying Windows": Windows logos fly towards you out of the
// dark. main.cpp starts it after cfg::SCREENSAVER_MS without a touch on the
// menu or in a game (see App::suspend()). A tap ends it.
class Screensaver : public App {
public:
    const char* name() const override { return "Screen Saver"; }
    void begin(TFT_eSPI& tft) override;
    void update(const TouchEvent& ev) override;
    bool finished() const override { return exit_; }

private:
    static constexpr int COUNT = 24;

    struct Logo {
        float x, y, z;               // world position; z shrinks as it flies closer
        int16_t sx, sy, size;        // where it is on screen, size 0 = not drawn
    };

    void spawn(Logo& l, float z);
    void frame();

    TFT_eSPI* tft_ = nullptr;
    bool exit_ = false;
    uint32_t nextFrameMs_ = 0;
    Logo logos_[COUNT];
};
