#pragma once

#include <stdint.h>

// A single tap: reported once when the finger goes down.
struct TouchEvent {
    bool tapped = false;
    int16_t x = 0;
    int16_t y = 0;
};

class Input {
public:
    void begin();
    TouchEvent poll();

private:
    bool down_ = false;
    uint32_t lastContactMs_ = 0;
};
