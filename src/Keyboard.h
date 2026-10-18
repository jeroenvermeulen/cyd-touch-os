#pragma once

#include <TFT_eSPI.h>

#include "Input.h"

// Full-screen on-screen keyboard for entering a line of text (passwords,
// names). Draws its own window: a text field at the top and a QWERTY layout
// with a digits row, a symbols page (?123), caps lock (Shift) and a backspace
// that repeats while held.
class Keyboard {
public:
    enum class Result : uint8_t { None, Done, Cancelled };

    // Edits buf (cap bytes including the terminator) in place, starting from
    // whatever it holds. OK stays greyed out until the text is minLen long.
    void begin(TFT_eSPI& tft, const char* title, char* buf, size_t cap, size_t minLen = 0);
    Result update(const TouchEvent& ev);

private:
    struct Key {
        int16_t x, y, w;
        char code;  // the character typed, or one of the special codes in Keyboard.cpp
    };
    static constexpr int MAX_KEYS = 48;

    void layout();  // fills keys_ for the current page and shift state
    void addRow(int row, const char* chars, int16_t left, int16_t right);
    void addKey(int row, int16_t x, int16_t w, char code);
    void drawKeys();
    void drawKey(int i, bool pressed = false);
    void drawField();
    int keyAt(int16_t x, int16_t y) const;
    Result press(char code);
    void erase();
    void textChanged();
    bool okEnabled() const { return len_ >= minLen_; }

    TFT_eSPI* tft_ = nullptr;
    char* buf_ = nullptr;
    size_t cap_ = 0;
    size_t len_ = 0;
    size_t minLen_ = 0;

    Key keys_[MAX_KEYS];
    int count_ = 0;
    int okKey_ = -1;
    bool shift_ = false;
    bool symbols_ = false;
    bool okShown_ = false;  // OK's enabled state as last drawn
    int pressed_ = -1;      // key under the finger
    uint32_t repeatAt_ = 0;
};
