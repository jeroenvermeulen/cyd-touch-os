#pragma once

#include <stdint.h>

// One poll of the touch screen.
struct TouchEvent {
    bool tapped = false;    // finger just went down (reported once per touch)
    bool touching = false;  // finger is on the screen; x/y hold its current position
    bool released = false;  // finger was lifted (reported once per touch, after the debounce)
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
