#include "apps/Minesweeper.h"

#include <Arduino.h>
#include <Preferences.h>

#include "Config.h"

namespace {
constexpr int16_t CELL = 18;
constexpr int16_t BOARD_X = 66;
constexpr int16_t BOARD_Y = 12;
constexpr int16_t BOARD_W = 14 * CELL;  // must match Minesweeper::COLS
constexpr int16_t BOARD_H = 12 * CELL;  // must match Minesweeper::ROWS

// Left-hand panel: pause, mine counter, face (new game), timer, mode.
constexpr int16_t PANEL_X = 4;
constexpr int16_t PANEL_W = 56;
constexpr int16_t WELL_H = 30;
constexpr int16_t COUNT_Y = 26;
constexpr int16_t TIMER_Y = 112;

constexpr uint32_t LONG_PRESS_MS = 400;
constexpr uint32_t END_MS = 1500;  // board stays visible before the dialog
constexpr int MAX_SECS = 999;

constexpr uint16_t RED = 0xF800;
constexpr uint16_t FACE_YELLOW = 0xFFE0;
// The classic number colours: 1 blue, 2 green, 3 red, 4 navy, 5 maroon, 6 teal, 7 black, 8 grey.
const uint16_t NUM_COLOR[] = {0, 0x001F, 0x0400, 0xF800, 0x0010, 0x8000, 0x0410, 0x0000, 0x8410};

const Button BTN_FACE{PANEL_X + (PANEL_W - 44) / 2, 62, 44, 44, ""};
const Button BTN_DIG{PANEL_X, 150, PANEL_W, 38, "Dig"};
const Button BTN_FLAG{PANEL_X, 192, PANEL_W, 38, "Flag"};

const Button BTN_RESUME{75, 140, 80, 36, "Resume"};
const Button BTN_RETRY{75, 140, 80, 36, "Retry"};
const Button BTN_MENU{165, 140, 80, 36, "Menu"};

const char* const PREFS_NS = "mines";

bool inPauseZone(const TouchEvent& ev) { return ev.x < 34 && ev.y < 24; }

// What a cell looks like; 0..8 is an open cell with that number.
enum Tile : uint8_t { HIDDEN = 9, FLAG, MINE, MINE_HIT, MINE_WRONG };

void drawMine(TFT_eSPI& g, int16_t cx, int16_t cy) {
    g.drawFastHLine(cx - 6, cy, 13, TFT_BLACK);
    g.drawFastVLine(cx, cy - 6, 13, TFT_BLACK);
    g.drawLine(cx - 4, cy - 4, cx + 4, cy + 4, TFT_BLACK);
    g.drawLine(cx - 4, cy + 4, cx + 4, cy - 4, TFT_BLACK);
    g.fillCircle(cx, cy, 4, TFT_BLACK);
    g.fillRect(cx - 2, cy - 2, 2, 2, TFT_WHITE);
}

void drawFlag(TFT_eSPI& g, int16_t cx, int16_t cy) {
    g.fillTriangle(cx + 1, cy - 6, cx + 1, cy, cx - 5, cy - 3, RED);
    g.drawFastVLine(cx + 1, cy - 6, 9, TFT_BLACK);
    g.fillRect(cx - 2, cy + 2, 6, 2, TFT_BLACK);
    g.fillRect(cx - 4, cy + 4, 10, 2, TFT_BLACK);
}

// Open cells have a grey grid line on their top and left, like the original.
void drawTile(TFT_eSPI& g, int16_t x, int16_t y, int16_t s, uint8_t t) {
    const int16_t cx = x + s / 2, cy = y + s / 2;
    if (t == HIDDEN || t == FLAG) {
        ui::drawPanel(g, x, y, s, s);
        if (t == FLAG) drawFlag(g, cx, cy);
        return;
    }
    g.fillRect(x, y, s, s, t == MINE_HIT ? RED : color::FACE);
    g.drawFastHLine(x, y, s, color::EDGE_SHADOW);
    g.drawFastVLine(x, y, s, color::EDGE_SHADOW);
    if (t >= MINE) {
        drawMine(g, cx + 1, cy + 1);
        if (t == MINE_WRONG) {
            g.drawLine(x + 3, y + 3, x + s - 3, y + s - 3, RED);
            g.drawLine(x + 3, y + s - 3, x + s - 3, y + 3, RED);
        }
    } else if (t > 0) {
        char num[2] = {static_cast<char>('0' + t), 0};
        g.setTextDatum(MC_DATUM);
        g.setTextColor(NUM_COLOR[t]);
        // Twice, one pixel apart, for bold digits.
        g.drawString(num, cx, cy + 1, 2);
        g.drawString(num, cx + 1, cy + 1, 2);
    }
}

void drawSmiley(TFT_eSPI& g, int16_t cx, int16_t cy, uint8_t face) {
    g.fillCircle(cx, cy, 13, FACE_YELLOW);
    g.drawCircle(cx, cy, 13, TFT_BLACK);
    switch (face) {
        case 1:  // wow: round eyes and mouth while a cell is held
            g.fillCircle(cx - 4, cy - 4, 2, TFT_BLACK);
            g.fillCircle(cx + 4, cy - 4, 2, TFT_BLACK);
            g.drawCircle(cx, cy + 5, 3, TFT_BLACK);
            return;
        case 2:  // dead: crossed eyes, frown
            for (int16_t ex : {cx - 5, cx + 3}) {
                g.drawLine(ex - 2, cy - 6, ex + 2, cy - 2, TFT_BLACK);
                g.drawLine(ex - 2, cy - 2, ex + 2, cy - 6, TFT_BLACK);
            }
            g.drawLine(cx - 6, cy + 8, cx - 3, cy + 5, TFT_BLACK);
            g.drawFastHLine(cx - 3, cy + 5, 7, TFT_BLACK);
            g.drawLine(cx + 3, cy + 5, cx + 6, cy + 8, TFT_BLACK);
            return;
        case 3:  // cool: sunglasses
            g.fillRect(cx - 9, cy - 6, 7, 5, TFT_BLACK);
            g.fillRect(cx + 2, cy - 6, 7, 5, TFT_BLACK);
            g.drawFastHLine(cx - 12, cy - 5, 24, TFT_BLACK);
            break;
        default:
            g.fillCircle(cx - 4, cy - 4, 1, TFT_BLACK);
            g.fillCircle(cx + 4, cy - 4, 1, TFT_BLACK);
            break;
    }
    g.drawLine(cx - 6, cy + 3, cx - 3, cy + 6, TFT_BLACK);
    g.drawFastHLine(cx - 3, cy + 6, 7, TFT_BLACK);
    g.drawLine(cx + 3, cy + 6, cx + 6, cy + 3, TFT_BLACK);
}

// A sunken black well with red digits, like the original's LED displays.
void drawLed(TFT_eSPI& g, int16_t y, int value) {
    ui::drawPanel(g, PANEL_X, y, PANEL_W, WELL_H, ui::Bevel::Sunken, TFT_BLACK);
    char buf[8];
    snprintf(buf, sizeof(buf), "%03d", constrain(value, -99, MAX_SECS));
    g.setTextDatum(MC_DATUM);
    g.setTextColor(RED, TFT_BLACK);
    g.drawString(buf, PANEL_X + PANEL_W / 2, y + WELL_H / 2 + 1, 4);
}
}  // namespace

// ---------------------------------------------------------------------------
// App interface

void Minesweeper::drawIcon(TFT_eSPI& tft, int16_t x, int16_t y, int16_t w, int16_t h) {
    constexpr int16_t S = 16;
    // A corner of a real board: mines under the flag and the hidden cell.
    static const char* const rows[] = {
        "1F1.",
        "111.",
        "..11",
        "..1#",
    };
    const int16_t ox = x + (w - 4 * S) / 2;
    const int16_t oy = y + (h - 4 * S) / 2;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            const char ch = rows[r][c];
            const uint8_t t = ch == 'F' ? FLAG : ch == '#' ? HIDDEN : ch == '.' ? 0 : ch - '0';
            drawTile(tft, ox + c * S, oy + r * S, S, t);
        }
}

void Minesweeper::begin(TFT_eSPI& tft) {
    tft_ = &tft;
    exit_ = false;

    Preferences prefs;
    prefs.begin(PREFS_NS, true);
    best_ = prefs.getInt("best", 0);
    prefs.end();

    newGame();
}

void Minesweeper::update(const TouchEvent& ev) {
    const uint32_t now = millis();

    switch (state_) {
        case State::Paused:
            if (!ev.tapped) return;
            if (BTN_RESUME.contains(ev.x, ev.y)) {
                startMs_ += now - pausedAt_;  // paused time doesn't count
                state_ = pausedFrom_;
                drawScreen();
            } else if (BTN_MENU.contains(ev.x, ev.y)) {
                exit_ = true;
            }
            return;

        case State::Over:
            if (!ev.tapped) return;
            if (BTN_RETRY.contains(ev.x, ev.y)) newGame();
            else if (BTN_MENU.contains(ev.x, ev.y)) exit_ = true;
            return;

        case State::Ended:
            // A tap skips the wait.
            if (ev.tapped || (int32_t)(now - endAt_) >= 0) showOver();
            return;

        case State::Ready:
        case State::Playing:
            boardInput(ev, now);
            if (state_ == State::Playing && elapsedSecs() != shownSecs_) drawTimer();
            return;
    }
}

// The screensaver pauses the game; it waits for an animation to finish.
bool Minesweeper::suspend() {
    if (state_ == State::Ended) return false;  // let it finish first
    if (state_ != State::Paused && state_ != State::Over) showPause();
    return true;
}

// Back from the screensaver: the game as it was, under its dialog.
void Minesweeper::restore() {
    drawScreen();
    if (state_ == State::Paused) drawPauseDialog();
    else showOver();
}

// ---------------------------------------------------------------------------
// Game flow

void Minesweeper::newGame() {
    randomSeed(esp_random());
    state_ = State::Ready;
    memset(mine_, 0, sizeof(mine_));
    memset(adj_, 0, sizeof(adj_));
    for (Cover& c : cover_) c = Cover::Hidden;
    opened_ = flags_ = 0;
    won_ = false;
    hit_ = -1;
    press_ = -1;
    longDone_ = false;
    finalSecs_ = 0;
    drawScreen();
}

// A cell acts when the finger lifts: dig (or flag, in Flag mode). Holding it
// does the other one straight away, so both work without switching modes.
void Minesweeper::boardInput(const TouchEvent& ev, uint32_t now) {
    if (ev.tapped) {
        if (inPauseZone(ev)) {
            showPause();
            return;
        }
        if (BTN_FACE.contains(ev.x, ev.y)) {
            newGame();
            return;
        }
        if (BTN_DIG.contains(ev.x, ev.y) || BTN_FLAG.contains(ev.x, ev.y)) {
            flagMode_ = BTN_FLAG.contains(ev.x, ev.y);
            drawModes();
            return;
        }
    }

    if (ev.touching && !longDone_) {
        const int cell = cellAt(ev.x, ev.y);
        // Sliding onto another cell moves the press there, as on a PC.
        if (cell != press_ && (ev.tapped || press_ >= 0)) {
            if (press_ >= 0) drawCell(press_);
            press_ = cell;
            pressAt_ = now;
            if (press_ >= 0 && !flagMode_) drawCell(press_, true);
            drawFace(press_ >= 0 ? Face::Wow : Face::Smile);
        }
        if (press_ >= 0 && (int32_t)(now - pressAt_) >= (int32_t)LONG_PRESS_MS) {
            longDone_ = true;
            const int i = press_;
            press_ = -1;
            drawCell(i);
            act(i, flagMode_);
            if (state_ == State::Ready || state_ == State::Playing) drawFace(Face::Smile);
        }
    }

    if (ev.released) {
        longDone_ = false;
        if (press_ < 0) return;
        const int i = press_;
        press_ = -1;
        drawCell(i);
        act(i, !flagMode_);
        if (state_ == State::Ready || state_ == State::Playing) drawFace(Face::Smile);
    }
}

void Minesweeper::act(int i, bool dig) {
    if (cover_[i] == Cover::Open) chord(i);
    else if (!dig) toggleFlag(i);
    else if (cover_[i] == Cover::Hidden) reveal(i);
}

// Mines go anywhere except the first dug cell and its neighbours, so the
// first dig always opens up an area.
void Minesweeper::placeMines(int safe) {
    const int sr = safe / COLS, sc = safe % COLS;
    int placed = 0;
    while (placed < MINES) {
        const int i = random(CELLS);
        if (mine_[i] || (abs(i / COLS - sr) <= 1 && abs(i % COLS - sc) <= 1)) continue;
        mine_[i] = true;
        placed++;
    }
    for (int i = 0; i < CELLS; i++) {
        const int r = i / COLS, c = i % COLS;
        for (int dr = -1; dr <= 1; dr++)
            for (int dc = -1; dc <= 1; dc++) {
                const int nr = r + dr, nc = c + dc;
                if (nr >= 0 && nr < ROWS && nc >= 0 && nc < COLS && mine_[nr * COLS + nc]) adj_[i]++;
            }
    }
    state_ = State::Playing;
    startMs_ = millis();
}

// Opens a cell; blank cells keep opening their neighbours (flood fill).
void Minesweeper::reveal(int i) {
    if (cover_[i] != Cover::Hidden) return;
    if (state_ == State::Ready) placeMines(i);
    if (mine_[i]) {
        lose(i);
        return;
    }

    int16_t queue[CELLS];
    int head = 0, tail = 0;
    cover_[i] = Cover::Open;
    queue[tail++] = i;
    while (head < tail) {
        const int j = queue[head++];
        opened_++;
        drawCell(j);
        if (adj_[j]) continue;
        const int r = j / COLS, c = j % COLS;
        for (int dr = -1; dr <= 1; dr++)
            for (int dc = -1; dc <= 1; dc++) {
                const int nr = r + dr, nc = c + dc;
                if (nr < 0 || nr >= ROWS || nc < 0 || nc >= COLS) continue;
                const int n = nr * COLS + nc;
                if (cover_[n] != Cover::Hidden) continue;
                cover_[n] = Cover::Open;
                queue[tail++] = n;
            }
    }
    if (opened_ == CELLS - MINES) win();
}

// Tapping a number whose mines are all flagged opens the rest of its neighbours.
void Minesweeper::chord(int i) {
    if (adj_[i] == 0) return;
    const int r = i / COLS, c = i % COLS;
    int around[8];
    int n = 0, flagged = 0;
    for (int dr = -1; dr <= 1; dr++)
        for (int dc = -1; dc <= 1; dc++) {
            const int nr = r + dr, nc = c + dc;
            if ((dr || dc) && nr >= 0 && nr < ROWS && nc >= 0 && nc < COLS) around[n++] = nr * COLS + nc;
        }
    for (int k = 0; k < n; k++) flagged += cover_[around[k]] == Cover::Flagged;
    if (flagged != adj_[i]) return;
    // A wrong flag means a mine goes off, which ends the game mid-loop.
    for (int k = 0; k < n && state_ == State::Playing; k++) reveal(around[k]);
}

void Minesweeper::toggleFlag(int i) {
    if (cover_[i] == Cover::Hidden) {
        cover_[i] = Cover::Flagged;
        flags_++;
    } else if (cover_[i] == Cover::Flagged) {
        cover_[i] = Cover::Hidden;
        flags_--;
    }
    drawCell(i);
    drawCounter();
}

void Minesweeper::lose(int i) {
    finalSecs_ = elapsedSecs();
    hit_ = i;
    state_ = State::Ended;
    endAt_ = millis() + END_MS;
    // Show every mine and every wrong flag.
    for (int j = 0; j < CELLS; j++)
        if ((mine_[j] && cover_[j] == Cover::Hidden) || (!mine_[j] && cover_[j] == Cover::Flagged)) drawCell(j);
    drawFace(Face::Dead);
}

void Minesweeper::win() {
    finalSecs_ = elapsedSecs();
    won_ = true;
    state_ = State::Ended;
    endAt_ = millis() + END_MS;
    for (int j = 0; j < CELLS; j++)
        if (mine_[j] && cover_[j] != Cover::Flagged) {
            cover_[j] = Cover::Flagged;
            drawCell(j);
        }
    flags_ = MINES;
    drawCounter();
    drawTimer();
    drawFace(Face::Cool);

    if (best_ == 0 || finalSecs_ < best_) {
        best_ = finalSecs_;
        Preferences prefs;
        prefs.begin(PREFS_NS, false);
        prefs.putInt("best", best_);
        prefs.end();
    }
}

int Minesweeper::elapsedSecs() const {
    switch (state_) {
        case State::Ready: return 0;
        case State::Playing: return min(MAX_SECS, (int)((millis() - startMs_) / 1000));
        case State::Paused:
            if (pausedFrom_ == State::Ready) return 0;
            return min(MAX_SECS, (int)((pausedAt_ - startMs_) / 1000));
        default: return finalSecs_;
    }
}

int Minesweeper::cellAt(int16_t x, int16_t y) const {
    if (x < BOARD_X || x >= BOARD_X + BOARD_W || y < BOARD_Y || y >= BOARD_Y + BOARD_H) return -1;
    return (y - BOARD_Y) / CELL * COLS + (x - BOARD_X) / CELL;
}

// ---------------------------------------------------------------------------
// Overlays

void Minesweeper::showPause() {
    if (press_ >= 0) drawCell(press_);
    press_ = -1;
    pausedFrom_ = state_;
    pausedAt_ = millis();
    state_ = State::Paused;
    drawPauseDialog();
}

void Minesweeper::drawPauseDialog() {
    ui::drawDialog(*tft_, "Paused");
    char buf[32];
    snprintf(buf, sizeof(buf), "Mines left %d", MINES - flags_);
    tft_->setTextColor(color::FACE_TEXT, color::FACE);
    tft_->drawString(buf, cfg::SCREEN_W / 2, 94, 2);
    tft_->setTextColor(color::EDGE_SHADOW, color::FACE);
    if (best_) snprintf(buf, sizeof(buf), "Best time %d s", best_);
    else snprintf(buf, sizeof(buf), "No best time yet");
    tft_->drawString(buf, cfg::SCREEN_W / 2, 112, 2);
    BTN_RESUME.draw(*tft_);
    BTN_MENU.draw(*tft_);
}

void Minesweeper::showOver() {
    state_ = State::Over;
    ui::drawDialog(*tft_, won_ ? "You Win!" : "Boom!");
    char buf[32];
    tft_->setTextColor(color::FACE_TEXT, color::FACE);
    if (won_) snprintf(buf, sizeof(buf), "Cleared in %d s", finalSecs_);
    else snprintf(buf, sizeof(buf), "You hit a mine");
    tft_->drawString(buf, cfg::SCREEN_W / 2, 94, 2);
    tft_->setTextColor(color::EDGE_SHADOW, color::FACE);
    if (best_) snprintf(buf, sizeof(buf), "Best time %d s", best_);
    else snprintf(buf, sizeof(buf), "No best time yet");
    tft_->drawString(buf, cfg::SCREEN_W / 2, 112, 2);
    BTN_RETRY.draw(*tft_);
    BTN_MENU.draw(*tft_);
}

// ---------------------------------------------------------------------------
// Rendering

void Minesweeper::drawScreen() {
    tft_->fillScreen(color::FACE);
    ui::drawBevel(*tft_, BOARD_X - ui::BEVEL, BOARD_Y - ui::BEVEL, BOARD_W + 2 * ui::BEVEL,
                  BOARD_H + 2 * ui::BEVEL, ui::Bevel::Sunken);
    for (int i = 0; i < CELLS; i++) drawCell(i);
    ui::drawPauseButton(*tft_, PANEL_X, 4);
    drawCounter();
    drawTimer();
    drawFace(state_ != State::Ended && state_ != State::Over ? Face::Smile : won_ ? Face::Cool : Face::Dead);
    drawModes();
}

void Minesweeper::drawCell(int i, bool pressed) {
    uint8_t t;
    const bool shown = state_ == State::Ended || state_ == State::Over;
    if (cover_[i] == Cover::Open) t = adj_[i];
    else if (shown && !won_ && mine_[i] && cover_[i] == Cover::Hidden) t = i == hit_ ? MINE_HIT : MINE;
    else if (shown && !won_ && !mine_[i] && cover_[i] == Cover::Flagged) t = MINE_WRONG;
    else if (cover_[i] == Cover::Flagged) t = FLAG;
    else t = pressed ? 0 : HIDDEN;
    drawTile(*tft_, BOARD_X + i % COLS * CELL, BOARD_Y + i / COLS * CELL, CELL, t);
}

void Minesweeper::drawCounter() { drawLed(*tft_, COUNT_Y, MINES - flags_); }

void Minesweeper::drawTimer() {
    shownSecs_ = elapsedSecs();
    drawLed(*tft_, TIMER_Y, shownSecs_);
}

void Minesweeper::drawFace(Face f) {
    BTN_FACE.draw(*tft_);
    drawSmiley(*tft_, BTN_FACE.x + BTN_FACE.w / 2, BTN_FACE.y + BTN_FACE.h / 2, static_cast<uint8_t>(f));
}

void Minesweeper::drawModes() {
    BTN_DIG.draw(*tft_, !flagMode_);
    BTN_FLAG.draw(*tft_, flagMode_);
}
