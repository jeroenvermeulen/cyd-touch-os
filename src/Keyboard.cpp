#include "Keyboard.h"

#include <Arduino.h>

#include "Config.h"
#include "UI.h"

namespace {
// Special keys, as Key::code values below the printable range.
constexpr char K_SHIFT = 1;
constexpr char K_BKSP = 2;
constexpr char K_MODE = 3;
constexpr char K_CANCEL = 4;
constexpr char K_OK = 5;

constexpr int16_t FIELD_X = 6, FIELD_Y = ui::contentTop() + 3, FIELD_W = 308, FIELD_H = 26;

// Ten columns of UNIT px; keys are one pixel narrower so a sliver of the
// face shows between them.
constexpr int16_t KB_X = 5;
constexpr int16_t KB_W = 310;
constexpr int16_t KB_Y = FIELD_Y + FIELD_H + 5;
constexpr int16_t UNIT = 31;
constexpr int16_t WIDE = UNIT * 3 / 2;  // Shift, Backspace and the page switch
constexpr int16_t ROW_PITCH = 36;
constexpr int16_t KEY_H = ROW_PITCH - 2;

constexpr uint32_t REPEAT_DELAY_MS = 500;
constexpr uint32_t REPEAT_MS = 80;

const char* const LETTERS[4] = {"1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm"};
const char* const SYMBOLS[4] = {"!@#$%^&*()", "-_=+[]{}\\|", ";:'\",.<>/", "?`~"};
}  // namespace

void Keyboard::begin(TFT_eSPI& tft, const char* title, char* buf, size_t cap, size_t minLen) {
    tft_ = &tft;
    buf_ = buf;
    cap_ = cap;
    len_ = strlen(buf);
    minLen_ = minLen;
    shift_ = symbols_ = false;

    ui::drawHeader(tft, title);
    drawField();
    layout();
    drawKeys();
}

Keyboard::Result Keyboard::update(const TouchEvent& ev) {
    if (ev.tapped) {
        const int k = keyAt(ev.x, ev.y);
        if (k >= 0) {
            pressed_ = k;
            drawKey(k, true);
            repeatAt_ = millis() + REPEAT_DELAY_MS;
            return press(keys_[k].code);
        }
    }
    if (pressed_ < 0) return Result::None;

    if (ev.released) {
        const int k = pressed_;
        pressed_ = -1;
        drawKey(k);
    } else if (ev.touching && keys_[pressed_].code == K_BKSP && (int32_t)(millis() - repeatAt_) >= 0) {
        repeatAt_ += REPEAT_MS;
        erase();
    }
    return Result::None;
}

Keyboard::Result Keyboard::press(char code) {
    switch (code) {
        case K_SHIFT:
        case K_MODE:
            if (code == K_MODE) symbols_ = !symbols_;
            else if (!symbols_) shift_ = !shift_;
            else return Result::None;  // Shift does nothing on the symbols page
            pressed_ = -1;  // the whole keyboard is redrawn
            layout();
            drawKeys();
            return Result::None;
        case K_BKSP:
            erase();
            return Result::None;
        case K_CANCEL:
            return Result::Cancelled;
        case K_OK:
            return okEnabled() ? Result::Done : Result::None;
        default:
            if (len_ + 1 >= cap_) return Result::None;
            buf_[len_++] = code;
            buf_[len_] = 0;
            textChanged();
            return Result::None;
    }
}

void Keyboard::erase() {
    if (len_ == 0) return;
    buf_[--len_] = 0;
    textChanged();
}

void Keyboard::textChanged() {
    drawField();
    if (okEnabled() != okShown_) drawKey(okKey_, pressed_ == okKey_);
}

// ---------------------------------------------------------------------------
// Layout

void Keyboard::layout() {
    count_ = 0;
    const char* const* rows = symbols_ ? SYMBOLS : LETTERS;
    addRow(0, rows[0], KB_X, KB_X + KB_W);
    addRow(1, rows[1], KB_X, KB_X + KB_W);
    addRow(2, rows[2], KB_X, KB_X + KB_W);

    addKey(3, KB_X, WIDE, K_SHIFT);
    addRow(3, rows[3], KB_X + WIDE, KB_X + KB_W - WIDE);
    addKey(3, KB_X + KB_W - WIDE, WIDE, K_BKSP);

    // Bottom row: page switch, space bar, Cancel, OK.
    constexpr int16_t BTN_W = 2 * UNIT;
    constexpr int16_t SPACE_W = KB_W - WIDE - 2 * BTN_W;
    addKey(4, KB_X, WIDE, K_MODE);
    addKey(4, KB_X + WIDE, SPACE_W, ' ');
    addKey(4, KB_X + WIDE + SPACE_W, BTN_W, K_CANCEL);
    okKey_ = count_;
    addKey(4, KB_X + KB_W - BTN_W, BTN_W, K_OK);
}

// One key per character, centred between left and right.
void Keyboard::addRow(int row, const char* chars, int16_t left, int16_t right) {
    const int n = strlen(chars);
    int16_t x = left + (right - left - n * UNIT) / 2;
    for (int i = 0; i < n; i++, x += UNIT) {
        const char c = chars[i];
        addKey(row, x, UNIT, shift_ && c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
    }
}

void Keyboard::addKey(int row, int16_t x, int16_t w, char code) {
    if (count_ < MAX_KEYS) keys_[count_++] = {x, int16_t(KB_Y + row * ROW_PITCH), w, code};
}

int Keyboard::keyAt(int16_t x, int16_t y) const {
    for (int i = 0; i < count_; i++) {
        const Key& k = keys_[i];
        if (x >= k.x && x < k.x + k.w && y >= k.y && y < k.y + ROW_PITCH) return i;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Drawing

void Keyboard::drawKeys() {
    tft_->fillRect(KB_X, KB_Y, KB_W, 5 * ROW_PITCH, color::FACE);
    for (int i = 0; i < count_; i++) drawKey(i);
}

void Keyboard::drawKey(int i, bool pressed) {
    if (i < 0) return;
    const Key& k = keys_[i];
    char ch[2] = {k.code, 0};
    const char* label = ch;
    bool enabled = true;
    switch (k.code) {
        case K_SHIFT:
            label = "Shift";
            enabled = !symbols_;
            pressed |= shift_;  // stays down while caps lock is on
            break;
        case K_BKSP: label = "Bksp"; break;
        case K_MODE: label = symbols_ ? "abc" : "?123"; break;
        case K_CANCEL: label = "Cancel"; break;
        case K_OK:
            label = "OK";
            enabled = okShown_ = okEnabled();
            break;
        case ' ': label = ""; break;
    }
    Button(k.x, k.y, k.w - 1, KEY_H, label).draw(*tft_, pressed, enabled);
}

// White sunken field with the end of the text and a caret; text too long to
// fit scrolls off to the left.
void Keyboard::drawField() {
    ui::drawPanel(*tft_, FIELD_X, FIELD_Y, FIELD_W, FIELD_H, ui::Bevel::Sunken, color::ICON_BG);
    const int16_t maxW = FIELD_W - 16;
    const char* s = buf_;
    while (*s && tft_->textWidth(s, 2) > maxW) s++;

    const int16_t tx = FIELD_X + 6;
    const int16_t cy = FIELD_Y + FIELD_H / 2;
    tft_->setTextDatum(ML_DATUM);
    tft_->setTextColor(color::FACE_TEXT, color::ICON_BG);
    tft_->drawString(s, tx, cy, 2);
    tft_->drawFastVLine(tx + tft_->textWidth(s, 2) + 1, FIELD_Y + 5, FIELD_H - 10, color::FACE_TEXT);
}
