#include "games/Snake.h"

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

#include "Config.h"

namespace {
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

constexpr uint16_t BODY = rgb(70, 190, 80);
constexpr uint16_t HEAD = rgb(150, 235, 110);
constexpr uint16_t EYE = rgb(20, 20, 20);
constexpr uint16_t FOOD = rgb(230, 50, 50);
constexpr uint16_t LEAF = rgb(60, 160, 60);

constexpr int16_t CELL = 10;
constexpr int16_t HUD_H = 20;  // field is 32x22 cells below the HUD bar

constexpr int START_LEN = 4;
constexpr int GROW_PER_FOOD = 2;
constexpr uint32_t START_STEP_MS = 160;
constexpr uint32_t MIN_STEP_MS = 60;
constexpr uint32_t SPEEDUP_MS = 4;  // per food eaten
constexpr uint32_t DEATH_MS = 700;

const Button BTN_RESUME{75, 140, 80, 36, "Resume"};
const Button BTN_RETRY{75, 140, 80, 36, "Retry"};
const Button BTN_MENU{165, 140, 80, 36, "Menu"};

const char* const PREFS_NS = "snake";

constexpr int FIELD_COLS = cfg::SCREEN_W / CELL;  // must match Snake::COLS

int16_t cellX(uint16_t c) { return (c % FIELD_COLS) * CELL; }
int16_t cellY(uint16_t c) { return HUD_H + (c / FIELD_COLS) * CELL; }

bool inPauseZone(const TouchEvent& ev) { return ev.x >= cfg::SCREEN_W - 40 && ev.y < HUD_H + 8; }
}  // namespace

// ---------------------------------------------------------------------------
// Game interface

void Snake::drawIcon(TFT_eSPI& tft, int16_t x, int16_t y, int16_t w, int16_t h) {
    constexpr int16_t S = 8;
    // A snake winding down towards an apple, on a 6x5 grid.
    static const int8_t path[][2] = {{0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0}, {5, 0}, {5, 1}, {5, 2}, {4, 2},
                                     {3, 2}, {2, 2}, {1, 2}, {1, 3}, {1, 4}, {2, 4}, {3, 4}};
    const int16_t ox = x + (w - 6 * S) / 2;
    const int16_t oy = y + (h - 5 * S) / 2;
    const int n = sizeof(path) / sizeof(path[0]);
    for (int i = 0; i < n; i++) {
        const uint16_t c = i == n - 1 ? HEAD : BODY;
        tft.fillRoundRect(ox + path[i][0] * S, oy + path[i][1] * S, S - 1, S - 1, 2, c);
    }
    tft.fillCircle(ox + 5 * S + S / 2, oy + 4 * S + S / 2, 3, FOOD);
    tft.fillRect(ox + 5 * S + S / 2, oy + 4 * S - 1, 2, 2, LEAF);
}

void Snake::begin(TFT_eSPI& tft) {
    tft_ = &tft;
    exit_ = false;

    Preferences prefs;
    prefs.begin(PREFS_NS, true);
    best_ = prefs.getInt("best", 0);
    prefs.end();

    newGame();
}

void Snake::update(const TouchEvent& ev) {
    const uint32_t now = millis();

    switch (state_) {
        case State::Paused:
            if (!ev.tapped) return;
            if (BTN_RESUME.contains(ev.x, ev.y)) {
                drawField();
                state_ = State::Playing;
                nextStepAt_ = now + stepMs_;
            } else if (BTN_MENU.contains(ev.x, ev.y)) {
                exit_ = true;
            }
            return;

        case State::GameOver:
            if (!ev.tapped) return;
            if (BTN_RETRY.contains(ev.x, ev.y)) newGame();
            else if (BTN_MENU.contains(ev.x, ev.y)) exit_ = true;
            return;

        case State::Ready:
            if (!ev.tapped) return;
            if (inPauseZone(ev)) {
                showPause();
                return;
            }
            drawField();  // clears the hint
            state_ = State::Playing;
            nextStepAt_ = now + stepMs_;
            return;

        case State::Playing:
            if (ev.tapped) {
                if (inPauseZone(ev)) {
                    showPause();
                    return;
                }
                queueTurn(ev.x, ev.y);
            }
            if ((int32_t)(now - nextStepAt_) >= 0) {
                nextStepAt_ = now + stepMs_;
                step();
            }
            return;

        case State::Dying:
            if ((int32_t)(now - deathAt_) >= (int32_t)DEATH_MS) showGameOver();
            return;
    }
}

// ---------------------------------------------------------------------------
// Game flow

void Snake::newGame() {
    randomSeed(esp_random());
    state_ = State::Ready;
    stepMs_ = START_STEP_MS;
    score_ = 0;
    won_ = false;
    grow_ = 0;
    queued_ = 0;
    dir_ = Dir::Right;

    memset(occ_, 0, sizeof(occ_));
    len_ = 0;
    head_ = CELLS - 1;
    const int row = ROWS / 2;
    for (int i = 0; i < START_LEN; i++) {
        head_ = (head_ + 1) % CELLS;
        body_[head_] = row * COLS + 6 + i;
        setOccupied(body_[head_], true);
        len_++;
    }
    placeFood();

    drawField();
    drawHint();
}

// Taps turn the snake towards the finger: above/below the head while moving
// sideways, left/right of it while moving up or down.
void Snake::queueTurn(int16_t x, int16_t y) {
    if (queued_ >= QUEUE) return;
    const Dir last = queued_ ? queue_[queued_ - 1] : dir_;
    const uint16_t h = segment(0);
    const int16_t hx = cellX(h) + CELL / 2;
    const int16_t hy = cellY(h) + CELL / 2;

    Dir next;
    if (last == Dir::Left || last == Dir::Right) next = y < hy ? Dir::Up : Dir::Down;
    else next = x < hx ? Dir::Left : Dir::Right;
    queue_[queued_++] = next;
}

void Snake::step() {
    if (queued_) {
        dir_ = queue_[0];
        queue_[0] = queue_[1];
        queued_--;
    }

    const uint16_t h = segment(0);
    int nx = h % COLS;
    int ny = h / COLS;
    switch (dir_) {
        case Dir::Up: ny--; break;
        case Dir::Down: ny++; break;
        case Dir::Left: nx--; break;
        case Dir::Right: nx++; break;
    }
    if (nx < 0 || nx >= COLS || ny < 0 || ny >= ROWS) {
        die();
        return;
    }

    const uint16_t next = ny * COLS + nx;
    const bool eating = next == food_ && !won_;
    const bool growing = eating || grow_ > 0;
    const uint16_t tail = segment(len_ - 1);
    // Moving into the tail is fine when it moves away this step.
    if (occupied(next) && (growing || next != tail)) {
        die();
        return;
    }

    if (eating) grow_ += GROW_PER_FOOD;
    if (grow_ > 0) {
        grow_--;
    } else {
        setOccupied(tail, false);
        len_--;
        eraseCell(tail);
    }

    drawBody(h);
    head_ = (head_ + 1) % CELLS;
    body_[head_] = next;
    setOccupied(next, true);
    len_++;
    drawHead(HEAD);

    if (eating) {
        score_++;
        stepMs_ = max(MIN_STEP_MS, stepMs_ - SPEEDUP_MS);
        placeFood();
        if (won_) {
            die();
            return;
        }
        drawFood();
        drawHud();
    }
}

void Snake::die() {
    state_ = State::Dying;
    deathAt_ = millis();
    if (!won_) drawHead(color::PLAYER_O);

    if (score_ > best_) {
        best_ = score_;
        Preferences prefs;
        prefs.begin(PREFS_NS, false);
        prefs.putInt("best", best_);
        prefs.end();
    }
    drawHud();
}

// Picks a uniformly random free cell; sets won_ when the board is full.
void Snake::placeFood() {
    const int freeCells = CELLS - len_;
    if (freeCells <= 0) {
        won_ = true;
        return;
    }
    int k = random(freeCells);
    for (uint16_t c = 0; c < CELLS; c++) {
        if (occupied(c)) continue;
        if (k-- == 0) {
            food_ = c;
            return;
        }
    }
}

void Snake::setOccupied(uint16_t c, bool on) {
    if (on) occ_[c >> 3] |= 1 << (c & 7);
    else occ_[c >> 3] &= ~(1 << (c & 7));
}

void Snake::showPause() {
    state_ = State::Paused;
    ui::drawDialog(*tft_, "Paused");
    char buf[32];
    snprintf(buf, sizeof(buf), "Score %d   Best %d", score_, best_);
    tft_->setTextColor(color::FACE_TEXT, color::FACE);
    tft_->drawString(buf, cfg::SCREEN_W / 2, 100, 2);
    BTN_RESUME.draw(*tft_);
    BTN_MENU.draw(*tft_);
}

void Snake::showGameOver() {
    state_ = State::GameOver;
    ui::drawDialog(*tft_, won_ ? "You Win!" : "Game Over");

    char buf[32];
    snprintf(buf, sizeof(buf), "Score %d   Best %d", score_, best_);
    tft_->setTextColor(color::FACE_TEXT, color::FACE);
    tft_->drawString(buf, cfg::SCREEN_W / 2, 100, 2);

    BTN_RETRY.draw(*tft_);
    BTN_MENU.draw(*tft_);
}

// ---------------------------------------------------------------------------
// Rendering: only the cells that change are redrawn each step.

void Snake::drawField() {
    tft_->fillRect(0, HUD_H, cfg::SCREEN_W, cfg::SCREEN_H - HUD_H, color::BG);
    for (int i = 1; i < len_; i++) drawBody(segment(i));
    drawHead(HEAD);
    if (!won_) drawFood();
    drawHud();
}

void Snake::drawHud() {
    ui::drawPanel(*tft_, 0, 0, cfg::SCREEN_W, HUD_H);
    char buf[16];
    tft_->setTextColor(color::FACE_TEXT, color::FACE);
    tft_->setTextDatum(ML_DATUM);
    snprintf(buf, sizeof(buf), "Score %d", score_);
    tft_->drawString(buf, 6, HUD_H / 2, 2);
    tft_->setTextDatum(MC_DATUM);
    snprintf(buf, sizeof(buf), "Best %d", best_);
    tft_->drawString(buf, cfg::SCREEN_W / 2, HUD_H / 2, 2);

    ui::drawPauseButton(*tft_, cfg::SCREEN_W - ui::PAUSE_W - 3, (HUD_H - ui::PAUSE_H) / 2);
}

void Snake::drawHint() {
    tft_->setTextDatum(MC_DATUM);
    tft_->setTextColor(color::ACCENT);
    tft_->drawString("Tap to start", cfg::SCREEN_W / 2, 70, 4);
    tft_->setTextColor(color::TEXT_DIM);
    tft_->drawString("Tap above/below or left/right", cfg::SCREEN_W / 2, 175, 2);
    tft_->drawString("of the head to turn", cfg::SCREEN_W / 2, 193, 2);
}

void Snake::eraseCell(uint16_t c) {
    tft_->fillRect(cellX(c), cellY(c), CELL, CELL, color::BG);
}

void Snake::drawBody(uint16_t c) {
    eraseCell(c);
    tft_->fillRoundRect(cellX(c) + 1, cellY(c) + 1, CELL - 2, CELL - 2, 2, BODY);
}

void Snake::drawHead(uint16_t headColor) {
    const uint16_t c = segment(0);
    const int16_t x = cellX(c);
    const int16_t y = cellY(c);
    tft_->fillRoundRect(x, y, CELL, CELL, 3, headColor);

    // Eyes on the leading side
    int16_t ex1 = x + 2, ey1 = y + 2, ex2 = x + 6, ey2 = y + 2;
    switch (dir_) {
        case Dir::Up: break;
        case Dir::Down: ey1 = ey2 = y + 6; break;
        case Dir::Left: ex2 = x + 2; ey2 = y + 6; break;
        case Dir::Right: ex1 = x + 6; ey2 = y + 6; break;
    }
    tft_->fillRect(ex1, ey1, 2, 2, EYE);
    tft_->fillRect(ex2, ey2, 2, 2, EYE);
}

void Snake::drawFood() {
    const int16_t x = cellX(food_);
    const int16_t y = cellY(food_);
    // Radius 4 around (5, 5) stays inside the cell, so eraseCell() clears all of it.
    tft_->fillCircle(x + CELL / 2, y + CELL / 2, 4, FOOD);
    tft_->fillRect(x + CELL / 2, y, 2, 2, LEAF);
}
