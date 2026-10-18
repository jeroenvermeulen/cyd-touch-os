#include "games/Tetris.h"

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

#include "Config.h"

namespace {
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
// Halfway towards white / black, for the bevel on each block.
constexpr uint16_t lighten(uint16_t c) { return ((c >> 1) & 0x7BEF) + 0x7BEF; }
constexpr uint16_t darken(uint16_t c) { return (c >> 1) & 0x7BEF; }

// Pieces in order I, J, L, O, S, T, Z.
const uint16_t PIECE_COLOR[] = {
    rgb(0, 220, 230),  rgb(40, 90, 240),  rgb(250, 150, 30), rgb(245, 220, 30),
    rgb(60, 210, 70),  rgb(170, 70, 230), rgb(235, 50, 50),
};

// Each rotation is a 4x4 grid packed into 16 bits, top row in the high nibble.
const uint16_t SHAPES[7][4] = {
    {0x0F00, 0x2222, 0x00F0, 0x4444},  // I
    {0x8E00, 0x6440, 0x0E20, 0x44C0},  // J
    {0x2E00, 0x4460, 0x0E80, 0xC440},  // L
    {0x6600, 0x6600, 0x6600, 0x6600},  // O
    {0x6C00, 0x4620, 0x06C0, 0x8C40},  // S
    {0x4E00, 0x4640, 0x0E40, 0x4C40},  // T
    {0xC600, 0x2640, 0x0C60, 0x4C80},  // Z
};

bool filled(uint16_t shape, int r, int c) { return shape & (0x8000 >> (r * 4 + c)); }

// Cell values in Tetris::shown_ beyond the 1..7 block colours.
constexpr uint8_t GHOST = 8;  // + piece type
constexpr uint8_t FLASH = 15;

constexpr int16_t CELL = 11;
constexpr int16_t BOARD_X = 105;
constexpr int16_t BOARD_Y = 10;
constexpr int16_t BOARD_W = 10 * CELL;  // must match Tetris::COLS
constexpr int16_t BOARD_H = 20 * CELL;  // must match Tetris::ROWS
constexpr int16_t RIGHT_X = BOARD_X + BOARD_W + 5;
constexpr int16_t RIGHT_W = 94;
constexpr int16_t NEXT_Y = 24;  // sunken preview box under the "Next" label
constexpr int16_t NEXT_H = 44;

constexpr uint32_t START_FALL_MS = 800;
constexpr uint32_t MIN_FALL_MS = 50;
constexpr uint32_t LOCK_MS = 500;
constexpr int MAX_LOCK_RESETS = 15;  // stops a piece from being spun forever
constexpr uint32_t CLEAR_MS = 250;
constexpr uint32_t DEATH_MS = 700;
constexpr uint32_t REPEAT_DELAY_MS = 180;  // holding left/right: first repeat...
constexpr uint32_t REPEAT_MS = 60;         // ...then this often
constexpr uint32_t SOFT_DROP_MS = 50;      // holding drop: one row this often
const int LINE_SCORE[] = {0, 100, 300, 500, 800};

const Button BTN_LEFT{4, 120, 45, 112, ""};
const Button BTN_RIGHT{53, 120, 45, 112, ""};
const Button BTN_ROTATE{RIGHT_X, 120, RIGHT_W, 54, ""};
const Button BTN_DROP{RIGHT_X, 178, RIGHT_W, 54, ""};

const Button BTN_RESUME{75, 140, 80, 36, "Resume"};
const Button BTN_RETRY{75, 140, 80, 36, "Retry"};
const Button BTN_MENU{165, 140, 80, 36, "Menu"};

const char* const PREFS_NS = "tetris";

bool inPauseZone(const TouchEvent& ev) { return ev.x >= cfg::SCREEN_W - 45 && ev.y < 30; }

void drawBlock(TFT_eSPI& g, int16_t x, int16_t y, int16_t s, int type) {
    const uint16_t c = PIECE_COLOR[type];
    g.fillRect(x, y, s - 1, s - 1, c);
    g.drawFastHLine(x, y, s - 1, lighten(c));
    g.drawFastVLine(x, y, s - 1, lighten(c));
    g.drawFastHLine(x + 1, y + s - 2, s - 2, darken(c));
    g.drawFastVLine(x + s - 2, y + 1, s - 2, darken(c));
}

int firstRow(uint16_t shape) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            if (filled(shape, r, c)) return r;
    return 0;
}
}  // namespace

// ---------------------------------------------------------------------------
// Game interface

void Tetris::drawIcon(TFT_eSPI& tft, int16_t x, int16_t y, int16_t w, int16_t h) {
    constexpr int16_t S = 10;
    // A T falling onto a small stack ('1'..'7' = piece type + 1).
    static const char* const rows[] = {
        ".666..",
        "..6...",
        ".....1",
        "4....1",
        "4..551",
        "4455.1",
    };
    const int16_t ox = x + (w - 6 * S) / 2;
    const int16_t oy = y + (h - 6 * S) / 2;
    for (int r = 0; r < 6; r++)
        for (int c = 0; c < 6; c++)
            if (rows[r][c] != '.') drawBlock(tft, ox + c * S, oy + r * S, S, rows[r][c] - '1');
}

void Tetris::begin(TFT_eSPI& tft) {
    tft_ = &tft;
    exit_ = false;

    Preferences prefs;
    prefs.begin(PREFS_NS, true);
    best_ = prefs.getInt("best", 0);
    prefs.end();

    newGame();
}

void Tetris::update(const TouchEvent& ev) {
    const uint32_t now = millis();

    switch (state_) {
        case State::Paused:
            if (!ev.tapped) return;
            if (BTN_RESUME.contains(ev.x, ev.y)) {
                if (!active_) {  // paused before the first piece
                    newGame();
                    return;
                }
                drawScreen();
                state_ = State::Playing;
                fallAt_ = now + gravityMs();
                lockAt_ = now + LOCK_MS;
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
            memset(shown_, 0xFF, sizeof(shown_));  // clears the hint
            state_ = State::Playing;
            spawn();
            return;

        case State::Clearing:
            if (ev.released) setHold(Hold::None);
            if ((int32_t)(now - clearAt_) >= 0) finishClear();
            return;

        case State::Dying:
            if ((int32_t)(now - deathAt_) >= (int32_t)DEATH_MS) showGameOver();
            return;

        case State::Playing:
            playInput(ev, now);
            if (state_ != State::Playing) return;

            if (fits(type_, rot_, px_, py_ + 1)) {
                resting_ = false;
                if ((int32_t)(now - fallAt_) >= 0) {
                    fallAt_ = now + gravityMs();
                    py_++;
                    render();
                }
            } else if (!resting_) {
                resting_ = true;
                lockAt_ = now + LOCK_MS;
            } else if ((int32_t)(now - lockAt_) >= 0) {
                lock();
            }
            return;
    }
}

// ---------------------------------------------------------------------------
// Game flow

void Tetris::newGame() {
    randomSeed(esp_random());
    state_ = State::Ready;
    memset(board_, 0, sizeof(board_));
    active_ = false;
    hold_ = Hold::None;
    score_ = lines_ = level_ = 0;
    clearRows_ = 0;
    bagPos_ = PIECES;
    next_ = nextFromBag();

    drawScreen();
    tft_->setTextDatum(MC_DATUM);
    tft_->setTextColor(color::ACCENT);
    tft_->drawString("Tap to start", BOARD_X + BOARD_W / 2, BOARD_Y + BOARD_H / 2, 2);
}

void Tetris::playInput(const TouchEvent& ev, uint32_t now) {
    if (ev.released) setHold(Hold::None);

    if (ev.tapped) {
        if (inPauseZone(ev)) {
            showPause();
        } else if (BTN_LEFT.contains(ev.x, ev.y)) {
            tryMove(-1, 0);
            setHold(Hold::Left);
            repeatAt_ = now + REPEAT_DELAY_MS;
        } else if (BTN_RIGHT.contains(ev.x, ev.y)) {
            tryMove(1, 0);
            setHold(Hold::Right);
            repeatAt_ = now + REPEAT_DELAY_MS;
        } else if (BTN_ROTATE.contains(ev.x, ev.y)) {
            tryRotate();
        } else if (BTN_DROP.contains(ev.x, ev.y)) {
            softDrop();
            setHold(Hold::Down);
            repeatAt_ = now + SOFT_DROP_MS;
        }
        return;
    }

    // Holding left/right/drop keeps moving; sliding off the button stops it.
    if (hold_ != Hold::None && ev.touching && (int32_t)(now - repeatAt_) >= 0) {
        if (!holdButton(hold_)->contains(ev.x, ev.y)) {
            setHold(Hold::None);
            return;
        }
        if (hold_ == Hold::Down) {
            softDrop();
            repeatAt_ = now + SOFT_DROP_MS;
        } else {
            tryMove(hold_ == Hold::Left ? -1 : 1, 0);
            repeatAt_ = now + REPEAT_MS;
        }
    }
}

void Tetris::spawn() {
    type_ = next_;
    next_ = nextFromBag();
    rot_ = 0;
    px_ = 3;
    py_ = -firstRow(SHAPES[type_][0]);
    active_ = true;
    resting_ = false;
    lockResets_ = 0;
    fallAt_ = millis() + gravityMs();
    drawNext();
    render();
    if (!fits(type_, rot_, px_, py_)) topOut();
}

void Tetris::lock() {
    const uint16_t shape = SHAPES[type_][rot_];
    bool above = false;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            if (!filled(shape, r, c)) continue;
            if (py_ + r < 0) above = true;
            else board_[py_ + r][px_ + c] = type_ + 1;
        }
    active_ = false;
    if (above) {
        topOut();
        return;
    }

    clearRows_ = 0;
    int n = 0;
    for (int r = 0; r < ROWS; r++) {
        bool full = true;
        for (int c = 0; c < COLS && full; c++) full = board_[r][c] != 0;
        if (full) {
            clearRows_ |= 1u << r;
            n++;
        }
    }
    if (n == 0) {
        spawn();
        return;
    }

    score_ += LINE_SCORE[n] * (level_ + 1);
    lines_ += n;
    level_ = lines_ / 10;
    drawStats();
    state_ = State::Clearing;
    clearAt_ = millis() + CLEAR_MS;
    render();
}

void Tetris::finishClear() {
    int w = ROWS - 1;
    for (int r = ROWS - 1; r >= 0; r--) {
        if (clearRows_ & (1u << r)) continue;
        if (w != r) memcpy(board_[w], board_[r], COLS);
        w--;
    }
    for (; w >= 0; w--) memset(board_[w], 0, COLS);
    clearRows_ = 0;
    state_ = State::Playing;
    spawn();
}

void Tetris::topOut() {
    state_ = State::Dying;
    deathAt_ = millis();
    if (score_ > best_) {
        best_ = score_;
        Preferences prefs;
        prefs.begin(PREFS_NS, false);
        prefs.putInt("best", best_);
        prefs.end();
    }
    render();
    drawStats();
}

void Tetris::refillBag() {
    for (int i = 0; i < PIECES; i++) bag_[i] = i;
    for (int i = PIECES - 1; i > 0; i--) {
        const int j = random(i + 1);
        const uint8_t t = bag_[i];
        bag_[i] = bag_[j];
        bag_[j] = t;
    }
    bagPos_ = 0;
}

// "7-bag" randomiser: every piece once in each group of seven, so droughts
// of one piece can't happen.
uint8_t Tetris::nextFromBag() {
    if (bagPos_ >= PIECES) refillBag();
    return bag_[bagPos_++];
}

// ---------------------------------------------------------------------------
// Piece movement

// Cells above the board count as empty so pieces can spawn partly hidden.
bool Tetris::fits(int type, int rot, int x, int y) const {
    const uint16_t shape = SHAPES[type][rot];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            if (!filled(shape, r, c)) continue;
            const int bx = x + c, by = y + r;
            if (bx < 0 || bx >= COLS || by >= ROWS) return false;
            if (by >= 0 && board_[by][bx]) return false;
        }
    return true;
}

bool Tetris::tryMove(int dx, int dy) {
    if (!fits(type_, rot_, px_ + dx, py_ + dy)) return false;
    px_ += dx;
    py_ += dy;
    if (resting_ && lockResets_ < MAX_LOCK_RESETS) {
        lockResets_++;
        lockAt_ = millis() + LOCK_MS;
    }
    render();
    return true;
}

// Rotates clockwise, nudging the piece sideways or up if it's blocked
// (a simplified wall kick).
bool Tetris::tryRotate() {
    static const int8_t kicks[][2] = {{0, 0}, {-1, 0}, {1, 0}, {0, -1}, {-2, 0}, {2, 0}};
    const int rot = (rot_ + 1) % 4;
    for (const auto& k : kicks) {
        if (!fits(type_, rot, px_ + k[0], py_ + k[1])) continue;
        rot_ = rot;
        px_ += k[0];
        py_ += k[1];
        if (resting_ && lockResets_ < MAX_LOCK_RESETS) {
            lockResets_++;
            lockAt_ = millis() + LOCK_MS;
        }
        render();
        return true;
    }
    return false;
}

// Moves the piece down one row for a point. It doesn't lock straight away,
// so it can still be slid or rotated once it lands.
bool Tetris::softDrop() {
    if (!tryMove(0, 1)) return false;
    fallAt_ = millis() + gravityMs();
    score_++;
    drawStats();
    return true;
}

int Tetris::ghostY() const {
    int y = py_;
    while (fits(type_, rot_, px_, y + 1)) y++;
    return y;
}

uint32_t Tetris::gravityMs() const {
    return max(MIN_FALL_MS, (uint32_t)(START_FALL_MS * powf(0.8f, level_)));
}

// ---------------------------------------------------------------------------
// Overlays

void Tetris::showPause() {
    state_ = State::Paused;
    setHold(Hold::None);
    ui::drawDialog(*tft_, "Paused");
    char buf[32];
    snprintf(buf, sizeof(buf), "Score %d   Best %d", score_, best_);
    tft_->setTextColor(color::FACE_TEXT, color::FACE);
    tft_->drawString(buf, cfg::SCREEN_W / 2, 100, 2);
    BTN_RESUME.draw(*tft_);
    BTN_MENU.draw(*tft_);
}

void Tetris::showGameOver() {
    state_ = State::GameOver;
    ui::drawDialog(*tft_, "Game Over");
    char buf[32];
    tft_->setTextColor(color::FACE_TEXT, color::FACE);
    snprintf(buf, sizeof(buf), "Score %d   Lines %d", score_, lines_);
    tft_->drawString(buf, cfg::SCREEN_W / 2, 94, 2);
    tft_->setTextColor(color::EDGE_SHADOW, color::FACE);
    snprintf(buf, sizeof(buf), "Best %d", best_);
    tft_->drawString(buf, cfg::SCREEN_W / 2, 112, 2);
    BTN_RETRY.draw(*tft_);
    BTN_MENU.draw(*tft_);
}

// ---------------------------------------------------------------------------
// Rendering

void Tetris::drawScreen() {
    tft_->fillScreen(color::FACE);
    ui::drawBevel(*tft_, BOARD_X - ui::BEVEL, BOARD_Y - ui::BEVEL, BOARD_W + 2 * ui::BEVEL,
                  BOARD_H + 2 * ui::BEVEL, ui::Bevel::Sunken);
    memset(shown_, 0xFF, sizeof(shown_));
    render();
    drawStats();
    drawNext();
    drawControls();
}

// Works out what every cell should look like and redraws the ones that differ.
void Tetris::render() {
    uint8_t want[ROWS][COLS];
    memcpy(want, board_, sizeof(want));
    for (int r = 0; r < ROWS; r++)
        if (clearRows_ & (1u << r)) memset(want[r], FLASH, COLS);

    if (active_) {
        const uint16_t shape = SHAPES[type_][rot_];
        const int gy = ghostY();
        for (int pass = 0; pass < 2; pass++) {
            const int y0 = pass == 0 ? gy : py_;
            const uint8_t v = pass == 0 ? GHOST + type_ : type_ + 1;
            for (int r = 0; r < 4; r++)
                for (int c = 0; c < 4; c++) {
                    const int by = y0 + r;
                    if (filled(shape, r, c) && by >= 0 && by < ROWS) want[by][px_ + c] = v;
                }
        }
    }

    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            if (want[r][c] != shown_[r][c]) {
                drawCell(c, r, want[r][c]);
                shown_[r][c] = want[r][c];
            }
}

void Tetris::drawCell(int col, int row, uint8_t v) {
    const int16_t x = BOARD_X + col * CELL;
    const int16_t y = BOARD_Y + row * CELL;
    tft_->fillRect(x, y, CELL, CELL, color::BG);
    if (v == FLASH) {
        tft_->fillRect(x, y, CELL - 1, CELL - 1, color::TEXT);
    } else if (v >= GHOST) {
        tft_->drawRect(x + 1, y + 1, CELL - 3, CELL - 3, darken(PIECE_COLOR[v - GHOST]));
    } else if (v > 0) {
        drawBlock(*tft_, x, y, CELL, v - 1);
    }
}

void Tetris::drawStats() {
    char buf[16];
    // Padding overwrites the old text, so the panel doesn't flicker while
    // the score ticks up during a soft drop.
    tft_->setTextPadding(BOARD_X - 10);
    tft_->setTextDatum(TL_DATUM);
    tft_->setTextColor(color::EDGE_SHADOW, color::FACE);
    tft_->drawString("Score", 8, 8, 2);
    tft_->setTextColor(color::FACE_TEXT, color::FACE);
    snprintf(buf, sizeof(buf), "%d", score_);
    tft_->drawString(buf, 8, 24, 4);
    snprintf(buf, sizeof(buf), "Lines %d", lines_);
    tft_->drawString(buf, 8, 58, 2);
    snprintf(buf, sizeof(buf), "Level %d", level_ + 1);
    tft_->drawString(buf, 8, 76, 2);
    tft_->setTextColor(color::EDGE_SHADOW, color::FACE);
    snprintf(buf, sizeof(buf), "Best %d", best_);
    tft_->drawString(buf, 8, 94, 2);
    tft_->setTextPadding(0);
}

void Tetris::drawNext() {
    ui::drawPanel(*tft_, RIGHT_X, NEXT_Y, RIGHT_W, NEXT_H, ui::Bevel::Sunken, color::BG);
    tft_->setTextDatum(TL_DATUM);
    tft_->setTextColor(color::EDGE_SHADOW, color::FACE);
    tft_->drawString("Next", RIGHT_X + 4, 8, 2);

    // Centre the piece's bounding box in the preview area.
    const uint16_t shape = SHAPES[next_][0];
    int minR = 4, maxR = -1, minC = 4, maxC = -1;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            if (filled(shape, r, c)) {
                minR = min(minR, r);
                maxR = max(maxR, r);
                minC = min(minC, c);
                maxC = max(maxC, c);
            }
    const int16_t ox = RIGHT_X + (RIGHT_W - (maxC - minC + 1) * CELL) / 2;
    const int16_t oy = NEXT_Y + (NEXT_H - (maxR - minR + 1) * CELL) / 2;
    for (int r = minR; r <= maxR; r++)
        for (int c = minC; c <= maxC; c++)
            if (filled(shape, r, c)) drawBlock(*tft_, ox + (c - minC) * CELL, oy + (r - minR) * CELL, CELL, next_);
}

void Tetris::drawControls() {
    for (const Button* b : {&BTN_LEFT, &BTN_RIGHT, &BTN_ROTATE, &BTN_DROP}) drawControl(*b, false);
    ui::drawPauseButton(*tft_, cfg::SCREEN_W - ui::PAUSE_W - 4, 6);
}

// Held buttons are drawn pushed in, with the glyph shifted along.
void Tetris::drawControl(const Button& b, bool pressed) {
    b.draw(*tft_, pressed);
    const int16_t d = pressed ? 1 : 0;
    int16_t cx = b.x + b.w / 2 + d;
    int16_t cy = b.y + b.h / 2 + d;
    const uint16_t ink = color::FACE_TEXT;

    if (&b == &BTN_LEFT) {
        tft_->fillTriangle(cx - 9, cy, cx + 7, cy - 11, cx + 7, cy + 11, ink);
    } else if (&b == &BTN_RIGHT) {
        tft_->fillTriangle(cx + 9, cy, cx - 7, cy - 11, cx - 7, cy + 11, ink);
    } else if (&b == &BTN_ROTATE) {
        // Clockwise arrow: a ring with its top-right quarter cut away, ending in
        // an arrowhead at the top that points right.
        cy += 2;
        tft_->fillCircle(cx, cy, 12, ink);
        tft_->fillCircle(cx, cy, 8, color::FACE);
        tft_->fillRect(cx + 1, cy - 13, 13, 13, color::FACE);
        tft_->fillTriangle(cx + 1, cy - 17, cx + 1, cy - 3, cx + 9, cy - 10, ink);
    } else {
        tft_->fillTriangle(cx, cy + 9, cx - 12, cy - 7, cx + 12, cy - 7, ink);
    }
}

const Button* Tetris::holdButton(Hold h) {
    switch (h) {
        case Hold::Left: return &BTN_LEFT;
        case Hold::Right: return &BTN_RIGHT;
        case Hold::Down: return &BTN_DROP;
        default: return nullptr;
    }
}

void Tetris::setHold(Hold h) {
    if (h == hold_) return;
    if (const Button* b = holdButton(hold_)) drawControl(*b, false);
    hold_ = h;
    if (const Button* b = holdButton(hold_)) drawControl(*b, true);
}
