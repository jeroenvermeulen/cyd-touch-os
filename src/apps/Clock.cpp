#include "apps/Clock.h"

#include <Arduino.h>

#include "Config.h"
#include "Net.h"

namespace {
// Analog face in a sunken well on the left, digital time and date on the right.
constexpr int16_t WELL_X = 6, WELL_Y = ui::contentTop() + 4;
constexpr int16_t WELL_W = 204, WELL_H = cfg::SCREEN_H - WELL_Y - 6;
constexpr int16_t CX = WELL_X + WELL_W / 2;
constexpr int16_t CY = WELL_Y + WELL_H / 2;
constexpr int16_t FACE_R = 96;
constexpr int16_t TICK_OUT = 88;
constexpr int16_t HOUR_TICK_IN = 79;
constexpr int16_t MIN_TICK_IN = 84;
constexpr int16_t NUMERAL_R = 66;

// Hands stay inside the tick ring, so erasing them never touches the ticks.
constexpr float HOUR_LEN = 46, MINUTE_LEN = 70, SECOND_LEN = 76, SECOND_TAIL = 16;

constexpr int16_t SIDE_X = WELL_X + WELL_W + 6;
constexpr int16_t SIDE_W = cfg::SCREEN_W - SIDE_X - 6;
constexpr int16_t SIDE_CX = SIDE_X + SIDE_W / 2;
constexpr int16_t TIME_Y = WELL_Y + 4, TIME_H = 36;
constexpr int16_t DATE_Y = TIME_Y + TIME_H + 10;

constexpr uint16_t FACE_COLOR = color::ICON_BG;  // white dial
constexpr uint16_t RIM = color::TITLE;
constexpr uint16_t HAND = color::FACE_TEXT;
constexpr uint16_t SECOND_HAND = 0xD000;  // dark red

const Button BTN_BACK{SIDE_X, cfg::SCREEN_H - 6 - 34, SIDE_W, 34, "< Back"};

// Point at distance r from (cx, cy) in direction deg (clockwise from 12).
void polar(float cx, float cy, float deg, float r, float& x, float& y) {
    const float a = deg * DEG_TO_RAD;
    x = cx + sinf(a) * r;
    y = cy - cosf(a) * r;
}

// The dial on its own, scaled to radius r, with hands at hDeg/mDeg
// (no hands if hDeg is negative).
void drawMiniClock(TFT_eSPI& g, int16_t cx, int16_t cy, int16_t r, float hDeg, float mDeg, uint16_t bg) {
    g.fillSmoothCircle(cx, cy, r, RIM, bg);
    g.fillSmoothCircle(cx, cy, r - 2, FACE_COLOR, RIM);
    for (int i = 0; i < 12; i++) {
        float x0, y0, x1, y1;
        polar(cx, cy, i * 30, r - 4, x0, y0);
        polar(cx, cy, i * 30, r - (i % 3 ? 6 : 9), x1, y1);
        g.drawWideLine(x0, y0, x1, y1, i % 3 ? 1 : 2, HAND, FACE_COLOR);
    }
    if (hDeg < 0) return;
    float x, y;
    polar(cx, cy, hDeg, r * 0.5f, x, y);
    g.drawWedgeLine(cx, cy, x, y, 3, 1.5f, HAND, FACE_COLOR);
    polar(cx, cy, mDeg, r * 0.78f, x, y);
    g.drawWedgeLine(cx, cy, x, y, 2.5f, 1, HAND, FACE_COLOR);
    g.fillSmoothCircle(cx, cy, 2, HAND, FACE_COLOR);
}
}  // namespace

Clock::Hands Clock::handsFor(const struct tm& t) {
    // Hour and minute hands step once a minute, so only the second hand
    // has to be redrawn on most ticks.
    Hands h;
    h.hour = (t.tm_hour % 12) * 30.0f + t.tm_min * 0.5f;
    h.minute = t.tm_min * 6.0f;
    h.second = t.tm_sec * 6.0f;
    return h;
}

// ---------------------------------------------------------------------------
// App interface

void Clock::drawIcon(TFT_eSPI& tft, int16_t x, int16_t y, int16_t w, int16_t h) {
    // No hands until the time is known.
    float hDeg = -1, mDeg = -1;
    if (net::timeValid()) {
        const time_t now = time(nullptr);
        struct tm t;
        localtime_r(&now, &t);
        const Hands hands = handsFor(t);
        hDeg = hands.hour;
        mDeg = hands.minute;
    }
    drawMiniClock(tft, x + w / 2, y + h / 2, min(w, h) / 2 - 2, hDeg, mDeg, color::ICON_BG);
}

// Time zones are whole minutes off UTC, so this ticks over with the local minute.
uint32_t Clock::iconVersion() const { return net::timeValid() ? time(nullptr) / 60 : 0; }

void Clock::begin(TFT_eSPI& tft) {
    tft_ = &tft;
    exit_ = false;
    valid_ = false;
    lastSec_ = 0;
    lastDay_ = -1;
    lastStatus_ = -1;
    hands_ = Hands{};

    ui::drawHeader(tft, "Clock");
    drawFace();
    BTN_BACK.draw(tft);
    tick();
}

void Clock::update(const TouchEvent& ev) {
    if (ev.tapped && BTN_BACK.contains(ev.x, ev.y)) {
        exit_ = true;
        return;
    }
    if (time(nullptr) != lastSec_) tick();
}

// ---------------------------------------------------------------------------
// Drawing

void Clock::tick() {
    lastSec_ = time(nullptr);
    const bool valid = net::timeValid();

    if (!valid) {
        // Still waiting for NTP: keep the hands off and say why.
        if (valid_ || statusCode() != lastStatus_) {
            eraseChangedHands(Hands{});
            hands_ = Hands{};
            drawStatus();
        }
        valid_ = false;
        return;
    }

    struct tm t;
    localtime_r(&lastSec_, &t);
    if (!valid_) {
        // Just synced: replace the status text with the date.
        tft_->fillRect(SIDE_X, TIME_Y, SIDE_W, BTN_BACK.y - TIME_Y - 4, color::FACE);
        ui::drawPanel(*tft_, SIDE_X, TIME_Y, SIDE_W, TIME_H, ui::Bevel::Sunken, color::ICON_BG);
        lastDay_ = -1;
        lastStatus_ = -1;
        valid_ = true;
    }

    const Hands now = handsFor(t);
    eraseChangedHands(now);
    hands_ = now;
    drawNumerals();
    drawHands(now);
    drawDigital(t);
    if (t.tm_yday != lastDay_) {
        lastDay_ = t.tm_yday;
        drawDate(t);
    }
}

void Clock::drawFace() {
    ui::drawPanel(*tft_, WELL_X, WELL_Y, WELL_W, WELL_H, ui::Bevel::Sunken, color::BG);
    tft_->fillSmoothCircle(CX, CY, FACE_R, RIM, color::BG);
    tft_->fillSmoothCircle(CX, CY, FACE_R - 3, FACE_COLOR, RIM);
    for (int i = 0; i < 60; i++) {
        const bool hour = i % 5 == 0;
        float x0, y0, x1, y1;
        polar(CX, CY, i * 6, TICK_OUT, x0, y0);
        polar(CX, CY, i * 6, hour ? HOUR_TICK_IN : MIN_TICK_IN, x1, y1);
        tft_->drawWideLine(x0, y0, x1, y1, hour ? 3 : 1, hour ? HAND : color::EDGE_SHADOW, FACE_COLOR);
    }
    drawNumerals();
}

// Redrawn every tick: erasing a hand can cut into them.
void Clock::drawNumerals() {
    static const char* const LABELS[4] = {"12", "3", "6", "9"};
    tft_->setTextDatum(MC_DATUM);
    tft_->setTextColor(HAND);  // no background, so the hands underneath survive
    for (int i = 0; i < 4; i++) {
        float x, y;
        polar(CX, CY, i * 90, NUMERAL_R, x, y);
        tft_->drawString(LABELS[i], x, y + 1, 4);
    }
}

void Clock::drawHands(const Hands& h) {
    if (h.hour < 0) return;
    float x, y, tx, ty;
    polar(CX, CY, h.hour, HOUR_LEN, x, y);
    tft_->drawWedgeLine(CX, CY, x, y, 6, 2.5f, HAND, FACE_COLOR);
    polar(CX, CY, h.minute, MINUTE_LEN, x, y);
    tft_->drawWedgeLine(CX, CY, x, y, 4.5f, 1.5f, HAND, FACE_COLOR);
    polar(CX, CY, h.second, SECOND_LEN, x, y);
    polar(CX, CY, h.second + 180, SECOND_TAIL, tx, ty);
    tft_->drawWideLine(tx, ty, x, y, 1.5f, SECOND_HAND, FACE_COLOR);
    tft_->fillSmoothCircle(CX, CY, 4, HAND, FACE_COLOR);
    tft_->fillSmoothCircle(CX, CY, 2, SECOND_HAND, HAND);
}

// Paints the old hands over in the dial colour, using the exact same
// geometry, so no anti-aliased fringe is left behind.
void Clock::eraseChangedHands(const Hands& now) {
    float x, y, tx, ty;
    if (hands_.hour >= 0 && hands_.hour != now.hour) {
        polar(CX, CY, hands_.hour, HOUR_LEN, x, y);
        tft_->drawWedgeLine(CX, CY, x, y, 6, 2.5f, FACE_COLOR, FACE_COLOR);
    }
    if (hands_.minute >= 0 && hands_.minute != now.minute) {
        polar(CX, CY, hands_.minute, MINUTE_LEN, x, y);
        tft_->drawWedgeLine(CX, CY, x, y, 4.5f, 1.5f, FACE_COLOR, FACE_COLOR);
    }
    if (hands_.second >= 0 && hands_.second != now.second) {
        polar(CX, CY, hands_.second, SECOND_LEN, x, y);
        polar(CX, CY, hands_.second + 180, SECOND_TAIL, tx, ty);
        tft_->drawWideLine(tx, ty, x, y, 1.5f, FACE_COLOR, FACE_COLOR);
    }
    if (now.hour < 0) tft_->fillSmoothCircle(CX, CY, 4, FACE_COLOR, FACE_COLOR);
}

void Clock::drawDigital(const struct tm& t) {
    char hm[6], sec[3];
    snprintf(hm, sizeof(hm), "%02d:%02d", t.tm_hour, t.tm_min);
    snprintf(sec, sizeof(sec), "%02d", t.tm_sec);

    tft_->fillRect(SIDE_X + ui::BEVEL, TIME_Y + ui::BEVEL, SIDE_W - 2 * ui::BEVEL, TIME_H - 2 * ui::BEVEL,
                   color::ICON_BG);
    // HH:MM large and :SS small on a shared baseline. Font 2's colon hugs
    // its neighbours, so it gets a gap on both sides.
    constexpr int16_t GAP = 3;
    const int16_t w = tft_->textWidth(hm, 4) + GAP + tft_->textWidth(":", 2) + GAP + tft_->textWidth(sec, 2);
    int16_t x = SIDE_CX - w / 2;
    const int16_t base = TIME_Y + TIME_H - 10;
    tft_->setTextDatum(L_BASELINE);
    tft_->setTextColor(color::FACE_TEXT, color::ICON_BG);
    x += tft_->drawString(hm, x, base, 4) + GAP;
    x += tft_->drawString(":", x, base, 2) + GAP;
    tft_->drawString(sec, x, base, 2);
}

void Clock::drawDate(const struct tm& t) {
    char weekday[12], date[16];
    strftime(weekday, sizeof(weekday), "%A", &t);
    strftime(date, sizeof(date), "%d %b %Y", &t);
    tft_->fillRect(SIDE_X, DATE_Y, SIDE_W, 40, color::FACE);
    tft_->setTextDatum(TC_DATUM);
    tft_->setTextColor(color::FACE_TEXT, color::FACE);
    tft_->drawString(weekday, SIDE_CX, DATE_Y, 2);
    tft_->drawString(date, SIDE_CX, DATE_Y + 18, 2);
}

int Clock::statusCode() const {
    if (!net::configured()) return 0;
    return net::connected() ? 2 : 1;
}

void Clock::drawStatus() {
    static const char* const MESSAGES[3][3] = {
        {"No time yet.", "Set up Wi-Fi", "in Settings."},
        {"No time yet.", "Waiting for", "Wi-Fi..."},
        {"Getting the", "time from the", "internet..."},
    };
    lastStatus_ = statusCode();
    const char* const* lines = MESSAGES[lastStatus_];
    tft_->fillRect(SIDE_X, TIME_Y, SIDE_W, BTN_BACK.y - TIME_Y - 4, color::FACE);
    tft_->setTextDatum(TC_DATUM);
    tft_->setTextColor(color::FACE_TEXT, color::FACE);
    for (int i = 0; i < 3; i++) tft_->drawString(lines[i], SIDE_CX, TIME_Y + 10 + i * 18, 2);
}
